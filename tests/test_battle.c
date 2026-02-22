/*
 * Battle of Valentine's Day -- Integration Test
 *
 * Recreates an abridged 3-player battle from real packet traces.
 * Three headless clients connect to a live OpenBC server, exchange
 * ships, weapons fire, damage, repairs, collisions, and chat --
 * all through the wire protocol.
 *
 * Source: reference battle trace (3-player, 15 minutes, all opcode types)
 */

#include "test_util.h"
#include "test_harness.h"
#include "openbc/game_builders.h"
#include "openbc/game_events.h"
#include "openbc/opcodes.h"
#include <math.h>

/* ======================================================================
 * Captured payloads (byte-for-byte from packet_trace.log)
 * ====================================================================== */

/* Ship data blob for Cady2's ship (from line 512, 108 bytes after [03][00][02])
 * Galaxy-class, slot 0, team 2. Contains serialized class, position,
 * orientation, subsystem health, weapon loadout. */
static const u8 CADY_SHIP_DATA[] = {
    0x08, 0x80, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x3F, 0x01, 0x00, 0x00, 0xB0,
    0x42, 0x00, 0x00, 0x84, 0xC2, 0x00, 0x00, 0x92, 0xC2, 0xF5, 0x4A, 0x6F,
    0x3F, 0xFE, 0x8C, 0x96, 0x3E, 0x84, 0xE3, 0x4B, 0x3E, 0x38, 0x78, 0x4E,
    0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x43, 0x61, 0x64,
    0x79, 0x32, 0x06, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x31, 0xFF, 0xFF, 0x64,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x64, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x64, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x64, 0x60, 0x01, 0xFF, 0xFF, 0xFF, 0x64, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x64, 0x00, 0xFF, 0x64, 0xFF, 0xFF, 0xFF, 0x64, 0x01, 0xFF,
};

/* Ship data blob for XFS01 Dauntless (from line 5141, 121 bytes after [03][01][03])
 * Sovereign-class variant, slot 1, team 3. */
static const u8 DAUNTLESS_SHIP_DATA[] = {
    0x08, 0x80, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x40, 0x03, 0x00, 0x00, 0x60,
    0x41, 0x00, 0x00, 0xD8, 0xC1, 0x00, 0x00, 0x90, 0x41, 0xB5, 0xCE, 0x0F,
    0x3F, 0x39, 0xDC, 0xE3, 0xB3, 0x43, 0xC2, 0x15, 0xBF, 0x43, 0xC2, 0x15,
    0xBF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x58, 0x46, 0x53,
    0x30, 0x31, 0x20, 0x44, 0x61, 0x75, 0x6E, 0x74, 0x6C, 0x65, 0x73, 0x73,
    0x06, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x31, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x64, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x64, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x64, 0x60, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0x64, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x64, 0xFF,
    0xFF, 0xFF, 0x64, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x64, 0x01, 0xFF, 0xFF,
    0x64,
};

/* StartFiring event data (from line 2178, 24 bytes after opcode 0x07) */
static const u8 START_FIRING_DATA[] = {
    0x28, 0x81, 0x00, 0x00, 0xD8, 0x00, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* SetPhaserLevel event data (from line 26040, 17 bytes after opcode 0x12) */
static const u8 SET_PHASER_LEVEL_DATA[] = {
    0x05, 0x01, 0x00, 0x00, 0xE0, 0x00, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x40,
    0x00,
};

/* CollisionEffect data (from line 15597, 25 bytes after opcode 0x15) */
static const u8 COLLISION_EFFECT_DATA[] = {
    0x24, 0x81, 0x00, 0x00, 0x50, 0x00, 0x80, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x03, 0x40,
    0x01, 0x27, 0x77, 0x11, 0xB8, 0x9D, 0x47, 0x25,
    0x44,
};

/* ======================================================================
 * Server + client globals
 * ====================================================================== */

static bc_test_server_t g_server;
static bc_test_client_t g_cady, g_kirk, g_sep;
static int g_assertions = 0;

#define BATTLE_PORT      29876
#define TIMEOUT          1000  /* ms */
#define MANIFEST_PATH    "tests/fixtures/manifest.json"
#define GAME_DIR         "tests/fixtures/"

/* Assertion helper that counts */
#define BATTLE_ASSERT(cond) do { \
    g_assertions++; \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        goto cleanup; \
    } \
} while(0)

/* ======================================================================
 * Phase implementations
 * ====================================================================== */

