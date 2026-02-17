/*
 * GameSpy query/response tests.
 *
 * Unit tests for response building, secure challenge, gsmsalg, and
 * integration tests including a mock master server that performs
 * the full heartbeat -> secure -> validate -> status handshake.
 */

#include "test_util.h"
#include "test_harness.h"
#include "openbc/gamespy.h"
#include <string.h>

/* --- Helper: initialize a test bc_server_info_t --- */

static void gs_test_info(bc_server_info_t *info, const char *hostname,
                          const char *mapname, int numplayers, int maxplayers)
{
    memset(info, 0, sizeof(*info));
    snprintf(info->hostname, sizeof(info->hostname), "%s", hostname);
    snprintf(info->missionscript, sizeof(info->missionscript), "Multi1");
    snprintf(info->mapname, sizeof(info->mapname), "%s", mapname);
    snprintf(info->gamemode, sizeof(info->gamemode), "openplaying");
    snprintf(info->system, sizeof(info->system), "DeepSpace9");
    info->numplayers = numplayers;
    info->maxplayers = maxplayers;
    info->timelimit = 0;
    info->fraglimit = 0;
}

/* --- Helper: extract a GameSpy value from a response string --- */

static const char *gs_find_value(const char *response, int resp_len,
                                  const char *key, int *value_len)
{
    int klen = (int)strlen(key);

    for (int i = 0; i < resp_len; i++) {
        if (response[i] != '\\') continue;

        int start = i + 1;
        if (start + klen >= resp_len) continue;
        if (memcmp(response + start, key, (size_t)klen) != 0) continue;
        if (response[start + klen] != '\\') continue;

        int vstart = start + klen + 1;
        int vend = vstart;
        while (vend < resp_len && response[vend] != '\\')
            vend++;
        *value_len = vend - vstart;
        return response + vstart;
    }
    return NULL;
}

static bool gs_has_value(const char *response, int resp_len,
                          const char *key, const char *expected)
{
    int vlen = 0;
    const char *val = gs_find_value(response, resp_len, key, &vlen);
    if (!val) return false;
    int elen = (int)strlen(expected);
    return vlen == elen && memcmp(val, expected, (size_t)elen) == 0;
}

static bool gs_has_key(const char *response, int resp_len, const char *key)
{
    int vlen = 0;
    return gs_find_value(response, resp_len, key, &vlen) != NULL;
}

/* ======================================================================
 * Unit tests: bc_gamespy_build_response (stock BC QR1 format)
 * ====================================================================== */

TEST(response_basic_fields)
{
    bc_server_info_t info;
    gs_test_info(&info, "Test Server", "TestMap", 2, 6);

    u8 buf[1024];
    int len = bc_gamespy_build_response(buf, sizeof(buf), &info, NULL, 0);

    ASSERT(len > 0);

    /* Info callback comes FIRST (verified from stock dedi trace) */
    ASSERT(memcmp(buf, "\\gamename\\bcommander\\gamever\\60\\location\\0", 41) == 0);

    /* Basic callback fields */
    ASSERT(gs_has_value((char *)buf, len, "hostname", "Test Server"));
    ASSERT(gs_has_value((char *)buf, len, "missionscript", "Multi1"));
    ASSERT(gs_has_value((char *)buf, len, "mapname", "TestMap"));
    ASSERT(gs_has_value((char *)buf, len, "numplayers", "2"));
    ASSERT(gs_has_value((char *)buf, len, "maxplayers", "6"));
    ASSERT(gs_has_value((char *)buf, len, "gamemode", "openplaying"));

    /* Info callback fields (come FIRST in stock BC, format at 0x00959c50) */
    ASSERT(gs_has_value((char *)buf, len, "gamename", "bcommander"));
    ASSERT(gs_has_value((char *)buf, len, "gamever", "60"));
    ASSERT(gs_has_value((char *)buf, len, "location", "0"));

    /* Rules callback fields (format at 0x00959cf8) */
    ASSERT(gs_has_value((char *)buf, len, "timelimit", "0"));
    ASSERT(gs_has_value((char *)buf, len, "fraglimit", "0"));
    ASSERT(gs_has_value((char *)buf, len, "system", "DeepSpace9"));
    ASSERT(gs_has_value((char *)buf, len, "password", "0"));

    /* Stock QR SDK order: \final\ then \queryid\ (queryid is last field) */
    ASSERT(gs_has_key((char *)buf, len, "queryid"));
    ASSERT(gs_has_key((char *)buf, len, "final"));

    /* Verify \final\ comes BEFORE \queryid\ in the response */
    const char *final_pos = strstr((char *)buf, "\\final\\");
    const char *qid_pos = strstr((char *)buf, "\\queryid\\");
    ASSERT(final_pos != NULL);
    ASSERT(qid_pos != NULL);
    ASSERT(final_pos < qid_pos);
}

