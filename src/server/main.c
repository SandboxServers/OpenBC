#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/peer.h"
#include "openbc/transport.h"
#include "openbc/gamespy.h"
#include "openbc/cipher.h"
#include "openbc/opcodes.h"
#include "openbc/handshake.h"

#include <windows.h>  /* For Sleep(), GetTickCount() */

/* --- Server state --- */

static volatile bool g_running = true;

static bc_socket_t    g_socket;
static bc_peer_mgr_t  g_peers;
static bc_server_info_t g_info;

/* Game settings */
static bool        g_collision_dmg = true;
static bool        g_friendly_fire = false;
static const char *g_map_name = "DeepSpace9";
static f32         g_game_time = 0.0f;

/* --- Signal handler --- */

static BOOL WINAPI console_handler(DWORD type)
{
    (void)type;
    g_running = false;
    return TRUE;
}

/* --- Packet handling --- */

static void handle_gamespy(const bc_addr_t *from, const u8 *data, int len)
{
    (void)data;
    (void)len;

    u8 response[1024];
    g_info.numplayers = g_peers.count;

    int resp_len = bc_gamespy_build_response(response, sizeof(response), &g_info);
    if (resp_len > 0) {
        bc_socket_send(&g_socket, from, response, resp_len);
    }
}

static void send_ack(const bc_addr_t *to, u16 seq)
{
    u8 pkt[16];
    int len = bc_transport_build_ack(pkt, sizeof(pkt), seq, 0x80);
    if (len > 0) {
        alby_rules_cipher(pkt, (size_t)len);
        bc_socket_send(&g_socket, to, pkt, len);
    }
}

static void send_reliable(const bc_addr_t *to, int peer_slot,
                          const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_transport_build_reliable(pkt, sizeof(pkt),
                                          payload, payload_len,
                                          peer->reliable_seq_out++);
    if (len > 0) {
        alby_rules_cipher(pkt, (size_t)len);
        bc_socket_send(&g_socket, to, pkt, len);
    }
}

static void send_checksum_request(const bc_addr_t *to, int peer_slot, int round)
{
    u8 payload[256];
    int payload_len = bc_checksum_request_build(payload, sizeof(payload), round);
    if (payload_len > 0) {
        printf("[handshake] slot=%d sending checksum request round %d\n",
               peer_slot, round);
        send_reliable(to, peer_slot, payload, payload_len);
    }
}

static void send_settings_and_gameinit(const bc_addr_t *to, int peer_slot)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    u8 payload[512];

    /* Settings (opcode 0x00) -- reliable */
    int len = bc_settings_build(payload, sizeof(payload),
                                g_game_time, g_collision_dmg, g_friendly_fire,
                                (u8)peer_slot, g_map_name);
    if (len > 0) {
        printf("[handshake] slot=%d sending Settings (slot=%d, map=%s)\n",
               peer_slot, peer_slot, g_map_name);
        send_reliable(to, peer_slot, payload, len);
    }

    /* GameInit (opcode 0x01) -- reliable */
    len = bc_gameinit_build(payload, sizeof(payload));
    if (len > 0) {
        printf("[handshake] slot=%d sending GameInit\n", peer_slot);
        send_reliable(to, peer_slot, payload, len);
    }

    peer->state = PEER_LOBBY;
    printf("[handshake] slot=%d reached LOBBY state\n", peer_slot);
}

static void handle_connect(const bc_addr_t *from, int len)
{
    char addr_str[32];
    bc_addr_to_string(from, addr_str, sizeof(addr_str));

    int slot = bc_peers_find(&g_peers, from);
    if (slot >= 0) {
        printf("[net] Duplicate connect from %s (slot %d)\n", addr_str, slot);
        return;
    }

    slot = bc_peers_add(&g_peers, from);
    if (slot < 0) {
        printf("[net] Server full, rejecting %s\n", addr_str);
        return;
    }

    g_peers.peers[slot].last_recv_time = GetTickCount();
    printf("[net] Player connected from %s -> slot %d (%d/%d)\n",
           addr_str, slot, g_peers.count, g_info.maxplayers);

    /* Send a connect acknowledgment.
     * Format: [direction=0x01][count=1][type=0x05][len=2] */
    u8 ack[4];
    ack[0] = BC_DIR_SERVER;
    ack[1] = 1;
    ack[2] = BC_TRANSPORT_CONNECT_ACK;
    ack[3] = 2;
    alby_rules_cipher(ack, sizeof(ack));
    bc_socket_send(&g_socket, from, ack, sizeof(ack));

    /* Begin checksum exchange */
    g_peers.peers[slot].state = PEER_CHECKSUMMING;
    g_peers.peers[slot].checksum_round = 0;
    send_checksum_request(from, slot, 0);

    (void)len;
}

static void handle_checksum_response(const bc_addr_t *from, int peer_slot,
                                     const bc_transport_msg_t *msg)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];

    if (peer->state != PEER_CHECKSUMMING) {
        printf("[handshake] slot=%d unexpected checksum response (state=%d)\n",
               peer_slot, peer->state);
        return;
    }

    /* Accept any checksum response (permissive mode).
     * A full implementation would parse the response hashes and compare
     * against the hash manifest. For now we advance unconditionally. */
    printf("[handshake] slot=%d checksum round %d accepted (len=%d)\n",
           peer_slot, peer->checksum_round, msg->payload_len);

    peer->checksum_round++;

    if (peer->checksum_round < BC_CHECKSUM_ROUNDS) {
        /* Send next checksum request */
        send_checksum_request(from, peer_slot, peer->checksum_round);
    } else {
        /* All 4 rounds passed -- send Settings + GameInit */
        printf("[handshake] slot=%d all checksums passed\n", peer_slot);
        send_settings_and_gameinit(from, peer_slot);
    }
}

