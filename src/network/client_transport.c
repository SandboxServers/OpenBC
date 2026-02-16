#include "openbc/client_transport.h"
#include "openbc/transport.h"
#include "openbc/opcodes.h"
#include <string.h>

int bc_client_build_connect(u8 *out, int out_size, u32 local_ip)
{
    /* Wire: [dir=0xFF][count=1][type=0x03][totalLen=8][flags=0x01][pad:3][ip:4] */
    if (out_size < 10) return -1;

    out[0] = BC_DIR_INIT;     /* 0xFF */
    out[1] = 1;               /* 1 message */
    out[2] = BC_TRANSPORT_CONNECT; /* 0x03 */
    out[3] = 8;               /* totalLen */
    out[4] = 0x01;            /* flags */
    out[5] = 0x00;            /* pad */
    out[6] = 0x00;            /* pad */
    out[7] = 0x00;            /* pad */
    out[8] = (u8)(local_ip & 0xFF);
    out[9] = (u8)((local_ip >> 8) & 0xFF);

    return 10;
}

int bc_client_build_keepalive_name(u8 *out, int out_size, u8 slot,
                                    u32 local_ip, const char *name)
{
    /* Wire: [dir=0x02+slot][count=1][type=0x00][totalLen][flags=0x80]
     *   [pad:2][slot:1][ip:4][name_utf16le...] */
    int name_len = (int)strlen(name);
    int name_bytes = name_len * 2 + 2; /* UTF-16LE + null terminator */
    int payload_len = 1 + 2 + 1 + 4 + name_bytes; /* flags+pad+slot+ip+name */
    int msg_len = 2 + payload_len; /* type + totalLen + payload */
    int pkt_len = 2 + msg_len;     /* dir + count + msg */

    if (pkt_len > out_size || msg_len > 255) return -1;

    out[0] = BC_DIR_CLIENT + slot;
    out[1] = 1;
    out[2] = BC_TRANSPORT_KEEPALIVE;
    out[3] = (u8)msg_len;
    out[4] = 0x80;            /* flags */
    out[5] = 0x00;            /* pad */
    out[6] = 0x00;            /* pad */
    out[7] = slot;
    out[8] = (u8)(local_ip & 0xFF);
    out[9] = (u8)((local_ip >> 8) & 0xFF);
    out[10] = (u8)((local_ip >> 16) & 0xFF);
    out[11] = (u8)((local_ip >> 24) & 0xFF);

    /* UTF-16LE encode name */
    int pos = 12;
    for (int i = 0; i < name_len; i++) {
        out[pos++] = (u8)name[i]; /* low byte */
        out[pos++] = 0x00;        /* high byte (ASCII) */
    }
    /* Null terminator */
    out[pos++] = 0x00;
    out[pos++] = 0x00;

    return pkt_len;
}

int bc_client_build_reliable(u8 *out, int out_size,
                              u8 slot, const u8 *payload, int payload_len, u16 seq)
{
    /* Wire: [dir=0x02+slot][count=1][0x32][totalLen][flags=0x80][seqHi][0x00][payload] */
    int total_msg_len = 5 + payload_len;
    int pkt_len = 2 + total_msg_len;

    if (pkt_len > out_size || total_msg_len > 255) return -1;

    out[0] = BC_DIR_CLIENT + slot;
    out[1] = 1;
    out[2] = BC_TRANSPORT_RELIABLE;
    out[3] = (u8)total_msg_len;
    out[4] = 0x80;
    out[5] = (u8)(seq & 0xFF);
    out[6] = 0;
    memcpy(out + 7, payload, (size_t)payload_len);

    return pkt_len;
}

int bc_client_build_unreliable(u8 *out, int out_size,
                                u8 slot, const u8 *payload, int payload_len)
{
    /* Wire: [dir=0x02+slot][count=1][type=0x00][totalLen][payload] */
    int total_msg_len = 2 + payload_len;
    int pkt_len = 2 + total_msg_len;

    if (pkt_len > out_size || total_msg_len > 255) return -1;

    out[0] = BC_DIR_CLIENT + slot;
    out[1] = 1;
    out[2] = 0x00; /* unreliable type */
    out[3] = (u8)total_msg_len;
    memcpy(out + 4, payload, (size_t)payload_len);

    return pkt_len;
}

int bc_client_build_ack(u8 *out, int out_size, u8 slot, u16 seq, u8 flags)
{
    /* Wire: [dir=0x02+slot][count=1][0x01][counter][0x00][flags] */
    if (out_size < 6) return -1;

    out[0] = BC_DIR_CLIENT + slot;
    out[1] = 1;
    out[2] = BC_TRANSPORT_ACK;
    out[3] = (u8)(seq >> 8); /* counter = high byte of wire seq */
    out[4] = 0x00;
    out[5] = flags;

    return 6;
}

int bc_client_build_dummy_checksum_resp(u8 *buf, int buf_size, u8 round)
{
    /* Wire: [0x21][round][ref_hash:u32][dir_hash:u32][file_count=0:u16]
     * Minimal valid response with zero files. Passes --no-checksum. */
    if (buf_size < 12) return -1;

    buf[0] = BC_OP_CHECKSUM_RESP; /* 0x21 */
    buf[1] = round;
    /* ref_hash = 0 */
    buf[2] = 0; buf[3] = 0; buf[4] = 0; buf[5] = 0;
    /* dir_hash = 0 */
    buf[6] = 0; buf[7] = 0; buf[8] = 0; buf[9] = 0;
    /* file_count = 0 */
    buf[10] = 0; buf[11] = 0;

    return 12;
}

int bc_client_build_dummy_checksum_final(u8 *buf, int buf_size)
{
    /* Wire: [0x21][0xFF][dir_hash=0:u32][file_count=0:u32]
     * The final round uses u32 file_count (not u16). */
    if (buf_size < 10) return -1;

    buf[0] = BC_OP_CHECKSUM_RESP; /* 0x21 */
    buf[1] = 0xFF;
    /* dir_hash = 0 */
    buf[2] = 0; buf[3] = 0; buf[4] = 0; buf[5] = 0;
    /* file_count = 0 */
    buf[6] = 0; buf[7] = 0; buf[8] = 0; buf[9] = 0;

    return 10;
}
