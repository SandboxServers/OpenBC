#include "openbc/server_state.h"
#include "openbc/server_send.h"
#include "openbc/server_handshake.h"
#include "openbc/server_dispatch.h"
#include "openbc/transport.h"
#include "openbc/cipher.h"
#include "openbc/opcodes.h"
#include "openbc/handshake.h"
#include "openbc/gamespy.h"
#include "openbc/game_events.h"
#include "openbc/game_builders.h"
#include "openbc/player_ids.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/ship_power.h"
#include "openbc/combat.h"
#include "openbc/movement.h"
#include "openbc/torpedo_tracker.h"
#include "openbc/reliable.h"
#include "openbc/master.h"
#include "openbc/log.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#  include <windows.h>
#endif

/* Global auto-increment counter for subsystem object IDs (PythonEvent).
 * Each subsystem on every ship gets a sequential ID from this counter.
 *
 * Must match the client's auto-increment counter value at the time it
 * creates subsystem objects.  The client's counter starts at 1 but
 * advances through scene objects created during map loading (skybox,
 * asteroids, etc.) before any ship subsystems are constructed.
 *
 * Calibrated from stock dedi trace: repair subsystem has ID 0x1E (30)
 * and is the 15th subsystem assigned (counter + 14), so counter = 16. */
i32 g_script_obj_counter = 16;

/* --- GameSpy query handler --- */

void bc_handle_gamespy(bc_socket_t *sock, const bc_addr_t *from,
                       const u8 *data, int len)
{
    char addr_str[32];
    bc_addr_to_string(from, addr_str, sizeof(addr_str));
    LOG_DEBUG("gamespy", "Query from %s: %.*s", addr_str, len, (const char *)data);

    g_info.numplayers = g_peers.count > 0 ? g_peers.count - 1 : 0; /* exclude dedi slot */

    /* Rebuild player list: player_0 = "Dedicated Server" (always),
     * then one entry per connected human player. */
    g_info.player_count = 1;  /* slot 0 = dedi, already set at init */
    for (int i = 1; i < BC_MAX_PLAYERS && g_info.player_count < 8; i++) {
        if (g_peers.peers[i].state != PEER_EMPTY) {
            snprintf(g_info.player_names[g_info.player_count],
                     sizeof(g_info.player_names[0]),
                     "%s", g_peers.peers[i].name);
            g_info.player_count++;
        }
    }

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

        /* Check if this is a master server status-checking us */
        const char *listed = bc_master_record_status_check(&g_masters, from);
        if (listed) {
            LOG_INFO("master", "Listed on %s (status check)", listed);
        } else if (bc_master_is_from_master(&g_masters, from)) {
            LOG_DEBUG("master", "Status check from known master %s", addr_str);
        } else {
            LOG_DEBUG("gamespy", "Response to %s (%d bytes, sent=%d)",
                      addr_str, resp_len, sent);
        }
        (void)sent;
    }
}

/* --- Static combat/utility helpers --- */

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

/* Resolve an object ID to its owning player's name.
 * bc_object_id_to_slot() returns game_slot (0-based), but peers[]
 * uses peer_slot (1-based for humans).  game_slot 0 = peer_slot 1. */
static const char *object_owner_name(i32 object_id)
{
    int game_slot = bc_object_id_to_slot(object_id);
    if (game_slot < 0) return "???";
    int peer_slot = game_slot + 1;
    if (peer_slot >= BC_MAX_PLAYERS) return "???";
    return peer_name(peer_slot);
}

/* Find the peer that owns an object_id. Returns peer_slot or -1.
 * bc_object_id_to_slot() returns game_slot (0-based from Settings).
 * peer_slot = game_slot + 1 because peers[0] is the dedi server. */
int find_peer_by_object(i32 object_id)
{
    int game_slot = bc_object_id_to_slot(object_id);
    if (game_slot < 0) return -1;
    int peer_slot = game_slot + 1;
    if (peer_slot >= BC_MAX_PLAYERS) return -1;
    if (!g_peers.peers[peer_slot].has_ship) return -1;
    return peer_slot;
}

f32 bc_powered_efficiency(const bc_ship_state_t *ship,
                          const bc_ship_class_t *cls,
                          const char *child_type)
{
    const bc_ss_list_t *sl = &cls->ser_list;
    f32 min_eff = 1.0f;
    bool found = false;
    for (int i = 0; i < sl->count; i++) {
        const bc_ss_entry_t *e = &sl->entries[i];
        if (e->format != BC_SS_FORMAT_POWERED) continue;
        /* Check children for matching type */
        for (int c = 0; c < e->child_count; c++) {
            int ci = e->child_hp_index[c];
            if (ci >= 0 && ci < cls->subsystem_count &&
                strcmp(cls->subsystems[ci].type, child_type) == 0) {
                if (!found || ship->efficiency[i] < min_eff)
                    min_eff = ship->efficiency[i];
                found = true;
                break;
            }
        }
        /* Also check the entry itself (for childless powered entries) */
        if (!found && e->child_count == 0 &&
            e->hp_index >= 0 && e->hp_index < cls->subsystem_count &&
            strcmp(cls->subsystems[e->hp_index].type, child_type) == 0) {
            min_eff = ship->efficiency[i];
            found = true;
        }
    }
    return min_eff;
}

/* Send an immediate subsystem health update (flag 0x20) for a ship to all
 * clients.  Called after server-authoritative damage (collisions, beams,
 * torpedoes) so clients see the HP change without waiting for the periodic
 * health broadcast tick.
 *
 * Does NOT advance the round-robin cursor (subsys_rr_idx).  The periodic
 * health tick in main.c owns cursor advancement.  If damage handlers also
 * advanced it, the periodic cycle would be disrupted -- entries would be
 * sent out of cadence and the client would see flickering health bars. */
