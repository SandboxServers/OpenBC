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
#include "openbc/game_events.h"
#include "openbc/log.h"

#include <windows.h>  /* For Sleep(), GetTickCount() */

/* --- Server state --- */

static volatile bool g_running = true;

static bc_socket_t    g_socket;       /* Game port (default 22101) */
static bc_socket_t    g_query_socket; /* LAN query port (6500) */
static bool           g_query_socket_open = false;
static bc_peer_mgr_t  g_peers;
static bc_server_info_t g_info;

/* Game settings */
static bool        g_collision_dmg = true;
static bool        g_friendly_fire = false;
static const char *g_map_name = "Multiplayer.Episode.Mission1.Mission1";
static int         g_system_index = 1;   /* Star system 1-9 (SpeciesToSystem) */
static int         g_time_limit = -1;    /* Minutes, -1 = no limit */
static int         g_frag_limit = -1;    /* Kills, -1 = no limit */
static f32         g_game_time = 0.0f;

/* Manifest / checksum validation */
static bc_manifest_t g_manifest;
static bool          g_manifest_loaded = false;
static bool          g_no_checksum = false;  /* auto-set when no manifest */

/* Master servers */
static bc_master_list_t g_masters;

/* --- Signal handler --- */

static BOOL WINAPI console_handler(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_running = false;
        return TRUE;
    default:
        return FALSE;
    }
}

/* --- Packet handling --- */

static void handle_gamespy(bc_socket_t *sock, const bc_addr_t *from,
                           const u8 *data, int len)
{
    char addr_str[32];
    bc_addr_to_string(from, addr_str, sizeof(addr_str));
    LOG_DEBUG("gamespy", "Query from %s: %.*s", addr_str, len, (const char *)data);

    g_info.numplayers = g_peers.count;

    /* Handle \secure\ challenge from master server */
    if (bc_gamespy_is_secure(data, len)) {
        char challenge[64];
        int clen = bc_gamespy_extract_secure(data, len,
                                              challenge, sizeof(challenge));
        if (clen > 0) {
            u8 response[512];
            int resp_len = bc_gamespy_build_validate(
                response, sizeof(response), challenge);
            if (resp_len > 0) {
                bc_socket_send(sock, from, response, resp_len);
                LOG_INFO("gamespy", "Sent validate to %s (challenge: %s)",
                         addr_str, challenge);
            }
        }
        return;
    }

    /* Regular GameSpy query -- respond with server info */
    u8 response[1024];
    int resp_len = bc_gamespy_build_response(response, sizeof(response),
                                             &g_info, data, len);
    if (resp_len > 0) {
        int sent = bc_socket_send(sock, from, response, resp_len);
        LOG_DEBUG("gamespy", "Response to %s (%d bytes, sent=%d): %.*s",
                  addr_str, resp_len, sent, resp_len, (const char *)response);
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
        LOG_DEBUG("handshake", "slot=%d sending checksum request round %d",
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
        LOG_DEBUG("handshake", "slot=%d sending Settings (slot=%d, map=%s)",
                  peer_slot, peer_slot, g_map_name);
        queue_reliable(peer_slot, payload, len);
    }

    /* UICollisionSetting (opcode 0x16) -- reliable */
    len = bc_ui_collision_build(payload, sizeof(payload), g_collision_dmg);
    if (len > 0) {
        LOG_DEBUG("handshake", "slot=%d sending UICollisionSetting (collision=%d)",
                  peer_slot, g_collision_dmg);
        queue_reliable(peer_slot, payload, len);
    }

    /* GameInit (opcode 0x01) -- reliable */
    len = bc_gameinit_build(payload, sizeof(payload));
    if (len > 0) {
        LOG_DEBUG("handshake", "slot=%d sending GameInit", peer_slot);
        queue_reliable(peer_slot, payload, len);
    }

    peer->state = PEER_LOBBY;
    LOG_INFO("handshake", "slot=%d reached LOBBY state", peer_slot);

    /* Notify all peers (including the new player) about the new player.
     * Opcode 0x2A triggers client-side InitNetwork + object replication.
     * Wire format: [0x2A][0x20] — trailing space byte observed in traces. */
    u8 npig[2] = { BC_OP_NEW_PLAYER_IN_GAME, 0x20 };
    send_to_all(npig, 2);
    LOG_DEBUG("handshake", "slot=%d sent NewPlayerInGame to all", peer_slot);

    /* MissionInit (opcode 0x35) -- reliable, sent to joining player only.
     * In the original game, the host's Python InitNetwork() sends this.
     * We are the host, so we send it ourselves. Tells the client which
     * star system to load and what the match rules are. */
    len = bc_mission_init_build(payload, sizeof(payload),
                                g_system_index, g_info.maxplayers,
                                g_time_limit, g_frag_limit);
    if (len > 0) {
        LOG_DEBUG("handshake", "slot=%d sending MissionInit (system=%d)",
                  peer_slot, g_system_index);
        queue_reliable(peer_slot, payload, len);
    }
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

        LOG_DEBUG("net", "Sent DeletePlayer notifications for slot %d", slot);
    }

    bc_peers_remove(&g_peers, slot);
    LOG_INFO("net", "Player removed: %s (slot %d), %d remaining",
             addr_str, slot, g_peers.count);
}

