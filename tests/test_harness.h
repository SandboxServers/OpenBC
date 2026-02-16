#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

/*
 * Multi-client integration test harness.
 *
 * Spawns an OpenBC server as a subprocess, connects N headless clients,
 * and provides send/recv/assert helpers for protocol-level testing.
 *
 * Design:
 *   - Server as subprocess: CreateProcess("build\\openbc-server.exe", ...)
 *   - Real UDP, real AlbyRules cipher, real transport framing
 *   - Probe-based startup: GameSpy query loop until server responds
 *   - Auto-ACK: recv_msg automatically ACKs reliable messages
 *   - Keepalive/ACK filtering: expect_opcode skips non-game messages
 *   - Timeout-based assertions: every expect has a timeout
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/transport.h"
#include "openbc/cipher.h"
#include "openbc/opcodes.h"
#include "openbc/client_transport.h"

/* --- Server process --- */

typedef struct {
    PROCESS_INFORMATION pi;
    u16 port;
    bool running;
} bc_test_server_t;

/* Forward declarations */
static void test_server_stop(bc_test_server_t *srv);

static bool test_server_start(bc_test_server_t *srv, u16 port)
{
    memset(srv, 0, sizeof(*srv));
    srv->port = port;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "build\\openbc-server.exe --no-checksum -v --log-file server_test.log -p %u", port);

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

    /* Probe: send GameSpy query until server responds (max 3s) */
    bc_socket_t probe;
    if (!bc_socket_open(&probe, 0)) {
        fprintf(stderr, "  HARNESS: probe socket failed\n");
        return false;
    }

    bc_addr_t srv_addr;
    srv_addr.ip = htonl(0x7F000001); /* 127.0.0.1 */
    srv_addr.port = htons(port);

    /* GameSpy status query: "\\status\\" */
    const u8 query[] = "\\status\\";
    u8 resp[512];
    bc_addr_t from;

    bool ready = false;
    for (int attempt = 0; attempt < 30; attempt++) {
        bc_socket_send(&probe, &srv_addr, query, sizeof(query) - 1);
        Sleep(100);

        int got = bc_socket_recv(&probe, &from, resp, sizeof(resp));
        if (got > 0) {
            ready = true;
            break;
        }
    }

    bc_socket_close(&probe);

    if (!ready) {
        fprintf(stderr, "  HARNESS: server didn't respond to probe in 3s\n");
        test_server_stop(srv);
        return false;
    }

    return true;
}

static void test_server_stop(bc_test_server_t *srv)
{
    if (!srv->running) return;

    TerminateProcess(srv->pi.hProcess, 0);
    WaitForSingleObject(srv->pi.hProcess, 2000);
    CloseHandle(srv->pi.hProcess);
    CloseHandle(srv->pi.hThread);
    srv->running = false;
}

/* --- Test client --- */

typedef struct {
    bc_socket_t  sock;
    bc_addr_t    server_addr;
    u8           slot;
    u16          seq_out;    /* Next outgoing reliable seq counter */
    u16          seq_in;     /* Track incoming for duplicate detection */
    char         name[32];
    u8           recv_buf[2048];
    int          recv_len;
    bool         connected;
    /* Cached parsed packet for multi-message iteration */
    bc_packet_t  cached_pkt;
    int          cached_idx;   /* Next message index to check */
    bool         has_cached;
} bc_test_client_t;

/* Internal: send a raw packet (encrypt + send) */
static void tc_send_raw(bc_test_client_t *c, u8 *pkt, int len)
{
    alby_rules_cipher(pkt, (size_t)len);
    bc_socket_send(&c->sock, &c->server_addr, pkt, len);
}

/* Internal: receive and decrypt a packet. Returns length, 0 if none. */
static int tc_recv_raw(bc_test_client_t *c, int timeout_ms)
{
    u32 start = GetTickCount();
    bc_addr_t from;

    while ((int)(GetTickCount() - start) < timeout_ms) {
        int got = bc_socket_recv(&c->sock, &from, c->recv_buf, sizeof(c->recv_buf));
        if (got > 0) {
            alby_rules_cipher(c->recv_buf, (size_t)got);
            c->recv_len = got;
            return got;
        }
        Sleep(1);
    }
    return 0;
}

/* Internal: send ACK for a reliable message */
static void tc_send_ack(bc_test_client_t *c, u16 seq)
{
    u8 pkt[16];
    int len = bc_client_build_ack(pkt, sizeof(pkt), c->slot, seq, 0x80);
    if (len > 0) tc_send_raw(c, pkt, len);
}

/* Full handshake: Connect -> wait for ConnectAck -> send keepalive name ->
 * respond to 4+1 checksum rounds -> receive Settings/GameInit/NewPlayerInGame.
 * Returns true if handshake completed. */
