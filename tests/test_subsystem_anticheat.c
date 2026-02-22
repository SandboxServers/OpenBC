/*
 * Subsystem health anti-cheat gate tests (issue #45)
 *
 * Verifies that the server correctly gates beam and torpedo damage behind
 * live weapon subsystem HP checks, replicating the stock BC client-side
 * CanFire() guard server-side.
 *
 * Two test layers:
 *
 * 1. Network integration: a CardFreighter (species 16, no phaser/torpedo_tube
 *    subsystems) sends BeamFire over the wire.  The server must relay the
 *    visual to all peers but skip damage (no BC_OP_PYTHON_EVENT emitted).
 *
 * 2. Unit: directly zero weapon subsystem HP and verify that
 *    bc_combat_can_fire_phaser / bc_combat_can_fire_torpedo return false,
 *    confirming the combat layer's own guard that the dispatch check reinforces.
 */

#include "test_util.h"
#include "test_harness.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include "openbc/game_events.h"
#include "openbc/opcodes.h"

#include <string.h>
#include <math.h>

/* ======================================================================
 * Configuration
 * ====================================================================== */

#define AC_PORT          29895
#define AC_TIMEOUT       1000
#define AC_MANIFEST      "tests/fixtures/manifest.json"
#define AC_GAME_DIR      "tests/fixtures/"
#define AC_REGISTRY_DIR  "data/vanilla-1.1"

/* ======================================================================
 * Ship blobs
 *
 * Wire format (after [opcode][owner][team] envelope):
 *   [class_id:i32=0x00008008][object_id:i32][species_type:u8]
 *   [pos_x:f32][pos_y:f32][pos_z:f32][orientation:4xf32][speed:f32]
 *   [reserved:3][name_len:u8][name:var][set_len:u8][set:var][subsys...]
 *
 * AKIRA_DATA is captured from a reference packet trace (species_type=1).
 * CARDFREIGHTER_DATA is derived from the same trace with species_type
 * changed to 16 (CardFreighter) and spawn position set to the origin.
 * The CardFreighter has no phaser or torpedo_tube subsystems -- it is
 * used as a stand-in for any ship whose weapon subsystems are fully
 * destroyed, which is the precondition the new dispatch check catches.
 * ====================================================================== */