static void send_health_update_immediate(int target_slot)
{
    bc_peer_t *target = &g_peers.peers[target_slot];
    if (!target->has_ship || !target->ship.alive) return;

    const bc_ship_class_t *cls =
        bc_registry_get_ship(&g_registry, target->class_index);
    if (!cls) return;

    /* Send from current round-robin position; cursor is NOT advanced.
     * Owner gets is_own_ship=true (no power_pct in Powered entries),
     * remote observers get is_own_ship=false (with power_pct). */
    u8 hbuf_own[128], hbuf_rmt[128];
    u8 next_own, next_rmt;
    int hlen_own = bc_ship_build_health_update(
        &target->ship, cls, g_game_time,
        target->subsys_rr_idx, &next_own, true,
        hbuf_own, sizeof(hbuf_own));
    int hlen_rmt = bc_ship_build_health_update(
        &target->ship, cls, g_game_time,
        target->subsys_rr_idx, &next_rmt, false,
        hbuf_rmt, sizeof(hbuf_rmt));

    for (int j = 1; j < BC_MAX_PLAYERS; j++) {
        if (g_peers.peers[j].state < PEER_LOBBY) continue;
        if (j == target_slot && hlen_own > 0) {
            bc_queue_unreliable(j, hbuf_own, hlen_own);
        } else if (j != target_slot && hlen_rmt > 0) {
            bc_queue_unreliable(j, hbuf_rmt, hlen_rmt);
        }
    }

    LOG_DEBUG("health", "slot=%d immediate health (rr=%d)",
              target_slot, target->subsys_rr_idx);
    /* NOTE: subsys_rr_idx is NOT updated -- periodic tick owns the cursor */
}

/* Snapshot pre-damage HP, compare after, generate ADD_TO_REPAIR_LIST PythonEvents
 * for each newly-damaged subsystem.  This drives the client's repair queue UI,
 * damage VFX/SFX, and subsystem damage indicators. */
static void generate_damage_events(int target_slot,
                                    const f32 *hp_before,
                                    const bc_ship_class_t *cls)
{
    bc_peer_t *target = &g_peers.peers[target_slot];
    bc_ship_state_t *ship = &target->ship;

    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (ship->subsystem_hp[i] < hp_before[i] &&
            ship->subsystem_hp[i] < cls->subsystems[i].max_condition) {
            /* Subsystem took damage — try to add to repair queue */
            if (bc_repair_add(ship, (u8)i)) {
                /* New addition (not duplicate) — send ADD_TO_REPAIR_LIST */
                u8 evt[17];
                int len = bc_build_python_subsystem_event(
                    evt, sizeof(evt),
                    BC_EVENT_ADD_TO_REPAIR,
                    ship->subsys_obj_id[i],
                    ship->repair_subsys_obj_id);
                if (len > 0) bc_send_to_all(evt, len, true);
            }
        }
    }
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

    /* Compute impact direction: shooter -> target */
    bc_vec3_t impact_dir = bc_vec3_normalize(
        bc_vec3_sub(target->ship.pos, shooter->ship.pos));

    /* Snapshot HP before damage for PythonEvent generation */
    f32 hp_snap[BC_MAX_SUBSYSTEMS];
    memcpy(hp_snap, target->ship.subsystem_hp, sizeof(hp_snap));

    /* Apply damage server-side (phaser = directed, no blast radius) */
    bc_combat_apply_damage(&target->ship, target_cls, damage, 0.0f,
                           impact_dir, false, 1.0f);

    /* Generate ADD_TO_REPAIR_LIST PythonEvents for newly-damaged subsystems */
    generate_damage_events(target_slot, hp_snap, target_cls);

    /* Send authoritative health to all clients.
     * No server-generated Explosion — clients compute beam hit detection
     * locally and generate their own visual Explosion events.  Sending
     * Explosion from the server would cause double-damage (once from the
     * client's local hit, once from the server's Explosion). */
    send_health_update_immediate(target_slot);

    LOG_INFO("combat", "Server damage: %s -> %s, %.1f dmg (hull=%.1f)",
             peer_name(shooter_slot), peer_name(target_slot),
             damage, target->ship.hull_hp);

    /* Check for kill */
    if (!target->ship.alive) {
        LOG_INFO("combat", "%s destroyed by %s",
                 peer_name(target_slot), peer_name(shooter_slot));

        /* Send OBJECT_EXPLODING PythonEvent */
        {
            u8 expl[25];
            int elen = bc_build_python_exploding_event(
                expl, sizeof(expl),
                target->ship.object_id,
                shooter->ship.object_id,
                1.0f);
            if (elen > 0) bc_send_to_all(expl, elen, true);
        }

        /* Send Explosion (0x29) -- visual effect for the kill.
         * Stock BC does NOT send DestroyObject (0x14) for ship deaths;
         * the old ship is implicitly replaced when the respawn ObjCreateTeam
         * arrives. Verified: 0 DestroyObject messages across 59 ship deaths
         * in a 33.5-minute combat session. */
        {
            u8 boom[16];
            int blen = bc_build_explosion(boom, sizeof(boom),
                                           target->ship.object_id,
                                           target->ship.pos.x,
                                           target->ship.pos.y,
                                           target->ship.pos.z,
                                           damage, 300.0f);
            if (blen > 0) bc_send_to_all(boom, blen, true);
        }

        /* Update scores */
        shooter->score++;
        shooter->kills++;
        target->deaths++;

        i32 killer_player_id = bc_player_id_from_peer_slot(shooter_slot);
        i32 victim_player_id = bc_player_id_from_peer_slot(target_slot);
        if (!bc_is_valid_player_id(killer_player_id) ||
            !bc_is_valid_player_id(victim_player_id)) {
            LOG_WARN("combat", "Invalid ScoreChange IDs (killer slot=%d -> %d, victim slot=%d -> %d)",
                     shooter_slot, (int)killer_player_id,
                     target_slot, (int)victim_player_id);
            return;
        }

        u8 sc[64];
        int slen = bc_build_score_change(sc, sizeof(sc),
                                          killer_player_id,
                                          shooter->kills, shooter->score,
                                          victim_player_id,
                                          target->deaths,
                                          NULL, 0);
        if (slen > 0) bc_send_to_all(sc, slen, true);

        /* Clear victim ship state */
        target->has_ship = false;
        target->spawn_len = 0;
        if (!g_game_ended) {
            target->respawn_timer = 5.0f;
            target->respawn_class = target->class_index;
        }

        /* Check frag limit */
        if (!g_game_ended && g_frag_limit > 0 && shooter->score >= g_frag_limit) {
            u8 eg[8];
            int eglen = bc_build_end_game(eg, sizeof(eg), BC_END_REASON_FRAG_LIMIT);
            if (eglen > 0) bc_send_to_all(eg, eglen, true);
            g_game_ended = true;
            LOG_INFO("game", "Frag limit reached by %s (%d kills)",
                     peer_name(shooter_slot), shooter->score);
        }
    }
}

