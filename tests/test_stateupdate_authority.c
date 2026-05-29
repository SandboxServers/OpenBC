#include "test_util.h"
#include "test_harness.h"

#include "openbc/game_builders.h"
#include "openbc/game_events.h"
#include "openbc/buffer.h"
#include "openbc/opcodes.h"

#include <math.h>
#include <string.h>

#define SA_PORT      29960
#define SA_MANIFEST  "tests/fixtures/manifest.json"
#define SA_GAME_DIR  "tests/fixtures/"
#define SA_TIMEOUT   2000

/* Minimal ObjCreateTeam with parseable ship blob header. */
static int build_synthetic_spawn(u8 *buf, int buf_size, u8 owner_slot, u8 team_id)
{
    u8 blob[64];
    memset(blob, 0, sizeof(blob));
    blob[0] = 0x01; blob[1] = 0x00; blob[2] = 0x00; blob[3] = 0x00;
    {
        i32 obj_id = bc_make_ship_id(owner_slot);
        memcpy(blob + 4, &obj_id, 4);
    }
    blob[8] = 1; /* species_id */
    return bc_build_object_create_team(buf, buf_size, owner_slot, team_id,
                                       blob, 21);
}

static int build_owner_stateupdate(i32 object_id,
                                   f32 x, f32 y, f32 z,
                                   f32 speed,
                                   u8 *out, int out_size)
{
    u8 fields[96];
    bc_buffer_t fb;
    bc_buf_init(&fb, fields, sizeof(fields));

    if (!bc_buf_write_f32(&fb, x)) return -1;
    if (!bc_buf_write_f32(&fb, y)) return -1;
    if (!bc_buf_write_f32(&fb, z)) return -1;
    if (!bc_buf_write_bit(&fb, false)) return -1; /* no hash payload */
    if (!bc_buf_write_cv3(&fb, 0.0f, 1.0f, 0.0f)) return -1;
    if (!bc_buf_write_cv3(&fb, 0.0f, 0.0f, 1.0f)) return -1;
    if (!bc_buf_write_cf16(&fb, speed)) return -1;

    return bc_build_state_update(out, out_size,
                                 object_id, 42.0f,
                                 (u8)(BC_DIRTY_POSITION_ABS |
                                      BC_DIRTY_ORIENT_FWD |
                                      BC_DIRTY_ORIENT_UP |
                                      BC_DIRTY_SPEED),
                                 fields, (int)fb.pos);
}

/* Injected owner-update values -- the test asserts these propagate downstream. */
#define SA_INJ_X      123.0f
#define SA_INJ_Y      (-77.0f)
#define SA_INJ_Z      19.0f
#define SA_INJ_SPEED  8.0f
#define SA_TOL        0.01f

/* goto-cleanup check: log the failed condition, flag failure, jump to cleanup.
 * Used instead of raw ASSERT once sockets/server are live so the cleanup path
 * (disconnect / server stop / bc_net_shutdown) always runs and port 29960 is
 * released even on early failure. */
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failed = true; \
        goto cleanup; \
    } \
} while(0)

