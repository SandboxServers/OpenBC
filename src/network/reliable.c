#include "openbc/reliable.h"
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

int bc_reliable_check_retransmit(bc_reliable_queue_t *q, u32 now_ms)
{
    for (int i = 0; i < BC_RELIABLE_QUEUE_SIZE; i++) {
        bc_reliable_entry_t *e = &q->entries[i];
        if (!e->active) continue;

        if (now_ms - e->send_time >= BC_RELIABLE_RETRANSMIT_MS) {
            e->retries++;
            e->send_time = now_ms;
            return i;
        }
    }
    return -1;
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