/* Torpedo hit callback -- called from bc_torpedo_tick() */
void bc_torpedo_hit_callback(int shooter_slot, i32 target_id,
                             f32 damage, f32 damage_radius,
                             bc_vec3_t impact_pos,
                             void *user_data)
{
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

    /* Snapshot HP before damage for PythonEvent generation */
    f32 hp_snap[BC_MAX_SUBSYSTEMS];
    memcpy(hp_snap, target->ship.subsystem_hp, sizeof(hp_snap));

    /* Torpedoes are area-effect with a blast radius */
    bc_combat_apply_damage(&target->ship, target_cls, damage, damage_radius,
                           impact_dir, (damage_radius > 0.0f), 1.0f);

    /* Generate ADD_TO_REPAIR_LIST PythonEvents for newly-damaged subsystems */
    generate_damage_events(target_slot, hp_snap, target_cls);

    /* Send authoritative health to all clients.
     * No server-generated Explosion — clients generate their own
     * visual Explosion events from local torpedo hit detection. */
    send_health_update_immediate(target_slot);

    LOG_INFO("combat", "Torpedo hit: slot %d -> %s, %.1f dmg (hull=%.1f)",
             shooter_slot, peer_name(target_slot),
             damage, target->ship.hull_hp);

    /* Check for kill */
    if (!target->ship.alive) {
        LOG_INFO("combat", "%s destroyed by torpedo from %s",
                 peer_name(target_slot), peer_name(shooter_slot));

        /* Send OBJECT_EXPLODING PythonEvent */
        {
            u8 expl[25];
            int elen = bc_build_python_exploding_event(
                expl, sizeof(expl),
                target->ship.object_id,
                shooter->ship.object_id,
                1.0f);
            if (elen > 0) bc_send_to_all(expl, elen, true);
        }

        /* Send Explosion (0x29) -- visual effect for the kill.
         * Stock BC does NOT send DestroyObject (0x14) for ship deaths. */
        {
            u8 boom[16];
            int blen = bc_build_explosion(boom, sizeof(boom),
                                           target->ship.object_id,
                                           impact_pos.x,
                                           impact_pos.y,
                                           impact_pos.z,
                                           damage, damage_radius > 0.0f ? damage_radius : 300.0f);
            if (blen > 0) bc_send_to_all(boom, blen, true);
        }

        shooter->score++;
        shooter->kills++;
        target->deaths++;

        i32 killer_player_id = bc_player_id_from_peer_slot(shooter_slot);
        i32 victim_player_id = bc_player_id_from_peer_slot(target_slot);
        if (!bc_is_valid_player_id(killer_player_id) ||
            !bc_is_valid_player_id(victim_player_id)) {
            LOG_WARN("combat", "Invalid ScoreChange IDs (killer slot=%d -> %d, victim slot=%d -> %d)",
                     shooter_slot, (int)killer_player_id,
                     target_slot, (int)victim_player_id);
            return;
        }

        u8 sc[64];
        int slen = bc_build_score_change(sc, sizeof(sc),
                                          killer_player_id,
                                          shooter->kills, shooter->score,
                                          victim_player_id,
                                          target->deaths,
                                          NULL, 0);
        if (slen > 0) bc_send_to_all(sc, slen, true);

        target->has_ship = false;
        target->spawn_len = 0;
        if (!g_game_ended) {
            target->respawn_timer = 5.0f;
            target->respawn_class = target->class_index;
        }

        if (!g_game_ended && g_frag_limit > 0 && shooter->score >= g_frag_limit) {
            u8 eg[8];
            int eglen = bc_build_end_game(eg, sizeof(eg), BC_END_REASON_FRAG_LIMIT);
            if (eglen > 0) bc_send_to_all(eg, eglen, true);
            g_game_ended = true;
            LOG_INFO("game", "Frag limit reached by %s (%d kills)",
                     peer_name(shooter_slot), shooter->score);
        }
    }
}

