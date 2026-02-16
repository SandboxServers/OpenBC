#include "openbc/transport.h"
#include "openbc/cipher.h"
#include <string.h>

bool bc_transport_parse(const u8 *data, int len, bc_packet_t *pkt)
{
    if (len < 2) return false;

    pkt->direction = data[0];
    pkt->msg_count = data[1];

    if (pkt->msg_count == 0) return true;

    int pos = 2;
    int count = 0;

    for (int i = 0; i < pkt->msg_count && pos < len && count < 32; i++) {
        bc_transport_msg_t *msg = &pkt->msgs[count];
        msg->type = data[pos];

        if (msg->type == 0x01) {
            /* ACK: fixed 4 bytes [0x01][seq][0x00][flags] */
            if (pos + 4 > len) return false;
            msg->seq = (u16)data[pos + 1];
            msg->flags = data[pos + 3];
            msg->payload = NULL;
            msg->payload_len = 0;
            pos += 4;
        } else if (msg->type == 0x32) {
            /* Reliable: [0x32][totalLen][flags][seqHi][seqLo][payload...] */
            if (pos + 2 > len) return false;
            u8 total_len = data[pos + 1]; /* includes the 0x32 byte */
            if (total_len < 5) return false;
            if (pos + total_len > len) return false;

            msg->flags = data[pos + 2];
            msg->seq = ((u16)data[pos + 3] << 8) | (u16)data[pos + 4];
            msg->payload = (u8 *)data + pos + 5;
            msg->payload_len = total_len - 5;
            pos += total_len;
        } else {
            /* Generic: [type][totalLen][data...] */
            if (pos + 2 > len) return false;
            u8 total_len = data[pos + 1];
            if (total_len < 2) return false;
            if (pos + total_len > len) return false;

            msg->flags = 0;
            msg->seq = 0;
            msg->payload = (u8 *)data + pos + 2;
            msg->payload_len = total_len - 2;
            pos += total_len;
        }
        count++;
    }

    pkt->msg_count = (u8)count;
    return true;
}

int bc_transport_build_unreliable(u8 *out, int out_size,
                                  const u8 *payload, int payload_len)
{
    /* Format: [direction=0x01][count=1][type=0x00][totalLen][payload] */
    int total_msg_len = 2 + payload_len;  /* type + totalLen + payload */
    int packet_len = 2 + total_msg_len;   /* direction + count + msg */

    if (packet_len > out_size) return -1;

    out[0] = BC_DIR_SERVER;
    out[1] = 1;  /* 1 message */
    out[2] = 0x00;  /* unreliable type */
    out[3] = (u8)total_msg_len;
    memcpy(out + 4, payload, (size_t)payload_len);

    return packet_len;
}

int bc_transport_build_reliable(u8 *out, int out_size,
                                const u8 *payload, int payload_len,
                                u16 seq)
{
    /* Format: [direction=0x01][count=1][0x32][totalLen][flags][seqHi][seqLo][payload] */
    int total_msg_len = 5 + payload_len;  /* type(1) + totalLen(1) + flags(1) + seq(2) + payload */
    int packet_len = 2 + total_msg_len;   /* direction + count + msg */

    if (packet_len > out_size || total_msg_len > 255) return -1;

    out[0] = BC_DIR_SERVER;
    out[1] = 1;
    out[2] = BC_TRANSPORT_RELIABLE;
    out[3] = (u8)total_msg_len;
    out[4] = 0x80;  /* reliable flag */
    out[5] = (u8)(seq >> 8);
    out[6] = (u8)(seq & 0xFF);
    memcpy(out + 7, payload, (size_t)payload_len);

    return packet_len;
}

int bc_transport_build_ack(u8 *out, int out_size, u16 seq, u8 flags)
{
    /* Format: [direction=0x01][count=1][0x01][seq_lo][0x00][flags] */
    if (out_size < 6) return -1;

    out[0] = BC_DIR_SERVER;
    out[1] = 1;
    out[2] = BC_TRANSPORT_ACK;
    out[3] = (u8)(seq & 0xFF);
    out[4] = 0x00;
    out[5] = flags;

    return 6;
}
