#include "test_util.h"
#include "test_harness.h"
#include "openbc/opcodes.h"

#include <string.h>

#define RESTART_PORT   29882
#define MANIFEST_PATH  "tests/fixtures/manifest.json"
#define GAME_DIR       "tests/fixtures/"

static bool saw_opcode_within(bc_test_client_t *cli, u8 opcode, int timeout_ms)
{
    u32 start = bc_ms_now();
    while ((int)(bc_ms_now() - start) < timeout_ms) {
        int remaining = timeout_ms - (int)(bc_ms_now() - start);
        if (remaining > 150) remaining = 150;

        int msg_len = 0;
        const u8 *msg = test_client_recv_msg(cli, &msg_len, remaining);
        if (!msg || msg_len <= 0) continue;
        if (msg[0] == opcode) return true;
    }
    return false;
}

TEST(restart_requires_host_slot)
{
    bool net_ok = false;
    bool srv_ok = false;
    bool host_ok = false;
    bool guest_ok = false;
    int fail = 0;

    bc_test_server_t srv;
    bc_test_client_t host;
    bc_test_client_t guest;
    memset(&srv, 0, sizeof(srv));
    memset(&host, 0, sizeof(host));
    memset(&guest, 0, sizeof(guest));

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail++; \
        goto cleanup; \
    } \
} while (0)

    CHECK(bc_net_init());
    net_ok = true;

    CHECK(test_server_start(&srv, RESTART_PORT, MANIFEST_PATH));
    srv_ok = true;

    CHECK(test_client_connect(&host, RESTART_PORT, "Host", 0, GAME_DIR));
    host_ok = true;

    CHECK(test_client_connect(&guest, RESTART_PORT, "Guest", 1, GAME_DIR));
    guest_ok = true;

    test_client_drain(&host, 250);
    test_client_drain(&guest, 250);

    {
        const u8 restart[1] = { BC_MSG_RESTART };
        CHECK(test_client_send_reliable(&guest, restart, (int)sizeof(restart)));
    }

    CHECK(!saw_opcode_within(&host, BC_MSG_RESTART, 1200));
    CHECK(!saw_opcode_within(&guest, BC_MSG_RESTART, 1200));

    {
        const u8 restart[1] = { BC_MSG_RESTART };
        CHECK(test_client_send_reliable(&host, restart, (int)sizeof(restart)));
    }

    CHECK(saw_opcode_within(&host, BC_MSG_RESTART, 2000));
    CHECK(saw_opcode_within(&guest, BC_MSG_RESTART, 2000));

cleanup:
    if (guest_ok) test_client_disconnect(&guest);
    if (host_ok) test_client_disconnect(&host);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);

#undef CHECK
}

TEST_MAIN_BEGIN()
    RUN(restart_requires_host_slot);
TEST_MAIN_END()