/* Akira at (88, -66, -73), from Valentine's Day packet trace */
static const u8 AKIRA_DATA[] = {
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

/*
 * CardFreighter at origin (0, 0, 0).
 * Identical layout to AKIRA_DATA except:
 *   byte  8: species_type = 0x10 (16 = CardFreighter)
 *   bytes 9-20: position zeroed (three f32 = 0.0)
 * The subsystem state tail is reused as-is; the server ignores it (uses
 * bc_ship_init from the registry) and the test client doesn't render it.
 */
static const u8 CARDFREIGHTER_DATA[] = {
    0x08, 0x80, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x3F, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF5, 0x4A, 0x6F,
    0x3F, 0xFE, 0x8C, 0x96, 0x3E, 0x84, 0xE3, 0x4B, 0x3E, 0x38, 0x78, 0x4E,
    0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x43, 0x61, 0x64,
    0x79, 0x32, 0x06, 0x4D, 0x75, 0x6C, 0x74, 0x69, 0x31, 0xFF, 0xFF, 0x64,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x64, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x64, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x64, 0x60, 0x01, 0xFF, 0xFF, 0xFF, 0x64, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x64, 0x00, 0xFF, 0x64, 0xFF, 0xFF, 0xFF, 0x64, 0x01, 0xFF,
};

/* ======================================================================
 * Helper: drain all messages from a client for timeout_ms, recording
 * whether BC_OP_PYTHON_EVENT was seen.
 * ====================================================================== */

static bool drain_check_no_python_event(bc_test_client_t *c, int timeout_ms)
{
    bool got_python_event = false;
    u32 start = GetTickCount();
    while ((int)(GetTickCount() - start) < timeout_ms) {
        int msg_len = 0;
        const u8 *msg = test_client_recv_msg(c, &msg_len, 20);
        if (!msg) continue;
        if (msg_len > 0 && msg[0] == BC_OP_PYTHON_EVENT) {
            got_python_event = true;
        }
    }
    return !got_python_event;
}

/* ======================================================================
 * Integration test: beam fire from weaponless ship
 *
 * A CardFreighter (species 16) has no phaser or pulse_weapon subsystems.
 * When it sends BeamFire the server must:
 *   (a) relay the visual to all peers (bc_relay_to_others runs first), AND
 *   (b) skip server-side damage (no BC_OP_PYTHON_EVENT reaches the target).
 *
 * This exercises the new subsystem health gate added to BC_OP_BEAM_FIRE
 * dispatch (issue #45) and the existing apply_beam_damage fast-path that
 * already returns early when damage == 0.  Together they ensure a cheat
 * client cannot deal beam damage after all weapon subsystems are destroyed.
 * ====================================================================== */

TEST(beam_fire_weapons_destroyed_skips_damage)
{
    bc_test_server_t srv;
    bc_test_client_t freighter, akira;
    memset(&srv,      0, sizeof(srv));
    memset(&freighter, 0, sizeof(freighter));
    memset(&akira,    0, sizeof(akira));

    bool net_ok = false, srv_ok = false;
    int  fail   = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail++; goto cleanup; \
    } \
} while (0)

    CHECK(bc_net_init());
    net_ok = true;

    CHECK(test_server_start(&srv, AC_PORT, AC_MANIFEST));
    srv_ok = true;

    /* Slot 0 = CardFreighter (no weapon subsystems), Slot 1 = Akira (target) */
    CHECK(test_client_connect(&freighter, AC_PORT, "Freighter", 0, AC_GAME_DIR));
    CHECK(test_client_connect(&akira,     AC_PORT, "Akira",     1, AC_GAME_DIR));

    /* Spawn CardFreighter */
    {
        u8 buf[256];
        int len = bc_build_object_create_team(buf, sizeof(buf), 0, 2,
                                               CARDFREIGHTER_DATA,
                                               (int)sizeof(CARDFREIGHTER_DATA));
        CHECK(len > 0);
        CHECK(test_client_send_reliable(&freighter, buf, len));
    }

    /* Spawn Akira */
    {
        u8 buf[256];
        int len = bc_build_object_create_team(buf, sizeof(buf), 1, 3,
                                               AKIRA_DATA,
                                               (int)sizeof(AKIRA_DATA));
        CHECK(len > 0);
        CHECK(test_client_send_reliable(&akira, buf, len));
    }

    Sleep(200);
    test_client_drain(&freighter, 200);
    test_client_drain(&akira, 200);

    /* CardFreighter fires beam at Akira.
     *   shooter = bc_make_ship_id(0) = 1  (CardFreighter, peer_slot 1)
     *   target  = bc_make_ship_id(1) = 3  (Akira, peer_slot 2)
     * The server sets peer->object_id = bc_make_ship_id(game_slot) so the
     * target is found via find_peer_by_object(3). */
    {
        u8 buf[64];
        int len = bc_build_beam_fire(buf, sizeof(buf),
                                      bc_make_ship_id(0), 0x01,
                                      0.0f, 1.0f, 0.0f,
                                      true, bc_make_ship_id(1));
        CHECK(len > 0);
        CHECK(test_client_send_reliable(&freighter, buf, len));
    }

    Sleep(100);

    /* Akira MUST receive the BeamFire visual (relay runs before the check) */
    {
        int msg_len = 0;
        const u8 *msg = test_client_expect_opcode(&akira, BC_OP_BEAM_FIRE,
                                                    &msg_len, AC_TIMEOUT);
        CHECK(msg != NULL);
        bc_beam_event_t bev;
        CHECK(bc_parse_beam_fire(msg, msg_len, &bev));
        CHECK(bev.has_target);
    }

    /* Akira must NOT receive a BC_OP_PYTHON_EVENT (no server damage applied).
     * Drain for 400 ms -- enough for any in-flight reliable retransmit to
     * arrive if damage had been computed. */
    CHECK(drain_check_no_python_event(&akira, 400));

#undef CHECK

cleanup:
    test_client_disconnect(&freighter);
    test_client_disconnect(&akira);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);
}

