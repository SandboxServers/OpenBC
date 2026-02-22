#include "test_util.h"
#include "test_harness.h"
#include "openbc/game_builders.h"
#include "openbc/buffer.h"
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

static int build_env_collision_effect(u8 *buf, int buf_size,
                                      i32 target_object_id,
                                      f32 collision_force)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_COLLISION_EFFECT)) return -1;
    if (!bc_buf_write_i32(&b, 0x00008124)) return -1; /* event class */
    if (!bc_buf_write_i32(&b, 0x00800050)) return -1; /* event code */
    if (!bc_buf_write_i32(&b, 0)) return -1;          /* source=environment */
    if (!bc_buf_write_i32(&b, target_object_id)) return -1;
    if (!bc_buf_write_u8(&b, 1)) return -1;           /* one contact point */
    if (!bc_buf_write_u8(&b, 0)) return -1;
    if (!bc_buf_write_u8(&b, 0)) return -1;
    if (!bc_buf_write_u8(&b, 0)) return -1;
    if (!bc_buf_write_u8(&b, 0)) return -1;
    if (!bc_buf_write_f32(&b, collision_force)) return -1;

    return (int)b.pos;
}

static void write_i32_le(u8 *p, i32 v)
{
    u32 x = (u32)v;
    p[0] = (u8)(x);
    p[1] = (u8)(x >> 8);
    p[2] = (u8)(x >> 16);
    p[3] = (u8)(x >> 24);
}

static bool log_file_contains(const char *path, const char *needle)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    char line[1024];
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, needle)) {
            found = true;
            break;
        }
    }

    fclose(fp);
    return found;
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
    bool got_zero_health_feedback = false;
    bool saw_destroy_obj = false;
    bool saw_server_respawn = false;
    bool exploding_before_health_feedback = false;
    int add_to_repair_events = 0;
    int zero_health_delay_ms = -1;
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
        if (msg[0] == BC_OP_STATE_UPDATE && msg_len >= 12) {
            i32 object_id = read_i32_le(msg + 1);
            if (object_id == ship_id && msg[9] == 0x20) {
                /* [start_idx][first_condition] for 0x20 payload. */
                if (msg[11] == 0) {
                    got_zero_health_feedback = true;
                    if (zero_health_delay_ms < 0)
                        zero_health_delay_ms = (int)(bc_ms_now() - start);
                }
            }
            continue;
        }
        if (msg[0] == BC_OP_PYTHON_EVENT && msg_len >= 17) {
            i32 factory_id = read_i32_le(msg + 1);
            i32 event_type = read_i32_le(msg + 5);
            if (factory_id == BC_FACTORY_SUBSYSTEM_EVENT &&
                event_type == BC_EVENT_ADD_TO_REPAIR) {
                add_to_repair_events++;
                continue;
            }
            if (msg_len < 25) continue;
            if (factory_id == BC_FACTORY_OBJECT_EXPLODING &&
                event_type == BC_EVENT_OBJECT_EXPLODING) {
                if (!got_zero_health_feedback)
                    exploding_before_health_feedback = true;
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

    CHECK(got_zero_health_feedback);
    CHECK(zero_health_delay_ms >= 0 && zero_health_delay_ms <= 400);
    CHECK(!exploding_before_health_feedback);
    CHECK(got_exploding);
    CHECK(got_score_change);
    CHECK(!saw_destroy_obj);
    CHECK(!saw_server_respawn);
    CHECK(add_to_repair_events > 0);
    CHECK(add_to_repair_events <= 6);

cleanup:
    if (cli_ok) test_client_disconnect(&cli);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);

#undef CHECK
}

