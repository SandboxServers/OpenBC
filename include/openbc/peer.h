#ifndef OPENBC_PEER_H
#define OPENBC_PEER_H

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/opcodes.h"
#include "openbc/transport.h"
#include "openbc/reliable.h"

/*
 * Peer management -- tracks connected clients in a fixed-size slot array.
 *
 * BC supports up to 6 players. Each peer progresses through states:
 *   EMPTY -> CONNECTING -> CHECKSUMMING -> LOBBY -> IN_GAME -> (disconnect)
 */

typedef enum {
    PEER_EMPTY,               /* Slot is available */
    PEER_CONNECTING,          /* Connection handshake in progress */
    PEER_CHECKSUMMING,        /* Running 4-round checksum validation */
    PEER_CHECKSUMMING_FINAL,  /* Waiting for 0xFF checksum round response */
    PEER_LOBBY,               /* In lobby, waiting for game start */
    PEER_IN_GAME,             /* Actively playing */
} peer_state_t;

typedef struct {
    peer_state_t      state;
    bc_addr_t         addr;
    u32               last_recv_time;  /* Timestamp of last received packet (ms) */
    u8                checksum_round;  /* Current checksum round (0-3) */
    u16               reliable_seq_out; /* Next outgoing reliable sequence number */
    u16               reliable_seq_in;  /* Next expected incoming reliable sequence */
    i32               object_id;       /* Player's ship object ID (-1 if none) */
    char                name[32];        /* Player name */
    bc_fragment_buf_t   fragment;        /* Fragment reassembly state */
    bc_reliable_queue_t reliable_out;    /* Outgoing reliable delivery queue */
    bc_outbox_t         outbox;          /* Outgoing message accumulator */
} bc_peer_t;

typedef struct {
    bc_peer_t peers[BC_MAX_PLAYERS];
    int       count;  /* Number of non-empty peers */
} bc_peer_mgr_t;

/* Initialize peer manager (all slots empty) */
void bc_peers_init(bc_peer_mgr_t *mgr);

/* Find a peer by address. Returns slot index, or -1 if not found. */
int bc_peers_find(const bc_peer_mgr_t *mgr, const bc_addr_t *addr);

/* Allocate a new peer slot. Returns slot index, or -1 if full. */
int bc_peers_add(bc_peer_mgr_t *mgr, const bc_addr_t *addr);

/* Remove a peer (set slot to EMPTY). */
void bc_peers_remove(bc_peer_mgr_t *mgr, int slot);

/* Check for timed-out peers. Removes peers with no activity for timeout_ms.
 * Returns number of peers removed. */
int bc_peers_timeout(bc_peer_mgr_t *mgr, u32 now_ms, u32 timeout_ms);

#endif /* OPENBC_PEER_H */