/* Torpedo target position callback -- called from bc_torpedo_tick() */
bool bc_torpedo_target_pos(i32 target_id, bc_vec3_t *out_pos,
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

/* --- Game message dispatch --- */

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
        bc_handle_checksum_response(peer_slot, &reassembled);
        return;
    }

    /* Below here, only accept messages from peers in LOBBY or IN_GAME state */
    if (peer->state < PEER_LOBBY) {
        g_stats.opcodes_rejected[opcode]++;
        LOG_WARN("game", "slot=%d opcode=0x%02X (%s) rejected (state=%d)",
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
        bc_relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

    /* --- Python events: relay to all others (reliable) --- */
    case BC_OP_PYTHON_EVENT:
    case BC_OP_PYTHON_EVENT2:
        bc_relay_to_others(peer_slot, payload, payload_len, true);
        break;

    /* --- Weapon/combat events: relay to all others (reliable) --- */
    case BC_OP_START_FIRING:
    case BC_OP_STOP_FIRING:
    case BC_OP_STOP_FIRING_AT:
    case BC_OP_SUBSYS_STATUS:
    case BC_OP_ADD_REPAIR_LIST:
    case BC_OP_CLIENT_EVENT:
        bc_relay_to_others(peer_slot, payload, payload_len, true);
        break;

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
        bc_relay_to_others(peer_slot, payload, payload_len, true);
        break;

    case BC_OP_STOP_CLOAK:
        if (g_registry_loaded && peer->has_ship) {
            bc_cloak_stop(&peer->ship);
            LOG_DEBUG("game", "slot=%d stopping cloak", peer_slot);
        }
        bc_relay_to_others(peer_slot, payload, payload_len, true);
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
        bc_relay_to_others(peer_slot, payload, payload_len, true);
        break;

    case BC_OP_REPAIR_PRIORITY:
    case BC_OP_TORP_TYPE_CHANGE:
        bc_relay_to_others(peer_slot, payload, payload_len, true);
        break;

    case BC_OP_SET_PHASER_LEVEL: {
        bc_phaser_level_event_t pl;
        if (bc_parse_set_phaser_level(payload, payload_len, &pl)) {
            int ps = find_peer_by_object(pl.source_object_id);
            if (ps >= 0) {
                g_peers.peers[ps].ship.phaser_level = pl.phaser_level;
                LOG_INFO("combat", "%s set phaser level %d",
                         object_owner_name(pl.source_object_id),
                         pl.phaser_level);
            }
        }
        bc_relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

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

        /* Relay torpedo visual to all others (strict N-1:1 ratio, no filtering).
         * Stock BC relays all weapon fire unconditionally. Anti-cheat checks
         * only gate server-side damage computation, not the visual relay. */
        bc_relay_to_others(peer_slot, payload, payload_len, true);

        if (g_registry_loaded && peer->has_ship) {
            const bc_ship_class_t *cls =
                bc_registry_get_ship(&g_registry, peer->class_index);

            /* Anti-cheat: cannot fire while cloaked -- skip damage only */
            if (cls && !bc_cloak_can_fire(&peer->ship)) {
                LOG_WARN("cheat", "slot=%d torpedo fire while cloaked, damage skipped",
                         peer_slot);
                break;
            }

            /* TODO: fire rate limiting -- disabled pending proper tuning.
             * Sovereign torpedoes fire every ~0.5s which tripped the old 2.0s limit. */
        }

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

        /* Relay beam visual to all others (strict N-1:1 ratio, no filtering).
         * Stock BC relays all weapon fire unconditionally. Anti-cheat checks
         * only gate server-side damage computation, not the visual relay. */
        bc_relay_to_others(peer_slot, payload, payload_len, true);

        if (g_registry_loaded && peer->has_ship) {
            const bc_ship_class_t *cls =
                bc_registry_get_ship(&g_registry, peer->class_index);

            /* Anti-cheat: cannot fire while cloaked -- skip damage only */
            if (cls && !bc_cloak_can_fire(&peer->ship)) {
                LOG_WARN("cheat", "slot=%d beam fire while cloaked, damage skipped",
                         peer_slot);
                break;
            }

            /* TODO: beam fire rate limiting -- disabled pending proper tuning. */

            /* Anti-cheat: range plausibility -- skip damage only */
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
                        break;  /* visual already relayed above; skip damage */
                    }
                }
            }
        }

        /* Server-side damage computation */
        if (g_registry_loaded && peer->has_ship && ev.has_target) {
            int target_slot = find_peer_by_object(ev.target_id);
            if (target_slot >= 0) {
                apply_beam_damage(peer_slot, target_slot);
            }
        }
        break;
    }

    /* --- Explosion: always relay (visual feedback for all clients) --- */
    case BC_OP_EXPLOSION: {
        bc_explosion_event_t ev;
        if (bc_parse_explosion(payload, payload_len, &ev)) {
            LOG_INFO("combat", "Client explosion on %s's ship: %.1f damage, radius %.1f",
                     object_owner_name(ev.object_id), ev.damage, ev.radius);
        }
        bc_relay_to_others(peer_slot, payload, payload_len, true);
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
                }

                /* Strip server-authoritative flag 0x20 (subsystem health).
                 * 0x80 (weapon state) is CLIENT-authoritative and must be relayed.
                 * The SUBSYSTEMS/WEAPONS flag split is ~96% direction-correlated
                 * (not 100%): in stock BC, the host's own ship can send 0x80 in
                 * the server→client direction. On our dedicated server there is no
                 * host-player ship, so dropping pure 0x20 packets from clients
                 * is still correct. */
                if (su.dirty == 0x20) {
                    /* Pure subsystem health update from client -- drop it.
                     * The server is authoritative for subsystem health. */
                    LOG_DEBUG("cheat", "slot=%d StateUpdate 0x20 suppressed",
                              peer_slot);
                    break;
                }
            }
        }
        bc_relay_to_others(peer_slot, payload, payload_len, false);
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

        /* Always set object_id from known game_slot (deterministic) */
        {
            int game_slot = peer_slot > 0 ? peer_slot - 1 : 0;
            peer->object_id = bc_make_ship_id(game_slot);
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
                    bc_ship_assign_subsystem_ids(&peer->ship, cls,
                                                  &g_script_obj_counter);
                    LOG_INFO("game", "slot=%d ship initialized: %s (species=%d, hull=%.0f, repair_obj=0x%X, next_counter=%d)",
                             peer_slot, cls->name, bhdr.species_id, cls->hull_hp,
                             peer->ship.repair_subsys_obj_id,
                             g_script_obj_counter);
                } else {
                    LOG_WARN("game", "slot=%d unknown species_id %d, no ship state",
                             peer_slot, bhdr.species_id);
                }
            }
        }

        bc_relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

    /* --- Object destruction: always relay (visual + game state for all clients) --- */
    case BC_OP_DESTROY_OBJ: {
        bc_destroy_event_t ev;
        if (bc_parse_destroy_obj(payload, payload_len, &ev)) {
            LOG_INFO("combat", "Client DestroyObj: %s's ship",
                     object_owner_name(ev.object_id));
        }
        bc_relay_to_others(peer_slot, payload, payload_len, true);
        break;
    }

    /* --- NewPlayerInGame (C->S, triggers MissionInit) --- */
    case BC_OP_NEW_PLAYER_IN_GAME: {
        LOG_INFO("handshake", "slot=%d sent NewPlayerInGame", peer_slot);
        /* Relay to all other peers (pending verification: stock behavior unclear).
         * Stock 0x2A flows client->server; whether the server relays it to
         * other clients has not been confirmed from traces. Kept for now. */
        bc_relay_to_others(peer_slot, payload, payload_len, true);
        /* Respond with MissionInit (0x35) -- tells client which star system
         * to load and what the match rules are.
         * Byte[1] is current_player_count (dynamic), not player_limit. */
        u8 mi[32];
        int connected = 0;
        for (int ci = 1; ci < BC_MAX_PLAYERS; ci++) {
            if (g_peers.peers[ci].state >= PEER_LOBBY) connected++;
        }
        int mi_len = bc_mission_init_build(mi, sizeof(mi),
                                            g_system_index, connected,
                                            g_time_limit, g_frag_limit);
        if (mi_len > 0) {
            LOG_DEBUG("handshake", "slot=%d sending MissionInit (system=%d)",
                      peer_slot, g_system_index);
            bc_queue_reliable(peer_slot, mi, mi_len);
            bc_flush_peer(peer_slot);
        }
        break;
    }

    /* --- Host message (C->S only, not relayed) --- */
    case BC_OP_HOST_MSG:
        LOG_DEBUG("game", "slot=%d host message len=%d", peer_slot, payload_len);
        break;

    /* --- Collision effect: parse and apply damage server-side.
     * Wire format from docs/collision-effect-wire-format.md.
     * source_obj=0 for environment collisions, otherwise other ship's ID.
     * The host applies damage; relay to others for visual effects. */
    case BC_OP_COLLISION_EFFECT: {
        LOG_DEBUG("game", "slot=%d collision effect len=%d", peer_slot, payload_len);
        /* Always relay for visual effects, even if damage is rejected */
        bc_relay_to_others(peer_slot, payload, payload_len, true);

        if (g_registry_loaded && g_collision_dmg) {
            bc_collision_event_t cev;
            if (!bc_parse_collision_effect(payload, payload_len, &cev)) {
                LOG_WARN("game", "slot=%d failed to parse CollisionEffect", peer_slot);
                break;
            }

            /* Ownership: sender must control source or target */
            i32 sender_oid = g_peers.peers[peer_slot].object_id;
            bool sender_is_source = (sender_oid == cev.source_object_id);
            bool sender_is_target = (sender_oid == cev.target_object_id);
            if (!sender_is_source && !sender_is_target) {
                LOG_WARN("combat", "slot=%d collision ownership fail "
                         "(sender=%d src=%d tgt=%d)",
                         peer_slot, sender_oid,
                         cev.source_object_id, cev.target_object_id);
                break;
            }

            /* Dedup: if sender is source and target is another player,
             * skip -- the target player will report their own collision */
            if (sender_is_source && cev.source_object_id != 0) {
                int other = find_peer_by_object(cev.target_object_id);
                if (other >= 0 &&
                    g_peers.peers[other].state >= PEER_IN_GAME) {
                    LOG_DEBUG("combat",
                              "slot=%d dedup: sender=source, target player exists",
                              peer_slot);
                    break;
                }
            }

            /* Proximity: reject implausible ship-vs-ship collisions */
            if (cev.source_object_id != 0) {
                int src_slot = find_peer_by_object(cev.source_object_id);
                int tgt_slot = find_peer_by_object(cev.target_object_id);
                if (src_slot >= 0 && tgt_slot >= 0) {
                    bc_vec3_t diff = bc_vec3_sub(
                        g_peers.peers[src_slot].ship.pos,
                        g_peers.peers[tgt_slot].ship.pos);
                    f32 dist_sq = bc_vec3_dot(diff, diff);
                    if (dist_sq > 2000.0f * 2000.0f) {
                        LOG_WARN("combat",
                                 "slot=%d collision proximity fail (dist=%.0f)",
                                 peer_slot, sqrtf(dist_sq));
                        break;
                    }
                }
            }

            int cc = cev.contact_count > 0 ? cev.contact_count : 1;

            /* Apply damage to the target ship */
            int target_slot = find_peer_by_object(cev.target_object_id);
            if (target_slot >= 0) {
                bc_peer_t *target = &g_peers.peers[target_slot];
                const bc_ship_class_t *tcls =
                    bc_registry_get_ship(&g_registry, target->class_index);

                if (tcls && target->ship.alive) {
                    f32 dmg = bc_combat_collision_damage_path2(
                        cev.collision_force, tcls->mass, cc);
                    if (dmg <= 0.0f) break;  /* dead zone — no damage */

                    /* Impact direction for shield facing */
                    bc_vec3_t impact_dir = {0.0f, 0.0f, 1.0f};
                    if (cev.source_object_id != 0) {
                        int src_slot = find_peer_by_object(cev.source_object_id);
                        if (src_slot >= 0) {
                            impact_dir = bc_vec3_normalize(bc_vec3_sub(
                                g_peers.peers[src_slot].ship.pos,
                                target->ship.pos));
                        }
                    }

                    /* Scale impact to ship surface for subsystem localization.
                     * Shield facing uses component comparison (scale-invariant). */
                    bc_vec3_t scaled_impact = {
                        impact_dir.x * tcls->bounding_extent,
                        impact_dir.y * tcls->bounding_extent,
                        impact_dir.z * tcls->bounding_extent,
                    };
                    f32 collision_radius = tcls->bounding_extent * 0.5f;

                    /* Snapshot HP before damage for PythonEvent generation */
                    f32 thp_snap[BC_MAX_SUBSYSTEMS];
                    memcpy(thp_snap, target->ship.subsystem_hp,
                           sizeof(thp_snap));

                    bc_combat_apply_damage(&target->ship, tcls, dmg,
                                           collision_radius, scaled_impact,
                                           false, 1.0f);

                    /* Generate ADD_TO_REPAIR_LIST PythonEvents */
                    generate_damage_events(target_slot, thp_snap, tcls);

                    /* Collision damage is communicated via health update
                     * (flag 0x20), NOT via Explosion events.  The stock host
                     * relays the original CollisionEffect (0x15) for visual
                     * feedback (done above) and sends authoritative subsystem
                     * conditions via the health round-robin.  Sending Explosion
                     * here would cause the client to double-apply damage
                     * (once locally from the Explosion, once from the health
                     * update), causing shield/subsystem flickering. */
                    send_health_update_immediate(target_slot);

                    LOG_INFO("combat",
                             "Collision: %s took %.1f damage (source=%s)",
                             peer_name(target_slot), dmg,
                             cev.source_object_id == 0 ? "environment" :
                             object_owner_name(cev.source_object_id));

                    if (!target->ship.alive) {
                        /* Send OBJECT_EXPLODING PythonEvent */
                        {
                            i32 killer_id = 0;
                            if (cev.source_object_id != 0) {
                                int ks = find_peer_by_object(
                                    cev.source_object_id);
                                if (ks >= 0)
                                    killer_id =
                                        g_peers.peers[ks].ship.object_id;
                            }
                            u8 expl[25];
                            int elen = bc_build_python_exploding_event(
                                expl, sizeof(expl),
                                target->ship.object_id,
                                killer_id, 1.0f);
                            if (elen > 0)
                                bc_send_to_all(expl, elen, true);
                        }

                        /* Send Explosion (0x29) -- visual kill effect.
                         * Stock BC does NOT send DestroyObject (0x14) for
                         * ship deaths. The old ship is implicitly replaced
                         * when the respawn ObjCreateTeam arrives. */
                        {
                            u8 boom[16];
                            int blen = bc_build_explosion(
                                boom, sizeof(boom),
                                target->ship.object_id,
                                target->ship.pos.x,
                                target->ship.pos.y,
                                target->ship.pos.z,
                                dmg, collision_radius);
                            if (blen > 0)
                                bc_send_to_all(boom, blen, true);
                        }
                        target->has_ship = false;
                        target->spawn_len = 0;
                        if (!g_game_ended) {
                            target->respawn_timer = 5.0f;
                            target->respawn_class = target->class_index;
                        }

                        /* Kill credit: if another ship caused this,
                         * credit them */
                        int killer = -1;
                        if (cev.source_object_id != 0)
                            killer = find_peer_by_object(
                                cev.source_object_id);
                        if (killer >= 0) {
                            bc_peer_t *k = &g_peers.peers[killer];
                            k->score++;
                            k->kills++;
                            target->deaths++;
                            i32 killer_player_id = bc_player_id_from_peer_slot(killer);
                            i32 victim_player_id = bc_player_id_from_peer_slot(target_slot);
                            if (!bc_is_valid_player_id(killer_player_id) ||
                                !bc_is_valid_player_id(victim_player_id)) {
                                LOG_WARN("combat", "Invalid collision ScoreChange IDs (killer slot=%d -> %d, victim slot=%d -> %d)",
                                         killer, (int)killer_player_id,
                                         target_slot, (int)victim_player_id);
                                break;
                            }
                            u8 sc[64];
                            int slen = bc_build_score_change(
                                sc, sizeof(sc),
                                killer_player_id,
                                k->kills, k->score,
                                victim_player_id,
                                target->deaths, NULL, 0);
                            if (slen > 0)
                                bc_send_to_all(sc, slen, true);
                            LOG_INFO("combat",
                                     "%s destroyed in collision with %s",
                                     peer_name(target_slot),
                                     peer_name(killer));

                            if (!g_game_ended && g_frag_limit > 0 &&
                                k->score >= g_frag_limit) {
                                u8 eg[8];
                                int eglen = bc_build_end_game(
                                    eg, sizeof(eg),
                                    BC_END_REASON_FRAG_LIMIT);
                                if (eglen > 0)
                                    bc_send_to_all(eg, eglen, true);
                                g_game_ended = true;
                                LOG_INFO("game",
                                         "Frag limit reached by %s "
                                         "(%d kills)",
                                         peer_name(killer), k->score);
                            }
                        } else {
                            target->deaths++;
                            LOG_INFO("combat",
                                     "%s destroyed in collision",
                                     peer_name(target_slot));
                        }
                    }
                }
            }

            /* Ship-vs-ship: also damage the source ship */
            if (cev.source_object_id != 0) {
                int src_slot = find_peer_by_object(cev.source_object_id);
                if (src_slot >= 0) {
                    bc_peer_t *source = &g_peers.peers[src_slot];
                    const bc_ship_class_t *scls =
                        bc_registry_get_ship(&g_registry, source->class_index);

                    if (scls && source->ship.alive) {
                        f32 sdmg = bc_combat_collision_damage_path2(
                            cev.collision_force, scls->mass, cc);
                        if (sdmg <= 0.0f) break;  /* dead zone */

                        /* Flipped impact direction */
                        bc_vec3_t src_impact = {0.0f, 0.0f, 1.0f};
                        if (target_slot >= 0) {
                            src_impact = bc_vec3_normalize(bc_vec3_sub(
                                g_peers.peers[target_slot].ship.pos,
                                source->ship.pos));
                        }

                        /* Scale impact to ship surface for subsystem localization */
                        bc_vec3_t src_scaled = {
                            src_impact.x * scls->bounding_extent,
                            src_impact.y * scls->bounding_extent,
                            src_impact.z * scls->bounding_extent,
                        };
                        f32 src_coll_radius = scls->bounding_extent * 0.5f;

                        /* Snapshot HP for PythonEvent generation */
                        f32 shp_snap[BC_MAX_SUBSYSTEMS];
                        memcpy(shp_snap, source->ship.subsystem_hp,
                               sizeof(shp_snap));

                        bc_combat_apply_damage(&source->ship, scls, sdmg,
                                               src_coll_radius, src_scaled,
                                               false, 1.0f);

                        /* Generate ADD_TO_REPAIR_LIST PythonEvents */
                        generate_damage_events(src_slot, shp_snap, scls);

                        /* No Explosion for collision — see target path comment */
                        send_health_update_immediate(src_slot);

                        LOG_INFO("combat",
                                 "Collision: %s also took %.1f damage",
                                 peer_name(src_slot), sdmg);

                        if (!source->ship.alive) {
                            /* Send OBJECT_EXPLODING PythonEvent */
                            {
                                i32 killer_id = 0;
                                if (target_slot >= 0 &&
                                    g_peers.peers[target_slot].has_ship)
                                    killer_id =
                                        g_peers.peers[target_slot]
                                            .ship.object_id;
                                u8 expl2[25];
                                int elen2 =
                                    bc_build_python_exploding_event(
                                        expl2, sizeof(expl2),
                                        source->ship.object_id,
                                        killer_id, 1.0f);
                                if (elen2 > 0)
                                    bc_send_to_all(expl2, elen2, true);
                            }

                            /* Send Explosion (0x29) -- visual kill effect.
                             * Stock BC does NOT send DestroyObject (0x14)
                             * for ship deaths. */
                            {
                                u8 boom2[16];
                                int blen2 = bc_build_explosion(
                                    boom2, sizeof(boom2),
                                    source->ship.object_id,
                                    source->ship.pos.x,
                                    source->ship.pos.y,
                                    source->ship.pos.z,
                                    sdmg, src_coll_radius);
                                if (blen2 > 0)
                                    bc_send_to_all(boom2, blen2, true);
                            }
                            source->has_ship = false;
                            source->spawn_len = 0;
                            if (!g_game_ended) {
                                source->respawn_timer = 5.0f;
                                source->respawn_class =
                                    source->class_index;
                            }

                            /* Kill credit: target killed the source */
                            if (target_slot >= 0 &&
                                g_peers.peers[target_slot].has_ship) {
                                bc_peer_t *k =
                                    &g_peers.peers[target_slot];
                                k->score++;
                                k->kills++;
                                source->deaths++;
                                i32 killer_player_id =
                                    bc_player_id_from_peer_slot(target_slot);
                                i32 victim_player_id =
                                    bc_player_id_from_peer_slot(src_slot);
                                if (!bc_is_valid_player_id(killer_player_id) ||
                                    !bc_is_valid_player_id(victim_player_id)) {
                                    LOG_WARN("combat", "Invalid collision ScoreChange IDs (killer slot=%d -> %d, victim slot=%d -> %d)",
                                             target_slot, (int)killer_player_id,
                                             src_slot, (int)victim_player_id);
                                    break;
                                }
                                u8 sc2[64];
                                int slen2 = bc_build_score_change(
                                    sc2, sizeof(sc2),
                                    killer_player_id,
                                    k->kills, k->score,
                                    victim_player_id,
                                    source->deaths, NULL, 0);
                                if (slen2 > 0)
                                    bc_send_to_all(sc2, slen2, true);
                                LOG_INFO("combat",
                                         "%s destroyed in collision "
                                         "with %s",
                                         peer_name(src_slot),
                                         peer_name(target_slot));

                                if (!g_game_ended &&
                                    g_frag_limit > 0 &&
                                    k->score >= g_frag_limit) {
                                    u8 eg2[8];
                                    int eglen2 = bc_build_end_game(
                                        eg2, sizeof(eg2),
                                        BC_END_REASON_FRAG_LIMIT);
                                    if (eglen2 > 0)
                                        bc_send_to_all(eg2, eglen2,
                                                       true);
                                    g_game_ended = true;
                                    LOG_INFO("game",
                                             "Frag limit reached by "
                                             "%s (%d kills)",
                                             peer_name(target_slot),
                                             k->score);
                                }
                            } else {
                                source->deaths++;
                                LOG_INFO("combat",
                                         "%s destroyed in collision",
                                         peer_name(src_slot));
                            }
                        }
                    }
                }
            }
        }
        break;
    }

    /* --- Request object (C->S, server responds with object data) --- */
    case BC_OP_REQUEST_OBJ:
        LOG_DEBUG("game", "slot=%d request object len=%d", peer_slot, payload_len);
        break;

    default:
        g_stats.opcodes_rejected[opcode]++;
        LOG_WARN("game", "slot=%d opcode=0x%02X (%s) len=%d (unhandled)",
                 peer_slot, opcode, name ? name : "?", payload_len);
        break;
    }
}