static int run_battle(void)
{
    printf("  Battle of Valentine's Day\n");

    /* === Phase 1: CONNECT === */
    printf("    Phase 1: Connect 3 players...\n");

    BATTLE_ASSERT(bc_net_init());
    BATTLE_ASSERT(test_server_start(&g_server, BATTLE_PORT, MANIFEST_PATH));

    BATTLE_ASSERT(test_client_connect(&g_cady, BATTLE_PORT, "Cady2", 0, GAME_DIR));
    BATTLE_ASSERT(test_client_connect(&g_kirk, BATTLE_PORT, "Kirk", 1, GAME_DIR));
    BATTLE_ASSERT(test_client_connect(&g_sep,  BATTLE_PORT, "Sep", 2, GAME_DIR));

    /* Small delay for server to process all connections */
    Sleep(200);

    /* Drain any NewPlayerInGame notifications from other players joining */
    test_client_drain(&g_cady, 300);
    test_client_drain(&g_kirk, 300);
    test_client_drain(&g_sep,  300);

    /* === Phase 2: SPAWN SHIPS === */
    printf("    Phase 2: Spawn ships (extracted payloads)...\n");
    {
        u8 buf[256];

        /* Cady spawns a ship (owner=0, team=2) */
        int len = bc_build_object_create_team(buf, sizeof(buf), 0, 2,
                                               CADY_SHIP_DATA,
                                               sizeof(CADY_SHIP_DATA));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_cady, buf, len));

        /* Kirk spawns a ship (owner=1, team=2) -- same ship class */
        len = bc_build_object_create_team(buf, sizeof(buf), 1, 2,
                                           CADY_SHIP_DATA,
                                           sizeof(CADY_SHIP_DATA));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        /* Sep spawns a Dauntless (owner=2, team=4) */
        len = bc_build_object_create_team(buf, sizeof(buf), 2, 4,
                                           DAUNTLESS_SHIP_DATA,
                                           sizeof(DAUNTLESS_SHIP_DATA));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_sep, buf, len));

        Sleep(200);

        /* Each client should receive the other 2 ObjectCreateTeam messages */
        int msg_len = 0;
        const u8 *msg;

        msg = test_client_expect_opcode(&g_cady, BC_OP_OBJ_CREATE_TEAM,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        msg = test_client_expect_opcode(&g_cady, BC_OP_OBJ_CREATE_TEAM,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);

        msg = test_client_expect_opcode(&g_kirk, BC_OP_OBJ_CREATE_TEAM,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        msg = test_client_expect_opcode(&g_kirk, BC_OP_OBJ_CREATE_TEAM,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);

        msg = test_client_expect_opcode(&g_sep, BC_OP_OBJ_CREATE_TEAM,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        msg = test_client_expect_opcode(&g_sep, BC_OP_OBJ_CREATE_TEAM,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
    }

    /* === Phase 3: MANEUVER === */
    printf("    Phase 3: StateUpdate maneuver...\n");
    {
        u8 buf[64];
        u8 field_data[] = { 0x7F, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x7F,
                            0x50, 0x42, 0x00, 0x54 };

        int len = bc_build_state_update(buf, sizeof(buf),
                                         bc_make_ship_id(0), 10.0f,
                                         BC_DIRTY_POSITION_DELTA | BC_DIRTY_ORIENT_FWD |
                                         BC_DIRTY_ORIENT_UP | BC_DIRTY_SPEED,
                                         field_data, sizeof(field_data));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_unreliable(&g_cady, buf, len));

        len = bc_build_state_update(buf, sizeof(buf),
                                     bc_make_ship_id(1), 10.0f, 0x1E,
                                     field_data, sizeof(field_data));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_unreliable(&g_kirk, buf, len));

        len = bc_build_state_update(buf, sizeof(buf),
                                     bc_make_ship_id(2), 10.0f, 0x1E,
                                     field_data, sizeof(field_data));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_unreliable(&g_sep, buf, len));

        Sleep(200);

        /* Sep should receive at least 1 StateUpdate (unreliable, may be lost) */
        int msg_len = 0;
        const u8 *msg = test_client_expect_opcode(&g_sep, BC_OP_STATE_UPDATE,
                                                    &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
    }

    /* === Phase 4: POWER ALLOCATION === */
    printf("    Phase 4: SetPhaserLevel...\n");
    {
        u8 buf[32];
        int len = bc_build_event_forward(buf, sizeof(buf),
                                          BC_OP_SET_PHASER_LEVEL,
                                          SET_PHASER_LEVEL_DATA,
                                          sizeof(SET_PHASER_LEVEL_DATA));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        BATTLE_ASSERT(test_client_send_reliable(&g_cady, buf, len));

        Sleep(200);

        /* Sep receives both */
        int msg_len = 0;
        const u8 *msg = test_client_expect_opcode(&g_sep, BC_OP_SET_PHASER_LEVEL,
                                                    &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        msg = test_client_expect_opcode(&g_sep, BC_OP_SET_PHASER_LEVEL,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
    }

    /* === Phase 5: FIRST ENGAGEMENT (Kirk fires at Sep) === */
    printf("    Phase 5: Kirk fires at Sep...\n");
    {
        u8 buf[64];
        int len;

        /* StartFiring */
        len = bc_build_event_forward(buf, sizeof(buf),
                                      BC_OP_START_FIRING,
                                      START_FIRING_DATA,
                                      sizeof(START_FIRING_DATA));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        /* TorpedoFire x2 (built with proper builder) */
        len = bc_build_torpedo_fire(buf, sizeof(buf),
                                     bc_make_ship_id(1), 6,
                                     0.0f, 0.0f, 1.0f,
                                     true, bc_make_ship_id(2),
                                     50.0f, 30.0f, 10.0f);
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        /* BeamFire */
        len = bc_build_beam_fire(buf, sizeof(buf),
                                  bc_make_ship_id(1), 0x01,
                                  0.0f, 0.0f, 1.0f,
                                  true, bc_make_ship_id(2));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        /* StopFiring */
        u8 stop_data[] = { 0x28, 0x81, 0x00, 0x00, 0xD8, 0x00, 0x80, 0x00 };
        len = bc_build_event_forward(buf, sizeof(buf),
                                      BC_OP_STOP_FIRING,
                                      stop_data, sizeof(stop_data));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        Sleep(300);

        /* Sep receives all 5 messages */
        int msg_len = 0;
        const u8 *msg;

        msg = test_client_expect_opcode(&g_sep, BC_OP_START_FIRING,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);

        msg = test_client_expect_opcode(&g_sep, BC_OP_TORPEDO_FIRE,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        /* Verify torpedo parses correctly */
        bc_torpedo_event_t tev;
        BATTLE_ASSERT(bc_parse_torpedo_fire(msg, msg_len, &tev));
        BATTLE_ASSERT(bc_object_id_to_slot(tev.shooter_id) == 1);
        BATTLE_ASSERT(tev.has_target);
        BATTLE_ASSERT(bc_object_id_to_slot(tev.target_id) == 2);

        msg = test_client_expect_opcode(&g_sep, BC_OP_TORPEDO_FIRE,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);

        msg = test_client_expect_opcode(&g_sep, BC_OP_BEAM_FIRE,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        bc_beam_event_t bev;
        BATTLE_ASSERT(bc_parse_beam_fire(msg, msg_len, &bev));
        BATTLE_ASSERT(bev.has_target);

        msg = test_client_expect_opcode(&g_sep, BC_OP_STOP_FIRING,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);

        /* Cady (spectator) also receives them */
        msg = test_client_expect_opcode(&g_cady, BC_OP_START_FIRING,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        msg = test_client_expect_opcode(&g_cady, BC_OP_TORPEDO_FIRE,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
    }

    /* === Phase 6: TORPEDO IMPACT + DAMAGE === */
    printf("    Phase 6: Explosion + damage...\n");
    {
        u8 buf[64];

        /* Sep reports explosion on own ship */
        int len = bc_build_explosion(buf, sizeof(buf),
                                      bc_make_ship_id(2),
                                      10.0f, 20.0f, 30.0f,
                                      150.0f, 25.0f);
        BATTLE_ASSERT(len == 14);
        BATTLE_ASSERT(test_client_send_reliable(&g_sep, buf, len));

        Sleep(200);

        /* Kirk receives the explosion */
        int msg_len = 0;
        const u8 *msg = test_client_expect_opcode(&g_kirk, BC_OP_EXPLOSION,
                                                    &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);

        /* Roundtrip verify */
        bc_explosion_event_t eev;
        BATTLE_ASSERT(bc_parse_explosion(msg, msg_len, &eev));
        BATTLE_ASSERT(bc_object_id_to_slot(eev.object_id) == 2);
        BATTLE_ASSERT(fabsf(eev.damage - 150.0f) < 3.0f);
        BATTLE_ASSERT(fabsf(eev.radius - 25.0f) < 1.0f);
    }

    /* === Phase 7: SEP RETALIATES === */
    printf("    Phase 7: Sep retaliates...\n");
    {
        u8 buf[64];

        int len = bc_build_torpedo_fire(buf, sizeof(buf),
                                         bc_make_ship_id(2), 6,
                                         1.0f, 0.0f, 0.0f,
                                         true, bc_make_ship_id(1),
                                         40.0f, 20.0f, 5.0f);
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_sep, buf, len));

        len = bc_build_beam_fire(buf, sizeof(buf),
                                  bc_make_ship_id(2), 0x01,
                                  1.0f, 0.0f, 0.0f,
                                  true, bc_make_ship_id(1));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_sep, buf, len));

        /* Kirk takes an explosion */
        len = bc_build_explosion(buf, sizeof(buf),
                                  bc_make_ship_id(1),
                                  5.0f, 10.0f, 15.0f,
                                  120.0f, 30.0f);
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        /* SubsysStatus event from Kirk */
        u8 subsys_data[] = { 0xFF, 0xFF, 0xFF, 0x3F, 0x05, 0x00, 0xD0, 0x42 };
        len = bc_build_event_forward(buf, sizeof(buf),
                                      BC_OP_SUBSYS_STATUS,
                                      subsys_data, sizeof(subsys_data));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        Sleep(300);

        /* Cady receives torpedo + beam from Sep */
        int msg_len = 0;
        const u8 *msg;
        msg = test_client_expect_opcode(&g_cady, BC_OP_TORPEDO_FIRE,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        msg = test_client_expect_opcode(&g_cady, BC_OP_BEAM_FIRE,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        msg = test_client_expect_opcode(&g_cady, BC_OP_EXPLOSION,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
    }

    /* === Phase 8: REPAIR === */
    printf("    Phase 8: Repair system...\n");
    {
        u8 buf[32];

        u8 repair_data[] = { 0xFF, 0xFF, 0xFF, 0x3F, 0x05, 0x00 };
        int len = bc_build_event_forward(buf, sizeof(buf),
                                          BC_OP_ADD_REPAIR_LIST,
                                          repair_data, sizeof(repair_data));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        u8 priority_data[] = { 0xFF, 0xFF, 0xFF, 0x3F, 0x05, 0x01 };
        len = bc_build_event_forward(buf, sizeof(buf),
                                      BC_OP_REPAIR_PRIORITY,
                                      priority_data, sizeof(priority_data));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        Sleep(200);

        int msg_len = 0;
        const u8 *msg;
        msg = test_client_expect_opcode(&g_sep, BC_OP_ADD_REPAIR_LIST,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        msg = test_client_expect_opcode(&g_sep, BC_OP_REPAIR_PRIORITY,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
    }

    /* === Phase 9: COLLISION === */
    printf("    Phase 9: Collision...\n");
    {
        u8 buf[64];

        int len = bc_build_event_forward(buf, sizeof(buf),
                                          BC_OP_COLLISION_EFFECT,
                                          COLLISION_EFFECT_DATA,
                                          sizeof(COLLISION_EFFECT_DATA));
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_cady, buf, len));

        Sleep(200);

        /* Verify byte-identical delivery */
        int msg_len = 0;
        const u8 *msg = test_client_expect_opcode(&g_kirk, BC_OP_COLLISION_EFFECT,
                                                    &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        BATTLE_ASSERT(msg_len == 1 + (int)sizeof(COLLISION_EFFECT_DATA));
        BATTLE_ASSERT(msg[0] == BC_OP_COLLISION_EFFECT);
        BATTLE_ASSERT(memcmp(msg + 1, COLLISION_EFFECT_DATA,
                             sizeof(COLLISION_EFFECT_DATA)) == 0);
    }

    /* === Phase 10: KILL SHOT + VICTORY === */
    printf("    Phase 10: Kill shot + gg...\n");
    {
        u8 buf[64];
        int len;

        /* Kirk fires 2 more torpedoes */
        len = bc_build_torpedo_fire(buf, sizeof(buf),
                                     bc_make_ship_id(1), 6,
                                     0.0f, 0.0f, 1.0f,
                                     true, bc_make_ship_id(2),
                                     50.0f, 30.0f, 10.0f);
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        /* Sep's ship explodes */
        len = bc_build_explosion(buf, sizeof(buf),
                                  bc_make_ship_id(2),
                                  25.0f, 50.0f, 10.0f,
                                  200.0f, 50.0f);
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_sep, buf, len));

        /* DestroyObj */
        len = bc_build_destroy_obj(buf, sizeof(buf), bc_make_ship_id(2));
        BATTLE_ASSERT(len == 5);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        /* Kirk says "gg" */
        len = bc_build_chat(buf, sizeof(buf), 1, false, "gg");
        BATTLE_ASSERT(len > 0);
        BATTLE_ASSERT(test_client_send_reliable(&g_kirk, buf, len));

        Sleep(300);

        /* Sep receives DestroyObj */
        int msg_len = 0;
        const u8 *msg;

        /* Drain torpedoes first */
        test_client_expect_opcode(&g_sep, BC_OP_TORPEDO_FIRE, &msg_len, TIMEOUT);
        test_client_expect_opcode(&g_sep, BC_OP_TORPEDO_FIRE, &msg_len, TIMEOUT);

        msg = test_client_expect_opcode(&g_sep, BC_OP_DESTROY_OBJ,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        bc_destroy_event_t dev;
        BATTLE_ASSERT(bc_parse_destroy_obj(msg, msg_len, &dev));
        BATTLE_ASSERT(bc_object_id_to_slot(dev.object_id) == 2);

        /* Sep receives "gg" chat */
        msg = test_client_expect_opcode(&g_sep, BC_MSG_CHAT,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        bc_chat_event_t cev;
        BATTLE_ASSERT(bc_parse_chat_message(msg, msg_len, &cev));
        BATTLE_ASSERT(strcmp(cev.message, "gg") == 0);
        BATTLE_ASSERT(cev.sender_slot == 1);

        /* Cady also receives DestroyObj and Chat
         * (expect_opcode skips intermediate torpedo/explosion messages) */
        msg = test_client_expect_opcode(&g_cady, BC_OP_DESTROY_OBJ,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
        msg = test_client_expect_opcode(&g_cady, BC_MSG_CHAT,
                                         &msg_len, TIMEOUT);
        BATTLE_ASSERT(msg != NULL);
    }

    printf("    All %d assertions passed!\n", g_assertions);

cleanup:
    test_client_disconnect(&g_cady);
    test_client_disconnect(&g_kirk);
    test_client_disconnect(&g_sep);
    Sleep(100);
    test_server_stop(&g_server);
    bc_net_shutdown();

    return (g_assertions > 0 && test_fail == 0) ? 0 : 1;
}

/* Wrap in test framework */
TEST(battle_of_valentines_day)
{
    ASSERT(run_battle() == 0);
}

/* === Real checksum handshake test ===
 * Connects with wire-accurate checksums computed from test fixture files.
 * Server validates hashes against a manifest. */
TEST(real_checksum_handshake)
{
    bc_test_server_t srv;
    bc_test_client_t client;
    bool net_ok = false, srv_ok = false, cli_ok = false;
    int fail = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail++; goto cleanup; \
    } \
} while(0)

    CHECK(bc_net_init());
    net_ok = true;

    CHECK(test_server_start(&srv, 29877, MANIFEST_PATH));
    srv_ok = true;

    CHECK(test_client_connect(&client, 29877, "Tester", 0, GAME_DIR));
    cli_ok = true;

    /* If we get here, the server accepted our real checksums! */

#undef CHECK

cleanup:
    if (cli_ok) test_client_disconnect(&client);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);
}

/* === Step-by-step join flow test ===
 * Validates every individual handshake step for 3 clients with
 * explicit assertions. A protocol audit, not a black-box connect. */
TEST(full_join_flow_multi_client)
{
    bc_test_server_t srv;
    bc_test_client_t clients[3];
    const char *names[] = { "Alpha", "Beta", "Gamma" };
    bool net_ok = false, srv_ok = false;
    int fail = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail++; goto cleanup; \
    } \
} while(0)

#define CHECK_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: %s == 0x%X, expected 0x%X\n", \
               __FILE__, __LINE__, #a, (unsigned)(a), (unsigned)(b)); \
        fail++; goto cleanup; \
    } \
} while(0)

    memset(clients, 0, sizeof(clients));

    CHECK(bc_net_init());
    net_ok = true;
    CHECK(test_server_start(&srv, 29878, MANIFEST_PATH));
    srv_ok = true;

    for (int c = 0; c < 3; c++) {
        bc_test_client_t *cl = &clients[c];
        cl->slot = (u8)c;
        cl->seq_out = 0;
        strncpy(cl->name, names[c], sizeof(cl->name) - 1);
        cl->server_addr.ip = htonl(0x7F000001);
        cl->server_addr.port = htons(29878);

        /* Step 1: Open socket */
        CHECK(bc_socket_open(&cl->sock, 0));

        /* Step 2: Send Connect */
        u8 pkt[512];
        int len = bc_client_build_connect(pkt, sizeof(pkt), htonl(0x7F000001));
        CHECK(len == 10);
        tc_send_raw(cl, pkt, len);

        /* Step 3: Receive ConnectAck + ChecksumReq round 0 (batched) */
        CHECK(tc_recv_raw(cl, 2000) > 0);
        bc_packet_t parsed;
        CHECK(bc_transport_parse(cl->recv_buf, cl->recv_len, &parsed));
        CHECK(parsed.msg_count >= 1);
        CHECK_EQ(parsed.msgs[0].type, BC_TRANSPORT_CONNECT);

        /* Extract ChecksumReq round 0 from batched packet */
        bc_transport_msg_t *round0_msg = tc_find_reliable_payload(cl, &parsed);
        CHECK(round0_msg != NULL);
        CHECK_EQ(round0_msg->payload[0], BC_OP_CHECKSUM_REQ);
        CHECK_EQ(round0_msg->payload[1], 0);

        /* Step 4: Send keepalive with name */
        len = bc_client_build_keepalive_name(pkt, sizeof(pkt), (u8)c,
                                              htonl(0x7F000001), names[c]);
        CHECK(len > 0);
        tc_send_raw(cl, pkt, len);

        /* Step 5: 4 checksum rounds (round 0 from batch, 1-3 via recv) */
        for (int round = 0; round < 4; round++) {
            bc_transport_msg_t *rmsg;

            if (round == 0) {
                rmsg = round0_msg;
            } else {
                rmsg = NULL;
                u32 round_start = GetTickCount();
                while ((int)(GetTickCount() - round_start) < 2000) {
                    if (tc_recv_raw(cl, 200) <= 0) continue;
                    bc_packet_t rp;
                    if (!bc_transport_parse(cl->recv_buf, cl->recv_len, &rp))
                        continue;
                    rmsg = tc_find_reliable_payload(cl, &rp);
                    if (rmsg) break;
                }
                CHECK(rmsg != NULL);
            }

            CHECK_EQ(rmsg->payload[0], BC_OP_CHECKSUM_REQ);
            CHECK_EQ(rmsg->payload[1], (u8)round);

            bc_checksum_request_t req;
            CHECK(bc_client_parse_checksum_request(rmsg->payload,
                                                      rmsg->payload_len, &req));
            CHECK_EQ(req.round, (u8)round);

            bc_client_dir_scan_t scan;
            CHECK(bc_client_scan_directory(GAME_DIR, req.directory,
                                             req.filter, req.recursive, &scan));

            u8 resp[4096];
            int resp_len = tc_build_real_checksum(resp, sizeof(resp),
                                                    (u8)round, &scan);
            CHECK(resp_len > 0);

            u8 out[4096];
            int out_len = bc_client_build_reliable(out, sizeof(out), (u8)c,
                                                     resp, resp_len, cl->seq_out++);
            CHECK(out_len > 0);
            tc_send_raw(cl, out, out_len);
        }

        /* Step 6: Final round (0xFF) */
        {
            bc_transport_msg_t *rmsg = NULL;
            u32 ff_start = GetTickCount();
            while ((int)(GetTickCount() - ff_start) < 2000) {
                if (tc_recv_raw(cl, 200) <= 0) continue;
                bc_packet_t rp;
                if (!bc_transport_parse(cl->recv_buf, cl->recv_len, &rp))
                    continue;
                rmsg = tc_find_reliable_payload(cl, &rp);
                if (rmsg) break;
            }
            CHECK(rmsg != NULL);
            CHECK_EQ(rmsg->payload[0], BC_OP_CHECKSUM_REQ);
            CHECK_EQ(rmsg->payload[1], 0xFF);

            u8 resp[32];
            int resp_len = bc_client_build_checksum_final(resp, sizeof(resp), 0);
            CHECK(resp_len > 0);

            u8 out[512];
            int out_len = bc_client_build_reliable(out, sizeof(out), (u8)c,
                                                     resp, resp_len, cl->seq_out++);
            CHECK(out_len > 0);
            tc_send_raw(cl, out, out_len);
        }

        /* Step 7: Receive Settings + GameInit, send 0x2A, receive MissionInit */
        {
            bool got_settings = false, got_gameinit = false;
            u32 start = GetTickCount();

            while ((int)(GetTickCount() - start) < 2000) {
                if (tc_recv_raw(cl, 200) <= 0) {
                    if (got_settings && got_gameinit) break;
                    continue;
                }
                if (!bc_transport_parse(cl->recv_buf, cl->recv_len, &parsed))
                    continue;

                for (int i = 0; i < parsed.msg_count; i++) {
                    bc_transport_msg_t *msg = &parsed.msgs[i];
                    if (msg->type == BC_TRANSPORT_RELIABLE && (msg->flags & 0x80)) {
                        tc_send_ack(cl, msg->seq);
                        if (msg->payload_len > 0) {
                            u8 op = msg->payload[0];
                            if (op == BC_OP_SETTINGS) got_settings = true;
                            if (op == BC_OP_GAME_INIT) got_gameinit = true;
                        }
                    }
                }
                if (got_settings && got_gameinit) break;
            }

            CHECK(got_settings);
            CHECK(got_gameinit);

            /* Client sends NewPlayerInGame */
            u8 npig[2] = { BC_OP_NEW_PLAYER_IN_GAME, 0x20 };
            u8 npig_out[64];
            int npig_len = bc_client_build_reliable(npig_out, sizeof(npig_out),
                                                      (u8)c, npig, 2, cl->seq_out++);
            CHECK(npig_len > 0);
            tc_send_raw(cl, npig_out, npig_len);

            /* Receive MissionInit response */
            bool got_mission = false;
            u8 mission_total_slots = 0;
            u32 mi_start = GetTickCount();
            while ((int)(GetTickCount() - mi_start) < 2000) {
                if (tc_recv_raw(cl, 200) <= 0) continue;
                if (!bc_transport_parse(cl->recv_buf, cl->recv_len, &parsed))
                    continue;
                for (int i = 0; i < parsed.msg_count; i++) {
                    bc_transport_msg_t *msg = &parsed.msgs[i];
                    if (msg->type == BC_TRANSPORT_RELIABLE && (msg->flags & 0x80)) {
                        tc_send_ack(cl, msg->seq);
                        if (msg->payload_len > 1 &&
                            msg->payload[0] == BC_MSG_MISSION_INIT) {
                            got_mission = true;
                            mission_total_slots = msg->payload[1];
                        }
                    }
                }
                if (got_mission) break;
            }

            CHECK(got_mission);
            CHECK_EQ(mission_total_slots, 9);
        }

        cl->connected = true;
    }

    /* All 3 connected. Verify chat relay. */
    Sleep(200);
    test_client_drain(&clients[0], 300);
    test_client_drain(&clients[1], 300);
    test_client_drain(&clients[2], 300);

    /* Client 0 sends chat -> clients 1,2 receive it */
    {
        u8 buf[64];
        int len = bc_build_chat(buf, sizeof(buf), 0, false, "hello");
        CHECK(len > 0);
        CHECK(test_client_send_reliable(&clients[0], buf, len));

        Sleep(200);

        int msg_len = 0;
        const u8 *msg;

        msg = test_client_expect_opcode(&clients[1], BC_MSG_CHAT, &msg_len, TIMEOUT);
        CHECK(msg != NULL);
        bc_chat_event_t cev;
        CHECK(bc_parse_chat_message(msg, msg_len, &cev));
        CHECK(strcmp(cev.message, "hello") == 0);
        CHECK_EQ(cev.sender_slot, 0);

        msg = test_client_expect_opcode(&clients[2], BC_MSG_CHAT, &msg_len, TIMEOUT);
        CHECK(msg != NULL);
    }

#undef CHECK
#undef CHECK_EQ

cleanup:
    for (int c = 0; c < 3; c++)
        test_client_disconnect(&clients[c]);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);
}

TEST_MAIN_BEGIN()
    RUN(battle_of_valentines_day);
    RUN(real_checksum_handshake);
    RUN(full_join_flow_multi_client);
TEST_MAIN_END()
