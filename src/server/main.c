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
#include "openbc/manifest.h"
#include "openbc/reliable.h"
#include "openbc/master.h"

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

/* Manifest / checksum validation */
static bc_manifest_t g_manifest;
static bool          g_manifest_loaded = false;
static bool          g_no_checksum = false;

/* Master server */
static bc_master_t   g_master;

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

/* Queue an ACK into a peer's outbox (piggybacked with next flush). */
static void queue_ack(int peer_slot, u16 seq)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    if (!bc_outbox_add_ack(&peer->outbox, seq, 0x80)) {
        bc_outbox_flush(&peer->outbox, &g_socket, &peer->addr);
        bc_outbox_add_ack(&peer->outbox, seq, 0x80);
    }
}

/* Queue a reliable message into a peer's outbox + track for retransmit. */
static void queue_reliable(int peer_slot, const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    u16 seq = peer->reliable_seq_out++;

    /* Track for retransmission */
    bc_reliable_add(&peer->reliable_out, payload, payload_len,
                    seq, GetTickCount());

    if (!bc_outbox_add_reliable(&peer->outbox, payload, payload_len, seq)) {
        bc_outbox_flush(&peer->outbox, &g_socket, &peer->addr);
        bc_outbox_add_reliable(&peer->outbox, payload, payload_len, seq);
    }
}

/* Queue an unreliable message into a peer's outbox. */
static void queue_unreliable(int peer_slot, const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    if (!bc_outbox_add_unreliable(&peer->outbox, payload, payload_len)) {
        bc_outbox_flush(&peer->outbox, &g_socket, &peer->addr);
        bc_outbox_add_unreliable(&peer->outbox, payload, payload_len);
    }
}

/* Send a single unreliable message directly (used for one-off sends
 * to addresses that don't have a peer slot yet, e.g. BootPlayer). */
static void send_unreliable_direct(const bc_addr_t *to,
                                   const u8 *payload, int payload_len)
{
    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_transport_build_unreliable(pkt, sizeof(pkt),
                                            payload, payload_len);
    if (len > 0) {
        alby_rules_cipher(pkt, (size_t)len);
        bc_socket_send(&g_socket, to, pkt, len);
    }
}

/* Relay a message to all connected peers except the sender.
 * Uses reliable delivery for guaranteed opcodes, unreliable otherwise. */
static void relay_to_others(int sender_slot, const u8 *payload, int payload_len,
                            bool reliable)
{
    for (int i = 0; i < BC_MAX_PLAYERS; i++) {
        if (i == sender_slot) continue;
        if (g_peers.peers[i].state < PEER_LOBBY) continue;

        if (reliable) {
            queue_reliable(i, payload, payload_len);
        } else {
            queue_unreliable(i, payload, payload_len);
        }
    }
}

/* Send a reliable message to all connected peers (including the target). */
static void send_to_all(const u8 *payload, int payload_len)
{
    for (int i = 0; i < BC_MAX_PLAYERS; i++) {
        if (g_peers.peers[i].state < PEER_LOBBY) continue;
        queue_reliable(i, payload, payload_len);
    }
}

static void send_checksum_request(int peer_slot, int round)
{
    u8 payload[256];
    int payload_len = bc_checksum_request_build(payload, sizeof(payload), round);
    if (payload_len > 0) {
        printf("[handshake] slot=%d sending checksum request round %d\n",
               peer_slot, round);
        queue_reliable(peer_slot, payload, payload_len);
    }
}

static void send_settings_and_gameinit(int peer_slot)
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
        queue_reliable(peer_slot, payload, len);
    }

    /* UICollisionSetting (opcode 0x16) -- reliable */
    len = bc_ui_collision_build(payload, sizeof(payload), g_collision_dmg);
    if (len > 0) {
        printf("[handshake] slot=%d sending UICollisionSetting (collision=%d)\n",
               peer_slot, g_collision_dmg);
        queue_reliable(peer_slot, payload, len);
    }

    /* GameInit (opcode 0x01) -- reliable */
    len = bc_gameinit_build(payload, sizeof(payload));
    if (len > 0) {
        printf("[handshake] slot=%d sending GameInit\n", peer_slot);
        queue_reliable(peer_slot, payload, len);
    }

    peer->state = PEER_LOBBY;
    printf("[handshake] slot=%d reached LOBBY state\n", peer_slot);

    /* Notify all peers (including the new player) about the new player.
     * Opcode 0x2A triggers client-side InitNetwork + object replication.
     * Wire format: [0x2A][0x20] — trailing space byte observed in traces. */
    u8 npig[2] = { BC_OP_NEW_PLAYER_IN_GAME, 0x20 };
    send_to_all(npig, 2);
    printf("[handshake] slot=%d sent NewPlayerInGame to all\n", peer_slot);
}

