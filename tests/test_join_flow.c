/*
 * Join Flow Test -- verifies DeletePlayerUI (0x17) is sent to the
 * joining player during the NewPlayerInGame response sequence.
 *
 * Issue #59: stock server sends 0x17 after NewPlayerInGame (0x2A)
 * to add the joining player to the engine's internal player list.
 * Without it, the scoreboard UI cannot display any players.
 */

#include "test_util.h"
#include "test_harness.h"
#include "openbc/game_builders.h"
#include "openbc/opcodes.h"
#include "openbc/handshake.h"

#include <string.h>

#define JF_PORT       29870
#define MANIFEST_PATH "tests/fixtures/manifest.json"
#define GAME_DIR      "tests/fixtures/"

static i32 read_i32_le(const u8 *p)
{
    return (i32)((u32)p[0] |
                 ((u32)p[1] << 8) |
                 ((u32)p[2] << 16) |
                 ((u32)p[3] << 24));
}

/*
 * Verify that the joining player receives their own DeletePlayerUI (0x17)
 * with event_code = BC_EVENT_NEW_PLAYER (0x008000F1) after sending
 * NewPlayerInGame (0x2A).
 */
TEST(join_sends_delete_player_ui_to_self)
{
    bool net_ok = false;
    bool srv_ok = false;
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

    CHECK(test_server_start(&srv, JF_PORT, MANIFEST_PATH));
    srv_ok = true;

    /* --- Manual handshake (replicates test_client_connect steps 1-5) --- */
    {
        const char *name = "JoinFlowTest";
        u8 slot = 0;
        memset(&cli, 0, sizeof(cli));
        cli.slot = slot;
        cli.seq_out = 0;
        strncpy(cli.name, name, sizeof(cli.name) - 1);
        cli.server_addr.ip = htonl(0x7F000001);
        cli.server_addr.port = htons(JF_PORT);
        CHECK(bc_socket_open(&cli.sock, 0));

        /* 1. Send Connect */
        u8 pkt[512];
        int len = bc_client_build_connect(pkt, sizeof(pkt), htonl(0x7F000001));
        CHECK(len > 0);
        tc_send_raw(&cli, pkt, len);

        /* 2. Receive ConnectAck + ChecksumReq round 0 */
        CHECK(tc_recv_raw(&cli, 2000) > 0);
        bc_packet_t first_pkt;
        CHECK(bc_transport_parse(cli.recv_buf, cli.recv_len, &first_pkt));
        bc_transport_msg_t *round0_msg = tc_find_reliable_payload(&cli, &first_pkt);
        CHECK(round0_msg != NULL);

        /* 3. Send keepalive with name */
        len = bc_client_build_keepalive_name(pkt, sizeof(pkt), slot,
                                              htonl(0x7F000001), name);
        if (len > 0) tc_send_raw(&cli, pkt, len);

        /* 4. Checksum rounds 0-4 */
        for (int round = 0; round < 5; round++) {
            bc_transport_msg_t *msg;
            if (round == 0) {
                msg = round0_msg;
            } else {
                msg = NULL;
                u32 round_start = bc_ms_now();
                while ((int)(bc_ms_now() - round_start) < 2000) {
                    if (tc_recv_raw(&cli, 200) <= 0) continue;
                    bc_packet_t parsed;
                    if (!bc_transport_parse(cli.recv_buf, cli.recv_len, &parsed))
                        continue;
                    msg = tc_find_reliable_payload(&cli, &parsed);
                    if (msg) break;
                }
                CHECK(msg != NULL);
            }

            u8 resp[4096];
            int resp_len;
            if (round < 4) {
                bc_checksum_request_t req;
                CHECK(bc_client_parse_checksum_request(msg->payload,
                                                        msg->payload_len, &req));
                bc_client_dir_scan_t scan;
                CHECK(bc_client_scan_directory(GAME_DIR, req.directory,
                                                req.filter, req.recursive, &scan));
                resp_len = tc_build_real_checksum(resp, sizeof(resp),
                                                   (u8)round, &scan);
            } else {
                resp_len = bc_client_build_checksum_final(resp, sizeof(resp), 0);
            }
            CHECK(resp_len > 0);

            u8 out[4096];
            int out_len = bc_client_build_reliable(out, sizeof(out), slot,
                                                    resp, resp_len, cli.seq_out++);
            CHECK(out_len > 0);
            tc_send_raw(&cli, out, out_len);
        }

        /* 5. Drain Settings + GameInit */
        bool got_settings = false;
        bool got_gameinit = false;
        u32 drain_start = bc_ms_now();
        while ((int)(bc_ms_now() - drain_start) < 3000) {
            if (tc_recv_raw(&cli, 200) <= 0) {
                if (got_settings && got_gameinit) break;
                continue;
            }
            bc_packet_t parsed;
            if (!bc_transport_parse(cli.recv_buf, cli.recv_len, &parsed))
                continue;
            for (int i = 0; i < parsed.msg_count; i++) {
                bc_transport_msg_t *m = &parsed.msgs[i];
                if (m->type == BC_TRANSPORT_RELIABLE && (m->flags & 0x80)) {
                    tc_send_ack(&cli, m->seq);
                    if (m->payload_len > 0) {
                        if (m->payload[0] == BC_OP_SETTINGS) got_settings = true;
                        if (m->payload[0] == BC_OP_GAME_INIT) got_gameinit = true;
                    }
                }
            }
            if (got_settings && got_gameinit) break;
        }
        CHECK(got_settings);
        CHECK(got_gameinit);

        /* 6. Send NewPlayerInGame */
        u8 npig[2] = { BC_OP_NEW_PLAYER_IN_GAME, 0x20 };
        u8 npig_pkt[64];
        int npig_len = bc_client_build_reliable(npig_pkt, sizeof(npig_pkt), slot,
                                                  npig, 2, cli.seq_out++);
        CHECK(npig_len > 0);
        tc_send_raw(&cli, npig_pkt, npig_len);

        /* 7. Receive all join response messages and verify 0x17 is present */
        bool got_mission_init = false;
        bool got_delete_player_ui = false;
        u8 dpui_payload[32];
        int dpui_len = 0;

        u32 mi_start = bc_ms_now();
        while ((int)(bc_ms_now() - mi_start) < 3000) {
            if (tc_recv_raw(&cli, 200) <= 0) continue;

            bc_packet_t parsed;
            if (!bc_transport_parse(cli.recv_buf, cli.recv_len, &parsed))
                continue;

            for (int i = 0; i < parsed.msg_count; i++) {
                bc_transport_msg_t *m = &parsed.msgs[i];
                if (m->type == BC_TRANSPORT_RELIABLE && (m->flags & 0x80)) {
                    tc_send_ack(&cli, m->seq);
                    if (m->payload_len > 0) {
                        if (m->payload[0] == BC_MSG_MISSION_INIT)
                            got_mission_init = true;
                        if (m->payload[0] == BC_OP_DELETE_PLAYER_UI) {
                            got_delete_player_ui = true;
                            dpui_len = m->payload_len < (int)sizeof(dpui_payload)
                                     ? m->payload_len : (int)sizeof(dpui_payload);
                            memcpy(dpui_payload, m->payload, (size_t)dpui_len);
                        }
                    }
                }
            }
            if (got_mission_init && got_delete_player_ui) break;
        }

        cli.connected = true;

        /* Core assertions: joining player receives both MissionInit and 0x17 */
        CHECK(got_mission_init);
        CHECK(got_delete_player_ui);

        /* Verify 0x17 wire format: 18 bytes total
         * [0x17][factory=0x866:i32][event_code=0x8000F1:i32]
         * [src=0:i32][tgt=ship_id:i32][wire_peer:u8] */
        CHECK(dpui_len == 18);
        CHECK(dpui_payload[0] == BC_OP_DELETE_PLAYER_UI);

        /* Factory class ID = 0x00000866 (LE) */
        i32 factory = read_i32_le(dpui_payload + 1);
        CHECK(factory == (i32)BC_FACTORY_DELETE_PLAYER_UI);

        /* Event code = 0x008000F1 (NEW_PLAYER) */
        i32 event_code = read_i32_le(dpui_payload + 5);
        CHECK(event_code == (i32)BC_EVENT_NEW_PLAYER);

        /* Source object ID = 0 (no source) */
        i32 src_obj = read_i32_le(dpui_payload + 9);
        CHECK(src_obj == 0);

        /* Target object ID = ship ID for this player's game slot */
        i32 tgt_obj = read_i32_le(dpui_payload + 13);
        i32 expected_ship_id = bc_make_ship_id(0);  /* game_slot=0 for peer_slot=1 */
        CHECK(tgt_obj == expected_ship_id);

        /* Wire peer ID = peer_slot + 1 = 2 (first human player) */
        CHECK(dpui_payload[17] == 2);
    }

cleanup:
    if (cli.connected) test_client_disconnect(&cli);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);