TEST(stateupdate_downstream_is_server_shaped)
{
    bc_test_server_t srv;
    bc_test_client_t a, b;
    bool failed = false;
    bool net_up = false, srv_up = false, a_up = false, b_up = false;

    memset(&srv, 0, sizeof(srv));
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    CHECK(bc_net_init());           net_up = true;
    CHECK(test_server_start(&srv, SA_PORT, SA_MANIFEST)); srv_up = true;
    CHECK(test_client_connect(&a, SA_PORT, "SA_A", 0, SA_GAME_DIR)); a_up = true;
    CHECK(test_client_connect(&b, SA_PORT, "SA_B", 1, SA_GAME_DIR)); b_up = true;

    test_client_drain(&a, 500);
    test_client_drain(&b, 500);

    /* Ensure both peers have ships so the 10 Hz lane broadcaster is active. */
    {
        u8 spawn_a[96], spawn_b[96];
        int len_a = build_synthetic_spawn(spawn_a, sizeof(spawn_a), 0, 0);
        int len_b = build_synthetic_spawn(spawn_b, sizeof(spawn_b), 1, 1);
        CHECK(len_a > 0);
        CHECK(len_b > 0);
        CHECK(test_client_send_reliable(&a, spawn_a, len_a));
        CHECK(test_client_send_reliable(&b, spawn_b, len_b));
    }

    Sleep(250);
    test_client_drain(&a, 300);
    test_client_drain(&b, 300);

    /* Send an owner-style movement update from A. */
    u8 su[128];
    int su_len = build_owner_stateupdate(bc_make_ship_id(0),
                                         SA_INJ_X, SA_INJ_Y, SA_INJ_Z,
                                         SA_INJ_SPEED,
                                         su, sizeof(su));
    CHECK(su_len > 0);
    CHECK(test_client_send_unreliable(&a, su, su_len));

    i32 ship_a_id = bc_make_ship_id(0);

    bool saw_state = false;
    bool saw_subsystems = false;
    bool saw_mixed_3x = false;
    bool saw_weapon_flag = false;
    bool saw_verbatim = false;
    bool saw_transform = false; /* downstream 0x1C for ship A with our values */

    u32 start = GetTickCount();
    while ((int)(GetTickCount() - start) < 1200) {
        int msg_len = 0;
        const u8 *msg = test_client_recv_msg(&b, &msg_len, 100);
        if (!msg || msg_len <= 0) continue;
        if (msg[0] != BC_OP_STATE_UPDATE) continue;

        saw_state = true;
        if (msg_len == su_len && memcmp(msg, su, (size_t)su_len) == 0) {
            saw_verbatim = true;
        }

        if (msg_len > 9) {
            u8 dirty = msg[9];
            if (dirty & BC_DIRTY_SUBSYSTEM_STATES) saw_subsystems = true;
            if ((dirty & BC_DIRTY_SUBSYSTEM_STATES) && (dirty & 0x1C))
                saw_mixed_3x = true;
            if (dirty & BC_DIRTY_WEAPON_STATES) saw_weapon_flag = true;
        }

        /* Decode the authoritative transform and verify A's injected
         * owner-update actually propagated, not merely that some server-shaped
         * 0x1C arrived.  The injected update has fwd=(0,1,0) speed=8, so the
         * server's movement tick integrates the ship along +Y only -- X and Z
         * stay pinned at the injected values, Y drifts forward, and the speed
         * is carried verbatim.  If the owner update were ignored, the ship
         * would sit at the origin with speed 0, so these checks are decisive.
         * (We snapshot the FIRST matching ship-A packet; later packets drift
         *  further in Y, but X/Z/speed remain stable.) */
        if (!saw_transform) {
            bc_state_update_t out;
            if (bc_parse_state_update(msg, msg_len, &out) &&
                out.object_id == ship_a_id &&
                (out.dirty & BC_DIRTY_POSITION_ABS) &&
                (out.dirty & BC_DIRTY_SPEED) &&
                fabsf(out.pos_x - SA_INJ_X) < SA_TOL &&
                fabsf(out.pos_z - SA_INJ_Z) < SA_TOL &&
                fabsf(out.speed - SA_INJ_SPEED) < SA_TOL &&
                /* Y has drifted forward from the injected start by at most
                 * speed * a generous elapsed bound; never below the start. */
                out.pos_y >= (SA_INJ_Y - SA_TOL) &&
                out.pos_y <= (SA_INJ_Y + SA_INJ_SPEED * 5.0f)) {
                saw_transform = true;
            }
        }
    }

    CHECK(saw_state);
    CHECK(saw_subsystems);
    CHECK(saw_mixed_3x);
    CHECK(!saw_weapon_flag);
    CHECK(!saw_verbatim);
    CHECK(saw_transform);

cleanup:
    if (b_up) test_client_disconnect(&b);
    if (a_up) test_client_disconnect(&a);
    Sleep(100);
    if (srv_up) test_server_stop(&srv);
    if (net_up) bc_net_shutdown();

    if (failed) {
        test_fail++;
        test_pass--; /* undo the pre-increment in the run_ wrapper */
        return;
    }
}

TEST_MAIN_BEGIN()
    RUN(stateupdate_downstream_is_server_shaped);
TEST_MAIN_END()
