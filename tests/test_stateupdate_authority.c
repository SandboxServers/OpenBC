#include "test_util.h"
#include "test_harness.h"

#include "openbc/game_builders.h"
#include "openbc/buffer.h"
#include "openbc/opcodes.h"

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

TEST(stateupdate_downstream_is_server_shaped)
{
    bc_test_server_t srv;
    bc_test_client_t a, b;
    memset(&srv, 0, sizeof(srv));
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    ASSERT(bc_net_init());
    ASSERT(test_server_start(&srv, SA_PORT, SA_MANIFEST));
    ASSERT(test_client_connect(&a, SA_PORT, "SA_A", 0, SA_GAME_DIR));
    ASSERT(test_client_connect(&b, SA_PORT, "SA_B", 1, SA_GAME_DIR));

    test_client_drain(&a, 500);
    test_client_drain(&b, 500);

    /* Ensure both peers have ships so the 10 Hz lane broadcaster is active. */
    {
        u8 spawn_a[96], spawn_b[96];
        int len_a = build_synthetic_spawn(spawn_a, sizeof(spawn_a), 0, 0);
        int len_b = build_synthetic_spawn(spawn_b, sizeof(spawn_b), 1, 1);
        ASSERT(len_a > 0);
        ASSERT(len_b > 0);
        ASSERT(test_client_send_reliable(&a, spawn_a, len_a));
        ASSERT(test_client_send_reliable(&b, spawn_b, len_b));
    }

    Sleep(250);
    test_client_drain(&a, 300);
    test_client_drain(&b, 300);

    /* Send an owner-style movement update from A. */
    u8 su[128];
    int su_len = build_owner_stateupdate(bc_make_ship_id(0),
                                         123.0f, -77.0f, 19.0f,
                                         8.0f,
                                         su, sizeof(su));
    ASSERT(su_len > 0);
    ASSERT(test_client_send_unreliable(&a, su, su_len));

    bool saw_state = false;
    bool saw_subsystems = false;
    bool saw_mixed_3x = false;
    bool saw_weapon_flag = false;
    bool saw_verbatim = false;

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
    }

    ASSERT(saw_state);
    ASSERT(saw_subsystems);
    ASSERT(saw_mixed_3x);
    ASSERT(!saw_weapon_flag);
    ASSERT(!saw_verbatim);

    test_client_disconnect(&b);
    test_client_disconnect(&a);
    Sleep(100);
    test_server_stop(&srv);
    bc_net_shutdown();
}

TEST_MAIN_BEGIN()
    RUN(stateupdate_downstream_is_server_shaped);
TEST_MAIN_END()