#undef CHECK
}

/*
 * Verify that when a second player joins, the first player receives
 * a DeletePlayerUI (0x17) for the new player (relay-to-others path).
 */
TEST(join_sends_delete_player_ui_to_others)
{
    bool net_ok = false;
    bool srv_ok = false;
    bool cli1_ok = false;
    bool cli2_ok = false;
    int fail = 0;

    bc_test_server_t srv;
    bc_test_client_t cli1, cli2;
    memset(&srv, 0, sizeof(srv));
    memset(&cli1, 0, sizeof(cli1));
    memset(&cli2, 0, sizeof(cli2));

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fail++; \
        goto cleanup2; \
    } \
} while (0)

    CHECK(bc_net_init());
    net_ok = true;

    CHECK(test_server_start(&srv, JF_PORT + 1, MANIFEST_PATH));
    srv_ok = true;

    /* Connect first client normally */
    CHECK(test_client_connect(&cli1, JF_PORT + 1, "Player1", 0, GAME_DIR));
    cli1_ok = true;
    Sleep(100);
    test_client_drain(&cli1, 200);

    /* Connect second client */
    CHECK(test_client_connect(&cli2, JF_PORT + 1, "Player2", 0, GAME_DIR));
    cli2_ok = true;

    /* First client should receive DeletePlayerUI (0x17) for the second player */
    int msg_len = 0;
    const u8 *msg = test_client_expect_opcode(&cli1, BC_OP_DELETE_PLAYER_UI,
                                               &msg_len, 2000);
    CHECK(msg != NULL);
    CHECK(msg_len == 18);

    /* Verify it's a NEW_PLAYER event */
    i32 event_code = read_i32_le(msg + 5);
    CHECK(event_code == (i32)BC_EVENT_NEW_PLAYER);

    /* Wire peer ID should be the second player's wire slot */
    u8 wire_peer = msg[17];
    CHECK(wire_peer == 3);  /* peer_slot=2 -> wire_slot=3 */

cleanup2:
    if (cli2_ok) test_client_disconnect(&cli2);
    if (cli1_ok) test_client_disconnect(&cli1);
    Sleep(100);
    if (srv_ok) test_server_stop(&srv);
    if (net_ok) bc_net_shutdown();
    ASSERT(fail == 0);

#undef CHECK
}

TEST_MAIN_BEGIN()
    RUN(join_sends_delete_player_ui_to_self);
    RUN(join_sends_delete_player_ui_to_others);
TEST_MAIN_END()
