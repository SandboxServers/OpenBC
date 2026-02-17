#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>

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
#include "openbc/game_builders.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/combat.h"
#include "openbc/movement.h"
#include "openbc/torpedo_tracker.h"
#include "openbc/log.h"

#include <windows.h>  /* For Sleep(), GetTickCount() */

/* --- Session statistics --- */

typedef struct {
    char name[32];
    u32  connect_time;      /* GetTickCount() when connected */
    u32  disconnect_time;   /* GetTickCount() when left (0 = still connected) */
} player_record_t;

typedef struct {
    u32  start_time;
    u32  total_connections;
    u32  peak_players;
    u32  boots_full;
    u32  boots_checksum;
    u32  disconnects;
    u32  timeouts;
    u32  gamespy_queries;
    u32  reliable_retransmits;
    u32  opcodes_recv[256];
    player_record_t players[32];
    int  player_count;
} bc_session_stats_t;

static bc_session_stats_t g_stats;

/* --- Server state --- */

static volatile bool g_running = true;

static bc_socket_t    g_socket;       /* Game port (default 22101) */
static bc_socket_t    g_query_socket; /* LAN query port (6500) */
static bool           g_query_socket_open = false;
static bc_peer_mgr_t  g_peers;
static bc_server_info_t g_info;

/* Ship data registry (Phase E: server-authoritative damage) */
static bc_game_registry_t g_registry;
static bool               g_registry_loaded = false;
static bc_torpedo_mgr_t   g_torpedoes;

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

/* Forward declarations */
static void flush_peer(int slot);

/* --- Packet handling --- */

static void handle_gamespy(bc_socket_t *sock, const bc_addr_t *from,
                           const u8 *data, int len)
{
    char addr_str[32];
    bc_addr_to_string(from, addr_str, sizeof(addr_str));
    LOG_DEBUG("gamespy", "Query from %s: %.*s", addr_str, len, (const char *)data);

    g_info.numplayers = g_peers.count > 0 ? g_peers.count - 1 : 0; /* exclude dedi slot */

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
                const char *master = bc_master_mark_verified(&g_masters, from);
                if (master)
                    LOG_INFO("master", "Registered with %s", master);
                else
                    LOG_DEBUG("gamespy", "Sent validate to %s (challenge: %s)",
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
        g_stats.gamespy_queries++;
        int sent = bc_socket_send(sock, from, response, resp_len);
        const char *master = bc_master_mark_verified(&g_masters, from);
        if (master)
            LOG_INFO("master", "Registered with %s", master);
        else
            LOG_DEBUG("gamespy", "Response to %s (%d bytes, sent=%d): %.*s",
                      addr_str, resp_len, sent, resp_len, (const char *)response);
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
        flush_peer(peer_slot);
        bc_outbox_add_reliable(&peer->outbox, payload, payload_len, seq);
    }
}

/* Queue an unreliable message into a peer's outbox. */
static void queue_unreliable(int peer_slot, const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    if (!bc_outbox_add_unreliable(&peer->outbox, payload, payload_len)) {
        flush_peer(peer_slot);
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
        bc_packet_t trace;
        if (bc_transport_parse(pkt, len, &trace))
            bc_log_packet_trace(&trace, -1, "SEND");
        alby_cipher_encrypt(pkt, (size_t)len);
        bc_socket_send(&g_socket, to, pkt, len);
    }
}

/* Flush a peer's outbox with optional SEND trace logging.
 * Replaces direct bc_outbox_flush() calls so all outgoing packets are traced. */
static void flush_peer(int slot)
{
    bc_peer_t *peer = &g_peers.peers[slot];
    if (!bc_outbox_pending(&peer->outbox)) return;

    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(&peer->outbox, pkt, sizeof(pkt));
    if (len > 0) {
        /* Trace-log outgoing packet before encryption */
        bc_packet_t trace;
        if (bc_transport_parse(pkt, len, &trace))
            bc_log_packet_trace(&trace, slot, "SEND");
        alby_cipher_encrypt(pkt, (size_t)len);
        bc_socket_send(&g_socket, &peer->addr, pkt, len);
    }
}

/* Relay a message to all connected peers except the sender.
 * Uses reliable delivery for guaranteed opcodes, unreliable otherwise. */
static void relay_to_others(int sender_slot, const u8 *payload, int payload_len,
                            bool reliable)
{
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {  /* skip slot 0 = dedi */
        if (i == sender_slot) continue;
        if (g_peers.peers[i].state < PEER_LOBBY) continue;

        if (reliable) {
            queue_reliable(i, payload, payload_len);
        } else {
            queue_unreliable(i, payload, payload_len);
        }
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
        /* Flush immediately -- stock dedi sends ACK + next ChecksumReq in
         * one packet within 1ms of receiving the response.  Waiting for the
         * 100ms tick would add unnecessary latency during handshake. */
        flush_peer(peer_slot);
    }
}

static void send_settings_and_gameinit(int peer_slot)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    u8 payload[512];
    int len;

    /* Opcode 0x28 -- sent before Settings (observed in all stock dedi traces).
     * Purpose unclear (possibly "checksum exchange complete" signal).
     * Wire format: 1 byte, just the opcode, no payload. */
    payload[0] = BC_OP_UNKNOWN_28;
    queue_reliable(peer_slot, payload, 1);
    LOG_DEBUG("handshake", "slot=%d sending opcode 0x28", peer_slot);

    /* Settings (opcode 0x00) -- reliable.
     * Player slot in Settings is the game-level slot (0-based), not the
     * network peer index. Stock dedi sends slot=0 for first joiner.
     * peer_slot 1 → game_slot 0, peer_slot 2 → game_slot 1, etc. */
    u8 game_slot = (u8)(peer_slot > 0 ? peer_slot - 1 : 0);
    len = bc_settings_build(payload, sizeof(payload),
                            g_game_time, g_collision_dmg, g_friendly_fire,
                            game_slot, g_map_name);
    if (len > 0) {
        LOG_DEBUG("handshake", "slot=%d sending Settings (game_slot=%d, map=%s)",
                  peer_slot, game_slot, g_map_name);
        queue_reliable(peer_slot, payload, len);
    }

    /* UICollisionSetting (0x16) is NOT sent during handshake -- collision
     * is already in the Settings bit flags.  Stock dedi never sends 0x16
     * during initial connection (verified from traces). */

    /* GameInit (opcode 0x01) -- reliable */
    len = bc_gameinit_build(payload, sizeof(payload));
    if (len > 0) {
        LOG_DEBUG("handshake", "slot=%d sending GameInit", peer_slot);
        queue_reliable(peer_slot, payload, len);
    }

    peer->state = PEER_LOBBY;
    LOG_INFO("handshake", "slot=%d reached LOBBY state", peer_slot);

    /* --- Late-join data: existing ships, scores, DeletePlayerUI --- */

    /* Send Score (0x37) with all players' current scores.
     * Scores are indexed by game_slot (peer_slot - 1). */
    {
        i32 scores[BC_MAX_PLAYERS];
        int score_count = 0;
        for (int i = 1; i < BC_MAX_PLAYERS; i++) {
            if (g_peers.peers[i].state >= PEER_LOBBY) {
                scores[i - 1] = g_peers.peers[i].score;
                if (i >= score_count + 1) score_count = i;
            } else {
                scores[i - 1] = 0;
            }
        }
        if (score_count > 0) {
            u8 score_buf[128];
            int slen = bc_build_score(score_buf, sizeof(score_buf),
                                       scores, score_count);
            if (slen > 0) {
                queue_reliable(peer_slot, score_buf, slen);
                LOG_DEBUG("handshake", "slot=%d sending Score (%d players)",
                          peer_slot, score_count);
            }
        }
    }

    /* Forward cached ObjCreateTeam for every already-spawned ship */
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        if (i == peer_slot) continue;
        bc_peer_t *other = &g_peers.peers[i];
        if (other->state >= PEER_LOBBY && other->spawn_len > 0) {
            queue_reliable(peer_slot, other->spawn_payload, other->spawn_len);
            LOG_DEBUG("handshake", "slot=%d forwarding spawn from slot %d (%d bytes)",
                      peer_slot, i, other->spawn_len);
        }
    }

    /* Send DeletePlayerUI (0x17) for each connected player slot so the
     * joining client clears any stale UI entries for those slots. */
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        if (i == peer_slot) continue;
        if (g_peers.peers[i].state >= PEER_LOBBY) {
            u8 del_ui[4];
            u8 gs = (u8)(i > 0 ? i - 1 : 0);
            int dlen = bc_delete_player_ui_build(del_ui, sizeof(del_ui), gs);
            if (dlen > 0)
                queue_reliable(peer_slot, del_ui, dlen);
        }
    }

    /* Flush immediately so the client gets 0x28+Settings+GameInit+join data
     * without waiting for the next 100ms tick. */
    flush_peer(peer_slot);

    /* Do NOT send NewPlayerInGame (0x2A) here -- that is a CLIENT-to-SERVER
     * message.  The client sends 0x2A after receiving Settings + GameInit.
     * When we receive it, we respond with MissionInit (see handle_game_message). */
}