/* Notify all other peers that a player has left, then remove them. */
static void handle_peer_disconnect(int slot)
{
    if (g_peers.peers[slot].state == PEER_EMPTY) return;

    char addr_str[32];
    bc_addr_to_string(&g_peers.peers[slot].addr, addr_str, sizeof(addr_str));

    /* Only send delete notifications if the peer had reached LOBBY state */
    if (g_peers.peers[slot].state >= PEER_LOBBY) {
        u8 payload[4];
        int len;

        len = bc_delete_player_ui_build(payload, sizeof(payload));
        if (len > 0) relay_to_others(slot, payload, len, true);

        len = bc_delete_player_anim_build(payload, sizeof(payload));
        if (len > 0) relay_to_others(slot, payload, len, true);

        printf("[net] Sent DeletePlayer notifications for slot %d\n", slot);
    }

    bc_peers_remove(&g_peers, slot);
    printf("[net] Player removed: %s (slot %d), %d remaining\n",
           addr_str, slot, g_peers.count);
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
        printf("[net] Server full, sending BootPlayer to %s\n", addr_str);
        u8 boot_payload[4];
        int boot_len = bc_bootplayer_build(boot_payload, sizeof(boot_payload),
                                            BC_BOOT_SERVER_FULL);
        if (boot_len > 0) {
            send_unreliable_direct(from, boot_payload, boot_len);
        }
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
    send_checksum_request(slot, 0);

    (void)len;
}

static void handle_checksum_response(int peer_slot,
                                     const bc_transport_msg_t *msg)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];

    if (peer->state != PEER_CHECKSUMMING) {
        printf("[handshake] slot=%d unexpected checksum response (state=%d)\n",
               peer_slot, peer->state);
        return;
    }

    int round = peer->checksum_round;

    if (g_no_checksum || !g_manifest_loaded) {
        /* Permissive mode: accept without validation */
        printf("[handshake] slot=%d checksum round %d accepted (permissive, len=%d)\n",
               peer_slot, round, msg->payload_len);
    } else {
        /* Parse and validate against manifest */
        bc_checksum_resp_t resp;
        if (!bc_checksum_response_parse(&resp, msg->payload, msg->payload_len)) {
            printf("[handshake] slot=%d round %d parse error (len=%d)\n",
                   peer_slot, round, msg->payload_len);
            u8 boot[4];
            int blen = bc_bootplayer_build(boot, sizeof(boot), BC_BOOT_CHECKSUM);
            if (blen > 0) queue_reliable(peer_slot, boot, blen);
            handle_peer_disconnect(peer_slot);
            return;
        }

        bc_checksum_result_t result =
            bc_checksum_response_validate(&resp, &g_manifest.dirs[round]);

        if (result != CHECKSUM_OK) {
            printf("[handshake] slot=%d round %d FAILED: %s "
                   "(dir=0x%08X, %d files)\n",
                   peer_slot, round, bc_checksum_result_name(result),
                   resp.dir_hash, resp.file_count);
            u8 boot[4];
            int blen = bc_bootplayer_build(boot, sizeof(boot), BC_BOOT_CHECKSUM);
            if (blen > 0) queue_reliable(peer_slot, boot, blen);
            handle_peer_disconnect(peer_slot);
            return;
        }

        printf("[handshake] slot=%d checksum round %d validated "
               "(%d files, dir=0x%08X)\n",
               peer_slot, round, resp.file_count, resp.dir_hash);
    }

    peer->checksum_round++;

    if (peer->checksum_round < BC_CHECKSUM_ROUNDS) {
        send_checksum_request(peer_slot, peer->checksum_round);
    } else {
        printf("[handshake] slot=%d all checksums passed\n", peer_slot);
        send_settings_and_gameinit(peer_slot);
    }
}