TEST(response_password_field)
{
    bc_server_info_t info;
    gs_test_info(&info, "PW Test", "Map", 0, 8);

    u8 buf[1024];
    int len = bc_gamespy_build_response(buf, sizeof(buf), &info, NULL, 0);

    ASSERT(len > 0);
    ASSERT(gs_has_value((char *)buf, len, "password", "0"));
}

TEST(response_queryid_echoed)
{
    bc_server_info_t info;
    gs_test_info(&info, "QID Test", "Map", 0, 8);

    /* Query with queryid -- should echo it back */
    const char *query = "\\basic\\\\queryid\\42.1\\";
    u8 buf[1024];
    int len = bc_gamespy_build_response(buf, sizeof(buf), &info,
                                         (const u8 *)query,
                                         (int)strlen(query));

    ASSERT(len > 0);
    ASSERT(gs_has_value((char *)buf, len, "queryid", "42.1"));

    /* Stock QR SDK: response ends with \queryid\N.M (no trailing \) */
    const char *last_qid = strstr((char *)buf, "\\queryid\\42.1");
    ASSERT(last_qid != NULL);
}

TEST(response_default_queryid)
{
    /* Query WITHOUT queryid -- response should still have queryid 1.1
     * (stock BC always appends queryid via qr_flush_send) */
    bc_server_info_t info;
    gs_test_info(&info, "DefQID", "Map", 0, 8);

    const char *query = "\\basic\\";
    u8 buf[1024];
    int len = bc_gamespy_build_response(buf, sizeof(buf), &info,
                                         (const u8 *)query,
                                         (int)strlen(query));

    ASSERT(len > 0);

    /* Default queryid 1.1 should be present */
    ASSERT(gs_has_value((char *)buf, len, "queryid", "1.1"));

    /* \final\ must come before \queryid\ */
    const char *fp = strstr((char *)buf, "\\final\\");
    const char *qp = strstr((char *)buf, "\\queryid\\");
    ASSERT(fp != NULL && qp != NULL && fp < qp);
}

TEST(response_null_query)
{
    bc_server_info_t info;
    gs_test_info(&info, "Null", "Map", 1, 4);

    u8 buf[1024];
    int len = bc_gamespy_build_response(buf, sizeof(buf), &info, NULL, 0);

    ASSERT(len > 0);
    ASSERT(gs_has_value((char *)buf, len, "hostname", "Null"));

    /* Default queryid should be present even with NULL query */
    ASSERT(gs_has_value((char *)buf, len, "queryid", "1.1"));

    /* \final\ before \queryid\ */
    const char *fp = strstr((char *)buf, "\\final\\");
    const char *qp = strstr((char *)buf, "\\queryid\\");
    ASSERT(fp != NULL && qp != NULL && fp < qp);
}

TEST(is_query_detection)
{
    const u8 query[] = "\\basic\\";
    ASSERT(bc_gamespy_is_query(query, sizeof(query) - 1));

    const u8 game_pkt[] = { 0xFF, 0x01, 0x04, 0x02 };
    ASSERT(!bc_gamespy_is_query(game_pkt, sizeof(game_pkt)));

    ASSERT(!bc_gamespy_is_query(NULL, 0));
}

TEST(response_status_query)
{
    bc_server_info_t info;
    gs_test_info(&info, "Status Test", "Map", 3, 8);

    const char *query = "\\status\\\\queryid\\7.1\\";
    u8 buf[1024];
    int len = bc_gamespy_build_response(buf, sizeof(buf), &info,
                                         (const u8 *)query,
                                         (int)strlen(query));

    ASSERT(len > 0);
    ASSERT(gs_has_value((char *)buf, len, "hostname", "Status Test"));
    ASSERT(gs_has_value((char *)buf, len, "numplayers", "3"));
    ASSERT(gs_has_value((char *)buf, len, "queryid", "7.1"));
    ASSERT(gs_has_value((char *)buf, len, "password", "0"));
    ASSERT(gs_has_value((char *)buf, len, "timelimit", "0"));
    ASSERT(gs_has_value((char *)buf, len, "fraglimit", "0"));
    ASSERT(gs_has_value((char *)buf, len, "system", "DeepSpace9"));
}

