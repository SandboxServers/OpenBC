/*
 * Opcode Coverage Tests (Integration)
 *
 * Spawns a real server subprocess, connects 2 headless clients,
 * and verifies relay/dispatch behavior for opcodes that previously
 * had zero test coverage.
 *
 * Covers:
 *   - Relay-only opcodes: StopFiringAt, ClientEvent, TorpTypeChange
 *   - Absorbed opcodes: PythonEvent2 (C->S only, not relayed)
 *   - Warp anti-cheat: alive relay
 *   - RequestObj / ObjNotFound
 *   - TeamMessage relay
 *   - Graceful disconnect (transport 0x06)
 *   - NetFile smoke test (unhandled opcodes don't crash server)
 */

#include "test_harness.h"
#include "openbc/game_builders.h"
#include "openbc/game_events.h"
#include "openbc/opcodes.h"
#include <stdio.h>
#include <string.h>

/* --- Configuration --- */
#define OC_PORT         29950
#define OC_MANIFEST     "tests/fixtures/manifest.json"
#define OC_GAME_DIR     "tests/fixtures/"
#define OC_TIMEOUT      2000

/* --- Helper: build a minimal ObjCreateTeam payload for a test client --- */
/* The server caches this and sets has_ship=true when it receives one.
 * Minimal ship blob: [prefix:4][object_id:i32][species_id:u8][pos:3xf32]...
 * We use a minimal synthetic blob that's long enough for the server to parse. */
static int build_synthetic_spawn(u8 *buf, int buf_size, u8 owner_slot, u8 team_id)
{
    /* Minimum: opcode(1) + owner(1) + team(1) + blob.
     * Blob must be >= 16 bytes for the server to parse the ship header.
     * Format: [prefix:4][object_id:i32][species_id:u8][pos:3xf32] = 21 bytes */
    u8 blob[64];
    memset(blob, 0, sizeof(blob));
    /* prefix bytes */
    blob[0] = 0x01; blob[1] = 0x00; blob[2] = 0x00; blob[3] = 0x00;
    /* object_id (i32 LE) - computed from game_slot */
    i32 obj_id = bc_make_ship_id(owner_slot);
    memcpy(blob + 4, &obj_id, 4);
    /* species_id (u8) = 1 (Akira class in vanilla-1.1 registry) */
    blob[8] = 1;
    /* pos_x, pos_y, pos_z (f32 LE) = (0, 0, 0) */
    /* Already zeroed */
    int blob_len = 21;

    return bc_build_object_create_team(buf, buf_size, owner_slot, team_id,
                                        blob, blob_len);
}

/* --- Test counters --- */
static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; \
        return; \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: %s == 0x%X, expected 0x%X\n", \
               __FILE__, __LINE__, #a, (unsigned)(a), (unsigned)(b)); \
        g_fail++; \
        return; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    int before = g_fail; \
    g_total++; \
    printf("  [%d] %s... ", g_total, #fn); \
    fn(); \
    if (g_fail == before) { g_pass++; printf("ok\n"); } \
} while (0)

/* ======================================================================
 * Relay-only opcodes: send from A, verify byte-identical at B
 * ====================================================================== */

static void stop_firing_at_relay(bc_test_client_t *cA, bc_test_client_t *cB)
{
    u8 data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    u8 payload[8];
    int len = bc_build_event_forward(payload, sizeof(payload),
                                      BC_OP_STOP_FIRING_AT, data, 4);
    CHECK(len == 5);
    CHECK(test_client_send_reliable(cA, payload, len));

    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(cB, BC_OP_STOP_FIRING_AT,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg != NULL);
    CHECK_EQ(recv_len, len);
    CHECK(memcmp(msg, payload, (size_t)len) == 0);
}

static void client_event_relay(bc_test_client_t *cA, bc_test_client_t *cB)
{
    u8 data[] = { 0x01, 0x02, 0x03, 0x04 };
    u8 payload[8];
    int len = bc_build_event_forward(payload, sizeof(payload),
                                      BC_OP_CLIENT_EVENT, data, 4);
    CHECK(len == 5);
    CHECK(test_client_send_reliable(cA, payload, len));

    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(cB, BC_OP_CLIENT_EVENT,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg != NULL);
    CHECK_EQ(recv_len, len);
    CHECK(memcmp(msg, payload, (size_t)len) == 0);
}

static void python_event2_absorbed(bc_test_client_t *cA, bc_test_client_t *cB)
{
    /* PythonEvent2 (0x0D) is C->S only; server absorbs, does NOT relay
     * (0 S->C instances in stock trace). */
    u8 data[16];
    memset(data, 0, sizeof(data));
    /* factory (i32) */
    data[0] = 0x0C; data[1] = 0x01; data[2] = 0x00; data[3] = 0x00;
    /* event_type (i32) */
    data[4] = 0x7C; data[5] = 0x00; data[6] = 0x80; data[7] = 0x00;
    /* source (i32) */
    data[8] = 0xFF; data[9] = 0xFF; data[10] = 0xFF; data[11] = 0x3F;
    /* dest (i32) */
    data[12] = 0xFF; data[13] = 0xFF; data[14] = 0x03; data[15] = 0x40;

    u8 payload[20];
    int len = bc_build_event_forward(payload, sizeof(payload),
                                      BC_OP_PYTHON_EVENT2, data, 16);
    CHECK(len == 17);
    CHECK(test_client_send_reliable(cA, payload, len));

    /* Should NOT be relayed to other clients */
    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(cB, BC_OP_PYTHON_EVENT2,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg == NULL);
}

