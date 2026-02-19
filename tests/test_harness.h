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
 *   - Real checksum validation: manifest + directory scanning
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
#include "openbc/checksum.h"
#include "openbc/buffer.h"

/* --- Server process --- */

typedef struct {
    PROCESS_INFORMATION pi;
    u16 port;
    bool running;
} bc_test_server_t;

/* Forward declarations */
static void test_server_stop(bc_test_server_t *srv);

/* Start server with manifest for real checksum validation. */
static bool test_server_start(bc_test_server_t *srv, u16 port,
                               const char *manifest_path)
{
    memset(srv, 0, sizeof(*srv));
    srv->port = port;

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "build\\openbc-server.exe --manifest %s -vv --log-file server_test_%u.log"
             " --no-master -p %u",
             manifest_path, port, port);

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

    /* Probe until server responds */
    bc_socket_t probe;
    if (!bc_socket_open(&probe, 0)) return false;

    bc_addr_t srv_addr;
    srv_addr.ip = htonl(0x7F000001);
    srv_addr.port = htons(port);

    const u8 query[] = "\\status\\";
    u8 resp[512];
    bc_addr_t from;

    bool ready = false;
    for (int attempt = 0; attempt < 30; attempt++) {
        bc_socket_send(&probe, &srv_addr, query, sizeof(query) - 1);
        Sleep(100);
        int got = bc_socket_recv(&probe, &from, resp, sizeof(resp));
        if (got > 0) { ready = true; break; }
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
    alby_cipher_encrypt(pkt, (size_t)len);
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
            alby_cipher_decrypt(c->recv_buf, (size_t)got);
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

/* Build a real checksum response from scanned directory data.
 * Handles rounds 0-3 (non-recursive and recursive). */
static int tc_build_real_checksum(u8 *buf, int buf_size, u8 round,
                                    const bc_client_dir_scan_t *scan)
{
    /* ref_hash = dir_hash for consistency with real BC client */
    if (round == 2 && scan->subdir_count > 0) {
        return bc_client_build_checksum_resp_recursive(
            buf, buf_size, round,
            scan->dir_hash, scan->dir_hash,
            scan->files, scan->file_count,
            scan->subdirs, scan->subdir_count);
    }
    return bc_client_build_checksum_resp(
        buf, buf_size, round,
        scan->dir_hash, scan->dir_hash,
        scan->files, scan->file_count);
}

/* Internal: find a reliable message with game payload in a parsed packet.
 * Auto-ACKs all reliable messages encountered.
 * Type 0x32 with flags & 0x80 = reliable; flags == 0x00 = unreliable (skip). */
static bc_transport_msg_t *tc_find_reliable_payload(bc_test_client_t *c,
                                                      bc_packet_t *parsed)
{
    bc_transport_msg_t *found = NULL;
    for (int i = 0; i < parsed->msg_count; i++) {
        if (parsed->msgs[i].type == BC_TRANSPORT_RELIABLE &&
            (parsed->msgs[i].flags & 0x80)) {
            tc_send_ack(c, parsed->msgs[i].seq);
            if (!found && parsed->msgs[i].payload_len > 0)
                found = &parsed->msgs[i];
        }
    }
    return found;
}

/* Full handshake with wire-accurate checksums computed from a game directory.
 * Connect -> ConnectAck (batched with ChecksumReq round 0) -> keepalive name
 * -> 4 more checksum rounds -> receive Settings/GameInit -> send 0x2A ->
 * receive MissionInit. */
static bool test_client_connect(bc_test_client_t *c, u16 port,
                                 const char *name, u8 slot,
                                 const char *game_dir)
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

    /* 2. Wait for ConnectAck + ChecksumReq round 0 (batched in one packet) */
    if (tc_recv_raw(c, 2000) <= 0) {
        fprintf(stderr, "  CLIENT %s: no ConnectAck\n", name);
        return false;
    }

    /* Parse the batched Connect+ChecksumReq packet */
    bc_packet_t first_pkt;
    if (!bc_transport_parse(c->recv_buf, c->recv_len, &first_pkt)) {
        fprintf(stderr, "  CLIENT %s: failed to parse ConnectAck packet\n", name);
        return false;
    }

    /* ACK any reliables and find the ChecksumReq (round 0) */
    bc_transport_msg_t *round0_msg = tc_find_reliable_payload(c, &first_pkt);
    if (!round0_msg) {
        fprintf(stderr, "  CLIENT %s: no checksum request in ConnectAck packet\n", name);
        return false;
    }

    /* 3. Send keepalive with name */
    len = bc_client_build_keepalive_name(pkt, sizeof(pkt), slot,
                                          htonl(0x7F000001), name);
    if (len > 0) tc_send_raw(c, pkt, len);

    /* 4. Handle checksum rounds with real hashes.
     * Round 0 is already parsed from the batched packet above.
     * Rounds 1-3 and the final round arrive in separate packets. */
    for (int round = 0; round < 5; round++) {
        bc_transport_msg_t *msg;

        if (round == 0) {
            /* Round 0 was in the batched ConnectAck packet */
            msg = round0_msg;
        } else {
            /* Receive a new packet for rounds 1-4.
             * May need multiple recv calls since server can send ACK-only
             * packets (keepalives, retransmit ACKs) between rounds. */
            msg = NULL;
            int dbg_pkts = 0, dbg_parse_fail = 0, dbg_no_reliable = 0;
            u32 round_start = GetTickCount();
            while ((int)(GetTickCount() - round_start) < 2000) {
                int got = tc_recv_raw(c, 200);
                if (got <= 0) continue;
                dbg_pkts++;

                bc_packet_t parsed;
                if (!bc_transport_parse(c->recv_buf, c->recv_len, &parsed)) {
                    dbg_parse_fail++;
                    fprintf(stderr, "  CLIENT %s: round %d recv'd %d bytes, parse FAILED, hex: ",
                            name, round, got);
                    for (int z = 0; z < (got < 32 ? got : 32); z++)
                        fprintf(stderr, "%02X ", c->recv_buf[z]);
                    fprintf(stderr, "\n");
                    continue;
                }

                fprintf(stderr, "  CLIENT %s: round %d recv'd pkt: dir=0x%02X msgs=%d\n",
                        name, round, parsed.direction, parsed.msg_count);
                for (int z = 0; z < parsed.msg_count; z++) {
                    bc_transport_msg_t *m = &parsed.msgs[z];
                    fprintf(stderr, "    msg[%d] type=0x%02X flags=0x%02X seq=0x%04X plen=%d",
                            z, m->type, m->flags, m->seq, m->payload_len);
                    if (m->payload_len > 0)
                        fprintf(stderr, " op=0x%02X", m->payload[0]);
                    fprintf(stderr, "\n");
                }

                msg = tc_find_reliable_payload(c, &parsed);
                if (msg) break;
                dbg_no_reliable++;
            }
            if (!msg) {
                fprintf(stderr, "  CLIENT %s: no checksum request round %d "
                        "(pkts=%d parse_fail=%d no_reliable=%d)\n",
                        name, round, dbg_pkts, dbg_parse_fail, dbg_no_reliable);
                return false;
            }
        }

        u8 resp[4096];
        int resp_len;

        if (round < 4) {
            bc_checksum_request_t req;
            if (!bc_client_parse_checksum_request(msg->payload, msg->payload_len, &req)) {
                fprintf(stderr, "  CLIENT %s: failed to parse checksum request round %d\n",
                        name, round);
                return false;
            }

            bc_client_dir_scan_t scan;
            if (!bc_client_scan_directory(game_dir, req.directory,
                                           req.filter, req.recursive, &scan)) {
                fprintf(stderr, "  CLIENT %s: failed to scan %s%s\n",
                        name, game_dir, req.directory);
                return false;
            }

            resp_len = tc_build_real_checksum(resp, sizeof(resp),
                                               (u8)round, &scan);
        } else {
            /* Final round: send empty response */
            resp_len = bc_client_build_checksum_final(resp, sizeof(resp), 0);
        }
        if (resp_len <= 0) return false;

        u8 out[4096];
        int out_len = bc_client_build_reliable(out, sizeof(out), slot,
                                                resp, resp_len, c->seq_out++);
        if (out_len > 0) tc_send_raw(c, out, out_len);
    }

    /* 5. Drain 0x28 + Settings + GameInit, then send NewPlayerInGame,
     *    then receive MissionInit. */
    u32 drain_start = GetTickCount();
    bool got_settings = false;
    bool got_gameinit = false;

    while ((int)(GetTickCount() - drain_start) < 3000) {
        if (tc_recv_raw(c, 200) <= 0) {
            if (got_settings && got_gameinit) break;
            continue;
        }

        bc_packet_t parsed;
        if (!bc_transport_parse(c->recv_buf, c->recv_len, &parsed))
            continue;

        for (int i = 0; i < parsed.msg_count; i++) {
            bc_transport_msg_t *msg = &parsed.msgs[i];
            if (msg->type == BC_TRANSPORT_RELIABLE && (msg->flags & 0x80)) {
                tc_send_ack(c, msg->seq);
                if (msg->payload_len > 0) {
                    u8 op = msg->payload[0];
                    if (op == BC_OP_SETTINGS) got_settings = true;
                    if (op == BC_OP_GAME_INIT) got_gameinit = true;
                }
            }
        }
        if (got_settings && got_gameinit) break;
    }

    if (!got_settings || !got_gameinit) {
        fprintf(stderr, "  CLIENT %s: handshake incomplete (settings=%d gameinit=%d)\n",
                name, got_settings, got_gameinit);
        return false;
    }

    /* 6. Send NewPlayerInGame (client -> server) */
    u8 npig[2] = { BC_OP_NEW_PLAYER_IN_GAME, 0x20 };
    u8 npig_pkt[64];
    int npig_len = bc_client_build_reliable(npig_pkt, sizeof(npig_pkt), slot,
                                              npig, 2, c->seq_out++);
    if (npig_len > 0) tc_send_raw(c, npig_pkt, npig_len);

    /* 7. Wait for MissionInit (0x35) response */
    bool got_mission = false;
    u32 mi_start = GetTickCount();
    while ((int)(GetTickCount() - mi_start) < 2000) {
        if (tc_recv_raw(c, 200) <= 0) continue;

        bc_packet_t parsed;
        if (!bc_transport_parse(c->recv_buf, c->recv_len, &parsed))
            continue;

        for (int i = 0; i < parsed.msg_count; i++) {
            bc_transport_msg_t *msg = &parsed.msgs[i];
            if (msg->type == BC_TRANSPORT_RELIABLE && (msg->flags & 0x80)) {
                tc_send_ack(c, msg->seq);
                if (msg->payload_len > 0 && msg->payload[0] == BC_MSG_MISSION_INIT)
                    got_mission = true;
            }
        }
        if (got_mission) break;
    }

    if (!got_mission) {
        fprintf(stderr, "  CLIENT %s: no MissionInit after NewPlayerInGame\n", name);
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

        /* Auto-ACK reliable messages (type 0x32 with flags & 0x80) */
        if (msg->type == BC_TRANSPORT_RELIABLE && (msg->flags & 0x80)) {
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
            if (parsed.msgs[i].type == BC_TRANSPORT_RELIABLE &&
                (parsed.msgs[i].flags & 0x80))
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
__attribute__((unused))
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
    if (c->connected) {
        /* Send a disconnect transport message */
        u8 pkt[8];
        pkt[0] = BC_DIR_CLIENT + c->slot;
        pkt[1] = 1;
        pkt[2] = BC_TRANSPORT_DISCONNECT;
        pkt[3] = 2; /* totalLen */
        alby_cipher_encrypt(pkt, 4);
        bc_socket_send(&c->sock, &c->server_addr, pkt, 4);
        c->connected = false;
    }
    /* Always close socket (even if connect failed partway) */
    bc_socket_close(&c->sock);
}

/* ======================================================================
 * Packet Trace Logger
 *
 * Binary trace format per record:
 *   [4 bytes: tick as u32 LE]
 *   [1 byte: 'S'=sent / 'R'=received]
 *   [1 byte: client slot]
 *   [2 bytes: payload length as u16 LE]
 *   [N bytes: raw game payload (starts with opcode byte)]
 *
 * File header: 8-byte magic "OBCTRACE"
 * ====================================================================== */

typedef struct {
    FILE *fp;
    bool enabled;
} bc_packet_log_t;

/* Global log pointer -- set by test to enable logging in send/recv helpers */
__attribute__((unused))
static bc_packet_log_t *g_packet_log = NULL;

__attribute__((unused))
static bool packet_log_open(bc_packet_log_t *log, const char *path)
{
    memset(log, 0, sizeof(*log));
    log->fp = fopen(path, "wb");
    if (!log->fp) return false;
    fwrite("OBCTRACE", 1, 8, log->fp);
    log->enabled = true;
    return true;
}

__attribute__((unused))
static void packet_log_write(bc_packet_log_t *log, u32 tick,
                              char dir, u8 slot,
                              const u8 *payload, u16 len)
{
    if (!log || !log->enabled || !log->fp) return;
    fwrite(&tick, 4, 1, log->fp);
    fwrite(&dir, 1, 1, log->fp);
    fwrite(&slot, 1, 1, log->fp);
    fwrite(&len, 2, 1, log->fp);
    fwrite(payload, 1, len, log->fp);
}

__attribute__((unused))
static void packet_log_close(bc_packet_log_t *log)
{
    if (!log) return;
    if (log->fp) { fclose(log->fp); log->fp = NULL; }
    log->enabled = false;
}

/* Dump a binary trace file to stdout in human-readable form */
__attribute__((unused))
static void packet_log_dump(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "Cannot open %s\n", path); return; }

    /* Verify header */
    char hdr[8];
    if (fread(hdr, 1, 8, fp) != 8 || memcmp(hdr, "OBCTRACE", 8) != 0) {
        fprintf(stderr, "Bad trace header\n");
        fclose(fp);
        return;
    }

    int records = 0;
    while (!feof(fp)) {
        u32 tick; char dir; u8 slot; u16 len;
        if (fread(&tick, 4, 1, fp) != 1) break;
        if (fread(&dir, 1, 1, fp) != 1) break;
        if (fread(&slot, 1, 1, fp) != 1) break;
        if (fread(&len, 2, 1, fp) != 1) break;

        u8 payload[4096];
        if (len > sizeof(payload)) break;
        if (fread(payload, 1, len, fp) != len) break;

        const char *opname = (len > 0) ? bc_opcode_name(payload[0]) : "?";
        if (!opname) opname = "?";

        printf("[tick %4u] %c slot=%d op=0x%02X (%-20s) len=%d  ",
               tick, dir, slot, len > 0 ? payload[0] : 0, opname, len);
        int show = len < 24 ? len : 24;
        for (int i = 0; i < show; i++) printf("%02X ", payload[i]);
        if (len > 24) printf("...");
        printf("\n");
        records++;
    }
    printf("Total: %d records\n", records);
    fclose(fp);
}

#endif /* TEST_HARNESS_H */
