#ifndef OPENBC_MASTER_H
#define OPENBC_MASTER_H

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/gamespy.h"

/*
 * Master server heartbeat -- registers with GameSpy-compatible master
 * servers for internet play discovery.
 *
 * Heartbeat format: \heartbeat\<port>\gamename\bcommander\
 * Shutdown format:  \heartbeat\<port>\gamename\bcommander\final\
 *
 * Supports multiple master servers (333networks affiliates, OpenSpy, etc.).
 * At startup, probes all masters and reports which responded.
 * Continues heartbeating all enabled masters regardless of probe result.
 */

#define BC_MAX_MASTERS              16
#define BC_MASTER_HEARTBEAT_INTERVAL 60000  /* 60 seconds */
#define BC_MASTER_PROBE_TIMEOUT_MS   3000   /* 3 second startup probe window */

typedef struct {
    bc_addr_t addr;              /* Resolved IP:port */
    char      hostname[128];     /* Original "host:port" for logging */
    u32       last_beat;         /* Timestamp of last heartbeat sent */
    bool      enabled;           /* DNS resolved, active */
    bool      verified;          /* Got response (secure or status query) */
    u32       status_checks;     /* Number of \status\ queries received */
} bc_master_entry_t;

typedef struct {
    bc_master_entry_t entries[BC_MAX_MASTERS];
    int  count;
    u16  game_port;              /* Local port to advertise */
} bc_master_list_t;

/* Initialize with default master servers. Returns count of resolved entries. */
int bc_master_init_defaults(bc_master_list_t *ml, u16 game_port);

/* Add a single master server by "host:port". Returns true if DNS resolved. */
bool bc_master_add(bc_master_list_t *ml, const char *host_port, u16 game_port);

/* Startup probe: heartbeat all masters, wait for responses, log results.
 * If info is non-NULL, responds to GameSpy queries received during probe. */
void bc_master_probe(bc_master_list_t *ml, bc_socket_t *sock,
                     const bc_server_info_t *info);

/* Check if a packet came from a known master address. */
bool bc_master_is_from_master(const bc_master_list_t *ml, const bc_addr_t *from);

/* Mark a master as verified (registered) if not already.
 * Returns the hostname if newly verified, NULL otherwise. */
const char *bc_master_mark_verified(bc_master_list_t *ml, const bc_addr_t *from);

/* Record a \status\ query from a master. Increments status_checks counter.
 * Returns the hostname if this is the first status check (= master listed us),
 * NULL if not from a known master or already status-checked. */
const char *bc_master_record_status_check(bc_master_list_t *ml, const bc_addr_t *from);

/* Periodic tick: heartbeat all enabled masters if interval elapsed. */
void bc_master_tick(bc_master_list_t *ml, bc_socket_t *sock, u32 now_ms);

/* Shutdown: send final heartbeat to all. */
void bc_master_shutdown(bc_master_list_t *ml, bc_socket_t *sock);

#endif /* OPENBC_MASTER_H */