static void torp_type_change_relay(bc_test_client_t *cA, bc_test_client_t *cB)
{
    u8 data[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    u8 payload[8];
    int len = bc_build_event_forward(payload, sizeof(payload),
                                      BC_OP_TORP_TYPE_CHANGE, data, 4);
    CHECK(len == 5);
    CHECK(test_client_send_reliable(cA, payload, len));

    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(cB, BC_OP_TORP_TYPE_CHANGE,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg != NULL);
    CHECK_EQ(recv_len, len);
    CHECK(memcmp(msg, payload, (size_t)len) == 0);
}

/* ======================================================================
 * Warp anti-cheat
 * ====================================================================== */

static void start_warp_alive_relays(bc_test_client_t *cA, bc_test_client_t *cB)
{
    /* Newly-connected client has max HP, so StartWarp should relay */
    u8 payload[8];
    int len = bc_build_event_forward(payload, sizeof(payload),
                                      BC_OP_START_WARP, NULL, 0);
    CHECK(len == 1);
    CHECK_EQ(payload[0], BC_OP_START_WARP);
    CHECK(test_client_send_reliable(cA, payload, len));

    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(cB, BC_OP_START_WARP,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg != NULL);
    CHECK_EQ(recv_len, 1);
    CHECK_EQ(msg[0], BC_OP_START_WARP);
}

/* ======================================================================
 * RequestObj / ObjNotFound
 * ====================================================================== */

static void request_obj_not_found_sends_nf(bc_test_client_t *cB)
{
    i32 fake_id = bc_make_ship_id(5);  /* No player in slot 5 */
    u8 req[8];
    req[0] = BC_OP_REQUEST_OBJ;
    memcpy(req + 1, &fake_id, 4);

    CHECK(test_client_send_reliable(cB, req, 5));

    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(cB, BC_OP_OBJ_NOT_FOUND,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg != NULL);
    CHECK_EQ(recv_len, 5);
    CHECK_EQ(msg[0], BC_OP_OBJ_NOT_FOUND);

    i32 returned_id;
    memcpy(&returned_id, msg + 1, 4);
    CHECK_EQ((u32)returned_id, (u32)fake_id);
}

static void request_obj_found_resends_spawn(bc_test_client_t *cA,
                                              bc_test_client_t *cB)
{
    /* Client A must first send ObjCreateTeam so the server caches its spawn.
     * First joiner = internal slot 1, game_slot 0, ship = bc_make_ship_id(0). */
    u8 spawn[64];
    int spawn_len = build_synthetic_spawn(spawn, sizeof(spawn), 0, 0);
    CHECK(spawn_len > 0);
    CHECK(test_client_send_reliable(cA, spawn, spawn_len));

    /* Client B receives the relayed ObjCreateTeam -- drain it */
    test_client_drain(cB, 500);

    /* Now Client B requests Client A's ship */
    i32 ship_a = bc_make_ship_id(0); /* game_slot 0 */
    u8 req[8];
    req[0] = BC_OP_REQUEST_OBJ;
    memcpy(req + 1, &ship_a, 4);

    CHECK(test_client_send_reliable(cB, req, 5));

    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(cB, BC_OP_OBJ_CREATE_TEAM,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg != NULL);
    CHECK(recv_len > 3); /* At least opcode + owner + team */
    CHECK_EQ(msg[0], BC_OP_OBJ_CREATE_TEAM);
}

/* ======================================================================
 * TeamMessage relay
 * ====================================================================== */

static void team_message_relay(bc_test_client_t *cA, bc_test_client_t *cB)
{
    u8 payload[8];
    int len = bc_build_team_message(payload, sizeof(payload), 2, 1);
    CHECK_EQ(len, 6);
    CHECK(test_client_send_reliable(cA, payload, len));

    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(cB, BC_MSG_TEAM_MESSAGE,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg != NULL);
    CHECK_EQ(recv_len, len);
    CHECK(memcmp(msg, payload, (size_t)len) == 0);
}

/* ======================================================================
 * NetFile smoke test
 * ====================================================================== */

static void netfile_opcodes_no_crash(bc_test_client_t *cA, bc_test_client_t *cB)
{
    u8 nf_opcodes[] = { BC_OP_VERSION_MISMATCH, BC_OP_SYS_CHECKSUM_FAIL,
                         BC_OP_FILE_TRANSFER, BC_OP_FILE_TRANSFER_ACK };

    for (int i = 0; i < (int)(sizeof(nf_opcodes) / sizeof(nf_opcodes[0])); i++) {
        u8 payload[4];
        payload[0] = nf_opcodes[i];
        payload[1] = 0x00;
        payload[2] = 0x00;
        test_client_send_reliable(cA, payload, 3);
    }

    Sleep(200);

    /* Verify server still alive: relay a normal opcode */
    u8 data[] = { 0x11, 0x22 };
    u8 normal_payload[8];
    int normal_len = bc_build_event_forward(normal_payload, sizeof(normal_payload),
                                             BC_OP_START_FIRING, data, 2);
    CHECK(test_client_send_reliable(cA, normal_payload, normal_len));

    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(cB, BC_OP_START_FIRING,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg != NULL);
}

/* ======================================================================
 * Graceful disconnect
 * ====================================================================== */

static void graceful_disconnect_removes_peer(void)
{
    bc_test_server_t srv2;
    bc_test_client_t cA, cB;

    CHECK(test_server_start(&srv2, OC_PORT + 10, OC_MANIFEST));

    if (!test_client_connect(&cA, OC_PORT + 10, "DiscoA", 0, OC_GAME_DIR)) {
        test_server_stop(&srv2);
        CHECK(false);
        return;
    }

    if (!test_client_connect(&cB, OC_PORT + 10, "DiscoB", 0, OC_GAME_DIR)) {
        test_client_disconnect(&cA);
        test_server_stop(&srv2);
        CHECK(false);
        return;
    }

    test_client_drain(&cA, 500);
    test_client_drain(&cB, 500);

    /* Client A must have a ship for the server to send DestroyObject.
     * First joiner = internal slot 1, game_slot 0. */
    u8 spawn[64];
    int spawn_len = build_synthetic_spawn(spawn, sizeof(spawn), 0, 0);
    CHECK(spawn_len > 0);
    CHECK(test_client_send_reliable(&cA, spawn, spawn_len));

    /* Client B receives the relayed ObjCreateTeam -- drain it */
    test_client_drain(&cB, 500);

    /* Client A disconnects */
    test_client_disconnect(&cA);

    /* Client B should receive DestroyObject for Client A's ship */
    int recv_len = 0;
    const u8 *msg = test_client_expect_opcode(&cB, BC_OP_DESTROY_OBJ,
                                                &recv_len, OC_TIMEOUT);
    CHECK(msg != NULL);
    CHECK_EQ(recv_len, 5);
    CHECK_EQ(msg[0], BC_OP_DESTROY_OBJ);

    test_client_disconnect(&cB);
    Sleep(100);
    test_server_stop(&srv2);
}

/* ======================================================================
 * Main
 * ====================================================================== */

int main(void)
{
    printf("Running %s\n", __FILE__);

    if (!bc_net_init()) {
        printf("bc_net_init failed\n");
        return 1;
    }

    /* --- Start server + 2 clients for most tests --- */
    bc_test_server_t srv;
    bc_test_client_t clientA, clientB;

    if (!test_server_start(&srv, OC_PORT, OC_MANIFEST)) {
        printf("SETUP FAILED: server start\n");
        return 1;
    }

    if (!test_client_connect(&clientA, OC_PORT, "Alice", 0, OC_GAME_DIR)) {
        printf("SETUP FAILED: client A connect\n");
        test_server_stop(&srv);
        return 1;
    }

    if (!test_client_connect(&clientB, OC_PORT, "Bob", 0, OC_GAME_DIR)) {
        printf("SETUP FAILED: client B connect\n");
        test_client_disconnect(&clientA);
        test_server_stop(&srv);
        return 1;
    }

    /* Drain post-connection messages */
    test_client_drain(&clientA, 500);
    test_client_drain(&clientB, 500);

    /* Relay tests */
#define RUN2(fn) do { \
    int before = g_fail; \
    g_total++; \
    printf("  [%d] %s... ", g_total, #fn); \
    fn(&clientA, &clientB); \
    if (g_fail == before) { g_pass++; printf("ok\n"); } \
} while (0)

#define RUN1(fn, c) do { \
    int before = g_fail; \
    g_total++; \
    printf("  [%d] %s... ", g_total, #fn); \
    fn(c); \
    if (g_fail == before) { g_pass++; printf("ok\n"); } \
} while (0)

    RUN2(stop_firing_at_relay);
    RUN2(client_event_relay);
    RUN2(python_event2_absorbed);
    RUN2(torp_type_change_relay);
    RUN2(start_warp_alive_relays);
    RUN1(request_obj_not_found_sends_nf, &clientB);
    RUN2(request_obj_found_resends_spawn);
    RUN2(team_message_relay);
    RUN2(netfile_opcodes_no_crash);

    /* Teardown main server */
    test_client_disconnect(&clientB);
    test_client_disconnect(&clientA);
    Sleep(100);
    test_server_stop(&srv);

    /* Disconnect test (uses its own server) */
    RUN_TEST(graceful_disconnect_removes_peer);

    printf("%d/%d tests passed\n", g_pass, g_total);
    return g_fail > 0 ? 1 : 0;
}
