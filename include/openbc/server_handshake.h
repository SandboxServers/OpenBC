#ifndef OPENBC_SERVER_HANDSHAKE_H
#define OPENBC_SERVER_HANDSHAKE_H

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/transport.h"

/* Handle a new connection request from a client. */
void bc_handle_connect(const bc_addr_t *from, int len);

/* Handle a checksum response from a client during handshake. */
void bc_handle_checksum_response(int peer_slot,
                                 const bc_transport_msg_t *msg);

/* Notify all other peers that a player has left, then remove them. */
void bc_handle_peer_disconnect(int slot);

/* Restore preserved score/team state for reconnecting players by name. */
void bc_try_restore_reconnect_score(int slot, const char *name);

#endif /* OPENBC_SERVER_HANDSHAKE_H */
