#include "test_util.h"
#include "test_harness.h"
#include "openbc/game_builders.h"
#include "openbc/opcodes.h"

#include <math.h>
#include <string.h>

#define SD_PORT       29881
#define MANIFEST_PATH "tests/fixtures/manifest.json"
#define GAME_DIR      "tests/fixtures/"

/* Reused from test_battle.c: valid serialized ship blob payload. */
static const u8 TEST_SHIP_DATA[] = {
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

static i32 read_i32_le(const u8 *p)
{
    return (i32)((u32)p[0] |
                 ((u32)p[1] << 8) |
                 ((u32)p[2] << 16) |
                 ((u32)p[3] << 24));
}

static f32 read_f32_le(const u8 *p)
{
    f32 v = 0.0f;
    memcpy(&v, p, sizeof(v));
    return v;
}

TEST(self_destruct_pipeline_matches_stock)
{
    bool net_ok = false;
    bool srv_ok = false;
    bool cli_ok = false;
    int fail = 0;

    bc_test_server_t srv;
    bc_test_client_t cli;
    memset(&srv, 0, sizeof(srv));
    memset(&cli, 0, sizeof(cli));

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail++; \
        goto cleanup; \
    } \
} while (0)

    CHECK(bc_net_init());
    net_ok = true;

    CHECK(test_server_start(&srv, SD_PORT, MANIFEST_PATH));
    srv_ok = true;

    CHECK(test_client_connect(&cli, SD_PORT, "SelfDestruct", 0, GAME_DIR));
    cli_ok = true;

    /* Spawn one ship so HostMsg self-destruct is accepted. */
    {
        u8 spawn[256];
        int slen = bc_build_object_create_team(spawn, sizeof(spawn), 0, 2,
                                               TEST_SHIP_DATA,
                                               (int)sizeof(TEST_SHIP_DATA));
        CHECK(slen > 0);
        CHECK(test_client_send_reliable(&cli, spawn, slen));
    }

    Sleep(150);
    test_client_drain(&cli, 200);

    /* Trigger self-destruct: HostMsg is a single-byte opcode 0x13. */
    {
        const u8 host_msg[1] = { BC_OP_HOST_MSG };
        CHECK(test_client_send_reliable(&cli, host_msg, (int)sizeof(host_msg)));
    }

    bool got_exploding = false;
    bool got_score_change = false;
    bool saw_destroy_obj = false;
    bool saw_server_respawn = false;
    const i32 ship_id = bc_make_ship_id(0);

    /* Observe for >5s to catch old auto-respawn timer behavior. */
    u32 start = bc_ms_now();
    while ((int)(bc_ms_now() - start) < 6200) {
        int msg_len = 0;
        const u8 *msg = test_client_recv_msg(&cli, &msg_len, 150);
        if (!msg || msg_len <= 0) continue;

        if (msg[0] == BC_OP_DESTROY_OBJ) {
            saw_destroy_obj = true;
            continue;
        }
        if (msg[0] == BC_OP_OBJ_CREATE_TEAM) {
            saw_server_respawn = true;
            continue;
        }
        if (msg[0] == BC_MSG_SCORE_CHANGE) {
            got_score_change = true;
            continue;
        }
        if (msg[0] == BC_OP_PYTHON_EVENT && msg_len >= 25) {
            i32 factory_id = read_i32_le(msg + 1);
            i32 event_type = read_i32_le(msg + 5);
            if (factory_id == BC_FACTORY_OBJECT_EXPLODING &&
                event_type == BC_EVENT_OBJECT_EXPLODING) {
                i32 source_obj = read_i32_le(msg + 9);
                i32 dest_obj = read_i32_le(msg + 13);
                f32 lifetime = read_f32_le(msg + 21);
                CHECK(source_obj == 0);
                CHECK(dest_obj == ship_id);
                CHECK(fabsf(lifetime - 9.5f) < 0.01f);
                got_exploding = true;
            }
        }
    }

    CHECK(got_exploding);
    CHECK(got_score_change);
    CHECK(!saw_destroy_obj);
    CHECK(!saw_server_respawn);

cleanup:
    if (cli_ok) test_client_disconnect(&cli);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);

#undef CHECK
}

TEST(mission_init_total_slots_stock_value)
{
    bool net_ok = false;
    bool srv_ok = false;
    bool cli_ok = false;
    int fail = 0;

    bc_test_server_t srv;
    bc_test_client_t cli;
    memset(&srv, 0, sizeof(srv));
    memset(&cli, 0, sizeof(cli));

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail++; \
        goto cleanup; \
    } \
} while (0)

    CHECK(bc_net_init());
    net_ok = true;

    CHECK(test_server_start(&srv, SD_PORT + 1, MANIFEST_PATH));
    srv_ok = true;

    CHECK(test_client_connect(&cli, SD_PORT + 1, "MissionInitProbe", 0, GAME_DIR));
    cli_ok = true;

    /* Re-send NewPlayerInGame; server should answer with MissionInit again. */
    {
        const u8 npig[2] = { BC_OP_NEW_PLAYER_IN_GAME, 0x20 };
        CHECK(test_client_send_reliable(&cli, npig, (int)sizeof(npig)));
    }

    {
        int msg_len = 0;
        const u8 *msg = test_client_expect_opcode(&cli, BC_MSG_MISSION_INIT,
                                                  &msg_len, 2000);
        CHECK(msg != NULL);
        CHECK(msg_len >= 2);
        CHECK(msg[1] == 9); /* stock dedicated totalSlots */
    }

cleanup:
    if (cli_ok) test_client_disconnect(&cli);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);

#undef CHECK
}

TEST_MAIN_BEGIN()
    RUN(self_destruct_pipeline_matches_stock);
    RUN(mission_init_total_slots_stock_value);
TEST_MAIN_END()