/* Notify all other peers that a player has left, then remove them. */
static void handle_peer_disconnect(int slot)
{
    if (g_peers.peers[slot].state == PEER_EMPTY) return;

    g_stats.disconnects++;

    /* Update player record with disconnect time */
    for (int i = 0; i < g_stats.player_count; i++) {
        if (g_stats.players[i].disconnect_time == 0 &&
            g_stats.players[i].connect_time == g_peers.peers[slot].connect_time) {
            g_stats.players[i].disconnect_time = GetTickCount();
            break;
        }
    }

    char addr_str[32];
    bc_addr_to_string(&g_peers.peers[slot].addr, addr_str, sizeof(addr_str));

    /* Only send delete notifications if the peer had reached LOBBY state */
    if (g_peers.peers[slot].state >= PEER_LOBBY) {
        u8 payload[64];
        int len;

        /* 1. DestroyObject (0x14) -- remove ship from game world */
        if (g_peers.peers[slot].spawn_len > 0) {
            i32 ship_id = bc_make_ship_id(slot);
            len = bc_build_destroy_obj(payload, sizeof(payload), ship_id);
            if (len > 0) relay_to_others(slot, payload, len, true);
        }

        /* 2. DeletePlayerUI (0x17) -- remove from scoreboard */
        u8 game_slot = (u8)(slot > 0 ? slot - 1 : 0);
        len = bc_delete_player_ui_build(payload, sizeof(payload), game_slot);
        if (len > 0) relay_to_others(slot, payload, len, true);

        /* 3. DeletePlayerAnim (0x18) -- "Player X has left" notification */
        len = bc_delete_player_anim_build(payload, sizeof(payload),
                                           g_peers.peers[slot].name);
        if (len > 0) relay_to_others(slot, payload, len, true);

        LOG_DEBUG("net", "Sent disconnect notifications for slot %d "
                  "(DestroyObj+DeletePlayerUI+DeletePlayerAnim)", slot);
    }

    bc_peers_remove(&g_peers, slot);
    LOG_INFO("net", "Player removed: %s (slot %d), %d remaining",
             addr_str, slot, g_peers.count - 1);
}