static bool test_client_connect(bc_test_client_t *c, u16 port,
                                 const char *name, u8 slot)
{
    memset(c, 0, sizeof(*c));
    c->slot = slot;
    c->seq_out = 0;
    strncpy(c->name, name, sizeof(c->name) - 1);

    c->server_addr.ip = htonl(0x7F000001);
    c->server_addr.port = htons(port);

    if (!bc_socket_open(&c->sock, 0)) {
        fprintf(stderr, "  CLIENT %s: socket open failed\n", name);
        return false;
    }

    /* 1. Send Connect packet */
    u8 pkt[512];
    int len = bc_client_build_connect(pkt, sizeof(pkt), htonl(0x7F000001));
    if (len <= 0) return false;
    tc_send_raw(c, pkt, len);

    /* 2. Wait for ConnectAck (type=0x05) */
    if (tc_recv_raw(c, 2000) <= 0) {
        fprintf(stderr, "  CLIENT %s: no ConnectAck\n", name);
        return false;
    }

    /* 3. Send keepalive with name */
    len = bc_client_build_keepalive_name(pkt, sizeof(pkt), slot,
                                          htonl(0x7F000001), name);
    if (len > 0) tc_send_raw(c, pkt, len);

    /* 4. Handle checksum rounds (4 regular + 1 final) */
    for (int round = 0; round < 5; round++) {
        /* Receive checksum request (reliable) */
        if (tc_recv_raw(c, 2000) <= 0) {
            fprintf(stderr, "  CLIENT %s: no checksum request round %d\n",
                    name, round);
            return false;
        }

        /* Parse to get the reliable seq for ACK */
        bc_packet_t parsed;
        if (!bc_transport_parse(c->recv_buf, c->recv_len, &parsed))
            return false;

        /* ACK all reliable messages in this packet */
        for (int i = 0; i < parsed.msg_count; i++) {
            if (parsed.msgs[i].type == BC_TRANSPORT_RELIABLE) {
                tc_send_ack(c, parsed.msgs[i].seq);
            }
        }

        /* Build and send checksum response */
        u8 resp[32];
        int resp_len;
        if (round < 4) {
            resp_len = bc_client_build_dummy_checksum_resp(resp, sizeof(resp),
                                                            (u8)round);
        } else {
            resp_len = bc_client_build_dummy_checksum_final(resp, sizeof(resp));
        }
        if (resp_len <= 0) return false;

        u8 out[512];
        int out_len = bc_client_build_reliable(out, sizeof(out), slot,
                                                resp, resp_len, c->seq_out++);
        if (out_len > 0) tc_send_raw(c, out, out_len);
    }

    /* 5. Receive Settings + UICollision + GameInit + NewPlayerInGame + MissionInit.
     * These arrive as reliable messages, possibly batched.
     * Drain for up to 2 seconds, ACKing everything. */
    u32 drain_start = GetTickCount();
    bool got_settings = false;
    bool got_npig = false;

    while ((int)(GetTickCount() - drain_start) < 2000) {
        if (tc_recv_raw(c, 200) <= 0) {
            if (got_settings && got_npig) break;
            continue;
        }

        bc_packet_t parsed;
        if (!bc_transport_parse(c->recv_buf, c->recv_len, &parsed))
            continue;

        for (int i = 0; i < parsed.msg_count; i++) {
            bc_transport_msg_t *msg = &parsed.msgs[i];
            if (msg->type == BC_TRANSPORT_RELIABLE) {
                tc_send_ack(c, msg->seq);

                if (msg->payload_len > 0) {
                    u8 op = msg->payload[0];
                    if (op == BC_OP_SETTINGS) got_settings = true;
                    if (op == BC_OP_NEW_PLAYER_IN_GAME) got_npig = true;
                }
            }
        }

        if (got_settings && got_npig) break;
    }

    if (!got_settings || !got_npig) {
        fprintf(stderr, "  CLIENT %s: handshake incomplete (settings=%d npig=%d)\n",
                name, got_settings, got_npig);
        return false;
    }

    c->connected = true;
    return true;
}

/* Send a reliable game payload (wrap in client transport, encrypt, send). */
static bool test_client_send_reliable(bc_test_client_t *c,
                                       const u8 *payload, int len)
{
    u8 pkt[512];
    int pkt_len = bc_client_build_reliable(pkt, sizeof(pkt), c->slot,
                                            payload, len, c->seq_out++);
    if (pkt_len <= 0) return false;
    tc_send_raw(c, pkt, pkt_len);
    return true;
}

/* Send an unreliable game payload. */
static bool test_client_send_unreliable(bc_test_client_t *c,
                                         const u8 *payload, int len)
{
    u8 pkt[512];
    int pkt_len = bc_client_build_unreliable(pkt, sizeof(pkt), c->slot,
                                              payload, len);
    if (pkt_len <= 0) return false;
    tc_send_raw(c, pkt, pkt_len);
    return true;
}

/* Internal: scan cached_pkt from cached_idx for the next game message.
 * Auto-ACKs reliables as it scans. Returns game payload or NULL. */
