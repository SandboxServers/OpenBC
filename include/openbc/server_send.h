#ifndef OPENBC_SERVER_SEND_H
#define OPENBC_SERVER_SEND_H

#include "openbc/types.h"
#include "openbc/net.h"

/* Queue a reliable message into a peer's outbox + track for retransmit. */
void bc_queue_reliable(int peer_slot, const u8 *payload, int payload_len);

/* Queue an unreliable message into a peer's outbox. */
void bc_queue_unreliable(int peer_slot, const u8 *payload, int payload_len);

/* Send a single unreliable message directly (used for one-off sends
 * to addresses that don't have a peer slot yet, e.g. BootPlayer). */
void bc_send_unreliable_direct(const bc_addr_t *to,
                               const u8 *payload, int payload_len);

/* Flush a peer's outbox with optional SEND trace logging. */
void bc_flush_peer(int slot);

/* Relay a message to all connected peers except the sender.
 * Uses reliable delivery for guaranteed opcodes, unreliable otherwise. */
void bc_relay_to_others(int sender_slot, const u8 *payload, int payload_len,
                        bool reliable);

/* Send a message to ALL peers (including the sender) reliably. */
void bc_send_to_all(const u8 *payload, int payload_len, bool reliable);

#endif /* OPENBC_SERVER_SEND_H */
