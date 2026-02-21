#ifndef OPENBC_PLAYER_IDS_H
#define OPENBC_PLAYER_IDS_H

#include "openbc/types.h"
#include "openbc/opcodes.h"

/*
 * Score messages (0x36/0x37) use the network player ID domain:
 * the same IDs returned by client GetNetID() (wire_slot numbering),
 * not ship/object IDs.
 */

static inline i32 bc_player_id_from_peer_slot(int peer_slot)
{
    if (peer_slot < 0 || peer_slot >= BC_MAX_PLAYERS) return 0;
    return (i32)(peer_slot + 1);  /* wire_slot */
}

static inline i32 bc_player_id_from_game_slot(int game_slot)
{
    /* game_slot 0 maps to peer_slot 1 on dedicated server */
    return bc_player_id_from_peer_slot(game_slot + 1);
}

static inline bool bc_is_valid_player_id(i32 player_id)
{
    return player_id > 0 && player_id <= BC_MAX_PLAYERS;
}

#endif /* OPENBC_PLAYER_IDS_H */