TEST(response_with_limits)
{
    /* Verify timelimit/fraglimit are correctly rendered */
    bc_server_info_t info;
    gs_test_info(&info, "Limits", "Map", 0, 8);
    info.timelimit = 15;
    info.fraglimit = 25;

    u8 buf[1024];
    int len = bc_gamespy_build_response(buf, sizeof(buf), &info, NULL, 0);

    ASSERT(len > 0);
    ASSERT(gs_has_value((char *)buf, len, "timelimit", "15"));
    ASSERT(gs_has_value((char *)buf, len, "fraglimit", "25"));
}

/* ======================================================================
 * Unit tests: \secure\ challenge detection and extraction
 * ====================================================================== */

TEST(is_secure_detection)
{
    const u8 secure[] = "\\secure\\abcdef";
    ASSERT(bc_gamespy_is_secure(secure, sizeof(secure) - 1));

    const u8 query[] = "\\basic\\";
    ASSERT(!bc_gamespy_is_secure(query, sizeof(query) - 1));

    const u8 short_pkt[] = "\\secure";
    ASSERT(!bc_gamespy_is_secure(short_pkt, sizeof(short_pkt) - 1));

    ASSERT(!bc_gamespy_is_secure(NULL, 0));
}

TEST(extract_secure_challenge)
{
    char out[64];

    const u8 pkt1[] = "\\secure\\abc123";
    int len = bc_gamespy_extract_secure(pkt1, sizeof(pkt1) - 1,
                                         out, sizeof(out));
    ASSERT(len == 6);
    ASSERT(strcmp(out, "abc123") == 0);

    const u8 pkt2[] = "\\secure\\HELLO\\final\\";
    len = bc_gamespy_extract_secure(pkt2, sizeof(pkt2) - 1,
                                     out, sizeof(out));
    ASSERT(len == 5);
    ASSERT(strcmp(out, "HELLO") == 0);

    const u8 pkt3[] = "\\secure\\";
    len = bc_gamespy_extract_secure(pkt3, sizeof(pkt3) - 1,
                                     out, sizeof(out));
    ASSERT(len == 0);

    const u8 pkt4[] = "\\basic\\";
    len = bc_gamespy_extract_secure(pkt4, sizeof(pkt4) - 1,
                                     out, sizeof(out));
    ASSERT(len == 0);
}

/* ======================================================================
 * Unit tests: gsmsalg and validate response
 * ====================================================================== */