static const u8 *tc_scan_cached(bc_test_client_t *c, int *out_len)
{
    while (c->cached_idx < c->cached_pkt.msg_count) {
        bc_transport_msg_t *msg = &c->cached_pkt.msgs[c->cached_idx++];

        /* Auto-ACK reliables */
        if (msg->type == BC_TRANSPORT_RELIABLE) {
            tc_send_ack(c, msg->seq);
        }

        /* Skip ACKs and keepalives without payload */
        if (msg->type == BC_TRANSPORT_ACK) continue;
        if (msg->payload_len == 0) continue;

        /* Skip keepalives (type 0x00 with small payload, no game opcode) */
        if (msg->type == BC_TRANSPORT_KEEPALIVE && msg->payload_len <= 2)
            continue;

        /* Return this game message */
        *out_len = msg->payload_len;
        return msg->payload;
    }
    c->has_cached = false;
    return NULL;
}

/* Receive the next game message (auto-ACKs reliables, skips keepalives/ACKs).
 * Returns pointer to the game payload (within recv_buf), and sets *out_len.
 * Returns NULL on timeout. */
static const u8 *test_client_recv_msg(bc_test_client_t *c, int *out_len,
                                       int timeout_ms)
{
    /* First check if there are remaining messages in the cached packet */
    if (c->has_cached) {
        const u8 *result = tc_scan_cached(c, out_len);
        if (result) return result;
    }

    u32 start = GetTickCount();

    while ((int)(GetTickCount() - start) < timeout_ms) {
        int remaining = timeout_ms - (int)(GetTickCount() - start);
        if (remaining <= 0) break;

        if (tc_recv_raw(c, remaining) <= 0)
            return NULL;

        if (!bc_transport_parse(c->recv_buf, c->recv_len, &c->cached_pkt))
            continue;

        c->cached_idx = 0;
        c->has_cached = true;

        const u8 *result = tc_scan_cached(c, out_len);
        if (result) return result;
    }

    return NULL;
}

/* Drain all pending messages (auto-ACKs reliables). Clears cached state. */
static void test_client_drain(bc_test_client_t *c, int timeout_ms)
{
    c->has_cached = false;

    u32 start = GetTickCount();
    while ((int)(GetTickCount() - start) < timeout_ms) {
        if (tc_recv_raw(c, 50) <= 0) continue;

        bc_packet_t parsed;
        if (!bc_transport_parse(c->recv_buf, c->recv_len, &parsed))
            continue;

        for (int i = 0; i < parsed.msg_count; i++) {
            if (parsed.msgs[i].type == BC_TRANSPORT_RELIABLE)
                tc_send_ack(c, parsed.msgs[i].seq);
        }
    }
}

/* Wait for a specific opcode. Skips other messages.
 * Returns pointer to full payload (starts with opcode byte), sets *out_len.
 * Returns NULL on timeout. */
static const u8 *test_client_expect_opcode(bc_test_client_t *c, u8 opcode,
                                             int *out_len, int timeout_ms)
{
    u32 start = GetTickCount();

    while ((int)(GetTickCount() - start) < timeout_ms) {
        int remaining = timeout_ms - (int)(GetTickCount() - start);
        if (remaining <= 0) break;

        int msg_len = 0;
        const u8 *msg = test_client_recv_msg(c, &msg_len, remaining);
        if (!msg) break;

        if (msg_len > 0 && msg[0] == opcode) {
            *out_len = msg_len;
            return msg;
        }
        /* Not the opcode we want -- continue waiting */
    }

    fprintf(stderr, "  EXPECT: timed out waiting for opcode 0x%02X (%s)\n",
            opcode, bc_opcode_name(opcode) ? bc_opcode_name(opcode) : "?");
    return NULL;
}

/* Assert: specific opcode arrives with exact payload bytes.
 * Returns true if matched. */
static bool test_client_expect_bytes(bc_test_client_t *c, u8 opcode,
                                      const u8 *expected, int expected_len,
                                      int timeout_ms)
{
    int msg_len = 0;
    const u8 *msg = test_client_expect_opcode(c, opcode, &msg_len, timeout_ms);
    if (!msg) return false;

    if (msg_len != expected_len) {
        fprintf(stderr, "  EXPECT: opcode 0x%02X length mismatch: got %d, expected %d\n",
                opcode, msg_len, expected_len);
        return false;
    }

    if (memcmp(msg, expected, (size_t)expected_len) != 0) {
        fprintf(stderr, "  EXPECT: opcode 0x%02X payload mismatch\n", opcode);
        for (int i = 0; i < expected_len; i++) {
            if (msg[i] != expected[i]) {
                fprintf(stderr, "%d (got 0x%02X, expected 0x%02X)\n",
                        i, msg[i], expected[i]);
                break;
            }
        }
        return false;
    }

    return true;
}

static void test_client_disconnect(bc_test_client_t *c)
{
    if (!c->connected) return;
    /* Send a disconnect transport message */
    u8 pkt[8];
    pkt[0] = BC_DIR_CLIENT + c->slot;
    pkt[1] = 1;
    pkt[2] = BC_TRANSPORT_DISCONNECT;
    pkt[3] = 2; /* totalLen */
    alby_rules_cipher(pkt, 4);
    bc_socket_send(&c->sock, &c->server_addr, pkt, 4);
    bc_socket_close(&c->sock);
    c->connected = false;
}

#endif /* TEST_HARNESS_H */