TEST(collision_death_no_auto_respawn_and_repair_events_bounded)
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

    CHECK(test_server_start(&srv, SD_PORT + 2, MANIFEST_PATH));
    srv_ok = true;

    CHECK(test_client_connect(&cli, SD_PORT + 2, "CollisionDeath", 0, GAME_DIR));
    cli_ok = true;

    /* Spawn one ship so collision damage can target it. */
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

    /* Trigger an environment collision with very large force to ensure death. */
    {
        u8 coll[64];
        int clen = build_env_collision_effect(coll, sizeof(coll),
                                              bc_make_ship_id(0),
                                              1000000.0f);
        CHECK(clen > 0);
        CHECK(test_client_send_reliable(&cli, coll, clen));
    }

    bool got_exploding = false;
    bool got_score_change = false;
    bool saw_destroy_obj = false;
    bool saw_server_respawn = false;
    bool saw_explosion_0x29 = false;
    int add_to_repair_events = 0;
    const i32 ship_id = bc_make_ship_id(0);

    /* Observe for >5s to catch old server auto-respawn behavior. */
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
        /* Issue #60: collision kills must NOT produce Explosion (0x29). */
        if (msg[0] == BC_OP_EXPLOSION) {
            saw_explosion_0x29 = true;
            continue;
        }
        if (msg[0] == BC_OP_PYTHON_EVENT && msg_len >= 17) {
            i32 factory_id = read_i32_le(msg + 1);
            i32 event_type = read_i32_le(msg + 5);

            if (factory_id == BC_FACTORY_SUBSYSTEM_EVENT &&
                event_type == BC_EVENT_ADD_TO_REPAIR) {
                add_to_repair_events++;
                continue;
            }

            if (msg_len >= 25 &&
                factory_id == BC_FACTORY_OBJECT_EXPLODING &&
                event_type == BC_EVENT_OBJECT_EXPLODING) {
                i32 dest_obj = read_i32_le(msg + 13);
                f32 lifetime = read_f32_le(msg + 21);
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
    CHECK(!saw_explosion_0x29);
    /* Death repair burst (issue #61) raises the upper bound. */
    CHECK(add_to_repair_events <= 30);

cleanup:
    if (cli_ok) test_client_disconnect(&cli);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);

#undef CHECK
}

TEST(mission_init_total_slots_matches_server_limit)
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
        CHECK(msg[1] == BC_MISSION_INIT_PLAYER_LIMIT);
    }

cleanup:
    if (cli_ok) test_client_disconnect(&cli);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);

#undef CHECK
}

TEST(respawn_collision_uses_new_object_id)
{
    bool net_ok = false;
    bool srv_ok = false;
    bool cli_ok = false;
    int fail = 0;

    const u16 port = SD_PORT + 3;
    const i32 respawn_obj = 0x40000021;
    char log_path[64];
    snprintf(log_path, sizeof(log_path), "server_test_%u.log", (unsigned)port);
    remove(log_path);

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

    CHECK(test_server_start(&srv, port, MANIFEST_PATH));
    srv_ok = true;

    CHECK(test_client_connect(&cli, port, "RespawnCollision", 0, GAME_DIR));
    cli_ok = true;

    /* Initial spawn uses stock slot-0 ship ID (0x3FFFFFFF). */
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

    /* Self-destruct clears has_ship but leaves mapping state behind. */
    {
        const u8 host_msg[1] = { BC_OP_HOST_MSG };
        CHECK(test_client_send_reliable(&cli, host_msg, (int)sizeof(host_msg)));
    }
    Sleep(200);
    test_client_drain(&cli, 250);

    /* Respawn with a new ship object ID from the same player range. */
    {
        u8 respawn_blob[sizeof(TEST_SHIP_DATA)];
        u8 respawn_pkt[256];
        memcpy(respawn_blob, TEST_SHIP_DATA, sizeof(respawn_blob));
        write_i32_le(respawn_blob + 4, respawn_obj);

        int rlen = bc_build_object_create_team(respawn_pkt, sizeof(respawn_pkt),
                                               0, 2,
                                               respawn_blob,
                                               (int)sizeof(respawn_blob));
        CHECK(rlen > 0);
        CHECK(test_client_send_reliable(&cli, respawn_pkt, rlen));
    }
    Sleep(150);
    test_client_drain(&cli, 250);

    /* Environment collision against the respawned object ID. */
    {
        u8 coll[64];
        int clen = build_env_collision_effect(coll, sizeof(coll),
                                              respawn_obj,
                                              1000000.0f);
        CHECK(clen > 0);
        CHECK(test_client_send_reliable(&cli, coll, clen));
    }

    Sleep(300);
    test_client_drain(&cli, 300);

cleanup:
    if (cli_ok) test_client_disconnect(&cli);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();

    if (fail == 0) {
        if (!log_file_contains(log_path, "Collision:")) {
            printf("FAIL\n    %s:%d: expected collision processing log in %s\n",
                   __FILE__, __LINE__, log_path);
            fail++;
        }
        if (log_file_contains(log_path, "slot=1 collision ownership fail")) {
            printf("FAIL\n    %s:%d: found slot-1 ownership failure log in %s\n",
                   __FILE__, __LINE__, log_path);
            fail++;
        }
    }

    ASSERT(fail == 0);

#undef CHECK
}

TEST_MAIN_BEGIN()
    RUN(self_destruct_pipeline_matches_stock);
    RUN(collision_death_no_auto_respawn_and_repair_events_bounded);
    RUN(mission_init_total_slots_matches_server_limit);
    RUN(respawn_collision_uses_new_object_id);
TEST_MAIN_END()