/* --- Packet handler --- */

void bc_handle_packet(const bc_addr_t *from, u8 *data, int len)
{
    /* Update peer timestamp if known */
    int slot = bc_peers_find(&g_peers, from);
    if (slot >= 0) {
        g_peers.peers[slot].last_recv_time = bc_ms_now();
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
            bc_handle_connect(from, len);
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

    /* Process each transport message.  Disconnect signals (0x05, 0x06) are
     * deferred until after all messages in the packet are processed, so that
     * ACKs and game data multiplexed in the same UDP packet are not skipped. */
    bool disconnect_pending = false;

    for (int i = 0; i < pkt.msg_count; i++) {
        bc_transport_msg_t *tmsg = &pkt.msgs[i];

        if (tmsg->type == BC_TRANSPORT_ACK) {
            bc_reliable_ack(&g_peers.peers[slot].reliable_out, tmsg->seq);
            continue;
        }

        if (tmsg->type == BC_TRANSPORT_DISCONNECT) {
            char addr_str[32];
            bc_addr_to_string(from, addr_str, sizeof(addr_str));
            LOG_INFO("net", "Player disconnected: %s (slot %d)", addr_str, slot);
            disconnect_pending = true;
            continue;
        }

        /* ConnectACK from a connected client = graceful disconnect signal.
         * The client sends type 0x05 when the user presses Escape / quits. */
        if (tmsg->type == BC_TRANSPORT_CONNECT_ACK) {
            LOG_INFO("net", "slot=%d graceful disconnect (ConnectACK)", slot);
            /* ACK the disconnect (reliable low-type).  Seq byte is payload[1]
             * (embedded reliable format: [flags:C0][seq_hi][seq_lo][slot][ip...]).
             * Stock server ACKs this before tearing down the peer. */
            if (tmsg->payload_len >= 2) {
                u16 disc_seq = (u16)tmsg->payload[1] << 8;
                bc_outbox_add_ack(&g_peers.peers[slot].outbox, disc_seq, 0x00);
            }
            disconnect_pending = true;
            continue;
        }

        /* Stale Connect retransmissions from a peer that's already connected.
         * These leak through when the client hasn't received our Connect
         * response yet.  Safe to ignore. */
        if (tmsg->type == BC_TRANSPORT_CONNECT ||
            tmsg->type == BC_TRANSPORT_CONNECT_DATA) {
            continue;
        }

        /* ACK incoming reliable messages.  Stock dedi ACKs every client
         * reliable immediately, batched with its next outgoing message.
         * This also drives the client's retransmit logic.
         * Only ACK messages with the reliable flag (0x80) -- type 0x32
         * with flags=0x00 is unreliable data (no seq, no ACK needed).
         * Fragment messages need a 5-byte ACK with frag_idx so the
         * client drains the correct retransmit queue entry. */
        if (tmsg->type == BC_TRANSPORT_RELIABLE && (tmsg->flags & 0x80)) {
            if ((tmsg->flags & BC_RELIABLE_FLAG_FRAGMENT) &&
                tmsg->payload_len >= 1) {
                u8 frag_idx = tmsg->payload[0];
                bc_outbox_add_fragment_ack(&g_peers.peers[slot].outbox,
                                            tmsg->seq, frag_idx);
            } else {
                bc_outbox_add_ack(&g_peers.peers[slot].outbox, tmsg->seq, 0x00);
            }
        }

        /* Keepalive (type 0x00): client sends these throughout the session.
         * First one carries UTF-16LE player name; subsequent ones are heartbeats.
         * Format: [0x00][totalLen][flags:1][?:2][slot?:1][ip:4][name_utf16le...]
         * Game data uses type 0x32 (both reliable and unreliable), never 0x00. */
        if (tmsg->type == BC_TRANSPORT_KEEPALIVE) {
            if (tmsg->payload_len >= 8) {
                bc_peer_t *peer = &g_peers.peers[slot];
                if (peer->name[0] == '\0') {
                    const u8 *name_start = tmsg->payload + 8;
                    int name_bytes = tmsg->payload_len - 8;
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
                    if (tmsg->payload_len <= (int)sizeof(peer->keepalive_data)) {
                        memcpy(peer->keepalive_data, tmsg->payload,
                               (size_t)tmsg->payload_len);
                        peer->keepalive_len = tmsg->payload_len;
                    }
                }
            }
            continue;  /* Keepalives are never game data */
        }

        /* Game message (reliable or unreliable) */
        if (tmsg->payload_len > 0) {
            handle_game_message(slot, tmsg);
        }
    }

    /* Deferred disconnect: flush queued ACK, then tear down the peer. */
    if (disconnect_pending) {
        bc_flush_peer(slot);
        bc_handle_peer_disconnect(slot);
    }
}
