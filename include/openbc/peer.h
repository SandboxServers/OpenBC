#ifndef OPENBC_PEER_H
#define OPENBC_PEER_H

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/opcodes.h"
#include "openbc/transport.h"
#include "openbc/reliable.h"
#include "openbc/ship_state.h"

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

/*
 * MAINTENANCE HAZARD -- GCC -O2 DEAD-STORE ELIMINATION
 *
 * i686-w64-mingw32-gcc -O2 silently drops stores into this struct that
 * follow a memset() when it can prove the destination is already zero.
 * bc_peers_add() and bc_peers_remove() use volatile byte-copy loops to
 * force the writes through.
 *
 * Adding ANY new field to bc_peer_t, or to any type embedded here
 * (bc_ship_state_t, bc_outbox_t, bc_reliable_queue_t, etc.), can retrigger
 * this bug on the next Win32 -O2 build -- the new field may silently read
 * back as zero when it should have been written.  After adding fields:
 *   1. Rebuild with i686-w64-mingw32-gcc -O2
 *   2. Verify peer state is intact after bc_peers_add() on a real client connect
 *   3. If the new field is a non-zero sentinel, add a volatile write for it in
 *      bc_peers_add() and bc_peers_remove() in peer.c
 * See peer.c for the volatile workaround and CLAUDE.md for the full history.
 */
typedef struct {
    peer_state_t      state;
    bc_addr_t         addr;
    u32               last_recv_time;  /* Timestamp of last received packet (ms) */
    u8                checksum_round;  /* Current checksum round (0-3) */
    u16               reliable_seq_out; /* Next outgoing reliable sequence number */
    u16               reliable_seq_in;  /* Next expected incoming reliable sequence */
    i32               object_id;       /* Player's ship object ID (-1 if none) */
    u32               connect_time;    /* GetTickCount() when peer connected */
    char                name[32];        /* Player name */
    u8                  keepalive_data[64]; /* Cached client keepalive payload for echo */
    int                 keepalive_len;     /* Length of cached keepalive (0 = none) */
    u8                  spawn_payload[256]; /* Cached ObjCreateTeam for late-join forwarding */
    int                 spawn_len;         /* 0 = no ship spawned yet */
    i32                 score;             /* Preserved across disconnect for rejoin */
    i32                 kills;             /* Kill count */
    i32                 deaths;            /* Death count */
    bc_fragment_buf_t   fragment;        /* Fragment reassembly state */
    bc_reliable_queue_t reliable_out;    /* Outgoing reliable delivery queue */
    bc_outbox_t         outbox;          /* Outgoing message accumulator */

    /* Server-authoritative ship state (Phase E) */
    bc_ship_state_t     ship;            /* Server-tracked ship HP, position, etc. */
    int                 class_index;     /* Index into registry->ships[] (-1 = none) */
    bool                has_ship;        /* True after ObjCreateTeam parsed */
    u8                  subsys_rr_idx;   /* Round-robin index for 0x20 health broadcasts */
    u32                 last_fire_time[BC_MAX_PHASER_BANKS];  /* Anti-cheat: last fire ms */
    u32                 last_torpedo_time[BC_MAX_TORPEDO_TUBES]; /* Anti-cheat: last torp ms */
    u8                  fire_violations; /* Rapid-fire violation counter */
    u32                 violation_window_start; /* Window start for rate-limit tracking */

    /* Respawn */
    f32                 respawn_timer;  /* Countdown to respawn (0 = not waiting) */
    int                 respawn_class;  /* Ship class for respawn (-1 = none) */
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
