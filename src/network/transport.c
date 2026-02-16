#include "openbc/transport.h"
#include "openbc/cipher.h"
#include "openbc/log.h"
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
    /* Format: [direction=0x01][count=1][0x32][totalLen][flags][seqHi][seqLo][payload]
     * Wire protocol: seq counter goes in seqHi byte, seqLo is always 0.
     * Real BC increments by 256 on the wire (only high byte changes). */
    int total_msg_len = 5 + payload_len;  /* type(1) + totalLen(1) + flags(1) + seq(2) + payload */
    int packet_len = 2 + total_msg_len;   /* direction + count + msg */

    if (packet_len > out_size || total_msg_len > 255) return -1;

    out[0] = BC_DIR_SERVER;
    out[1] = 1;
    out[2] = BC_TRANSPORT_RELIABLE;
    out[3] = (u8)total_msg_len;
    out[4] = 0x80;  /* reliable flag */
    out[5] = (u8)(seq & 0xFF);  /* counter → seqHi */
    out[6] = 0;                  /* seqLo always 0 */
    memcpy(out + 7, payload, (size_t)payload_len);

    return packet_len;
}

int bc_transport_build_ack(u8 *out, int out_size, u16 seq, u8 flags)
{
    /* Format: [direction=0x01][count=1][0x01][counter][0x00][flags]
     * The ACK byte references the seqHi byte of the reliable message.
     * Since incoming reliable seqs are parsed as (seqHi << 8 | seqLo),
     * we extract the counter with >> 8. */
    if (out_size < 6) return -1;

    out[0] = BC_DIR_SERVER;
    out[1] = 1;
    out[2] = BC_TRANSPORT_ACK;
    out[3] = (u8)(seq >> 8);  /* counter = high byte of wire seq */
    out[4] = 0x00;
    out[5] = flags;

    return 6;
}

int bc_transport_build_shutdown_notify(u8 *out, int out_size, u8 slot, u32 ip_be)
{
    /* Packet format from trace:
     * [0x01][0x01][0x05][0x0A][0xC0][0x00][0x00][slot][ip:4]
     * direction=server, count=1, type=ConnectAck, totalLen=10,
     * flags=0xC0, pad=0x00, pad=0x00, slot, ip in network byte order */
    if (out_size < 12) return -1;

    out[0]  = BC_DIR_SERVER;
    out[1]  = 1;                          /* 1 message */
    out[2]  = BC_TRANSPORT_CONNECT_ACK;   /* 0x05 */
    out[3]  = 0x0A;                       /* totalLen = 10 */
    out[4]  = 0xC0;                       /* flags */
    out[5]  = 0x00;                       /* padding */
    out[6]  = 0x00;                       /* padding */
    out[7]  = slot;                       /* player slot */
    out[8]  = (u8)(ip_be & 0xFF);         /* IP byte 0 (network order) */
    out[9]  = (u8)((ip_be >> 8) & 0xFF);  /* IP byte 1 */
    out[10] = (u8)((ip_be >> 16) & 0xFF); /* IP byte 2 */
    out[11] = (u8)((ip_be >> 24) & 0xFF); /* IP byte 3 */

    return 12;
}

/* --- Outbox --- */

void bc_outbox_init(bc_outbox_t *outbox)
{
    outbox->pos = 2;        /* Skip direction + msg_count header */
    outbox->msg_count = 0;
}

bool bc_outbox_add_unreliable(bc_outbox_t *outbox, const u8 *payload, int len)
{
    /* Format: [type=0x00][totalLen][payload...] */
    int msg_len = 2 + len;  /* type + totalLen + payload */
    if (outbox->pos + msg_len > BC_MAX_PACKET_SIZE) return false;
    if (msg_len > 255) return false;

    outbox->buf[outbox->pos++] = 0x00;         /* unreliable type */
    outbox->buf[outbox->pos++] = (u8)msg_len;  /* totalLen */
    memcpy(outbox->buf + outbox->pos, payload, (size_t)len);
    outbox->pos += len;
    outbox->msg_count++;
    return true;
}

bool bc_outbox_add_reliable(bc_outbox_t *outbox, const u8 *payload, int len, u16 seq)
{
    /* Format: [0x32][totalLen][flags=0x80][seqHi][seqLo=0x00][payload...] */
    int msg_len = 5 + len;  /* type + totalLen + flags + seqHi + seqLo + payload */
    if (outbox->pos + msg_len > BC_MAX_PACKET_SIZE) return false;
    if (msg_len > 255) return false;

    outbox->buf[outbox->pos++] = BC_TRANSPORT_RELIABLE;
    outbox->buf[outbox->pos++] = (u8)msg_len;
    outbox->buf[outbox->pos++] = 0x80;              /* reliable flag */
    outbox->buf[outbox->pos++] = (u8)(seq & 0xFF);  /* counter → seqHi */
    outbox->buf[outbox->pos++] = 0;                  /* seqLo always 0 */
    memcpy(outbox->buf + outbox->pos, payload, (size_t)len);
    outbox->pos += len;
    outbox->msg_count++;
    return true;
}