TEST(gsmsalg_produces_output)
{
    char dst[89];
    bc_gsmsalg(dst, "abcdef", BC_GAMESPY_SECRET_KEY, 0);
    ASSERT(strlen(dst) > 0);

    for (int i = 0; dst[i]; i++) {
        char c = dst[i];
        ASSERT((c >= 'A' && c <= 'Z') ||
               (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') ||
               c == '+' || c == '/');
    }
}

TEST(gsmsalg_deterministic)
{
    char dst1[89], dst2[89];
    bc_gsmsalg(dst1, "TEST42", BC_GAMESPY_SECRET_KEY, 0);
    bc_gsmsalg(dst2, "TEST42", BC_GAMESPY_SECRET_KEY, 0);
    ASSERT(strcmp(dst1, dst2) == 0);
}

TEST(gsmsalg_different_challenges)
{
    char dst1[89], dst2[89];
    bc_gsmsalg(dst1, "aaaaaa", BC_GAMESPY_SECRET_KEY, 0);
    bc_gsmsalg(dst2, "bbbbbb", BC_GAMESPY_SECRET_KEY, 0);
    ASSERT(strcmp(dst1, dst2) != 0);
}

TEST(gsmsalg_length_multiple_of_4)
{
    char dst[89];
    bc_gsmsalg(dst, "xyz", BC_GAMESPY_SECRET_KEY, 0);
    ASSERT(strlen(dst) % 4 == 0);

    bc_gsmsalg(dst, "abcdef", BC_GAMESPY_SECRET_KEY, 0);
    ASSERT(strlen(dst) % 4 == 0);
}

TEST(gsmsalg_empty_challenge)
{
    char dst[89];
    bc_gsmsalg(dst, "", BC_GAMESPY_SECRET_KEY, 0);
    ASSERT(dst[0] == '\0');
}

TEST(build_validate_response)
{
    u8 buf[512];
    int len = bc_gamespy_build_validate(buf, sizeof(buf), "testch");

    ASSERT(len > 0);
    ASSERT(gs_has_value((char *)buf, len, "gamename", "bcommander"));
    ASSERT(gs_has_value((char *)buf, len, "gamever", "60"));

    int vlen = 0;
    const char *val = gs_find_value((char *)buf, len, "validate", &vlen);
    ASSERT(val != NULL);
    ASSERT(vlen > 0);

    ASSERT(gs_has_value((char *)buf, len, "queryid", "1.1"));
}

TEST(build_validate_deterministic)
{
    u8 buf1[512], buf2[512];
    int len1 = bc_gamespy_build_validate(buf1, sizeof(buf1), "xyz123");
    int len2 = bc_gamespy_build_validate(buf2, sizeof(buf2), "xyz123");

    ASSERT(len1 == len2);
    ASSERT(memcmp(buf1, buf2, (size_t)len1) == 0);
}

/* ======================================================================
 * Integration tests: server responds to GameSpy queries
 * ====================================================================== */

#define GS_PORT      29877
#define GS_TIMEOUT   2000  /* ms */
#define MANIFEST_PATH "tests\\fixtures\\manifest.json"

TEST(server_responds_to_lan_query)
{
    bc_test_server_t srv;
    ASSERT(bc_net_init());
    ASSERT(test_server_start(&srv, GS_PORT, MANIFEST_PATH));

    bc_socket_t sock;
    ASSERT(bc_socket_open(&sock, 0));

    bc_addr_t srv_addr;
    srv_addr.ip = htonl(0x7F000001);
    srv_addr.port = htons(GS_PORT);

    const char *query = "\\status\\";
    bc_socket_send(&sock, &srv_addr, (const u8 *)query,
                   (int)strlen(query));

    u8 resp[1024];
    bc_addr_t from;
    bool got_response = false;

    for (int i = 0; i < 20; i++) {
        int got = bc_socket_recv(&sock, &from, resp, sizeof(resp));
        if (got > 0) {
            got_response = true;

            /* Stock BC response fields (verified from trace) */
            ASSERT(gs_has_value((char *)resp, got, "gamename", "bcommander"));
            ASSERT(gs_has_value((char *)resp, got, "gamever", "60"));
            ASSERT(gs_has_key((char *)resp, got, "hostname"));
            ASSERT(gs_has_key((char *)resp, got, "missionscript"));
            ASSERT(gs_has_key((char *)resp, got, "mapname"));
            ASSERT(gs_has_key((char *)resp, got, "numplayers"));
            ASSERT(gs_has_key((char *)resp, got, "maxplayers"));
            ASSERT(gs_has_key((char *)resp, got, "gamemode"));
            ASSERT(gs_has_key((char *)resp, got, "timelimit"));
            ASSERT(gs_has_key((char *)resp, got, "fraglimit"));
            ASSERT(gs_has_key((char *)resp, got, "system"));
            ASSERT(gs_has_value((char *)resp, got, "password", "0"));
            ASSERT(gs_has_key((char *)resp, got, "queryid"));
            /* \final\ before \queryid\ */
            ASSERT(strstr((char *)resp, "\\final\\") != NULL);
            ASSERT(strstr((char *)resp, "\\final\\") <
                   strstr((char *)resp, "\\queryid\\"));
            break;
        }
        Sleep(100);
    }
    ASSERT(got_response);

    bc_socket_close(&sock);
    test_server_stop(&srv);
    bc_net_shutdown();
}

TEST(server_responds_with_queryid)
{
    bc_test_server_t srv;
    ASSERT(bc_net_init());
    ASSERT(test_server_start(&srv, GS_PORT, MANIFEST_PATH));

    bc_socket_t sock;
    ASSERT(bc_socket_open(&sock, 0));

    bc_addr_t srv_addr;
    srv_addr.ip = htonl(0x7F000001);
    srv_addr.port = htons(GS_PORT);

    const char *query = "\\status\\\\queryid\\99.1\\";
    bc_socket_send(&sock, &srv_addr, (const u8 *)query,
                   (int)strlen(query));

    u8 resp[1024];
    bc_addr_t from;
    bool got_response = false;

    for (int i = 0; i < 20; i++) {
        int got = bc_socket_recv(&sock, &from, resp, sizeof(resp));
        if (got > 0) {
            got_response = true;
            ASSERT(gs_has_value((char *)resp, got, "queryid", "99.1"));
            ASSERT(gs_has_key((char *)resp, got, "hostname"));
            break;
        }
        Sleep(100);
    }
    ASSERT(got_response);

    bc_socket_close(&sock);
    test_server_stop(&srv);
    bc_net_shutdown();
}

TEST(server_responds_to_secure_challenge)
{
    bc_test_server_t srv;
    ASSERT(bc_net_init());
    ASSERT(test_server_start(&srv, GS_PORT, MANIFEST_PATH));

    bc_socket_t sock;
    ASSERT(bc_socket_open(&sock, 0));

    bc_addr_t srv_addr;
    srv_addr.ip = htonl(0x7F000001);
    srv_addr.port = htons(GS_PORT);

    const char *challenge = "\\secure\\abc123";
    bc_socket_send(&sock, &srv_addr, (const u8 *)challenge,
                   (int)strlen(challenge));

    u8 resp[1024];
    bc_addr_t from;
    bool got_response = false;

    for (int i = 0; i < 20; i++) {
        int got = bc_socket_recv(&sock, &from, resp, sizeof(resp));
        if (got > 0) {
            got_response = true;

            ASSERT(gs_has_value((char *)resp, got, "gamename", "bcommander"));
            ASSERT(gs_has_value((char *)resp, got, "gamever", "60"));

            int vlen = 0;
            const char *val = gs_find_value((char *)resp, got, "validate", &vlen);
            ASSERT(val != NULL);
            ASSERT(vlen > 0);

            char expected[89];
            bc_gsmsalg(expected, "abc123", BC_GAMESPY_SECRET_KEY, 0);
            ASSERT(vlen == (int)strlen(expected));
            ASSERT(memcmp(val, expected, (size_t)vlen) == 0);
            break;
        }
        Sleep(100);
    }
    ASSERT(got_response);

    bc_socket_close(&sock);
    test_server_stop(&srv);
    bc_net_shutdown();
}

/* ======================================================================
 * Integration test: mock master server
 *
 * Simulates the full master server handshake:
 *   1. Server sends heartbeat to mock master
 *   2. Mock master sends \secure\<challenge>
 *   3. Server responds with \validate\<hash>
 *   4. Mock master verifies the hash
 *   5. Mock master sends \status\ query
 *   6. Server responds with full server info
 * ====================================================================== */

#define MOCK_MASTER_PORT  29741
#define MOCK_GAME_PORT    29742

/* Start a server that heartbeats to our mock master */
static bool start_server_with_master(bc_test_server_t *srv, u16 game_port,
                                      u16 master_port, const char *manifest)
{
    memset(srv, 0, sizeof(*srv));
    srv->port = game_port;

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "build\\openbc-server.exe --manifest %s -v --log-file server_test.log "
             "--master 127.0.0.1:%u -p %u",
             manifest, master_port, game_port);

    STARTUPINFO si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &srv->pi)) {
        fprintf(stderr, "  HARNESS: CreateProcess failed (err=%lu)\n",
                GetLastError());
        return false;
    }

    srv->running = true;
    return true;
}