static void handle_connect(const bc_addr_t *from, int len)
{
    char addr_str[32];
    bc_addr_to_string(from, addr_str, sizeof(addr_str));

    int slot = bc_peers_find(&g_peers, from);
    if (slot >= 0) {
        LOG_WARN("net", "Duplicate connect from %s (slot %d)", addr_str, slot);
        return;
    }

    slot = bc_peers_add(&g_peers, from);
    if (slot < 0) {
        LOG_WARN("net", "Server full, sending BootPlayer to %s", addr_str);
        u8 boot_payload[4];
        int boot_len = bc_bootplayer_build(boot_payload, sizeof(boot_payload),
                                            BC_BOOT_SERVER_FULL);
        if (boot_len > 0) {
            send_unreliable_direct(from, boot_payload, boot_len);
        }
        return;
    }

    g_peers.peers[slot].last_recv_time = GetTickCount();
    LOG_INFO("net", "Player connected from %s -> slot %d (%d/%d)",
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

    /* Handle 0xFF final round response */
    if (peer->state == PEER_CHECKSUMMING_FINAL) {
        LOG_DEBUG("handshake", "slot=%d checksum round 0xFF accepted (len=%d)",
                  peer_slot, msg->payload_len);
        send_settings_and_gameinit(peer_slot);
        return;
    }

    if (peer->state != PEER_CHECKSUMMING) {
        LOG_WARN("handshake", "slot=%d unexpected checksum response (state=%d)",
                 peer_slot, peer->state);
        return;
    }

    int round = peer->checksum_round;

    if (g_no_checksum || !g_manifest_loaded) {
        /* Permissive mode: accept without validation */
        LOG_DEBUG("handshake", "slot=%d checksum round %d accepted (permissive, len=%d)",
                  peer_slot, round, msg->payload_len);
    } else {
        /* Parse and validate against manifest */
        bc_checksum_resp_t resp;
        if (!bc_checksum_response_parse(&resp, msg->payload, msg->payload_len)) {
            LOG_WARN("handshake", "slot=%d round %d parse error (len=%d)",
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
            LOG_WARN("handshake", "slot=%d round %d FAILED: %s "
                     "(dir=0x%08X, %d files)",
                     peer_slot, round, bc_checksum_result_name(result),
                     resp.dir_hash, resp.file_count);
            u8 boot[4];
            int blen = bc_bootplayer_build(boot, sizeof(boot), BC_BOOT_CHECKSUM);
            if (blen > 0) queue_reliable(peer_slot, boot, blen);
            handle_peer_disconnect(peer_slot);
            return;
        }

        LOG_DEBUG("handshake", "slot=%d checksum round %d validated "
                  "(%d files, dir=0x%08X)",
                  peer_slot, round, resp.file_count, resp.dir_hash);
    }

    peer->checksum_round++;

    if (peer->checksum_round < BC_CHECKSUM_ROUNDS) {
        send_checksum_request(peer_slot, peer->checksum_round);
    } else {
        /* All 4 regular rounds passed. Send the final 0xFF round.
         * Decompiled code (FUN_006a35b0) shows the server sends
         * [0x20][0xFF] after rounds 0-3 complete. */
        LOG_DEBUG("handshake", "slot=%d rounds 0-3 passed, sending final round 0xFF",
                  peer_slot);
        u8 payload[16];
        int plen = bc_checksum_request_final_build(payload, sizeof(payload));
        if (plen > 0) {
            queue_reliable(peer_slot, payload, plen);
        }
        peer->state = PEER_CHECKSUMMING_FINAL;
    }
}

/* Return a player's name for log output. Falls back to "slot N" if unnamed. */
static const char *peer_name(int slot)
{
    static char fallback[16];
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return "???";
    if (g_peers.peers[slot].name[0] != '\0')
        return g_peers.peers[slot].name;
    snprintf(fallback, sizeof(fallback), "slot %d", slot);
    return fallback;
}

/* Resolve an object ID to its owning player's name. */
static const char *object_owner_name(i32 object_id)
{
    int slot = bc_object_id_to_slot(object_id);
    if (slot < 0) return "???";
    return peer_name(slot);
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
            LOG_DEBUG("fragment", "slot=%d reassembled %d bytes from %d fragments",
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
        LOG_DEBUG("game", "slot=%d opcode=0x%02X (%s) ignored (state=%d)",
                  peer_slot, opcode, name ? name : "?", peer->state);
        return;
    }

    switch (opcode) {

    /* --- Chat relay (reliable) --- */
    case BC_MSG_CHAT:
    case BC_MSG_TEAM_CHAT: {
        bc_chat_event_t ev;
        if (bc_parse_chat_message(payload, payload_len, &ev)) {
            LOG_INFO("chat", "[%s] %s: %s",
                     opcode == BC_MSG_CHAT ? "ALL" : "TEAM",
                     peer_name(ev.sender_slot), ev.message);
        } else {
            LOG_INFO("chat", "slot=%d %s len=%d",
                     peer_slot, opcode == BC_MSG_CHAT ? "ALL" : "TEAM",
                     payload_len);
        }
        relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

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
    case BC_OP_ADD_REPAIR_LIST:
    case BC_OP_CLIENT_EVENT:
    case BC_OP_START_CLOAK:
    case BC_OP_STOP_CLOAK:
    case BC_OP_START_WARP:
    case BC_OP_REPAIR_PRIORITY:
    case BC_OP_SET_PHASER_LEVEL:
    case BC_OP_TORP_TYPE_CHANGE:
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    case BC_OP_TORPEDO_FIRE: {
        bc_torpedo_event_t ev;
        if (bc_parse_torpedo_fire(payload, payload_len, &ev)) {
            if (ev.has_target)
                LOG_INFO("combat", "%s fired torpedo -> %s (subsys=%d)",
                         object_owner_name(ev.shooter_id),
                         object_owner_name(ev.target_id),
                         ev.subsys_index);
            else
                LOG_INFO("combat", "%s fired torpedo (no lock)",
                         object_owner_name(ev.shooter_id));
        }
        relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

    case BC_OP_BEAM_FIRE: {
        bc_beam_event_t ev;
        if (bc_parse_beam_fire(payload, payload_len, &ev)) {
            if (ev.has_target)
                LOG_INFO("combat", "%s fired beam -> %s",
                         object_owner_name(ev.shooter_id),
                         object_owner_name(ev.target_id));
            else
                LOG_INFO("combat", "%s fired beam (no target)",
                         object_owner_name(ev.shooter_id));
        }
        relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

    /* --- Explosion: relay + log damage --- */
    case BC_OP_EXPLOSION: {
        bc_explosion_event_t ev;
        if (bc_parse_explosion(payload, payload_len, &ev)) {
            LOG_INFO("combat", "Explosion on %s's ship: %.1f damage, radius %.1f",
                     object_owner_name(ev.object_id), ev.damage, ev.radius);
        }
        relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

    /* --- StateUpdate: relay to all others (unreliable -- high frequency) --- */
    case BC_OP_STATE_UPDATE:
        relay_to_others(peer_slot, payload, payload_len, false);
        break;

    /* --- Object creation: relay + log --- */
    case BC_OP_OBJ_CREATE_TEAM:
    case BC_OP_OBJ_CREATE: {
        bc_object_create_header_t hdr;
        if (bc_parse_object_create_header(payload + 1, payload_len - 1, &hdr)) {
            if (hdr.has_team)
                LOG_INFO("game", "%s spawned object (owner=%s, team=%d)",
                         peer_name(peer_slot),
                         peer_name(hdr.owner_slot), hdr.team_id);
            else
                LOG_INFO("game", "%s spawned object (owner=%s)",
                         peer_name(peer_slot),
                         peer_name(hdr.owner_slot));
        } else {
            LOG_INFO("game", "%s spawned object", peer_name(peer_slot));
        }
        relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

    /* --- Object destruction: relay + log --- */
    case BC_OP_DESTROY_OBJ: {
        bc_destroy_event_t ev;
        if (bc_parse_destroy_obj(payload, payload_len, &ev)) {
            LOG_INFO("combat", "%s's ship destroyed",
                     object_owner_name(ev.object_id));
        }
        relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

    /* --- Host message (C->S only, not relayed) --- */
    case BC_OP_HOST_MSG:
        LOG_DEBUG("game", "slot=%d host message len=%d", peer_slot, payload_len);
        break;

    /* --- Collision effect (C->S, host processes damage authoritatively) ---
     * In stock BC, the client detects a collision and sends this to the host.
     * The host computes damage via FUN_006a2470. For Phase C relay, we relay
     * to others so they see the effect. Phase E will process damage locally. */
    case BC_OP_COLLISION_EFFECT:
        LOG_DEBUG("game", "slot=%d collision effect len=%d", peer_slot, payload_len);
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    /* --- Request object (C->S, server responds with object data) --- */
    case BC_OP_REQUEST_OBJ:
        LOG_DEBUG("game", "slot=%d request object len=%d", peer_slot, payload_len);
        break;

    default:
        LOG_DEBUG("game", "slot=%d opcode=0x%02X (%s) len=%d (unhandled)",
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
        {
            char hex[128];
            int hpos = 0;
            for (int j = 0; j < len && hpos < 120; j++)
                hpos += snprintf(hex + hpos, (size_t)(sizeof(hex) - hpos),
                                  "%02X ", data[j]);
            LOG_DEBUG("net", "Transport parse failed: len=%d decrypted=[%s]",
                      len, hex);
        }
        return;
    }

    /* Handle connection requests (direction 0xFF) */
    if (pkt.direction == BC_DIR_INIT) {
        handle_connect(from, len);
        return;
    }

    if (slot < 0) return;  /* Unknown peer, ignore */

    /* Validate direction byte: client packets use BC_DIR_CLIENT + slot_index.
     * Log a warning on mismatch but continue processing (not a hard reject). */
    u8 expected_dir = BC_DIR_CLIENT + (u8)slot;
    if (pkt.direction != expected_dir) {
        LOG_WARN("net", "slot=%d direction byte mismatch: got 0x%02X, expected 0x%02X",
                 slot, pkt.direction, expected_dir);
    }

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
            LOG_INFO("net", "Player disconnected: %s (slot %d)", addr_str, slot);
            handle_peer_disconnect(slot);
            return;
        }

        /* Keepalive with name: during handshake, client sends a keepalive
         * (type 0x00) with UTF-16LE player name embedded.
         * Format: [0x00][totalLen][flags:1][?:2][slot?:1][ip:4][name_utf16le...]
         * After handshake, type 0x00 carries unreliable game data (StateUpdate
         * etc.) -- so only consume as keepalive while name is still unknown. */
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
                    LOG_INFO("net", "slot=%d player name: %s", slot, peer->name);
                }
                continue;  /* Name keepalive handled, skip game processing */
            }
            /* Name already set -- fall through to game handler */
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
        "  -m <mode>          Game mode (default: \"Multiplayer.Episode.Mission1.Mission1\")\n"
        "  --system <n>       Star system index 1-9 (default: 1)\n"
        "  --max <n>          Max players (default: 6)\n"
        "  --time-limit <n>   Time limit in minutes (default: none)\n"
        "  --frag-limit <n>   Frag/kill limit (default: none)\n"
        "  --collision        Enable collision damage (default)\n"
        "  --no-collision     Disable collision damage\n"
        "  --friendly-fire    Enable friendly fire\n"
        "  --no-friendly-fire Disable friendly fire (default)\n"
        "  --manifest <path>  Hash manifest JSON (e.g. manifests/vanilla-1.1.json)\n"
        "  --master <h:p>     Master server address (repeatable; replaces defaults)\n"
        "  --no-master        Disable all master server heartbeating\n"
        "  --log-level <lvl>  Log verbosity: quiet|error|warn|info|debug|trace (default: info)\n"
        "  --log-file <path>  Also write log to file (append mode)\n"
        "  -q                 Shorthand for --log-level quiet\n"
        "  -v                 Shorthand for --log-level debug\n"
        "  -vv                Shorthand for --log-level trace\n"
        "  -h, --help         Show this help\n",
        prog);
}

static bc_log_level_t parse_log_level(const char *str)
{
    if (strcmp(str, "quiet") == 0) return LOG_QUIET;
    if (strcmp(str, "error") == 0) return LOG_ERROR;
    if (strcmp(str, "warn")  == 0) return LOG_WARN;
    if (strcmp(str, "info")  == 0) return LOG_INFO;
    if (strcmp(str, "debug") == 0) return LOG_DEBUG;
    if (strcmp(str, "trace") == 0) return LOG_TRACE;
    fprintf(stderr, "Unknown log level: %s (using info)\n", str);
    return LOG_INFO;
}

int main(int argc, char **argv)
{
    /* Defaults */
    u16 port = BC_DEFAULT_PORT;
    const char *name = "OpenBC Server";
    const char *map = "Multiplayer.Episode.Mission1.Mission1";
    int max_players = BC_MAX_PLAYERS;
    const char *manifest_path = NULL;
    const char *user_masters[BC_MAX_MASTERS];
    int user_master_count = 0;
    bool no_master = false;
    bc_log_level_t log_level = LOG_INFO;
    const char *log_file_path = NULL;

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
        } else if (strcmp(argv[i], "--system") == 0 && i + 1 < argc) {
            g_system_index = atoi(argv[++i]);
            if (g_system_index < 1) g_system_index = 1;
            if (g_system_index > 9) g_system_index = 9;
        } else if (strcmp(argv[i], "--time-limit") == 0 && i + 1 < argc) {
            g_time_limit = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frag-limit") == 0 && i + 1 < argc) {
            g_frag_limit = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
            manifest_path = argv[++i];
        } else if (strcmp(argv[i], "--collision") == 0) {
            g_collision_dmg = true;
        } else if (strcmp(argv[i], "--no-collision") == 0) {
            g_collision_dmg = false;
        } else if (strcmp(argv[i], "--friendly-fire") == 0) {
            g_friendly_fire = true;
        } else if (strcmp(argv[i], "--no-friendly-fire") == 0) {
            g_friendly_fire = false;
        } else if (strcmp(argv[i], "--master") == 0 && i + 1 < argc) {
            if (user_master_count < BC_MAX_MASTERS)
                user_masters[user_master_count++] = argv[++i];
        } else if (strcmp(argv[i], "--no-master") == 0) {
            no_master = true;
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            log_level = parse_log_level(argv[++i]);
        } else if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            log_file_path = argv[++i];
        } else if (strcmp(argv[i], "-q") == 0) {
            log_level = LOG_QUIET;
        } else if (strcmp(argv[i], "-vv") == 0) {
            log_level = LOG_TRACE;
        } else if (strcmp(argv[i], "-v") == 0) {
            log_level = LOG_DEBUG;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    /* Apply parsed settings to globals */
    g_map_name = map;

    /* Initialize logging (before anything that uses LOG_*) */
    bc_log_init(log_level, log_file_path);

    /* Load manifest */
    if (manifest_path) {
        if (bc_manifest_load(&g_manifest, manifest_path)) {
            g_manifest_loaded = true;
            bc_manifest_print_summary(&g_manifest);
        } else {
            LOG_ERROR("init", "Failed to load manifest: %s", manifest_path);
            bc_log_shutdown();
            return 1;
        }
    }

    if (!g_manifest_loaded && !g_no_checksum) {
        LOG_WARN("init", "No manifest loaded, running in permissive mode");
        LOG_WARN("init", "  Use --manifest <path> to enable checksum validation");
        g_no_checksum = true;
    }

    /* Initialize */
    if (!bc_net_init()) {
        LOG_ERROR("init", "Failed to initialize networking");
        bc_log_shutdown();
        return 1;
    }

    if (!bc_socket_open(&g_socket, port)) {
        LOG_ERROR("init", "Failed to bind port %u", port);
        bc_net_shutdown();
        bc_log_shutdown();
        return 1;
    }

    /* Open LAN query socket on port 6500 (GameSpy standard).
     * BC clients broadcast queries here for LAN server discovery.
     * Non-fatal if port is in use (e.g., another server instance). */
    if (port != BC_GAMESPY_QUERY_PORT) {
        if (bc_socket_open(&g_query_socket, BC_GAMESPY_QUERY_PORT)) {
            g_query_socket_open = true;
            LOG_INFO("init", "LAN query socket open on port %u",
                     BC_GAMESPY_QUERY_PORT);
        } else {
            LOG_WARN("init", "Could not bind LAN query port %u "
                     "(LAN browser discovery may not work)",
                     BC_GAMESPY_QUERY_PORT);
        }
    }

    bc_peers_init(&g_peers);

    /* Server info for GameSpy responses.
     * Fields must match stock BC QR1 callbacks (basic + info + rules). */
    memset(&g_info, 0, sizeof(g_info));
    snprintf(g_info.hostname, sizeof(g_info.hostname), "%s", name);
    snprintf(g_info.missionscript, sizeof(g_info.missionscript), "%s", map);
    snprintf(g_info.mapname, sizeof(g_info.mapname), "%s", map);
    snprintf(g_info.gamemode, sizeof(g_info.gamemode), "openplaying");
    snprintf(g_info.system, sizeof(g_info.system), "DeepSpace9");
    g_info.numplayers = 0;
    g_info.maxplayers = max_players;
    g_info.timelimit = g_time_limit > 0 ? g_time_limit : -1;
    g_info.fraglimit = g_frag_limit > 0 ? g_frag_limit : -1;

    /* Master server registration */
    memset(&g_masters, 0, sizeof(g_masters));
    if (!no_master) {
        if (user_master_count > 0) {
            for (int i = 0; i < user_master_count; i++)
                bc_master_add(&g_masters, user_masters[i], port);
        } else {
            bc_master_init_defaults(&g_masters, port);
        }
        if (g_masters.count > 0)
            bc_master_probe(&g_masters, &g_socket, &g_info);
    }

    /* Register CTRL+C handler */
    SetConsoleCtrlHandler(console_handler, TRUE);

    /* Startup banner (raw printf, not a log message) */
    printf("OpenBC Server v0.1.0\n");
    printf("Listening on port %u (%d max players)\n", port, max_players);
    printf("Server name: %s | Map: %s\n", name, map);
    printf("Collision damage: %s | Friendly fire: %s\n",
           g_collision_dmg ? "on" : "off",
           g_friendly_fire ? "on" : "off");
    if (g_manifest_loaded) {
        printf("Checksum validation: on (manifest loaded)\n");
    } else {
        printf("Checksum validation: off (no manifest, permissive mode)\n");
    }
    if (g_masters.count > 0) {
        int verified = 0;
        for (int i = 0; i < g_masters.count; i++)
            if (g_masters.entries[i].verified) verified++;
        printf("Master servers: %d registered, %d total\n",
               verified, g_masters.count);
    }
    printf("Press Ctrl+C to stop.\n\n");

    /* Main loop -- 100ms tick (~10 Hz, matches real BC server) */
    u8 recv_buf[2048];
    u32 last_tick = GetTickCount();
    u32 tick_counter = 0;

    while (g_running) {
        /* Receive all pending packets on game port */
        bc_addr_t from;
        int received;
        while ((received = bc_socket_recv(&g_socket, &from,
                                           recv_buf, sizeof(recv_buf))) > 0) {
            if (bc_gamespy_is_query(recv_buf, received)) {
                handle_gamespy(&g_socket, &from, recv_buf, received);
            } else {
                char pkt_addr[32];
                bc_addr_to_string(&from, pkt_addr, sizeof(pkt_addr));
                {
                    char hex[128];
                    int hpos = 0;
                    for (int j = 0; j < received && hpos < 120; j++)
                        hpos += snprintf(hex + hpos, (size_t)(sizeof(hex) - hpos),
                                          "%02X ", recv_buf[j]);
                    LOG_DEBUG("net", "Game packet from %s: %d bytes raw=[%s]",
                              pkt_addr, received, hex);
                }
                handle_packet(&from, recv_buf, received);
            }
        }

        /* Receive all pending packets on LAN query port (6500) */
        if (g_query_socket_open) {
            while ((received = bc_socket_recv(&g_query_socket, &from,
                                               recv_buf, sizeof(recv_buf))) > 0) {
                if (bc_gamespy_is_query(recv_buf, received)) {
                    handle_gamespy(&g_query_socket, &from, recv_buf, received);
                }
                /* Non-GameSpy packets on port 6500 are ignored */
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
                        LOG_INFO("net", "Peer %s (slot %d) timed out (no ACK)",
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
                        LOG_INFO("net", "Peer slot %d timed out (no packets)", i);
                        handle_peer_disconnect(i);
                    }
                }

                /* Master server heartbeat */
                bc_master_tick(&g_masters, &g_socket, now);
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

    LOG_INFO("shutdown", "Shutting down...");

    /* Flush all pending outbox data before sending shutdown */
    for (int i = 0; i < BC_MAX_PLAYERS; i++) {
        bc_peer_t *peer = &g_peers.peers[i];
        if (peer->state == PEER_EMPTY) continue;
        bc_outbox_flush(&peer->outbox, &g_socket, &peer->addr);
    }

    /* Send ConnectAck shutdown notification to all connected peers.
     * Real BC server sends ConnectAck (0x05) to each peer on shutdown,
     * NOT BootPlayer or DeletePlayer. */
    for (int i = 0; i < BC_MAX_PLAYERS; i++) {
        bc_peer_t *peer = &g_peers.peers[i];
        if (peer->state == PEER_EMPTY) continue;

        u8 pkt[16];
        int len = bc_transport_build_shutdown_notify(
            pkt, sizeof(pkt), (u8)(i + 1), peer->addr.ip);
        if (len > 0) {
            alby_rules_cipher(pkt, (size_t)len);
            bc_socket_send(&g_socket, &peer->addr, pkt, len);
            LOG_INFO("shutdown", "Sent shutdown to slot %d", i);
        }

        /* Clear peer state */
        peer->state = PEER_EMPTY;
    }
    g_peers.count = 0;

    /* Unregister from master servers (sends exit heartbeat) */
    bc_master_shutdown(&g_masters, &g_socket);

    /* Close all sockets */
    if (g_query_socket_open) {
        bc_socket_close(&g_query_socket);
        g_query_socket_open = false;
    }
    bc_socket_close(&g_socket);

    /* Release Winsock */
    bc_net_shutdown();

    /* Unregister console handler */
    SetConsoleCtrlHandler(console_handler, FALSE);

    LOG_INFO("shutdown", "Server stopped.");
    bc_log_shutdown();
    return 0;
}
