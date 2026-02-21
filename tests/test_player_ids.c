#include "test_util.h"
#include "openbc/player_ids.h"
#include "openbc/opcodes.h"

TEST(peer_slot_to_player_id)
{
    ASSERT_EQ_INT(bc_player_id_from_peer_slot(0), 1);
    ASSERT_EQ_INT(bc_player_id_from_peer_slot(1), 2);
    ASSERT_EQ_INT(bc_player_id_from_peer_slot(6), 7);
}

TEST(game_slot_to_player_id)
{
    ASSERT_EQ_INT(bc_player_id_from_game_slot(0), 2);
    ASSERT_EQ_INT(bc_player_id_from_game_slot(1), 3);
    ASSERT_EQ_INT(bc_player_id_from_game_slot(5), 7);
}

TEST(invalid_slot_returns_zero)
{
    ASSERT_EQ_INT(bc_player_id_from_peer_slot(-1), 0);
    ASSERT_EQ_INT(bc_player_id_from_peer_slot(BC_MAX_PLAYERS), 0);
    ASSERT_EQ_INT(bc_player_id_from_game_slot(-2), 0);
    ASSERT_EQ_INT(bc_player_id_from_game_slot(BC_MAX_PLAYERS), 0);
}

TEST(validity_check)
{
    ASSERT(!bc_is_valid_player_id(0));
    ASSERT(bc_is_valid_player_id(1));
    ASSERT(bc_is_valid_player_id(2));
    ASSERT(bc_is_valid_player_id(BC_MAX_PLAYERS));
    ASSERT(!bc_is_valid_player_id(BC_MAX_PLAYERS + 1));
}

TEST_MAIN_BEGIN()
    RUN(peer_slot_to_player_id);
    RUN(game_slot_to_player_id);
    RUN(invalid_slot_returns_zero);
    RUN(validity_check);
TEST_MAIN_END()
