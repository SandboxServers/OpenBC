#ifndef OPENBC_DRAIN_H
#define OPENBC_DRAIN_H

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/transport.h"

/*
 * Per-peer 4-pass outbound drain.
 *
 * Wire-protocol trace analysis of a stock dedicated server session shows
 * outbound UDP datagrams are NOT one-message-per-datagram: the stock server
 * packs up to 255 transport messages into a single 512-byte datagram per peer
 * per tick by draining four per-peer queues in a fixed priority order.  This
 * module reproduces that bundling so OpenBC's burst-absorption matches stock.
 *
 * The four queues and their drain passes:
 *
 *   Pass 1  priority-reliable, retx < 3   pack multiple; break on won't-fit
 *                                          or the 255-message cap
 *   Pass 2  reliable                       ONE-SHOT: attempt once, then break
 *                                          unconditionally (>= 1 reliable per
 *                                          datagram is the design cap)
 *   Pass 3  unreliable                     pack multiple; dequeue + free each
 *                                          as drained; PROMOTE to the reliable
 *                                          tracking path on first delivery
 *   Pass 4  priority-reliable, retx >= 3   gated on
 *           (free at retx >= 9)              (msg_count > 0 OR peer
 *                                            disconnecting) AND ack_count > 0
 *
 * Per-datagram constants come straight from the wire format:
 *   - MTU                 512 bytes (BC_MAX_PACKET_SIZE)
 *   - Datagram header     2 bytes: [u8 senderPeerId][u8 messageCount]
 *   - Message-count cap   255 per datagram
 *   - Fragment chunk max  412 bytes (MTU - 100), decided at queue time
 *
 * The Pass-4 gate is the documented ACK-outbox deadlock condition
 * (docs/bugs/ack-outbox-deadlock.md): stale priority-reliable retransmits only
 * flush when the datagram already carries fresh traffic (or the peer is
 * disconnecting) AND there are pending ACKs to coalesce with.
 */

#define BC_DRAIN_MSG_MAX     512   /* Max serialized bytes of one queued msg  */
#define BC_DRAIN_QUEUE_CAP   64    /* Per-queue ring capacity                 */
#define BC_DRAIN_MSGCOUNT_MAX 255  /* Per-datagram message-count cap          */
#define BC_DRAIN_HEADER_LEN  2     /* [senderPeerId][messageCount]            */
#define BC_DRAIN_FRAG_CHUNK_MAX 412 /* MTU - 100, fragment chunk decided at   */
                                    /* queue time                             */

/* P1 fires while retx < this; P4 fires while retx >= this. */
#define BC_DRAIN_RETX_STALE  3
/* A stale priority-reliable message is freed once retx reaches this. */
#define BC_DRAIN_RETX_FREE   9

/* One queued transport message, already serialized into wire bytes by the
 * existing bc_outbox_add_* encoders.  Storing the encoded bytes keeps the
 * on-wire format byte-for-byte identical to the single-message path. */
typedef struct {
    u8  bytes[BC_DRAIN_MSG_MAX]; /* Serialized transport message            */
    int len;                     /* Number of valid bytes                   */
    u16 seq;                     /* Reliable sequence (priority/reliable)   */
    u8  retx;                    /* Retransmit count (priority queue only)  */
    bool promote;                /* Pass 3: promote to reliable tracking    */
} bc_qmsg_t;

/* A simple ring queue of serialized messages. */
typedef struct {
    bc_qmsg_t slot[BC_DRAIN_QUEUE_CAP];
    int head;   /* Next dequeue index                                       */
    int count;  /* Number of queued messages                                */
} bc_msgq_t;

/* Per-peer drain state: the four queues plus the disconnecting flag that
 * (with a non-empty ACK queue) opens the Pass-4 gate. */
typedef struct {
    bc_msgq_t priority;     /* Priority-reliable (Pass 1 + Pass 4)          */
    bc_msgq_t reliable;     /* Reliable (Pass 2, one-shot)                  */
    bc_msgq_t unreliable;   /* Unreliable (Pass 3, drain + promote)         */
    bc_msgq_t ack;          /* ACK-outbox (Pass-4 gate: ack_count > 0)      */
    bool disconnecting;     /* Peer is tearing down (opens Pass-4 gate)     */
} bc_drain_t;

/* Reset all drain queues for a peer. */
void bc_drain_init(bc_drain_t *d);

/* Advance the round-robin peer cursor used for fairness across ticks.
 * 'cursor' is a 1-based slot index (slot 0 is the host and is never drained).
 * num_slots is the total number of peer slots (BC_MAX_PLAYERS).  Returns the
 * next cursor, wrapping over the 1..num_slots-1 range. */
int bc_drain_next_cursor(int cursor, int num_slots);

/* Enqueue helpers -- each serializes into wire bytes using the same encoders
 * as the single-message outbox, then stores the encoded message in a queue.
 * Return true on success, false if the target queue is full. */
bool bc_drain_enqueue_priority(bc_drain_t *d, const u8 *payload, int len, u16 seq);
bool bc_drain_enqueue_reliable(bc_drain_t *d, const u8 *payload, int len, u16 seq);
bool bc_drain_enqueue_unreliable(bc_drain_t *d, const u8 *payload, int len);
bool bc_drain_enqueue_ack(bc_drain_t *d, u16 seq, u8 flags);
bool bc_drain_enqueue_fragment_ack(bc_drain_t *d, u16 seq, u8 frag_idx);

/* True if any of the four queues has a pending message. */
bool bc_drain_pending(const bc_drain_t *d);

/*
 * Run the 4-pass drain for one peer into a single datagram and serialize it
 * into 'out' (caller supplies a BC_MAX_PACKET_SIZE buffer).  Writes the
 * 2-byte header [sender_id][msg_count].  Returns the datagram length, or 0 if
 * nothing was sent this tick (e.g. Pass-4 gate closed and no other traffic).
 *
 * promoted_out (optional) receives an array of pointers to the messages that
 * Pass 3 promoted to reliable tracking, so the caller can register them with
 * its reliable-retransmit queue.  Pass NULL to ignore promotion.
 */
int bc_drain_to_buf(bc_drain_t *d, u8 sender_id, u8 *out, int out_size,
                    const bc_qmsg_t **promoted_out, int promoted_cap,
                    int *promoted_count);

#endif /* OPENBC_DRAIN_H */
