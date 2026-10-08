#include "openbc/reliable.h"  /* pulls in transport.h for bc_outbox_t */
#include <string.h>

void bc_reliable_init(bc_reliable_queue_t *q)
{
    memset(q, 0, sizeof(*q));
}

bool bc_reliable_add(bc_reliable_queue_t *q,
                     const u8 *payload, int payload_len,
                     u16 seq, u32 now_ms)
{
    if (payload_len > BC_RELIABLE_MAX_PAYLOAD) return false;

    /* Find a free slot */
    for (int i = 0; i < BC_RELIABLE_QUEUE_SIZE; i++) {
        if (!q->entries[i].active) {
            memcpy(q->entries[i].payload, payload, (size_t)payload_len);
            q->entries[i].payload_len = payload_len;
            q->entries[i].seq = seq;
            q->entries[i].send_time = now_ms;
            q->entries[i].retries = 0;
            q->entries[i].active = true;
            q->entries[i].sent = false;  /* due for its first send on next flush */
            q->count++;
            return true;
        }
    }
    return false; /* Queue full */
}

bool bc_reliable_ack(bc_reliable_queue_t *q, u16 seq)
{
    for (int i = 0; i < BC_RELIABLE_QUEUE_SIZE; i++) {
        if (q->entries[i].active && q->entries[i].seq == seq) {
            q->entries[i].active = false;
            q->count--;
            return true;
        }
    }
    return false;
}

int bc_reliable_pack_due(bc_reliable_queue_t *q, bc_outbox_t *outbox,
                         u32 now_ms, int *retransmits_out)
{
    int packed = 0;
    int retransmits = 0;

    /* Entries are bundled in seq order to keep the on-wire ordering stable.
     * The queue is small (16) so a linear pass is fine. */
    for (int i = 0; i < BC_RELIABLE_QUEUE_SIZE; i++) {
        bc_reliable_entry_t *e = &q->entries[i];
        if (!e->active) continue;

        bool first_send = !e->sent;
        bool retransmit_due =
            (now_ms - e->send_time) >= BC_RELIABLE_RETRANSMIT_MS;

        /* Due = never sent OR the retransmit interval has elapsed. */
        if (!first_send && !retransmit_due) continue;

        /* Try to bundle into the current datagram.  If it won't fit, leave the
         * entry untouched (still due) -- the next flush picks it up.  This keeps
         * the message on the reliable queue until it is both packed AND ACKed. */
        if (!bc_outbox_add_reliable(outbox, e->payload, e->payload_len, e->seq))
            break;

        if (!first_send) {
            e->retries++;       /* count retransmits only, not the first send */
            retransmits++;
        }
        e->sent = true;
        e->send_time = now_ms;  /* idempotent within a tick: re-pack is not due */
        packed++;
    }

    if (retransmits_out) *retransmits_out = retransmits;
    return packed;
}

bool bc_reliable_check_timeout(const bc_reliable_queue_t *q)
{
    for (int i = 0; i < BC_RELIABLE_QUEUE_SIZE; i++) {
        if (q->entries[i].active &&
            q->entries[i].retries >= BC_RELIABLE_MAX_RETRIES) {
            return true;
        }
    }
    return false;
}
