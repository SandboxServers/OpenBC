#ifndef OPENBC_SERVER_SEND_H
#define OPENBC_SERVER_SEND_H

#include "openbc/types.h"
#include "openbc/net.h"

/* Queue a reliable message: adds it to the peer's reliable_out queue (the
 * ACK-pruned, retransmit-timed source of truth that flush bundles from). */
void bc_queue_reliable(int peer_slot, const u8 *payload, int payload_len);

/* Queue an unreliable message into a peer's outbox accumulator. */
void bc_queue_unreliable(int peer_slot, const u8 *payload, int payload_len);

/* Send a single unreliable message directly (used for one-off sends
 * to addresses that don't have a peer slot yet, e.g. BootPlayer). */
void bc_send_unreliable_direct(const bc_addr_t *to,
                               const u8 *payload, int payload_len);

/* Flush a peer's outbox in one bundled datagram: packs that peer's pending
 * ACKs / unreliable data plus every *due* reliable_out entry (never-sent, or
 * retransmit interval elapsed).  Idempotent within a tick -- flushing the same
 * peer twice in the same millisecond does not re-send a reliable message. */
void bc_flush_peer(int slot);

/* Flush every active peer (slot 0 = dedi host excluded).  Called once per
 * network tick; each peer gets its own bundled datagram. */
void bc_flush_all_peers(void);

/* Relay a message to all connected peers except the sender.
 * Uses reliable delivery for guaranteed opcodes, unreliable otherwise. */
void bc_relay_to_others(int sender_slot, const u8 *payload, int payload_len,
                        bool reliable);

/* Send a message to ALL peers (including the sender) reliably. */
void bc_send_to_all(const u8 *payload, int payload_len, bool reliable);

#endif /* OPENBC_SERVER_SEND_H */