/* ======================================================================
 * Unit test: subsystem HP = 0 prevents firing via combat layer
 *
 * The dispatch-layer check (issue #45) is defence-in-depth on top of the
 * pre-existing bc_combat_can_fire_phaser / bc_combat_can_fire_torpedo
 * checks in the simulation layer.  This test confirms the underlying
 * combat functions return false when the weapon subsystem HP reaches zero,
 * which is the condition both layers guard against.
 * ====================================================================== */

TEST(subsystem_hp_zero_gates_weapon_fire)
{
    bc_game_registry_t reg;
    memset(&reg, 0, sizeof(reg));
    ASSERT(bc_registry_load_dir(&reg, AC_REGISTRY_DIR));

    /* Use Galaxy class: has both phaser (index 6+) and torpedo_tube (0-5) */
    const bc_ship_class_t *galaxy_cls = bc_registry_find_ship(&reg, 3); /* species 3 */
    ASSERT(galaxy_cls != NULL);

    /* Find first phaser bank index and first torpedo tube index */
    int first_phaser_si   = -1;
    int first_tube_si     = -1;
    for (int i = 0; i < galaxy_cls->subsystem_count; i++) {
        const char *t = galaxy_cls->subsystems[i].type;
        if (first_phaser_si < 0 &&
            (strcmp(t, "phaser") == 0 || strcmp(t, "pulse_weapon") == 0))
            first_phaser_si = i;
        if (first_tube_si < 0 && strcmp(t, "torpedo_tube") == 0)
            first_tube_si = i;
    }
    ASSERT(first_phaser_si >= 0); /* Galaxy has phasers */
    ASSERT(first_tube_si   >= 0); /* Galaxy has torpedo tubes */

    bc_ship_state_t ship;
    bc_ship_init(&ship, galaxy_cls, 0, bc_make_ship_id(0), 0, 0);

    /* With full subsystem HP, both weapons should be fireable */
    ASSERT(bc_combat_can_fire_phaser(&ship, galaxy_cls, 0));
    ASSERT(bc_combat_can_fire_torpedo(&ship, galaxy_cls, 0));

    /* Zero the phaser subsystem HP -- phaser bank 0 must no longer fire */
    ship.subsystem_hp[first_phaser_si] = 0.0f;
    ASSERT(!bc_combat_can_fire_phaser(&ship, galaxy_cls, 0));

    /* Zero ALL phaser subsystems (mirrors "all weapons destroyed" server check) */
    for (int i = 0; i < galaxy_cls->subsystem_count; i++) {
        const char *t = galaxy_cls->subsystems[i].type;
        if (strcmp(t, "phaser") == 0 || strcmp(t, "pulse_weapon") == 0)
            ship.subsystem_hp[i] = 0.0f;
    }
    /* No phaser bank should be fireable */
    for (int b = 0; b < galaxy_cls->phaser_banks; b++) {
        ASSERT(!bc_combat_can_fire_phaser(&ship, galaxy_cls, b));
    }

    /* Restore phasers, zero torpedo tube 0 -- that tube must not fire */
    bc_ship_init(&ship, galaxy_cls, 0, bc_make_ship_id(0), 0, 0);
    ship.subsystem_hp[first_tube_si] = 0.0f;
    ASSERT(!bc_combat_can_fire_torpedo(&ship, galaxy_cls, 0));

    /* Zero ALL torpedo tubes (mirrors the torpedo dispatch check) */
    for (int i = 0; i < galaxy_cls->subsystem_count; i++) {
        if (strcmp(galaxy_cls->subsystems[i].type, "torpedo_tube") == 0)
            ship.subsystem_hp[i] = 0.0f;
    }
    for (int t = 0; t < galaxy_cls->torpedo_tubes; t++) {
        ASSERT(!bc_combat_can_fire_torpedo(&ship, galaxy_cls, t));
    }
}

/* ======================================================================
 * Main
 * ====================================================================== */

TEST_MAIN_BEGIN()
    RUN(beam_fire_weapons_destroyed_skips_damage);
    RUN(subsystem_hp_zero_gates_weapon_fire);
TEST_MAIN_END()