static void handle_game_message(int peer_slot, const bc_transport_msg_t *msg)
{
    if (msg->payload_len < 1) return;

    bc_peer_t *peer = &g_peers.peers[peer_slot];

    /* ACK reliable messages (piggybacked in outbox with next flush) */
    if (msg->type == BC_TRANSPORT_RELIABLE) {
        queue_ack(peer_slot, msg->seq);
    }

    /* Handle fragmented messages: accumulate until complete */
    const u8 *payload = msg->payload;
    int payload_len = msg->payload_len;

    if (msg->type == BC_TRANSPORT_RELIABLE &&
        (msg->flags & BC_RELIABLE_FLAG_FRAGMENT)) {
        if (bc_fragment_receive(&peer->fragment, payload, payload_len)) {
            /* Complete reassembled message */
            payload = peer->fragment.buf;
            payload_len = peer->fragment.buf_len;
            printf("[fragment] slot=%d reassembled %d bytes from %d fragments\n",
                   peer_slot, payload_len, peer->fragment.frags_expected);
            bc_fragment_reset(&peer->fragment);
        } else {
            /* Still waiting for more fragments */
            return;
        }
    }

    if (payload_len < 1) return;

    u8 opcode = payload[0];
    const char *name = bc_opcode_name(opcode);

    /* Dispatch checksum responses to the handshake handler */
    if (opcode == BC_OP_CHECKSUM_RESP) {
        /* Build a temporary msg with the (possibly reassembled) payload */
        bc_transport_msg_t reassembled = *msg;
        reassembled.payload = (u8 *)payload;
        reassembled.payload_len = payload_len;
        handle_checksum_response(peer_slot, &reassembled);
        return;
    }

    /* Below here, only accept messages from peers in LOBBY or IN_GAME state */
    if (peer->state < PEER_LOBBY) {
        printf("[game] slot=%d opcode=0x%02X (%s) ignored (state=%d)\n",
               peer_slot, opcode, name ? name : "?", peer->state);
        return;
    }

    switch (opcode) {

    /* --- Chat relay (reliable) --- */
    case BC_MSG_CHAT:
    case BC_MSG_TEAM_CHAT:
        printf("[chat] slot=%d %s len=%d\n",
               peer_slot, opcode == BC_MSG_CHAT ? "ALL" : "TEAM",
               payload_len);
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    /* --- Python events: relay to all others (reliable) --- */
    case BC_OP_PYTHON_EVENT:
    case BC_OP_PYTHON_EVENT2:
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    /* --- Weapon/combat events: relay to all others (reliable) --- */
    case BC_OP_START_FIRING:
    case BC_OP_STOP_FIRING:
    case BC_OP_STOP_FIRING_AT:
    case BC_OP_SUBSYS_STATUS:
    case BC_OP_EVENT_FWD_DF:
    case BC_OP_EVENT_FWD:
    case BC_OP_START_CLOAK:
    case BC_OP_STOP_CLOAK:
    case BC_OP_START_WARP:
    case BC_OP_TORP_TYPE_CHANGE:
    case BC_OP_TORPEDO_FIRE:
    case BC_OP_BEAM_FIRE:
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    /* --- StateUpdate: relay to all others (unreliable -- high frequency) --- */
    case BC_OP_STATE_UPDATE:
        relay_to_others(peer_slot, payload, payload_len, false);
        break;

    /* --- ObjectCreateTeam: relay to all others (reliable) --- */
    case BC_OP_OBJ_CREATE_TEAM:
    case BC_OP_OBJ_CREATE:
        printf("[game] slot=%d object create, relaying\n", peer_slot);
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    /* --- Host message (C->S only, not relayed) --- */
    case BC_OP_HOST_MSG:
        printf("[game] slot=%d host message len=%d\n", peer_slot, payload_len);
        break;

    /* --- Collision effect (C->S, server would process) --- */
    case BC_OP_UNKNOWN_15:
        printf("[game] slot=%d collision effect len=%d\n", peer_slot, payload_len);
        break;

    /* --- Request object (C->S, server responds with object data) --- */
    case BC_OP_REQUEST_OBJ:
        printf("[game] slot=%d request object len=%d\n", peer_slot, payload_len);
        break;

    default:
        printf("[game] slot=%d opcode=0x%02X (%s) len=%d (unhandled)\n",
               peer_slot, opcode, name ? name : "?", payload_len);
        break;
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
            bc_reliable_ack(&g_peers.peers[slot].reliable_out, msg->seq);
            continue;
        }

        if (msg->type == BC_TRANSPORT_DISCONNECT) {
            char addr_str[32];
            bc_addr_to_string(from, addr_str, sizeof(addr_str));
            printf("[net] Player disconnected: %s (slot %d)\n", addr_str, slot);
            handle_peer_disconnect(slot);
            return;
        }

        /* Keepalive: extract player name (UTF-16LE) from handshake.
         * Format: [0x00][totalLen][flags:1][?:2][slot?:1][ip:4][name_utf16le...] */
        if (msg->type == BC_TRANSPORT_KEEPALIVE && msg->payload_len >= 8) {
            bc_peer_t *peer = &g_peers.peers[slot];
            if (peer->name[0] == '\0') {
                const u8 *name_start = msg->payload + 8;
                int name_bytes = msg->payload_len - 8;
                int j = 0;
                for (int k = 0; k + 1 < name_bytes && j < 30; k += 2) {
                    u8 lo = name_start[k];
                    u8 hi = name_start[k + 1];
                    if (lo == 0 && hi == 0) break;
                    peer->name[j++] = (char)(hi == 0 ? lo : '?');
                }
                peer->name[j] = '\0';
                if (j > 0) {
                    printf("[net] slot=%d player name: %s\n", slot, peer->name);
                }
            }
            continue;
        }

        /* Game message (reliable or unreliable) */
        if (msg->payload_len > 0) {
            handle_game_message(slot, msg);
        }
    }
}