static void handle_connect(const bc_addr_t *from, int len)
{
    char addr_str[32];
    bc_addr_to_string(from, addr_str, sizeof(addr_str));
    LOG_DEBUG("net", "handle_connect: from=%s", addr_str);

    int slot = bc_peers_find(&g_peers, from);
    if (slot >= 0) {
        LOG_WARN("net", "Duplicate connect from %s (slot %d)", addr_str, slot);
        return;
    }

    slot = bc_peers_add(&g_peers, from);
    if (slot < 0) {
        LOG_WARN("net", "Server full, sending BootPlayer to %s", addr_str);
        g_stats.boots_full++;
        u8 boot_payload[4];
        int boot_len = bc_bootplayer_build(boot_payload, sizeof(boot_payload),
                                            BC_BOOT_SERVER_FULL);
        if (boot_len > 0) {
            send_unreliable_direct(from, boot_payload, boot_len);
        }
        return;
    }

    g_peers.peers[slot].last_recv_time = GetTickCount();
    g_peers.peers[slot].connect_time = GetTickCount();
    LOG_INFO("net", "Player connected from %s -> slot %d (%d/%d)",
             addr_str, slot, g_peers.count - 1, g_info.maxplayers);

    /* Session stats: connection */
    g_stats.total_connections++;
    {
        u32 active = g_peers.count > 1 ? (u32)(g_peers.count - 1) : 0;
        if (active > g_stats.peak_players) g_stats.peak_players = active;
    }
    if (g_stats.player_count < 32) {
        player_record_t *rec = &g_stats.players[g_stats.player_count++];
        memset(rec, 0, sizeof(*rec));
        snprintf(rec->name, sizeof(rec->name), "slot %d", slot);
        rec->connect_time = GetTickCount();
    }

    /* Send Connect response + first ChecksumReq batched in one packet.
     * Stock dedi always batches these (msgs=2).  This reduces round-trip
     * latency and matches trace behavior exactly.
     *
     * Connect response (type 0x03):
     *   [0x03][0x06][0xC0][0x00][0x00][slot]
     *   flags/len = 0xC006 = reliable+priority, totalLen=6.
     *   Payload = 1 byte peer slot number (wire_slot = array index + 1).
     *
     * ChecksumReq round 0: wrapped in Reliable (0x32). */
    g_peers.peers[slot].state = PEER_CHECKSUMMING;
    g_peers.peers[slot].checksum_round = 0;
    {
        u8 pkt[BC_MAX_PACKET_SIZE];
        int pos = 0;

        /* Packet header */
        pkt[pos++] = BC_DIR_SERVER;        /* direction */
        pkt[pos++] = 2;                    /* msg_count = 2 */

        /* Message 0: Connect response */
        pkt[pos++] = BC_TRANSPORT_CONNECT; /* 0x03 */
        pkt[pos++] = 0x06;                /* totalLen = 6 */
        pkt[pos++] = 0xC0;                /* flags = reliable + priority */
        pkt[pos++] = 0x00;                /* seq hi */
        pkt[pos++] = 0x00;                /* seq lo */
        pkt[pos++] = (u8)(slot + 1);      /* wire slot */

        /* Message 1: Reliable-wrapped ChecksumReq round 0 */
        u8 cs_payload[256];
        int cs_len = bc_checksum_request_build(cs_payload, sizeof(cs_payload), 0);
        if (cs_len > 0) {
            u16 seq = g_peers.peers[slot].reliable_seq_out++;
            bc_reliable_add(&g_peers.peers[slot].reliable_out,
                            cs_payload, cs_len, seq, GetTickCount());
            int msg_total = 5 + cs_len;
            pkt[pos++] = BC_TRANSPORT_RELIABLE;
            pkt[pos++] = (u8)msg_total;
            pkt[pos++] = 0x80;                    /* reliable flags */
            pkt[pos++] = (u8)(seq & 0xFF);         /* counter → seqHi */
            pkt[pos++] = 0;                        /* seqLo always 0 */
            memcpy(pkt + pos, cs_payload, (size_t)cs_len);
            pos += cs_len;
            LOG_DEBUG("handshake", "slot=%d sending checksum request round 0",
                      slot);
        }

        /* Trace-log before encryption */
        {
            bc_packet_t trace;
            if (bc_transport_parse(pkt, pos, &trace))
                bc_log_packet_trace(&trace, slot, "SEND");
        }
        alby_cipher_encrypt(pkt, (size_t)pos);
        bc_socket_send(&g_socket, from, pkt, pos);
    }

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
            g_stats.boots_checksum++;
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
            g_stats.boots_checksum++;
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
        u8 payload[256];
        int plen = bc_checksum_request_final_build(payload, sizeof(payload));
        if (plen > 0) {
            queue_reliable(peer_slot, payload, plen);
            flush_peer(peer_slot);
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

/* Send a message to ALL peers (including the sender) reliably. */
static void send_to_all(const u8 *payload, int payload_len, bool reliable)
{
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        if (g_peers.peers[i].state < PEER_LOBBY) continue;
        if (reliable)
            queue_reliable(i, payload, payload_len);
        else
            queue_unreliable(i, payload, payload_len);
    }
}

/* Find the peer that owns an object_id. Returns slot or -1. */
static int find_peer_by_object(i32 object_id)
{
    int slot = bc_object_id_to_slot(object_id);
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return -1;
    if (!g_peers.peers[slot].has_ship) return -1;
    return slot;
}

/* Apply beam damage: compute, send Explosion, check kill. */
static void apply_beam_damage(int shooter_slot, int target_slot)
{
    bc_peer_t *shooter = &g_peers.peers[shooter_slot];
    bc_peer_t *target = &g_peers.peers[target_slot];
    if (!target->has_ship || !target->ship.alive) return;

    const bc_ship_class_t *shooter_cls =
        bc_registry_get_ship(&g_registry, shooter->class_index);
    const bc_ship_class_t *target_cls =
        bc_registry_get_ship(&g_registry, target->class_index);
    if (!shooter_cls || !target_cls) return;

    /* Find the first alive phaser subsystem and use its max_damage.
     * The beam_fire event's flags byte encodes the bank index. */
    f32 damage = 0.0f;
    for (int i = 0; i < shooter_cls->subsystem_count; i++) {
        const bc_subsystem_def_t *ss = &shooter_cls->subsystems[i];
        if (strcmp(ss->type, "phaser") == 0 || strcmp(ss->type, "pulse_weapon") == 0) {
            if (shooter->ship.subsystem_hp[i] > 0.0f) {
                damage = ss->max_damage;
                break;
            }
        }
    }
    if (damage <= 0.0f) return;

    /* Compute impact direction: shooter → target */
    bc_vec3_t impact_dir = bc_vec3_normalize(
        bc_vec3_sub(target->ship.pos, shooter->ship.pos));

    /* Apply damage server-side */
    bc_combat_apply_damage(&target->ship, target_cls, damage, impact_dir);

    /* Build and send Explosion to all clients */
    u8 expl[32];
    int elen = bc_build_explosion(expl, sizeof(expl),
                                   target->ship.object_id,
                                   impact_dir.x, impact_dir.y, impact_dir.z,
                                   damage, 1.0f);
    if (elen > 0) {
        send_to_all(expl, elen, true);
    }

    LOG_INFO("combat", "Server damage: %s -> %s, %.1f dmg (hull=%.1f)",
             peer_name(shooter_slot), peer_name(target_slot),
             damage, target->ship.hull_hp);

    /* Check for kill */
    if (!target->ship.alive) {
        LOG_INFO("combat", "%s destroyed by %s",
                 peer_name(target_slot), peer_name(shooter_slot));

        /* Send DestroyObject to all */
        u8 dest[8];
        int dlen = bc_build_destroy_obj(dest, sizeof(dest),
                                         target->ship.object_id);
        if (dlen > 0) send_to_all(dest, dlen, true);

        /* Update scores */
        u8 killer_game_slot = (u8)(shooter_slot > 0 ? shooter_slot - 1 : 0);
        u8 victim_game_slot = (u8)(target_slot > 0 ? target_slot - 1 : 0);
        shooter->score++;

        u8 sc[16];
        int slen = bc_build_score_change(sc, sizeof(sc),
                                          killer_game_slot, victim_game_slot,
                                          shooter->score);
        if (slen > 0) send_to_all(sc, slen, true);

        /* Clear victim ship state */
        target->has_ship = false;
        target->spawn_len = 0;

        /* Check frag limit */
        if (g_frag_limit > 0 && shooter->score >= g_frag_limit) {
            u8 eg[4];
            int eglen = bc_build_end_game(eg, sizeof(eg), killer_game_slot);
            if (eglen > 0) send_to_all(eg, eglen, true);
            LOG_INFO("game", "Frag limit reached by %s (%d kills)",
                     peer_name(shooter_slot), shooter->score);
        }
    }
}

/* Torpedo hit callback -- called from bc_torpedo_tick() */
static void torpedo_hit_callback(int shooter_slot, i32 target_id,
                                  f32 damage, f32 damage_radius,
                                  bc_vec3_t impact_pos,
                                  void *user_data)
{
    (void)damage_radius;
    (void)user_data;

    int target_slot = find_peer_by_object(target_id);
    if (target_slot < 0) return;

    bc_peer_t *shooter = &g_peers.peers[shooter_slot];
    bc_peer_t *target = &g_peers.peers[target_slot];
    if (!target->has_ship || !target->ship.alive) return;

    const bc_ship_class_t *target_cls =
        bc_registry_get_ship(&g_registry, target->class_index);
    if (!target_cls) return;

    /* Impact direction from torpedo position to target */
    bc_vec3_t impact_dir = bc_vec3_normalize(
        bc_vec3_sub(target->ship.pos, impact_pos));

    bc_combat_apply_damage(&target->ship, target_cls, damage, impact_dir);

    /* Send Explosion */
    u8 expl[32];
    int elen = bc_build_explosion(expl, sizeof(expl),
                                   target->ship.object_id,
                                   impact_dir.x, impact_dir.y, impact_dir.z,
                                   damage, damage_radius);
    if (elen > 0) send_to_all(expl, elen, true);

    LOG_INFO("combat", "Torpedo hit: slot %d -> %s, %.1f dmg (hull=%.1f)",
             shooter_slot, peer_name(target_slot),
             damage, target->ship.hull_hp);

    /* Check for kill */
    if (!target->ship.alive) {
        LOG_INFO("combat", "%s destroyed by torpedo from %s",
                 peer_name(target_slot), peer_name(shooter_slot));

        u8 dest[8];
        int dlen = bc_build_destroy_obj(dest, sizeof(dest),
                                         target->ship.object_id);
        if (dlen > 0) send_to_all(dest, dlen, true);

        u8 killer_game_slot = (u8)(shooter_slot > 0 ? shooter_slot - 1 : 0);
        u8 victim_game_slot = (u8)(target_slot > 0 ? target_slot - 1 : 0);
        shooter->score++;

        u8 sc[16];
        int slen = bc_build_score_change(sc, sizeof(sc),
                                          killer_game_slot, victim_game_slot,
                                          shooter->score);
        if (slen > 0) send_to_all(sc, slen, true);

        target->has_ship = false;
        target->spawn_len = 0;

        if (g_frag_limit > 0 && shooter->score >= g_frag_limit) {
            u8 eg[4];
            int eglen = bc_build_end_game(eg, sizeof(eg), killer_game_slot);
            if (eglen > 0) send_to_all(eg, eglen, true);
        }
    }
}

/* Torpedo target position callback -- called from bc_torpedo_tick() */
static bool torpedo_target_pos(i32 target_id, bc_vec3_t *out_pos,
                                void *user_data)
{
    (void)user_data;
    int slot = find_peer_by_object(target_id);
    if (slot < 0) return false;
    if (!g_peers.peers[slot].has_ship || !g_peers.peers[slot].ship.alive)
        return false;
    *out_pos = g_peers.peers[slot].ship.pos;
    return true;
}

/* Anti-cheat: check fire rate for a weapon.
 * Returns true if fire is allowed, false if rate-limited. */
static bool check_fire_rate(bc_peer_t *peer, u32 *last_fire, f32 min_interval_s)
{
    u32 now = GetTickCount();
    u32 min_interval_ms = (u32)(min_interval_s * 800.0f); /* 0.8x = 20% tolerance */

    if (*last_fire > 0 && (now - *last_fire) < min_interval_ms) {
        /* Violation */
        if (now - peer->violation_window_start > 30000) {
            peer->fire_violations = 0;
            peer->violation_window_start = now;
        }
        peer->fire_violations++;
        return false;
    }
    *last_fire = now;
    return true;
}

static void handle_game_message(int peer_slot, const bc_transport_msg_t *msg)
{
    if (msg->payload_len < 1) return;

    bc_peer_t *peer = &g_peers.peers[peer_slot];

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

    g_stats.opcodes_recv[opcode]++;

    LOG_DEBUG("game", "slot=%d dispatch opcode=0x%02X (%s) len=%d state=%d",
              peer_slot, opcode, name ? name : "?", payload_len, peer->state);

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
        if (g_registry_loaded && peer->has_ship) {
            const bc_ship_class_t *cls =
                bc_registry_get_ship(&g_registry, peer->class_index);
            if (cls && !bc_cloak_start(&peer->ship, cls)) {
                LOG_WARN("cheat", "slot=%d invalid cloak start (state=%d)",
                         peer_slot, peer->ship.cloak_state);
                break; /* Don't relay invalid cloak attempt */
            }
            LOG_DEBUG("game", "slot=%d starting cloak", peer_slot);
        }
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    case BC_OP_STOP_CLOAK:
        if (g_registry_loaded && peer->has_ship) {
            bc_cloak_stop(&peer->ship);
            LOG_DEBUG("game", "slot=%d stopping cloak", peer_slot);
        }
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    case BC_OP_START_WARP:
        if (g_registry_loaded && peer->has_ship) {
            /* Verify warp drive subsystem is alive */
            const bc_ship_class_t *cls =
                bc_registry_get_ship(&g_registry, peer->class_index);
            if (cls) {
                bool warp_alive = false;
                for (int si = 0; si < cls->subsystem_count; si++) {
                    if (strcmp(cls->subsystems[si].type, "warp_drive") == 0) {
                        warp_alive = (peer->ship.subsystem_hp[si] > 0.0f);
                        break;
                    }
                }
                if (!warp_alive) {
                    LOG_WARN("cheat", "slot=%d warp with dead drive, dropped",
                             peer_slot);
                    break;
                }
            }
        }
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    case BC_OP_REPAIR_PRIORITY:
    case BC_OP_SET_PHASER_LEVEL:
    case BC_OP_TORP_TYPE_CHANGE:
        relay_to_others(peer_slot, payload, payload_len, true);
        break;

    case BC_OP_TORPEDO_FIRE: {
        bc_torpedo_event_t ev;
        if (!bc_parse_torpedo_fire(payload, payload_len, &ev)) {
            LOG_WARN("combat", "slot=%d malformed TorpedoFire", peer_slot);
            break;
        }

        if (ev.has_target)
            LOG_INFO("combat", "%s fired torpedo -> %s (subsys=%d)",
                     object_owner_name(ev.shooter_id),
                     object_owner_name(ev.target_id),
                     ev.subsys_index);
        else
            LOG_INFO("combat", "%s fired torpedo (no lock)",
                     object_owner_name(ev.shooter_id));

        if (g_registry_loaded && peer->has_ship) {
            const bc_ship_class_t *cls =
                bc_registry_get_ship(&g_registry, peer->class_index);

            /* Anti-cheat: cannot fire while cloaked */
            if (cls && !bc_cloak_can_fire(&peer->ship)) {
                LOG_WARN("cheat", "slot=%d torpedo fire while cloaked, dropped",
                         peer_slot);
                break;
            }

            /* Anti-cheat: fire rate limiting (per tube) */
            if (cls) {
                int tube_idx = 0; /* use first tube as reference */
                f32 min_interval = 2.0f; /* default torpedo reload */
                for (int si = 0; si < cls->subsystem_count; si++) {
                    if (strcmp(cls->subsystems[si].type, "torpedo_tube") == 0) {
                        min_interval = cls->subsystems[si].reload_delay;
                        break;
                    }
                }
                if (tube_idx < BC_MAX_TORPEDO_TUBES &&
                    !check_fire_rate(peer, &peer->last_torpedo_time[tube_idx],
                                     min_interval)) {
                    LOG_WARN("cheat", "slot=%d torpedo fire rate exceeded", peer_slot);
                    if (peer->fire_violations >= 5) {
                        LOG_WARN("cheat", "slot=%d booted: excessive rapid fire", peer_slot);
                        u8 boot[4];
                        int blen = bc_bootplayer_build(boot, sizeof(boot), BC_BOOT_CHECKSUM);
                        if (blen > 0) queue_reliable(peer_slot, boot, blen);
                        handle_peer_disconnect(peer_slot);
                        return;
                    }
                    break;
                }
            }
        }

        /* Relay torpedo visual to all others */
        relay_to_others(peer_slot, payload, payload_len, true);

        /* Spawn server-side torpedo tracker for damage computation */
        if (g_registry_loaded && peer->has_ship) {
            /* Look up projectile stats from registry */
            const bc_projectile_def_t *proj =
                bc_registry_get_projectile(&g_registry, peer->ship.torpedo_type);
            if (proj) {
                bc_vec3_t vel_dir = bc_vec3_normalize(
                    (bc_vec3_t){ev.vel_x, ev.vel_y, ev.vel_z});
                f32 dmg_radius = proj->damage * proj->damage_radius_factor;

                bc_torpedo_spawn(&g_torpedoes,
                                  ev.shooter_id, peer_slot,
                                  ev.has_target ? ev.target_id : -1,
                                  peer->ship.pos, vel_dir, proj->launch_speed,
                                  proj->damage, dmg_radius,
                                  proj->lifetime, proj->guidance_lifetime,
                                  proj->max_angular_accel);
            }
        }
        break;
    }

    case BC_OP_BEAM_FIRE: {
        bc_beam_event_t ev;
        if (!bc_parse_beam_fire(payload, payload_len, &ev)) {
            LOG_WARN("combat", "slot=%d malformed BeamFire", peer_slot);
            break;
        }

        if (ev.has_target)
            LOG_INFO("combat", "%s fired beam -> %s",
                     object_owner_name(ev.shooter_id),
                     object_owner_name(ev.target_id));
        else
            LOG_INFO("combat", "%s fired beam (no target)",
                     object_owner_name(ev.shooter_id));

        if (g_registry_loaded && peer->has_ship) {
            const bc_ship_class_t *cls =
                bc_registry_get_ship(&g_registry, peer->class_index);

            /* Anti-cheat: cannot fire while cloaked */
            if (cls && !bc_cloak_can_fire(&peer->ship)) {
                LOG_WARN("cheat", "slot=%d beam fire while cloaked, dropped",
                         peer_slot);
                break;
            }

            /* Anti-cheat: fire rate limiting */
            if (cls) {
                f32 min_interval = 0.5f; /* default phaser recharge ~0.5s */
                for (int si = 0; si < cls->subsystem_count; si++) {
                    const bc_subsystem_def_t *ss = &cls->subsystems[si];
                    if (strcmp(ss->type, "phaser") == 0 ||
                        strcmp(ss->type, "pulse_weapon") == 0) {
                        if (ss->recharge_rate > 0.0f) {
                            min_interval = ss->max_charge / ss->recharge_rate;
                        }
                        break;
                    }
                }
                if (!check_fire_rate(peer, &peer->last_fire_time[0], min_interval)) {
                    LOG_WARN("cheat", "slot=%d beam fire rate exceeded", peer_slot);
                    if (peer->fire_violations >= 5) {
                        LOG_WARN("cheat", "slot=%d booted: excessive rapid fire", peer_slot);
                        u8 boot[4];
                        int blen = bc_bootplayer_build(boot, sizeof(boot), BC_BOOT_CHECKSUM);
                        if (blen > 0) queue_reliable(peer_slot, boot, blen);
                        handle_peer_disconnect(peer_slot);
                        return;
                    }
                    break;
                }
            }

            /* Anti-cheat: range plausibility */
            if (ev.has_target && cls) {
                int target_slot = find_peer_by_object(ev.target_id);
                if (target_slot >= 0) {
                    f32 dist = bc_vec3_dist(peer->ship.pos,
                                             g_peers.peers[target_slot].ship.pos);
                    f32 max_range = 0.0f;
                    for (int si = 0; si < cls->subsystem_count; si++) {
                        if (strcmp(cls->subsystems[si].type, "phaser") == 0 ||
                            strcmp(cls->subsystems[si].type, "pulse_weapon") == 0) {
                            max_range = cls->subsystems[si].max_damage_distance;
                            break;
                        }
                    }
                    f32 target_speed = g_peers.peers[target_slot].ship.speed;
                    if (max_range > 0.0f && dist > max_range + target_speed * 0.5f) {
                        LOG_WARN("cheat", "slot=%d beam out of range (%.0f > %.0f)",
                                 peer_slot, dist, max_range);
                        /* Relay visual but skip damage */
                        relay_to_others(peer_slot, payload, payload_len, true);
                        break;
                    }
                }
            }
        }

        /* Relay beam visual to all others */
        relay_to_others(peer_slot, payload, payload_len, true);

        /* Server-side damage computation */
        if (g_registry_loaded && peer->has_ship && ev.has_target) {
            int target_slot = find_peer_by_object(ev.target_id);
            if (target_slot >= 0) {
                apply_beam_damage(peer_slot, target_slot);
            }
        }
        break;
    }

    /* --- Explosion: server-authoritative = drop client's, relay-only = pass through --- */
    case BC_OP_EXPLOSION: {
        bc_explosion_event_t ev;
        if (bc_parse_explosion(payload, payload_len, &ev)) {
            LOG_INFO("combat", "Client explosion on %s's ship: %.1f damage, radius %.1f",
                     object_owner_name(ev.object_id), ev.damage, ev.radius);
        }
        if (g_registry_loaded) {
            /* Server is authoritative -- drop client-originated explosions */
            LOG_DEBUG("cheat", "slot=%d client Explosion suppressed (server authority)",
                      peer_slot);
        } else {
            relay_to_others(peer_slot, payload, payload_len, true);
        }
        break;
    }

    /* --- StateUpdate: track position + relay (strip server-authoritative flags) --- */
    case BC_OP_STATE_UPDATE: {
        if (g_registry_loaded && peer->has_ship) {
            bc_state_update_t su;
            if (bc_parse_state_update(payload, payload_len, &su)) {
                /* Track position */
                if (su.dirty & 0x01) {
                    peer->ship.pos.x = su.pos_x;
                    peer->ship.pos.y = su.pos_y;
                    peer->ship.pos.z = su.pos_z;
                }
                if (su.dirty & 0x04) {
                    peer->ship.fwd.x = su.fwd_x;
                    peer->ship.fwd.y = su.fwd_y;
                    peer->ship.fwd.z = su.fwd_z;
                }
                if (su.dirty & 0x08) {
                    peer->ship.up.x = su.up_x;
                    peer->ship.up.y = su.up_y;
                    peer->ship.up.z = su.up_z;
                }
                if (su.dirty & 0x10) {
                    peer->ship.speed = su.speed;
                    /* Speed validation: clamp to max */
                    const bc_ship_class_t *cls =
                        bc_registry_get_ship(&g_registry, peer->class_index);
                    if (cls && peer->ship.speed > cls->max_speed * 1.1f) {
                        LOG_WARN("cheat", "slot=%d speed %.1f > max %.1f, clamped",
                                 peer_slot, peer->ship.speed, cls->max_speed);
                        peer->ship.speed = cls->max_speed;
                    }
                }

                /* Strip server-authoritative flags (0x20 subsystem, 0x80 weapon) */
                if (su.dirty & (0x20 | 0x80)) {
                    /* Rebuild with stripped dirty flags.
                     * For simplicity, only strip if ONLY 0x20/0x80 are set.
                     * If mixed with movement flags, relay the original
                     * (client shouldn't mix these, but be safe). */
                    u8 client_flags = su.dirty & ~(u8)(0x20 | 0x80);
                    if (client_flags == 0) {
                        /* Entirely server-authoritative data, drop entirely */
                        LOG_DEBUG("cheat", "slot=%d StateUpdate 0x%02X suppressed",
                                  peer_slot, su.dirty);
                        break;
                    }
                    /* Mixed flags -- relay as-is (rare edge case) */
                }
            }
        }
        relay_to_others(peer_slot, payload, payload_len, false);
        break;
    }

    /* --- Object creation: relay + cache + init server ship state --- */
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
        /* Cache the spawn payload so we can forward it to late joiners */
        if (payload_len <= (int)sizeof(peer->spawn_payload)) {
            memcpy(peer->spawn_payload, payload, (size_t)payload_len);
            peer->spawn_len = payload_len;
        }

        /* Initialize server-side ship state from the ship blob */
        if (g_registry_loaded && opcode == BC_OP_OBJ_CREATE_TEAM &&
            payload_len >= 4) {
            /* Ship blob starts after [opcode:1][owner:1][team:1] */
            const u8 *blob = payload + 3;
            int blob_len = payload_len - 3;
            bc_ship_blob_header_t bhdr;
            if (bc_parse_ship_blob_header(blob, blob_len, &bhdr)) {
                int cidx = bc_registry_find_ship_index(&g_registry,
                                                        bhdr.species_id);
                if (cidx >= 0) {
                    const bc_ship_class_t *cls =
                        bc_registry_get_ship(&g_registry, cidx);
                    u8 team_id = hdr.has_team ? hdr.team_id : 0;
                    bc_ship_init(&peer->ship, cls, cidx,
                                  bhdr.object_id,
                                  (u8)peer_slot, team_id);
                    peer->ship.pos.x = bhdr.pos_x;
                    peer->ship.pos.y = bhdr.pos_y;
                    peer->ship.pos.z = bhdr.pos_z;
                    peer->class_index = cidx;
                    peer->has_ship = true;
                    peer->subsys_rr_idx = 0;
                    memset(peer->last_fire_time, 0, sizeof(peer->last_fire_time));
                    memset(peer->last_torpedo_time, 0, sizeof(peer->last_torpedo_time));
                    peer->fire_violations = 0;
                    peer->violation_window_start = 0;
                    LOG_INFO("game", "slot=%d ship initialized: %s (species=%d, hull=%.0f)",
                             peer_slot, cls->name, bhdr.species_id, cls->hull_hp);
                } else {
                    LOG_WARN("game", "slot=%d unknown species_id %d, no ship state",
                             peer_slot, bhdr.species_id);
                }
            }
        }

        relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

    /* --- Object destruction: server-authoritative = drop, relay-only = pass through --- */
    case BC_OP_DESTROY_OBJ: {
        bc_destroy_event_t ev;
        if (bc_parse_destroy_obj(payload, payload_len, &ev)) {
            LOG_INFO("combat", "Client DestroyObj: %s's ship",
                     object_owner_name(ev.object_id));
        }
        if (g_registry_loaded) {
            LOG_DEBUG("cheat", "slot=%d client DestroyObj suppressed (server authority)",
                      peer_slot);
        } else {
            relay_to_others(peer_slot, payload, payload_len, true);
        }
        break;
    }

    /* --- NewPlayerInGame (C->S, triggers MissionInit) --- */
    case BC_OP_NEW_PLAYER_IN_GAME: {
        LOG_INFO("handshake", "slot=%d sent NewPlayerInGame", peer_slot);
        /* Relay to all other peers so they know about the new player */
        relay_to_others(peer_slot, payload, payload_len, true);
        /* Respond with MissionInit (0x35) -- tells client which star system
         * to load and what the match rules are. */
        u8 mi[32];
        int mi_len = bc_mission_init_build(mi, sizeof(mi),
                                            g_system_index, g_info.maxplayers,
                                            g_time_limit, g_frag_limit);
        if (mi_len > 0) {
            LOG_DEBUG("handshake", "slot=%d sending MissionInit (system=%d)",
                      peer_slot, g_system_index);
            queue_reliable(peer_slot, mi, mi_len);
            flush_peer(peer_slot);
        }
        break;
    }

    /* --- Host message (C->S only, not relayed) --- */
    case BC_OP_HOST_MSG:
        LOG_DEBUG("game", "slot=%d host message len=%d", peer_slot, payload_len);
        break;

    /* --- Collision effect: server computes damage when authoritative --- */
    case BC_OP_COLLISION_EFFECT: {
        LOG_DEBUG("game", "slot=%d collision effect len=%d", peer_slot, payload_len);
        if (g_registry_loaded && g_collision_dmg) {
            /* Parse: [0x15][obj1:i32][obj2:i32][impact_data...]
             * Apply collision damage to both ships based on mass * relative speed. */
            if (payload_len >= 9) {
                bc_buffer_t cbuf;
                bc_buf_init(&cbuf, (u8 *)payload, (size_t)payload_len);
                u8 cop;
                i32 obj1, obj2;
                bc_buf_read_u8(&cbuf, &cop);
                if (bc_buf_read_i32(&cbuf, &obj1) && bc_buf_read_i32(&cbuf, &obj2)) {
                    int s1 = find_peer_by_object(obj1);
                    int s2 = find_peer_by_object(obj2);
                    if (s1 >= 0 && s2 >= 0) {
                        bc_peer_t *p1 = &g_peers.peers[s1];
                        bc_peer_t *p2 = &g_peers.peers[s2];
                        const bc_ship_class_t *c1 =
                            bc_registry_get_ship(&g_registry, p1->class_index);
                        const bc_ship_class_t *c2 =
                            bc_registry_get_ship(&g_registry, p2->class_index);
                        if (c1 && c2 && p1->ship.alive && p2->ship.alive) {
                            f32 rel_speed = fabsf(p1->ship.speed - p2->ship.speed);
                            if (rel_speed < 1.0f) rel_speed = 1.0f;
                            f32 dmg1 = c2->mass * rel_speed * 0.001f;
                            f32 dmg2 = c1->mass * rel_speed * 0.001f;

                            bc_vec3_t dir12 = bc_vec3_normalize(
                                bc_vec3_sub(p2->ship.pos, p1->ship.pos));
                            bc_vec3_t dir21 = bc_vec3_scale(dir12, -1.0f);

                            bc_combat_apply_damage(&p1->ship, c1, dmg1, dir21);
                            bc_combat_apply_damage(&p2->ship, c2, dmg2, dir12);

                            /* Send explosions */
                            u8 expl[32];
                            int elen;
                            elen = bc_build_explosion(expl, sizeof(expl),
                                                       p1->ship.object_id,
                                                       dir21.x, dir21.y, dir21.z,
                                                       dmg1, 1.0f);
                            if (elen > 0) send_to_all(expl, elen, true);
                            elen = bc_build_explosion(expl, sizeof(expl),
                                                       p2->ship.object_id,
                                                       dir12.x, dir12.y, dir12.z,
                                                       dmg2, 1.0f);
                            if (elen > 0) send_to_all(expl, elen, true);

                            LOG_INFO("combat", "Collision: %s (%.1f dmg) <-> %s (%.1f dmg)",
                                     peer_name(s1), dmg1, peer_name(s2), dmg2);

                            /* Check kills (collision can kill both!) */
                            /* Note: no score awarded for collision kills */
                            if (!p1->ship.alive) {
                                u8 dest[8];
                                int dlen = bc_build_destroy_obj(dest, sizeof(dest),
                                                                 p1->ship.object_id);
                                if (dlen > 0) send_to_all(dest, dlen, true);
                                p1->has_ship = false;
                                p1->spawn_len = 0;
                                LOG_INFO("combat", "%s destroyed in collision",
                                         peer_name(s1));
                            }
                            if (!p2->ship.alive) {
                                u8 dest[8];
                                int dlen = bc_build_destroy_obj(dest, sizeof(dest),
                                                                 p2->ship.object_id);
                                if (dlen > 0) send_to_all(dest, dlen, true);
                                p2->has_ship = false;
                                p2->spawn_len = 0;
                                LOG_INFO("combat", "%s destroyed in collision",
                                         peer_name(s2));
                            }
                        }
                    }
                }
            }
            /* Do NOT relay client's CollisionEffect -- server generated its own damage */
        } else {
            relay_to_others(peer_slot, payload, payload_len, true);
        }
        break;
    }

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

    /* Decrypt (byte 0 = direction flag, skipped by cipher) */
    alby_cipher_decrypt(data, (size_t)len);

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

    /* Trace-log incoming packet after decryption */
    bc_log_packet_trace(&pkt, slot, "RECV");

    /* Handle connection requests (direction 0xFF) */
    if (pkt.direction == BC_DIR_INIT) {
        if (slot < 0) {
            /* Unknown peer with init direction -- new connection */
            handle_connect(from, len);
            return;
        }
        /* Known peer still using init direction (hasn't received ConnectAck yet).
         * Check if this is a Connect retry or a regular message (e.g. ACK).
         * Connect retries have transport type 0x03; other messages should
         * be processed normally. */
        bool has_connect = false;
        for (int i = 0; i < pkt.msg_count; i++) {
            if (pkt.msgs[i].type == BC_TRANSPORT_CONNECT) {
                has_connect = true;
                break;
            }
        }
        if (has_connect) {
            char dup_addr[32];
            bc_addr_to_string(from, dup_addr, sizeof(dup_addr));
            LOG_WARN("net", "Duplicate connect from %s (slot %d), resending Connect response",
                     dup_addr, slot);
            /* Resend Connect response with same wire slot */
            u8 resp[8];
            resp[0] = BC_DIR_SERVER;
            resp[1] = 1;
            resp[2] = BC_TRANSPORT_CONNECT;
            resp[3] = 0x06;
            resp[4] = 0xC0;
            resp[5] = 0x00;
            resp[6] = 0x00;
            resp[7] = (u8)(slot + 1);  /* wire_slot = array index + 1 */
            {
                bc_packet_t trace;
                if (bc_transport_parse(resp, (int)sizeof(resp), &trace))
                    bc_log_packet_trace(&trace, slot, "SEND");
            }
            alby_cipher_encrypt(resp, sizeof(resp));
            bc_socket_send(&g_socket, from, resp, sizeof(resp));
            return;
        }
        /* Fall through to process ACKs and other messages normally */
    }

    if (slot < 0) return;  /* Unknown peer, ignore */

    /* Validate direction byte: client's direction = wire_slot = slot + 1.
     * Init direction (0xFF) is acceptable during early handshake. */
    u8 expected_dir = (u8)(slot + 1);
    if (pkt.direction != expected_dir && pkt.direction != BC_DIR_INIT) {
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

        /* ConnectACK from a connected client = graceful disconnect signal.
         * The client sends type 0x05 when the user presses Escape / quits. */
        if (msg->type == BC_TRANSPORT_CONNECT_ACK) {
            LOG_INFO("net", "slot=%d graceful disconnect (ConnectACK)", slot);
            handle_peer_disconnect(slot);
            return;
        }

        /* Stale Connect retransmissions from a peer that's already connected.
         * These leak through when the client hasn't received our Connect
         * response yet.  Safe to ignore. */
        if (msg->type == BC_TRANSPORT_CONNECT ||
            msg->type == BC_TRANSPORT_CONNECT_DATA) {
            continue;
        }

        /* ACK incoming reliable messages.  Stock dedi ACKs every client
         * reliable immediately, batched with its next outgoing message.
         * This also drives the client's retransmit logic.
         * Only ACK messages with the reliable flag (0x80) -- type 0x32
         * with flags=0x00 is unreliable data (no seq, no ACK needed). */
        if (msg->type == BC_TRANSPORT_RELIABLE && (msg->flags & 0x80)) {
            bc_outbox_add_ack(&g_peers.peers[slot].outbox, msg->seq, 0x00);
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
                    /* Update player record with real name */
                    for (int r = 0; r < g_stats.player_count; r++) {
                        if (g_stats.players[r].connect_time == peer->connect_time) {
                            snprintf(g_stats.players[r].name,
                                     sizeof(g_stats.players[r].name),
                                     "%s", peer->name);
                            break;
                        }
                    }
                }
                /* Cache the raw keepalive payload so we can echo it back.
                 * Stock dedi mirrors the client's identity data in its
                 * keepalive responses (22 bytes, same format). */
                if (msg->payload_len <= (int)sizeof(peer->keepalive_data)) {
                    memcpy(peer->keepalive_data, msg->payload,
                           (size_t)msg->payload_len);
                    peer->keepalive_len = msg->payload_len;
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
        "  --data <path>      Ship data registry JSON (default: data/vanilla-1.1.json)\n"
        "  --manifest <path>  Hash manifest JSON (e.g. manifests/vanilla-1.1.json)\n"
        "  --master <h:p>     Master server address (repeatable; replaces defaults)\n"
        "  --no-master        Disable all master server heartbeating\n"
        "  --log-level <lvl>  Log verbosity: quiet|error|warn|info|debug|trace (default: info)\n"
        "  --log-file <path>  Write log to this file (default: openbc-YYYYMMDD-HHMMSS.log)\n"
        "  --no-log-file      Disable disk logging entirely\n"
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

/* Format a millisecond duration as "Xh Ym Zs" into buf. */
static void format_duration(u32 ms, char *buf, int bufsize)
{
    u32 secs = ms / 1000;
    u32 h = secs / 3600;
    u32 m = (secs % 3600) / 60;
    u32 s = secs % 60;
    if (h > 0)
        snprintf(buf, (size_t)bufsize, "%uh %um %us", h, m, s);
    else if (m > 0)
        snprintf(buf, (size_t)bufsize, "%um %us", m, s);
    else
        snprintf(buf, (size_t)bufsize, "%us", s);
}

/* Format a ms offset from session start as "H:MM:SS" into buf. */
static void format_time_offset(u32 offset_ms, char *buf, int bufsize)
{
    u32 secs = offset_ms / 1000;
    u32 h = secs / 3600;
    u32 m = (secs % 3600) / 60;
    u32 s = secs % 60;
    snprintf(buf, (size_t)bufsize, "%u:%02u:%02u", h, m, s);
}

static void log_session_summary(void)
{
    u32 now = GetTickCount();
    u32 elapsed = now - g_stats.start_time;
    char dur[32];
    format_duration(elapsed, dur, sizeof(dur));

    LOG_INFO("summary", "=== Session Summary ===");
    LOG_INFO("summary", "  Duration: %s", dur);
    LOG_INFO("summary", "  Connections: %u total, %u peak concurrent",
             g_stats.total_connections, g_stats.peak_players);
    LOG_INFO("summary", "  Disconnects: %u (%u timeout)",
             g_stats.disconnects, g_stats.timeouts);
    LOG_INFO("summary", "  Boots: %u (server full), %u (checksum fail)",
             g_stats.boots_full, g_stats.boots_checksum);

    /* Player history */
    if (g_stats.player_count > 0) {
        LOG_INFO("summary", "");
        LOG_INFO("summary", "  Players:");
        for (int i = 0; i < g_stats.player_count; i++) {
            player_record_t *p = &g_stats.players[i];
            char t_join[16], t_leave[16];
            format_time_offset(p->connect_time - g_stats.start_time,
                               t_join, sizeof(t_join));
            if (p->disconnect_time != 0)
                format_time_offset(p->disconnect_time - g_stats.start_time,
                                   t_leave, sizeof(t_leave));
            else
                snprintf(t_leave, sizeof(t_leave), "(active)");
            LOG_INFO("summary", "    %-20s %s - %s", p->name, t_join, t_leave);
        }
    }

    /* Opcode table -- collect non-zero entries and sort by count desc */
    typedef struct { int opcode; u32 count; } opcode_entry_t;
    opcode_entry_t entries[256];
    int entry_count = 0;
    for (int i = 0; i < 256; i++) {
        if (g_stats.opcodes_recv[i] > 0) {
            entries[entry_count].opcode = i;
            entries[entry_count].count = g_stats.opcodes_recv[i];
            entry_count++;
        }
    }
    /* Insertion sort descending by count */
    for (int i = 1; i < entry_count; i++) {
        opcode_entry_t tmp = entries[i];
        int j = i - 1;
        while (j >= 0 && entries[j].count < tmp.count) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = tmp;
    }
    if (entry_count > 0) {
        LOG_INFO("summary", "");
        LOG_INFO("summary", "  Opcodes received (client -> server):");
        for (int i = 0; i < entry_count; i++) {
            const char *oname = bc_opcode_name(entries[i].opcode);
            if (oname)
                LOG_INFO("summary", "    %-20s %u", oname, entries[i].count);
            else
                LOG_INFO("summary", "    0x%02X                 %u",
                         entries[i].opcode, entries[i].count);
        }
    }

    /* Network stats */
    if (g_stats.gamespy_queries > 0 || g_stats.reliable_retransmits > 0) {
        LOG_INFO("summary", "");
        LOG_INFO("summary", "  Network:");
        if (g_stats.gamespy_queries > 0)
            LOG_INFO("summary", "    GameSpy queries: %u",
                     g_stats.gamespy_queries);
        if (g_stats.reliable_retransmits > 0)
            LOG_INFO("summary", "    Reliable retransmits: %u",
                     g_stats.reliable_retransmits);
    }

    /* Master server status */
    if (g_masters.count > 0) {
        int verified = 0;
        for (int i = 0; i < g_masters.count; i++)
            if (g_masters.entries[i].verified) verified++;
        LOG_INFO("summary", "");
        LOG_INFO("summary", "  Master servers: %d/%d registered",
                 verified, g_masters.count);
        for (int i = 0; i < g_masters.count; i++) {
            if (g_masters.entries[i].verified)
                LOG_INFO("summary", "    + %s",
                         g_masters.entries[i].hostname);
        }
    }

    LOG_INFO("summary", "========================");
}

int main(int argc, char **argv)
{
    /* Defaults */
    u16 port = BC_DEFAULT_PORT;
    const char *name = "OpenBC Server";
    const char *map = "Multiplayer.Episode.Mission1.Mission1";
    int max_players = BC_MAX_PLAYERS;
    const char *manifest_path = NULL;
    const char *data_path = "data/vanilla-1.1.json";
    const char *user_masters[BC_MAX_MASTERS];
    int user_master_count = 0;
    bool no_master = false;
    bc_log_level_t log_level = LOG_INFO;
    const char *log_file_path = NULL;
    bool no_log_file = false;

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
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data_path = argv[++i];
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
        } else if (strcmp(argv[i], "--no-log-file") == 0) {
            no_log_file = true;
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

    /* Generate default log file name if none specified and not disabled.
     * Format: openbc-YYYYMMDD-HHMMSS.log (one file per session). */
    if (!log_file_path && !no_log_file) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        static char default_log[64];
        snprintf(default_log, sizeof(default_log),
                 "openbc-%04d%02d%02d-%02d%02d%02d.log",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
        log_file_path = default_log;
    }

    /* Initialize logging (before anything that uses LOG_*) */
    bc_log_init(log_level, log_file_path);

    /* Initialize session stats */
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.start_time = GetTickCount();

    /* Load manifest.
     * If --manifest was given, use that path.  Otherwise, scan manifests/
     * for .json files -- if exactly one exists, auto-load it. */
    if (!manifest_path) {
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA("manifests\\*.json", &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            static char auto_path[MAX_PATH];
            int json_count = 0;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    if (json_count == 0)
                        snprintf(auto_path, sizeof(auto_path),
                                 "manifests/%s", fd.cFileName);
                    json_count++;
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);

            if (json_count == 1) {
                manifest_path = auto_path;
                LOG_INFO("init", "Auto-detected manifest: %s", manifest_path);
            }
        }
    }

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

    /* Load ship data registry for server-authoritative damage */
    memset(&g_registry, 0, sizeof(g_registry));
    bc_torpedo_mgr_init(&g_torpedoes);
    if (data_path) {
        if (bc_registry_load(&g_registry, data_path)) {
            g_registry_loaded = true;
            LOG_INFO("init", "Ship registry loaded: %d ships, %d projectiles from %s",
                     g_registry.ship_count, g_registry.projectile_count, data_path);
        } else {
            LOG_WARN("init", "Failed to load ship registry: %s", data_path);
            LOG_WARN("init", "  Running in relay-only mode (no damage authority)");
        }
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

    /* Reserve slot 0 for the dedicated server itself.
     * The stock BC dedi creates a "Dedicated Server" pseudo-player at slot 0
     * that doesn't count as a joined player.  This ensures joining players
     * start at slot 1 (wire_slot=2, direction=0x02), matching stock behavior. */
    {
        bc_peer_t *dedi = &g_peers.peers[0];
        dedi->state = PEER_LOBBY;  /* Always "connected" */
        snprintf(dedi->name, sizeof(dedi->name), "Dedicated Server");
        g_peers.count++;
    }

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
    if (g_registry_loaded) {
        printf("Damage authority: server (%d ships, %d projectiles)\n",
               g_registry.ship_count, g_registry.projectile_count);
    } else {
        printf("Damage authority: client (relay-only, no registry)\n");
    }
    if (log_file_path)
        printf("Log file: %s\n", log_file_path);
    if (g_masters.count > 0) {
        int verified = 0;
        for (int i = 0; i < g_masters.count; i++)
            if (g_masters.entries[i].verified) verified++;
        printf("Master servers: %d/%d registered\n",
               verified, g_masters.count);
        for (int i = 0; i < g_masters.count; i++) {
            if (g_masters.entries[i].verified)
                printf("  + %s\n", g_masters.entries[i].hostname);
        }
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
                /* Retransmit unACKed reliable messages (skip slot 0 = dedi) */
                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
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
                        g_stats.reliable_retransmits++;
                        bc_reliable_entry_t *e = &peer->reliable_out.entries[idx];
                        u8 pkt[BC_MAX_PACKET_SIZE];
                        int len = bc_transport_build_reliable(
                            pkt, sizeof(pkt), e->payload, e->payload_len, e->seq);
                        if (len > 0) {
                            bc_packet_t trace;
                            if (bc_transport_parse(pkt, len, &trace))
                                bc_log_packet_trace(&trace, i, "RTXM");
                            alby_cipher_encrypt(pkt, (size_t)len);
                            bc_socket_send(&g_socket, &peer->addr, pkt, len);
                        }
                    }
                }

                /* Timeout stale peers (skip slot 0 = dedi) */
                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    if (g_peers.peers[i].state == PEER_EMPTY) continue;
                    if (now - g_peers.peers[i].last_recv_time > 30000) {
                        g_stats.timeouts++;
                        LOG_INFO("net", "Peer slot %d timed out (no packets)", i);
                        handle_peer_disconnect(i);
                    }
                }

                /* Master server heartbeat */
                bc_master_tick(&g_masters, &g_socket, now);
            }

            /* === Simulation tick (every 100ms when registry loaded) === */
            if (g_registry_loaded) {
                f32 dt = (f32)(now - last_tick) / 1000.0f;

                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *p = &g_peers.peers[i];
                    if (!p->has_ship || !p->ship.alive) continue;

                    const bc_ship_class_t *cls =
                        bc_registry_get_ship(&g_registry, p->class_index);
                    if (!cls) continue;

                    /* Shield recharge */
                    bc_combat_shield_tick(&p->ship, cls, dt);

                    /* Phaser charge + torpedo cooldown */
                    bc_combat_charge_tick(&p->ship, cls, 1.0f, dt);
                    bc_combat_torpedo_tick(&p->ship, cls, dt);

                    /* Cloak state machine */
                    bc_cloak_tick(&p->ship, dt);

                    /* Repair */
                    bc_repair_tick(&p->ship, cls, dt);
                    bc_repair_auto_queue(&p->ship, cls);
                }

                /* Torpedo tracker tick */
                if (g_torpedoes.count > 0) {
                    bc_torpedo_tick(&g_torpedoes, dt, 5.0f,
                                    torpedo_target_pos,
                                    torpedo_hit_callback, NULL);
                }
            }

            /* Health broadcast: every 5 ticks (~500ms), send 0x20 StateUpdate */
            if (g_registry_loaded && (tick_counter % 5 == 0)) {
                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *p = &g_peers.peers[i];
                    if (!p->has_ship || !p->ship.alive) continue;

                    const bc_ship_class_t *cls =
                        bc_registry_get_ship(&g_registry, p->class_index);
                    if (!cls) continue;

                    u8 hbuf[128];
                    int batch = 14;
                    int hlen = bc_ship_build_health_update(
                        &p->ship, cls, g_game_time,
                        p->subsys_rr_idx, batch, hbuf, sizeof(hbuf));
                    if (hlen > 0) {
                        for (int j = 1; j < BC_MAX_PLAYERS; j++) {
                            if (g_peers.peers[j].state >= PEER_LOBBY)
                                queue_unreliable(j, hbuf, hlen);
                        }
                    }
                    int count = cls->subsystem_count;
                    if (count > 0)
                        p->subsys_rr_idx = (u8)(((int)p->subsys_rr_idx + batch) % count);
                }
            }

            /* Every 10 ticks (~1 second): send keepalive to all active peers.
             * Stock dedi echoes the client's identity data (22 bytes) back
             * instead of sending a minimal [0x00][0x02] keepalive. */
            if (tick_counter % 10 == 0) {
                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *peer = &g_peers.peers[i];
                    if (peer->state < PEER_LOBBY) continue;
                    if (peer->keepalive_len > 0) {
                        bc_outbox_add_unreliable(&peer->outbox,
                                                  peer->keepalive_data,
                                                  peer->keepalive_len);
                    } else {
                        bc_outbox_add_keepalive(&peer->outbox);
                    }
                }
            }

            /* Flush all peer outboxes (skip slot 0 = dedi) */
            for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                if (g_peers.peers[i].state == PEER_EMPTY) continue;
                flush_peer(i);
            }

            last_tick = now;
        }

        /* Don't burn CPU -- sleep 1ms between polls */
        Sleep(1);
    }

    LOG_INFO("shutdown", "Shutting down...");

    /* Log session summary before tearing down */
    log_session_summary();

    /* Flush all pending outbox data before sending shutdown (skip slot 0 = dedi) */
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        if (g_peers.peers[i].state == PEER_EMPTY) continue;
        flush_peer(i);
    }

    /* Send ConnectAck shutdown notification to all connected peers.
     * Real BC server sends ConnectAck (0x05) to each peer on shutdown,
     * NOT BootPlayer or DeletePlayer. (skip slot 0 = dedi) */
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        bc_peer_t *peer = &g_peers.peers[i];
        if (peer->state == PEER_EMPTY) continue;

        u8 pkt[16];
        int len = bc_transport_build_shutdown_notify(
            pkt, sizeof(pkt), (u8)(i + 1), peer->addr.ip);
        if (len > 0) {
            bc_packet_t trace;
            if (bc_transport_parse(pkt, len, &trace))
                bc_log_packet_trace(&trace, i, "SEND");
            alby_cipher_encrypt(pkt, (size_t)len);
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