bool bc_outbox_add_ack(bc_outbox_t *outbox, u16 seq, u8 flags)
{
    /* Format: [0x01][counter][0x00][flags] -- 4 bytes fixed */
    if (outbox->pos + 4 > BC_MAX_PACKET_SIZE) return false;

    outbox->buf[outbox->pos++] = BC_TRANSPORT_ACK;
    outbox->buf[outbox->pos++] = (u8)(seq >> 8);  /* counter = high byte of wire seq */
    outbox->buf[outbox->pos++] = 0x00;
    outbox->buf[outbox->pos++] = flags;
    outbox->msg_count++;
    return true;
}

bool bc_outbox_add_keepalive(bc_outbox_t *outbox)
{
    /* Format: [type=0x00][totalLen=0x02] -- minimal keepalive */
    if (outbox->pos + 2 > BC_MAX_PACKET_SIZE) return false;

    outbox->buf[outbox->pos++] = BC_TRANSPORT_KEEPALIVE;
    outbox->buf[outbox->pos++] = 0x02;
    outbox->msg_count++;
    return true;
}

int bc_outbox_flush_to_buf(bc_outbox_t *outbox, u8 *out, int out_size)
{
    if (outbox->msg_count == 0) return 0;

    int pkt_len = outbox->pos;
    if (pkt_len > out_size) {
        bc_outbox_init(outbox);
        return -1;
    }

    /* Set header */
    outbox->buf[0] = BC_DIR_SERVER;
    outbox->buf[1] = (u8)outbox->msg_count;

    memcpy(out, outbox->buf, (size_t)pkt_len);
    bc_outbox_init(outbox);
    return pkt_len;
}

void bc_outbox_flush(bc_outbox_t *outbox, bc_socket_t *sock, const bc_addr_t *to)
{
    if (outbox->msg_count == 0) return;

    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(outbox, pkt, sizeof(pkt));
    if (len > 0) {
        alby_rules_cipher(pkt, (size_t)len);
        bc_socket_send(sock, to, pkt, len);
    }
}

bool bc_outbox_pending(const bc_outbox_t *outbox)
{
    return outbox->msg_count > 0;
}

/* --- Fragment reassembly --- */

void bc_fragment_reset(bc_fragment_buf_t *frag)
{
    frag->active = false;
    frag->buf_len = 0;
    frag->frags_expected = 0;
    frag->frags_received = 0;
}

bool bc_fragment_receive(bc_fragment_buf_t *frag,
                         const u8 *payload, int payload_len)
{
    if (payload_len < 1) return false;

    if (!frag->active) {
        /* First fragment: payload[0] = total_frags, rest = data */
        frag->active = true;
        frag->frags_expected = payload[0];
        frag->frags_received = 1;
        frag->buf_len = 0;

        if (frag->frags_expected < 2) {
            /* Not actually fragmented -- shouldn't happen but handle it */
            LOG_WARN("fragment", "invalid total_frags=%d",
                     frag->frags_expected);
            bc_fragment_reset(frag);
            return false;
        }

        int data_len = payload_len - 1;
        if (data_len > BC_FRAGMENT_BUF_SIZE) {
            LOG_ERROR("fragment", "first fragment too large (%d)", data_len);
            bc_fragment_reset(frag);
            return false;
        }

        memcpy(frag->buf, payload + 1, (size_t)data_len);
        frag->buf_len = data_len;
    } else {
        /* Continuation fragment: payload[0] = frag_idx, rest = data */
        u8 frag_idx = payload[0];
        (void)frag_idx; /* Index used for ordering; we trust sequential delivery */

        int data_len = payload_len - 1;
        if (frag->buf_len + data_len > BC_FRAGMENT_BUF_SIZE) {
            LOG_ERROR("fragment", "reassembly buffer overflow (%d + %d)",
                      frag->buf_len, data_len);
            bc_fragment_reset(frag);
            return false;
        }

        memcpy(frag->buf + frag->buf_len, payload + 1, (size_t)data_len);
        frag->buf_len += data_len;
        frag->frags_received++;
    }

    if (frag->frags_received >= frag->frags_expected) {
        /* All fragments received -- message is complete */
        return true;
    }

    return false;
}