TEST(mock_master_full_handshake)
{
    ASSERT(bc_net_init());

    /* Step 1: Open mock master socket */
    bc_socket_t master;
    ASSERT(bc_socket_open(&master, MOCK_MASTER_PORT));

    /* Step 2: Start server with our mock master */
    bc_test_server_t srv;
    ASSERT(start_server_with_master(&srv, MOCK_GAME_PORT, MOCK_MASTER_PORT,
                                     MANIFEST_PATH));

    /* Step 3: Wait for heartbeat from server */
    u8 recv_buf[2048];
    bc_addr_t from;
    bool got_heartbeat = false;

    for (int i = 0; i < 50; i++) {
        int got = bc_socket_recv(&master, &from, recv_buf, sizeof(recv_buf));
        if (got > 0) {
            recv_buf[got] = '\0';
            /* Verify heartbeat format: \heartbeat\<port>\gamename\bcommander\ */
            ASSERT(gs_has_key((char *)recv_buf, got, "heartbeat"));
            ASSERT(gs_has_value((char *)recv_buf, got, "gamename", "bcommander"));
            got_heartbeat = true;
            break;
        }
        Sleep(100);
    }
    ASSERT(got_heartbeat);

    /* Step 4: Send \secure\ challenge back to server */
    const char *challenge_str = "MOCKCH";
    char secure_pkt[64];
    int secure_len = snprintf(secure_pkt, sizeof(secure_pkt),
                               "\\secure\\%s", challenge_str);
    bc_socket_send(&master, &from, (const u8 *)secure_pkt, secure_len);

    /* Step 5: Wait for \validate\ response */
    bool got_validate = false;
    for (int i = 0; i < 30; i++) {
        int got = bc_socket_recv(&master, &from, recv_buf, sizeof(recv_buf));
        if (got > 0) {
            recv_buf[got] = '\0';
            if (gs_has_key((char *)recv_buf, got, "validate")) {
                got_validate = true;

                /* Verify gamename and gamever */
                ASSERT(gs_has_value((char *)recv_buf, got, "gamename", "bcommander"));
                ASSERT(gs_has_value((char *)recv_buf, got, "gamever", "60"));

                /* Verify the validate hash matches our own computation */
                char expected_hash[89];
                bc_gsmsalg(expected_hash, challenge_str, BC_GAMESPY_SECRET_KEY, 0);

                int vlen = 0;
                const char *val = gs_find_value((char *)recv_buf, got,
                                                 "validate", &vlen);
                ASSERT(val != NULL);
                ASSERT(vlen == (int)strlen(expected_hash));
                ASSERT(memcmp(val, expected_hash, (size_t)vlen) == 0);
                break;
            }
        }
        Sleep(100);
    }
    ASSERT(got_validate);

    /* Step 6: Send a \status\ query (like master verifying server info) */
    const char *status_query = "\\status\\";
    bc_socket_send(&master, &from, (const u8 *)status_query,
                   (int)strlen(status_query));

    /* Step 7: Wait for server info response */
    bool got_info = false;
    for (int i = 0; i < 30; i++) {
        int got = bc_socket_recv(&master, &from, recv_buf, sizeof(recv_buf));
        if (got > 0) {
            recv_buf[got] = '\0';
            if (gs_has_key((char *)recv_buf, got, "hostname")) {
                got_info = true;

                /* Verify all stock BC QR1 fields */
                /* Basic callback */
                ASSERT(gs_has_key((char *)recv_buf, got, "missionscript"));
                ASSERT(gs_has_key((char *)recv_buf, got, "mapname"));
                ASSERT(gs_has_key((char *)recv_buf, got, "numplayers"));
                ASSERT(gs_has_key((char *)recv_buf, got, "maxplayers"));
                ASSERT(gs_has_key((char *)recv_buf, got, "gamemode"));
                /* Info callback */
                ASSERT(gs_has_value((char *)recv_buf, got,
                                     "gamename", "bcommander"));
                ASSERT(gs_has_value((char *)recv_buf, got, "gamever", "60"));
                /* Rules callback */
                ASSERT(gs_has_key((char *)recv_buf, got, "timelimit"));
                ASSERT(gs_has_key((char *)recv_buf, got, "fraglimit"));
                ASSERT(gs_has_key((char *)recv_buf, got, "system"));
                ASSERT(gs_has_value((char *)recv_buf, got, "password", "0"));
                ASSERT(gs_has_key((char *)recv_buf, got, "queryid"));
                /* \final\ before \queryid\ */
                ASSERT(strstr((char *)recv_buf, "\\final\\") != NULL);
                ASSERT(strstr((char *)recv_buf, "\\final\\") <
                       strstr((char *)recv_buf, "\\queryid\\"));
                break;
            }
        }
        Sleep(100);
    }
    ASSERT(got_info);

    /* Cleanup */
    test_server_stop(&srv);
    bc_socket_close(&master);
    bc_net_shutdown();
}

/* ======================================================================
 * Test runner
 * ====================================================================== */

TEST_MAIN_BEGIN()

    /* Unit tests: response building */
    RUN(response_basic_fields);
    RUN(response_password_field);
    RUN(response_queryid_echoed);
    RUN(response_default_queryid);
    RUN(response_null_query);
    RUN(is_query_detection);
    RUN(response_status_query);
    RUN(response_with_limits);

    /* Unit tests: secure challenge */
    RUN(is_secure_detection);
    RUN(extract_secure_challenge);

    /* Unit tests: gsmsalg + validate */
    RUN(gsmsalg_produces_output);
    RUN(gsmsalg_deterministic);
    RUN(gsmsalg_different_challenges);
    RUN(gsmsalg_length_multiple_of_4);
    RUN(gsmsalg_empty_challenge);
    RUN(build_validate_response);
    RUN(build_validate_deterministic);

    /* Integration tests */
    RUN(server_responds_to_lan_query);
    RUN(server_responds_with_queryid);
    RUN(server_responds_to_secure_challenge);

    /* Mock master server test */
    RUN(mock_master_full_handshake);

TEST_MAIN_END()
