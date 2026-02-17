#ifndef OPENBC_GAMESPY_H
#define OPENBC_GAMESPY_H

#include "openbc/types.h"
#include "openbc/net.h"

/*
 * GameSpy query/response handler (QR1 protocol).
 *
 * BC uses the GameSpy QR SDK for server discovery:
 *   - LAN: Clients broadcast queries on port 6500 (BC_GAMESPY_QUERY_PORT)
 *   - Internet: Master servers query on the game port after heartbeat
 *
 * Query types: \basic\  \status\  \info\
 * Response:    \gamename\bcommander\hostname\...\final\
 *
 * Master server handshake:
 *   1. Server -> master:27900  \heartbeat\<port>\gamename\bcommander\
 *   2. Master -> server:game   \secure\<challenge>
 *   3. Server -> master        \validate\<hash>\final\  (gsmsalg response)
 */

#define BC_GAMESPY_QUERY_PORT   6500  /* LAN query port (standard GameSpy) */
#define BC_GAMESPY_SECRET_KEY   "Nm3aZ9"

/* Server info used to build GameSpy responses.
 * Fields match stock BC QR1 callbacks (basic + info + rules).
 *
 * Stock BC \status\ response calls four callbacks in order:
 *   Basic:   \hostname\  \missionscript\  \mapname\  \numplayers\  \maxplayers\  \gamemode\
 *   Info:    \gamename\bcommander  \gamever\1.1  \location\0
 *   Rules:   \timelimit\  \fraglimit\  \system\  \password\
 *   Players: \player_N\<name>  (per connected player)
 */
typedef struct {
    char hostname[64];
    char missionscript[64];    /* Mission script name (e.g. "Multi1") */
    char mapname[64];          /* Human-readable map name (e.g. "Deep Space Encounter") */
    char gamemode[32];         /* "openplaying", "settings", etc. */
    char system[64];           /* Star system script (e.g. "DeepSpace9") */
    int  numplayers;
    int  maxplayers;
    int  timelimit;            /* Minutes, 0 = no limit */
    int  fraglimit;            /* Kills, 0 = no limit */
} bc_server_info_t;

/* Check if a packet is a GameSpy query (starts with '\').
 * This works on both encrypted and unencrypted packets since
 * GameSpy packets are NOT encrypted. */
bool bc_gamespy_is_query(const u8 *data, int len);

/* Check if a packet is a \secure\ challenge from a master server. */
bool bc_gamespy_is_secure(const u8 *data, int len);

/* Extract the challenge string from a \secure\ packet.
 * Returns length of challenge written to 'out', or 0 on failure. */
int bc_gamespy_extract_secure(const u8 *data, int len,
                               char *out, int out_size);

/* Build a GameSpy server info response. Returns bytes written.
 * If query/query_len are provided, extracts queryid and echoes it back. */
int bc_gamespy_build_response(u8 *out, int out_size,
                              const bc_server_info_t *info,
                              const u8 *query, int query_len);

/* Build a \validate\ response to a \secure\ challenge.
 * Uses gsmsalg with the BC secret key. Returns bytes written. */
int bc_gamespy_build_validate(u8 *out, int out_size,
                               const char *challenge);

/* GameSpy Master Server Algorithm (gsmsalg).
 * Computes challenge-response hash for master server authentication.
 * dst must be at least 89 bytes. Returns dst. */
void bc_gsmsalg(char *dst, const char *challenge,
                const char *secret_key, int enctype);

#endif /* OPENBC_GAMESPY_H */
