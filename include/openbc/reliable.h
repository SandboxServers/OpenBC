#ifndef OPENBC_RELIABLE_H
#define OPENBC_RELIABLE_H

#include "openbc/types.h"
/* bc_reliable_pack_due() bundles due reliable entries into the per-peer outbox
 * accumulator (bc_outbox_t).  transport.h does not include reliable.h, so this
 * include is not circular. */
#include "openbc/transport.h"

/*
 * Reliable delivery queue -- tracks unACKed outgoing messages AND is the
 * single source of truth for bundling reliable traffic onto the wire.
 *
 * When a reliable message is queued it is added here (inactive->active) with
 * its sequence number and an `active`/`sent` state.  Each flush of a peer packs
 * every *due* entry -- one that has never been sent, or whose retransmit
 * interval has elapsed since it was last sent -- into the outgoing datagram,
 * marks it sent, and stamps send_time = now.  Because send_time is bumped to
 * `now` when an entry is packed, draining the same peer twice within the same
 * tick does NOT re-send it (the second drain sees `now - send_time == 0`, which
 * is below the retransmit interval): bundling is idempotent within a tick.
 *
 * An incoming ACK removes the matching entry (bc_reliable_ack), so an ACKed
 * message is never packed again -- ACK removal and bundling share one queue.
 *
 * The server periodically (every ~1s) packs due entries during the per-peer
 * flush and disconnects peers that fail to ACK after BC_RELIABLE_MAX_RETRIES.
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
    u32  send_time;    /* Timestamp when last packed onto the wire (ms) */
    u8   retries;      /* Number of retransmission attempts */
    bool active;       /* Entry is in use (waiting for ACK) */
    bool sent;         /* Has been packed at least once (false = first-send due) */
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

/* Pack every *due* reliable entry into the outbox accumulator, bundling them
 * into the same datagram as any ACKs / unreliable data already queued there.
 *
 * An entry is due when it has never been sent (active && !sent) OR its
 * retransmit interval has elapsed (now_ms - send_time >= BC_RELIABLE_RETRANSMIT_MS).
 * For each packed entry: sent=true, send_time=now_ms, and retries is bumped only
 * when this was a retransmit (not the first send).
 *
 * Stamping send_time=now_ms makes this idempotent within a tick: a second call
 * at the same now_ms re-packs nothing, so draining a peer multiple times per
 * tick never duplicates a reliable message on the wire.
 *
 * Returns the number of entries packed.  retransmits_out (optional) receives the
 * count of packed entries that were retransmits (already-sent), for stats. */
int bc_reliable_pack_due(bc_reliable_queue_t *q, bc_outbox_t *outbox,
                         u32 now_ms, int *retransmits_out);

/* Check if any entry has exceeded max retries.
 * Returns true if the peer should be considered dead. */
bool bc_reliable_check_timeout(const bc_reliable_queue_t *q);

#endif /* OPENBC_RELIABLE_H */
