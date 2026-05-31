#include "openbc/drain.h"
#include "openbc/opcodes.h"
#include <string.h>

/*
 * 4-pass per-peer outbound drain.  See include/openbc/drain.h for the pass
 * table and the per-datagram constants.  The on-wire message encodings are
 * identical to the single-message outbox path -- only the bundling order and
 * buffer management live here.
 */

/* --- Queue primitives --- */

static void msgq_init(bc_msgq_t *q)
{
    q->head = 0;
    q->count = 0;
}

/* Append an already-serialized message to a ring queue.  Returns the stored
 * slot (so the caller can fill seq/retx/promote), or NULL if the queue is
 * full or the message is too large. */
static bc_qmsg_t *msgq_push(bc_msgq_t *q, const u8 *bytes, int len)
{
    if (len <= 0 || len > BC_DRAIN_MSG_MAX) return NULL;
    if (q->count >= BC_DRAIN_QUEUE_CAP) return NULL;

    int tail = (q->head + q->count) % BC_DRAIN_QUEUE_CAP;
    bc_qmsg_t *m = &q->slot[tail];
    memcpy(m->bytes, bytes, (size_t)len);
    m->len = len;
    m->seq = 0;
    m->retx = 0;
    m->promote = false;
    q->count++;
    return m;
}

/* Peek the message at the front of the queue without removing it. */
static bc_qmsg_t *msgq_front(bc_msgq_t *q)
{
    if (q->count == 0) return NULL;
    return &q->slot[q->head];
}

/* Remove and discard the front message. */
static void msgq_pop(bc_msgq_t *q)
{
    if (q->count == 0) return;
    q->head = (q->head + 1) % BC_DRAIN_QUEUE_CAP;
    q->count--;
}

/* --- Message encoders (byte-for-byte identical to the single-message path) --- */

/* Serialize a type 0x32 game message.  reliable=true emits the 5-byte header
 * [0x32][totalLen][0x80][seqHi][seqLo=0]; reliable=false emits the 3-byte
 * header [0x32][totalLen][0x00]. */
static int encode_game(u8 *out, const u8 *payload, int len, bool reliable, u16 seq)
{
    if (reliable) {
        int msg_len = 5 + len;
        if (msg_len > 255) return 0;
        out[0] = BC_TRANSPORT_RELIABLE;
        out[1] = (u8)msg_len;
        out[2] = 0x80;
        out[3] = (u8)(seq & 0xFF);
        out[4] = 0;
        memcpy(out + 5, payload, (size_t)len);
        return msg_len;
    } else {
        int msg_len = 3 + len;
        if (msg_len > 255) return 0;
        out[0] = BC_TRANSPORT_RELIABLE;
        out[1] = (u8)msg_len;
        out[2] = 0x00;
        memcpy(out + 3, payload, (size_t)len);
        return msg_len;
    }
}

/* --- Public enqueue API --- */

void bc_drain_init(bc_drain_t *d)
{
    msgq_init(&d->priority);
    msgq_init(&d->reliable);
    msgq_init(&d->unreliable);
    msgq_init(&d->ack);
    d->disconnecting = false;
}

int bc_drain_next_cursor(int cursor, int num_slots)
{
    int peers = num_slots - 1;  /* slot 0 is the host, excluded */
    if (peers <= 0) return 1;
    return 1 + (cursor % peers);
}

bool bc_drain_enqueue_priority(bc_drain_t *d, const u8 *payload, int len, u16 seq)
{
    u8 tmp[BC_DRAIN_MSG_MAX];
    int n = encode_game(tmp, payload, len, true, seq);
    if (n <= 0) return false;
    bc_qmsg_t *m = msgq_push(&d->priority, tmp, n);
    if (!m) return false;
    m->seq = seq;
    m->retx = 0;
    return true;
}

bool bc_drain_enqueue_reliable(bc_drain_t *d, const u8 *payload, int len, u16 seq)
{
    u8 tmp[BC_DRAIN_MSG_MAX];
    int n = encode_game(tmp, payload, len, true, seq);
    if (n <= 0) return false;
    bc_qmsg_t *m = msgq_push(&d->reliable, tmp, n);
    if (!m) return false;
    m->seq = seq;
    return true;
}

bool bc_drain_enqueue_unreliable(bc_drain_t *d, const u8 *payload, int len)
{
    u8 tmp[BC_DRAIN_MSG_MAX];
    int n = encode_game(tmp, payload, len, false, 0);
    if (n <= 0) return false;
    bc_qmsg_t *m = msgq_push(&d->unreliable, tmp, n);
    if (!m) return false;
    /* Pass 3 promotes an unreliable message to reliable tracking on first
     * delivery -- matching the stock burst behaviour where a drained
     * unreliable message graduates into the retransmit path. */
    m->promote = true;
    return true;
}

bool bc_drain_enqueue_ack(bc_drain_t *d, u16 seq, u8 flags)
{
    u8 tmp[4];
    tmp[0] = BC_TRANSPORT_ACK;
    tmp[1] = (u8)(seq >> 8);
    tmp[2] = 0x00;
    tmp[3] = flags;
    return msgq_push(&d->ack, tmp, 4) != NULL;
}

bool bc_drain_enqueue_fragment_ack(bc_drain_t *d, u16 seq, u8 frag_idx)
{
    u8 tmp[5];
    tmp[0] = BC_TRANSPORT_ACK;
    tmp[1] = (u8)(seq >> 8);
    tmp[2] = 0x00;
    tmp[3] = 0x01;          /* is_fragmented flag */
    tmp[4] = frag_idx;
    return msgq_push(&d->ack, tmp, 5) != NULL;
}