/* --- Main --- */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -p <port>          Listen port (default: 22101)\n"
        "  -n <name>          Server name (default: \"OpenBC Server\")\n"
        "  -m <map>           Map name (default: \"DeepSpace9\")\n"
        "  --max <n>          Max players (default: 6)\n"
        "  --collision        Enable collision damage (default)\n"
        "  --no-collision     Disable collision damage\n"
        "  --friendly-fire    Enable friendly fire\n"
        "  --no-friendly-fire Disable friendly fire (default)\n"
        "  --manifest <path>  Hash manifest JSON (e.g. manifests/vanilla-1.1.json)\n"
        "  --no-checksum      Accept all checksums (testing without game files)\n"
        "  --master <h:p>     Master server address (e.g. master.gamespy.com:27900)\n"
        "  -h, --help         Show this help\n",
        prog);
}

int main(int argc, char **argv)
{
    /* Defaults */
    u16 port = BC_DEFAULT_PORT;
    const char *name = "OpenBC Server";
    const char *map = "DeepSpace9";
    int max_players = BC_MAX_PLAYERS;
    const char *manifest_path = NULL;
    const char *master_addr = NULL;

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
        } else if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
            manifest_path = argv[++i];
        } else if (strcmp(argv[i], "--no-checksum") == 0) {
            g_no_checksum = true;
        } else if (strcmp(argv[i], "--collision") == 0) {
            g_collision_dmg = true;
        } else if (strcmp(argv[i], "--no-collision") == 0) {
            g_collision_dmg = false;
        } else if (strcmp(argv[i], "--friendly-fire") == 0) {
            g_friendly_fire = true;
        } else if (strcmp(argv[i], "--no-friendly-fire") == 0) {
            g_friendly_fire = false;
        } else if (strcmp(argv[i], "--master") == 0 && i + 1 < argc) {
            master_addr = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    /* Load manifest */
    if (manifest_path) {
        if (bc_manifest_load(&g_manifest, manifest_path)) {
            g_manifest_loaded = true;
            bc_manifest_print_summary(&g_manifest);
        } else {
            fprintf(stderr, "Failed to load manifest: %s\n", manifest_path);
            return 1;
        }
    }

    if (!g_manifest_loaded && !g_no_checksum) {
        printf("Warning: no manifest loaded, running in permissive mode\n");
        printf("  Use --manifest <path> to enable checksum validation\n");
        printf("  Use --no-checksum to suppress this warning\n\n");
        g_no_checksum = true;
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

    /* Master server registration */
    if (master_addr) {
        if (!bc_master_init(&g_master, master_addr, port)) {
            fprintf(stderr, "Warning: master server registration failed\n");
        }
    }

    /* Register CTRL+C handler */
    SetConsoleCtrlHandler(console_handler, TRUE);

    printf("OpenBC Server v0.1.0\n");
    printf("Listening on port %u (%d max players)\n", port, max_players);
    printf("Server name: %s | Map: %s\n", name, map);
    printf("Collision damage: %s | Friendly fire: %s\n",
           g_collision_dmg ? "on" : "off",
           g_friendly_fire ? "on" : "off");
    if (g_manifest_loaded) {
        printf("Checksum validation: on (manifest loaded)\n");
    } else if (g_no_checksum) {
        printf("Checksum validation: off (--no-checksum)\n");
    } else {
        printf("Checksum validation: off (no manifest loaded)\n");
    }
    if (g_master.enabled) {
        printf("Master server: heartbeat enabled\n");
    }
    printf("Press Ctrl+C to stop.\n\n");

    /* Main loop -- 100ms tick (~10 Hz, matches real BC server) */
    u8 recv_buf[2048];
    u32 last_tick = GetTickCount();
    u32 tick_counter = 0;

    while (g_running) {
        /* Receive all pending packets */
        bc_addr_t from;
        int received;
        while ((received = bc_socket_recv(&g_socket, &from,
                                           recv_buf, sizeof(recv_buf))) > 0) {
            if (bc_gamespy_is_query(recv_buf, received)) {
                handle_gamespy(&from, recv_buf, received);
            } else {
                handle_packet(&from, recv_buf, received);
            }
        }

        /* Tick at 100ms intervals */
        u32 now = GetTickCount();
        if (now - last_tick >= 100) {
            /* Advance game clock */
            g_game_time += (f32)(now - last_tick) / 1000.0f;
            tick_counter++;

            /* Every 10 ticks (~1 second): retransmit, timeout, master heartbeat */
            if (tick_counter % 10 == 0) {
                /* Retransmit unACKed reliable messages */
                for (int i = 0; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *peer = &g_peers.peers[i];
                    if (peer->state == PEER_EMPTY) continue;

                    /* Check for dead peers (max retries exceeded) */
                    if (bc_reliable_check_timeout(&peer->reliable_out)) {
                        char addr_str[32];
                        bc_addr_to_string(&peer->addr, addr_str, sizeof(addr_str));
                        printf("[net] Peer %s (slot %d) timed out (no ACK)\n",
                               addr_str, i);
                        handle_peer_disconnect(i);
                        continue;
                    }

                    /* Retransmit overdue messages (direct send, not via outbox) */
                    int idx;
                    while ((idx = bc_reliable_check_retransmit(
                                &peer->reliable_out, now)) >= 0) {
                        bc_reliable_entry_t *e = &peer->reliable_out.entries[idx];
                        u8 pkt[BC_MAX_PACKET_SIZE];
                        int len = bc_transport_build_reliable(
                            pkt, sizeof(pkt), e->payload, e->payload_len, e->seq);
                        if (len > 0) {
                            alby_rules_cipher(pkt, (size_t)len);
                            bc_socket_send(&g_socket, &peer->addr, pkt, len);
                        }
                    }
                }

                /* Timeout stale peers (30 second timeout) */
                for (int i = 0; i < BC_MAX_PLAYERS; i++) {
                    if (g_peers.peers[i].state == PEER_EMPTY) continue;
                    if (now - g_peers.peers[i].last_recv_time > 30000) {
                        printf("[net] Peer slot %d timed out (no packets)\n", i);
                        handle_peer_disconnect(i);
                    }
                }

                /* Master server heartbeat */
                bc_master_tick(&g_master, &g_socket, now);
            }

            /* Every 10 ticks (~1 second): send keepalive to all active peers */
            if (tick_counter % 10 == 0) {
                for (int i = 0; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *peer = &g_peers.peers[i];
                    if (peer->state < PEER_LOBBY) continue;
                    bc_outbox_add_keepalive(&peer->outbox);
                }
            }

            /* Flush all peer outboxes (sends batched messages) */
            for (int i = 0; i < BC_MAX_PLAYERS; i++) {
                bc_peer_t *peer = &g_peers.peers[i];
                if (peer->state == PEER_EMPTY) continue;
                bc_outbox_flush(&peer->outbox, &g_socket, &peer->addr);
            }

            last_tick = now;
        }

        /* Don't burn CPU -- sleep 1ms between polls */
        Sleep(1);
    }

    printf("\nShutting down...\n");
    bc_master_shutdown(&g_master, &g_socket);
    bc_socket_close(&g_socket);
    bc_net_shutdown();
    return 0;
}
