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
    char missionscript[64];    /* Mission script path (e.g. "Multiplayer.Episode.Mission1.Mission1") */
    char mapname[64];          /* Game mode display (e.g. "DM") -- shown in Type column */
    char gamemode[32];         /* "openplaying", "settings", etc. */
    char system[64];           /* System key (e.g. "Multi1") -- shown in Game Info */
    int  numplayers;
    int  maxplayers;
    int  timelimit;            /* Minutes, 0 = no limit */
    int  fraglimit;            /* Kills, 0 = no limit */
    /* Player list for GameSpy \player_N\ entries.
     * player_names[0] = "Dedicated Server" (always present). */
    char player_names[8][32];
    int  player_count;         /* Number of entries in player_names[] */
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

/* Sanitize a player name for safe embedding in a GameSpy QR1 response.
 *
 * GameSpy QR1 uses backslash (\) as the key-value delimiter, so a raw
 * player name that contains backslashes can inject extra key-value pairs
 * into the response -- e.g. a name like "Eve\numplayers\99" would corrupt
 * the numplayers field seen by LAN browsers and master servers.
 *
 * This function copies src into dst (up to dst_size-1 bytes), silently
 * dropping every character that is a backslash or an ASCII control
 * character (< 0x20).  The result is always NUL-terminated.
 *
 * Call this whenever a network-supplied name is stored into
 * bc_server_info_t.player_names[] before building a GameSpy response. */
void bc_gamespy_sanitize_name(char *dst, size_t dst_size, const char *src);

#endif /* OPENBC_GAMESPY_H */