bool bc_drain_pending(const bc_drain_t *d)
{
    return d->priority.count > 0 || d->reliable.count > 0 ||
           d->unreliable.count > 0 || d->ack.count > 0;
}

/* --- The 4-pass drain --- */

/* Append a serialized message into the datagram if it fits and the count cap
 * is not yet hit.  Returns true if written. */
static bool try_pack(u8 *out, int *pos, int *msg_count, const bc_qmsg_t *m)
{
    if (*msg_count >= BC_DRAIN_MSGCOUNT_MAX) return false;
    if (*pos + m->len > BC_MAX_PACKET_SIZE) return false;
    memcpy(out + *pos, m->bytes, (size_t)m->len);
    *pos += m->len;
    (*msg_count)++;
    return true;
}

int bc_drain_to_buf(bc_drain_t *d, u8 sender_id, u8 *out, int out_size,
                    const bc_qmsg_t **promoted_out, int promoted_cap,
                    int *promoted_count)
{
    if (out_size < BC_MAX_PACKET_SIZE) return 0;

    int pos = BC_DRAIN_HEADER_LEN;  /* reserve 2-byte header */
    int msg_count = 0;
    int promoted = 0;

    /* --- Pass 1: priority-reliable, retx < 3 -- pack until won't-fit/cap --- */
    {
        int scanned = 0;
        int total = d->priority.count;
        while (scanned < total) {
            /* Walk the queue without removing fresh entries: priority
             * messages persist until ACKed.  We index from the head. */
            int idx = (d->priority.head + scanned) % BC_DRAIN_QUEUE_CAP;
            bc_qmsg_t *m = &d->priority.slot[idx];
            scanned++;
            if (m->retx >= BC_DRAIN_RETX_STALE) continue;  /* belongs to Pass 4 */
            if (!try_pack(out, &pos, &msg_count, m)) break;  /* won't fit / cap */
            m->retx++;
        }
    }

    /* --- Pass 2: reliable -- ONE-SHOT: attempt once, then break --- */
    {
        bc_qmsg_t *m = msgq_front(&d->reliable);
        if (m) {
            if (try_pack(out, &pos, &msg_count, m)) {
                msgq_pop(&d->reliable);
            }
            /* Unconditional break: at most one reliable message per
             * datagram regardless of whether it fit.  No loop. */
        }
    }

    /* --- Pass 3: unreliable -- drain + free each; promote on first delivery --- */
    while (d->unreliable.count > 0) {
        bc_qmsg_t *m = msgq_front(&d->unreliable);
        if (!try_pack(out, &pos, &msg_count, m)) break;  /* won't fit / cap */
        if (m->promote && promoted_out && promoted < promoted_cap) {
            promoted_out[promoted++] = m;  /* caller registers for retransmit */
        }
        msgq_pop(&d->unreliable);  /* dequeue + free */
    }

    /* --- ACK-outbox: pending ACKs coalesce into the datagram (fresh ACKs,
     * like the first ACK-processing pass).  They contribute to msg_count and
     * gate Pass 4 below.  ack_count is captured before draining so the gate
     * term reflects whether any ACK was pending this tick. --- */
    int ack_count = d->ack.count;
    while (d->ack.count > 0) {
        bc_qmsg_t *a = msgq_front(&d->ack);
        if (!try_pack(out, &pos, &msg_count, a)) break;
        msgq_pop(&d->ack);
    }

    /* --- Pass 4 gate: (msg_count > 0 OR disconnecting) AND ack_count > 0 --- *
     * This is the ACK-outbox-deadlock gate.  Stale priority-reliable
     * retransmits only flush when the datagram already carries fresh traffic
     * (or the peer is disconnecting) AND there were pending ACKs to coalesce
     * with.  If the gate is closed, the stale entries stay queued. */
    if ((msg_count > 0 || d->disconnecting) && ack_count > 0) {
        /* Pass 4: priority-reliable, retx >= 3; free at retx >= 9. */
        int scanned = 0;
        int guard = d->priority.count;
        while (scanned < guard) {
            int idx = (d->priority.head + scanned) % BC_DRAIN_QUEUE_CAP;
            bc_qmsg_t *m = &d->priority.slot[idx];
            scanned++;
            if (m->retx < BC_DRAIN_RETX_STALE) continue;  /* belongs to Pass 1 */
            if (!try_pack(out, &pos, &msg_count, m)) break;  /* won't fit / cap */
            m->retx++;
            if (m->retx >= BC_DRAIN_RETX_FREE) {
                /* Remove this stale entry.  Compact by shifting the tail
                 * down one slot; adjust the scan cursor so we don't skip
                 * the entry that slid into this position. */
                int n = d->priority.count;
                int from = scanned;  /* logical index after the removed one */
                while (from < n) {
                    int dst = (d->priority.head + from - 1) % BC_DRAIN_QUEUE_CAP;
                    int src = (d->priority.head + from) % BC_DRAIN_QUEUE_CAP;
                    d->priority.slot[dst] = d->priority.slot[src];
                    from++;
                }
                d->priority.count--;
                guard--;
                scanned--;  /* re-examine the slid-in entry next iteration */
            }
        }
    }

    if (promoted_count) *promoted_count = promoted;

    if (msg_count == 0) return 0;  /* nothing to send this tick */

    out[0] = sender_id;
    out[1] = (u8)msg_count;
    return pos;
}
