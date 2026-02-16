#ifndef OPENBC_GAMESPY_H
#define OPENBC_GAMESPY_H

#include "openbc/types.h"
#include "openbc/net.h"

/*
 * GameSpy LAN query handler.
 *
 * BC uses the GameSpy QR SDK for LAN server discovery. Queries arrive
 * as plaintext on the shared game UDP socket (first byte = '\\').
 *
 * Query:    \basic\  or  \status\  or  \info\
 * Response: \hostname\...\numplayers\...\maxplayers\...\mapname\...\gametype\...\hostport\...\
 */

/* Server info used to build GameSpy responses */
typedef struct {
    char hostname[64];
    char mapname[64];
    char gametype[32];
    u16  hostport;
    int  numplayers;
    int  maxplayers;
} bc_server_info_t;

/* Check if a packet is a GameSpy query (starts with '\').
 * This works on both encrypted and unencrypted packets since
 * GameSpy packets are NOT encrypted. */
bool bc_gamespy_is_query(const u8 *data, int len);

/* Build a GameSpy response into 'out'. Returns bytes written.
 * Caller must send the response back to the query sender. */
int bc_gamespy_build_response(u8 *out, int out_size,
                              const bc_server_info_t *info);

#endif /* OPENBC_GAMESPY_H */