static void handle_game_message(const bc_addr_t *from, int peer_slot,
                                const bc_transport_msg_t *msg)
{
    if (msg->payload_len < 1) return;

    u8 opcode = msg->payload[0];
    const char *name = bc_opcode_name(opcode);

    printf("[game] slot=%d opcode=0x%02X (%s) len=%d\n",
           peer_slot, opcode, name ? name : "?", msg->payload_len);

    /* ACK reliable messages */
    if (msg->type == BC_TRANSPORT_RELIABLE) {
        send_ack(from, msg->seq);
    }

    /* Dispatch checksum responses to the handshake handler */
    if (opcode == BC_OP_CHECKSUM_RESP) {
        handle_checksum_response(from, peer_slot, msg);
        return;
    }
}

static void handle_packet(const bc_addr_t *from, u8 *data, int len)
{
    /* Update peer timestamp if known */
    int slot = bc_peers_find(&g_peers, from);
    if (slot >= 0) {
        g_peers.peers[slot].last_recv_time = GetTickCount();
    }

    /* Decrypt */
    alby_rules_cipher(data, (size_t)len);

    /* Parse transport envelope */
    bc_packet_t pkt;
    if (!bc_transport_parse(data, len, &pkt)) {
        return;
    }

    /* Handle connection requests (direction 0xFF) */
    if (pkt.direction == BC_DIR_INIT) {
        handle_connect(from, len);
        return;
    }

    if (slot < 0) return;  /* Unknown peer, ignore */

    /* Process each transport message */
    for (int i = 0; i < pkt.msg_count; i++) {
        bc_transport_msg_t *msg = &pkt.msgs[i];

        if (msg->type == BC_TRANSPORT_ACK) {
            /* ACK received -- would update reliable delivery state */
            continue;
        }

        if (msg->type == BC_TRANSPORT_DISCONNECT) {
            char addr_str[32];
            bc_addr_to_string(from, addr_str, sizeof(addr_str));
            printf("[net] Player disconnected: %s (slot %d)\n", addr_str, slot);
            bc_peers_remove(&g_peers, slot);
            return;
        }

        /* Game message (reliable or unreliable) */
        if (msg->payload_len > 0) {
            handle_game_message(from, slot, msg);
        }
    }
}

/* --- Main --- */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -p <port>     Listen port (default: 22101)\n"
        "  -n <name>     Server name (default: \"OpenBC Server\")\n"
        "  -m <map>      Map name (default: \"DeepSpace9\")\n"
        "  --max <n>     Max players (default: 6)\n",
        prog);
}

int main(int argc, char **argv)
{
    /* Defaults */
    u16 port = BC_DEFAULT_PORT;
    const char *name = "OpenBC Server";
    const char *map = "DeepSpace9";
    int max_players = BC_MAX_PLAYERS;

    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = (u16)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            map = argv[++i];
        } else if (strcmp(argv[i], "--max") == 0 && i + 1 < argc) {
            max_players = atoi(argv[++i]);
            if (max_players < 1) max_players = 1;
            if (max_players > BC_MAX_PLAYERS) max_players = BC_MAX_PLAYERS;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    /* Initialize */
    if (!bc_net_init()) {
        fprintf(stderr, "Failed to initialize networking\n");
        return 1;
    }

    if (!bc_socket_open(&g_socket, port)) {
        fprintf(stderr, "Failed to bind port %u\n", port);
        bc_net_shutdown();
        return 1;
    }

    bc_peers_init(&g_peers);

    /* Server info for GameSpy */
    snprintf(g_info.hostname, sizeof(g_info.hostname), "%s", name);
    snprintf(g_info.mapname, sizeof(g_info.mapname), "%s", map);
    snprintf(g_info.gametype, sizeof(g_info.gametype), "Deathmatch");
    g_info.hostport = port;
    g_info.numplayers = 0;
    g_info.maxplayers = max_players;

    /* Register CTRL+C handler */
    SetConsoleCtrlHandler(console_handler, TRUE);

    printf("OpenBC Server v0.1.0\n");
    printf("Listening on port %u (%d max players)\n", port, max_players);
    printf("Server name: %s\n", name);
    printf("Press Ctrl+C to stop.\n\n");

    /* Main loop */
    u8 recv_buf[2048];
    u32 last_tick = GetTickCount();

    while (g_running) {
        bc_addr_t from;
        int received = bc_socket_recv(&g_socket, &from, recv_buf, sizeof(recv_buf));

        if (received > 0) {
            /* GameSpy queries are plaintext (not encrypted) */
            if (bc_gamespy_is_query(recv_buf, received)) {
                handle_gamespy(&from, recv_buf, received);
            } else {
                handle_packet(&from, recv_buf, received);
            }
        }

        /* Periodic tasks (every ~1 second) */
        u32 now = GetTickCount();
        if (now - last_tick >= 1000) {
            /* Advance game clock */
            g_game_time += (f32)(now - last_tick) / 1000.0f;

            /* Timeout stale peers (30 second timeout) */
            int removed = bc_peers_timeout(&g_peers, now, 30000);
            if (removed > 0) {
                printf("[net] Timed out %d peer(s)\n", removed);
            }
            last_tick = now;
        }

        /* Don't burn CPU -- sleep 1ms between polls */
        Sleep(1);
    }

    printf("\nShutting down...\n");
    bc_socket_close(&g_socket);
    bc_net_shutdown();
    return 0;
}
