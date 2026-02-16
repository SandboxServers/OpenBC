#ifndef OPENBC_MASTER_H
#define OPENBC_MASTER_H

#include "openbc/types.h"
#include "openbc/net.h"

/*
 * Master server heartbeat -- registers with a GameSpy-compatible master
 * server for internet play discovery.
 *
 * Heartbeat format: \heartbeat\<port>\gamename\stbc\
 * Shutdown format:  \heartbeat\<port>\gamename\stbc\final\
 *
 * Sent via UDP to the master server at a configurable interval (default 60s).
 * The master server tracks active game servers and responds to GameSpy
 * browser queries with server lists.
 */

#define BC_MASTER_HEARTBEAT_INTERVAL 60000  /* 60 seconds */

typedef struct {
    bc_addr_t addr;         /* Master server address (resolved at init) */
    u16       game_port;    /* Local game port to advertise */
    u32       last_beat;    /* Timestamp of last heartbeat sent */
    bool      enabled;      /* Heartbeat active */
    bool      resolved;     /* DNS resolution succeeded */
} bc_master_t;

/* Initialize the master server connection.
 * host_port is e.g. "master.gamespy.com:27900" or "192.168.1.100:27900".
 * game_port is the local game port being advertised.
 * Returns true if DNS resolution succeeded. */
bool bc_master_init(bc_master_t *ms, const char *host_port, u16 game_port);

/* Send a periodic heartbeat if the interval has elapsed.
 * Call this from the main loop every tick. */
void bc_master_tick(bc_master_t *ms, bc_socket_t *sock, u32 now_ms);

/* Send a final heartbeat (server shutting down). */
void bc_master_shutdown(bc_master_t *ms, bc_socket_t *sock);

#endif /* OPENBC_MASTER_H */
