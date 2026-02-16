#ifndef OPENBC_RELIABLE_H
#define OPENBC_RELIABLE_H

#include "openbc/types.h"

/*
 * Reliable delivery queue -- tracks unACKed outgoing messages.
 *
 * When a reliable message is sent, it's added to the queue with its
 * sequence number and timestamp. The server periodically checks for
 * messages that need retransmission (every 2 seconds) and disconnects
 * peers that fail to ACK after 8 retries.
 *
 * Ring buffer of 16 entries should be sufficient for the handshake
 * phase (4 checksum requests + settings + gameinit) and typical
 * in-game reliable traffic.
 */

#define BC_RELIABLE_QUEUE_SIZE   16
#define BC_RELIABLE_MAX_PAYLOAD  512
#define BC_RELIABLE_RETRANSMIT_MS 2000  /* Retransmit after 2 seconds */
#define BC_RELIABLE_MAX_RETRIES  8      /* Give up after 8 retries */

typedef struct {
    u8   payload[BC_RELIABLE_MAX_PAYLOAD];
    int  payload_len;
    u16  seq;
    u32  send_time;    /* Timestamp when last sent (ms) */
    u8   retries;      /* Number of retransmission attempts */
    bool active;       /* Entry is in use (waiting for ACK) */
} bc_reliable_entry_t;

typedef struct {
    bc_reliable_entry_t entries[BC_RELIABLE_QUEUE_SIZE];
    int count;  /* Number of active entries */
} bc_reliable_queue_t;

/* Initialize a reliable queue (all entries inactive). */
void bc_reliable_init(bc_reliable_queue_t *q);

/* Add a message to the queue. Returns true on success.
 * Caller provides the pre-built reliable payload and sequence number.
 * now_ms is the current timestamp. */
bool bc_reliable_add(bc_reliable_queue_t *q,
                     const u8 *payload, int payload_len,
                     u16 seq, u32 now_ms);

/* Mark a message as acknowledged (remove from queue).
 * Returns true if the seq was found and removed. */
bool bc_reliable_ack(bc_reliable_queue_t *q, u16 seq);

/* Check for messages that need retransmission.
 * Returns the index of the next entry needing retransmit, or -1 if none.
 * Caller should send the payload and call again until -1 is returned.
 * Updates the entry's send_time and retry count. */
int bc_reliable_check_retransmit(bc_reliable_queue_t *q, u32 now_ms);

/* Check if any entry has exceeded max retries.
 * Returns true if the peer should be considered dead. */
bool bc_reliable_check_timeout(const bc_reliable_queue_t *q);

#endif /* OPENBC_RELIABLE_H */
