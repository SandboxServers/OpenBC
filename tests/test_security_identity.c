#include "test_util.h"
#include "test_harness.h"
#include "openbc/game_builders.h"
#include "openbc/game_events.h"
#include "openbc/opcodes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID_PORT_BASE   29882
#define MANIFEST_PATH  "tests/fixtures/manifest.json"
#define GAME_DIR       "tests/fixtures/"

/* Minimal ship blob header accepted by bc_parse_ship_blob_header(). */
static const u8 MIN_SHIP_BLOB[] = {
    0x08, 0x80, 0x00, 0x00, /* blob prefix */
    0xFF, 0xFF, 0xFF, 0x3F, /* object_id = 0x3FFFFFFF */
    0x01,                   /* species_id */
    0x00, 0x00, 0x00, 0x00, /* pos.x */
    0x00, 0x00, 0x00, 0x00, /* pos.y */
    0x00, 0x00, 0x00, 0x00, /* pos.z */
};

static bool wait_for_opcode(bc_test_client_t *c, u8 opcode, int timeout_ms)
{
    u32 start = bc_ms_now();
    while ((int)(bc_ms_now() - start) < timeout_ms) {
        int msg_len = 0;
        const u8 *msg = test_client_recv_msg(c, &msg_len, 50);
        if (!msg || msg_len <= 0) continue;
        if (msg[0] == opcode) return true;
    }
    return false;
}

static bool file_contains_text(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }

    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return false;
    }
    rewind(f);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);

    bool found = strstr(buf, needle) != NULL;
    free(buf);
    return found;
}

TEST(chat_sender_spoof_is_corrected)
{
    bool net_ok = false;
    bool srv_ok = false;
    bool c0_ok = false;
    bool c1_ok = false;
    int fail = 0;

    bc_test_server_t srv;
    bc_test_client_t c0, c1;
    memset(&srv, 0, sizeof(srv));
    memset(&c0, 0, sizeof(c0));
    memset(&c1, 0, sizeof(c1));

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail++; \
        goto cleanup; \
    } \
} while (0)

    CHECK(bc_net_init());
    net_ok = true;

    CHECK(test_server_start(&srv, ID_PORT_BASE, MANIFEST_PATH));
    srv_ok = true;

    CHECK(test_client_connect(&c0, ID_PORT_BASE, "Spoofer", 0, GAME_DIR));
    c0_ok = true;
    CHECK(test_client_connect(&c1, ID_PORT_BASE, "Observer", 1, GAME_DIR));
    c1_ok = true;

    test_client_drain(&c0, 300);
    test_client_drain(&c1, 300);

    {
        u8 buf[128];
        int len = bc_build_chat(buf, sizeof(buf), 7, false, "spoof-all");
        CHECK(len > 0);
        CHECK(test_client_send_reliable(&c0, buf, len));

        len = bc_build_chat(buf, sizeof(buf), 6, true, "spoof-team");
        CHECK(len > 0);
        CHECK(test_client_send_reliable(&c0, buf, len));
    }

    {
        int msg_len = 0;
        const u8 *msg = test_client_expect_opcode(&c1, BC_MSG_CHAT, &msg_len, 2000);
        CHECK(msg != NULL);

        bc_chat_event_t ev;
        CHECK(bc_parse_chat_message(msg, msg_len, &ev));
        CHECK(ev.sender_slot == 0);
        CHECK(strcmp(ev.message, "spoof-all") == 0);

        msg = test_client_expect_opcode(&c1, BC_MSG_TEAM_CHAT, &msg_len, 2000);
        CHECK(msg != NULL);
        CHECK(bc_parse_chat_message(msg, msg_len, &ev));
        CHECK(ev.sender_slot == 0);
        CHECK(strcmp(ev.message, "spoof-team") == 0);
    }

cleanup:
    if (c1_ok) test_client_disconnect(&c1);
    if (c0_ok) test_client_disconnect(&c0);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);

#undef CHECK
}

TEST(objcreate_owner_spoof_is_rejected)
{
    bool net_ok = false;
    bool srv_ok = false;
    bool c0_ok = false;
    bool c1_ok = false;
    int fail = 0;
    const int port = ID_PORT_BASE + 1;
    const char *log_path = "server_test_29883.log";

    bc_test_server_t srv;
    bc_test_client_t c0, c1;
    memset(&srv, 0, sizeof(srv));
    memset(&c0, 0, sizeof(c0));
    memset(&c1, 0, sizeof(c1));

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail++; \
        goto cleanup; \
    } \
} while (0)

    remove(log_path);

    CHECK(bc_net_init());
    net_ok = true;

    CHECK(test_server_start(&srv, (u16)port, MANIFEST_PATH));
    srv_ok = true;

    CHECK(test_client_connect(&c0, (u16)port, "Spoofer", 0, GAME_DIR));
    c0_ok = true;
    CHECK(test_client_connect(&c1, (u16)port, "Observer", 1, GAME_DIR));
    c1_ok = true;

    test_client_drain(&c0, 300);
    test_client_drain(&c1, 300);

    /* Owner spoof: sender is slot 0, payload claims owner slot 1. */
    {
        u8 spawn[128];
        int len = bc_build_object_create_team(spawn, sizeof(spawn), 1, 0,
                                              MIN_SHIP_BLOB,
                                              (int)sizeof(MIN_SHIP_BLOB));
        CHECK(len > 0);
        CHECK(test_client_send_reliable(&c0, spawn, len));
    }

    Sleep(200);
    CHECK(!wait_for_opcode(&c1, BC_OP_OBJ_CREATE_TEAM, 400));

    /* Valid spawn should still relay normally after the rejected spoof. */
    {
        u8 spawn[128];
        int len = bc_build_object_create_team(spawn, sizeof(spawn), 0, 0,
                                              MIN_SHIP_BLOB,
                                              (int)sizeof(MIN_SHIP_BLOB));
        CHECK(len > 0);
        CHECK(test_client_send_reliable(&c0, spawn, len));
    }

    {
        int msg_len = 0;
        const u8 *msg = test_client_expect_opcode(&c1, BC_OP_OBJ_CREATE_TEAM,
                                                  &msg_len, 2000);
        CHECK(msg != NULL);
    }

    /* Team reassignment attempt while ship is active should be ignored. */
    {
        u8 spawn[128];
        int len = bc_build_object_create_team(spawn, sizeof(spawn), 0, 1,
                                              MIN_SHIP_BLOB,
                                              (int)sizeof(MIN_SHIP_BLOB));
        CHECK(len > 0);
        CHECK(test_client_send_reliable(&c0, spawn, len));
    }

    Sleep(300);

cleanup:
    if (c1_ok) test_client_disconnect(&c1);
    if (c0_ok) test_client_disconnect(&c0);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();

    if (fail == 0) {
        if (!file_contains_text(log_path, "spoofed ObjCreate owner=1, rejecting")) {
            printf("FAIL\n    %s:%d: missing owner spoof warning in %s\n",
                   __FILE__, __LINE__, log_path);
            fail++;
        }
        if (!file_contains_text(log_path, "ObjCreateTeam team=1 ignored (already has ship)")) {
            printf("FAIL\n    %s:%d: missing team reassignment warning in %s\n",
                   __FILE__, __LINE__, log_path);
            fail++;
        }
    }
    ASSERT(fail == 0);

#undef CHECK
}

TEST_MAIN_BEGIN()
    RUN(chat_sender_spoof_is_corrected);
    RUN(objcreate_owner_spoof_is_rejected);
TEST_MAIN_END()
