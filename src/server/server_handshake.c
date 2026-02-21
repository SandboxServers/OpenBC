#include "openbc/server_state.h"
#include "openbc/server_send.h"
#include "openbc/server_handshake.h"
#include "openbc/transport.h"
#include "openbc/cipher.h"
#include "openbc/opcodes.h"
#include "openbc/handshake.h"
#include "openbc/reliable.h"
#include "openbc/master.h"
#include "openbc/game_events.h"
#include "openbc/game_builders.h"
#include "openbc/ship_state.h"
#include "openbc/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#endif

static void send_checksum_request(int peer_slot, int round)
{
    u8 payload[256];
    int payload_len = bc_checksum_request_build(payload, sizeof(payload), round);
    if (payload_len > 0) {
        LOG_DEBUG("handshake", "slot=%d sending checksum request round %d",
                  peer_slot, round);
        bc_queue_reliable(peer_slot, payload, payload_len);
        /* Flush immediately -- stock dedi sends ACK + next ChecksumReq in
         * one packet within 1ms of receiving the response.  Waiting for the
         * 100ms tick would add unnecessary latency during handshake. */
        bc_flush_peer(peer_slot);
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
    bc_queue_reliable(peer_slot, payload, 1);
    LOG_DEBUG("handshake", "slot=%d sending opcode 0x28", peer_slot);

    /* Settings (opcode 0x00) -- reliable.
     * Player slot in Settings is the game-level slot (0-based), not the
     * network peer index. Stock dedi sends slot=0 for first joiner.
     * peer_slot 1 -> game_slot 0, peer_slot 2 -> game_slot 1, etc. */
    u8 game_slot = (u8)(peer_slot > 0 ? peer_slot - 1 : 0);
    len = bc_settings_build(payload, sizeof(payload),
                            g_game_time, g_collision_dmg, g_friendly_fire,
                            game_slot, g_map_name);
    if (len > 0) {
        LOG_DEBUG("handshake", "slot=%d sending Settings (game_slot=%d, map=%s)",
                  peer_slot, game_slot, g_map_name);
        bc_queue_reliable(peer_slot, payload, len);
    }

    /* UICollisionSetting (0x16) is NOT sent during handshake -- collision
     * is already in the Settings bit flags.  Stock dedi never sends 0x16
     * during initial connection (verified from traces). */

    /* GameInit (opcode 0x01) -- reliable */
    len = bc_gameinit_build(payload, sizeof(payload));
    if (len > 0) {
        LOG_DEBUG("handshake", "slot=%d sending GameInit", peer_slot);
        bc_queue_reliable(peer_slot, payload, len);
    }

    peer->state = PEER_LOBBY;
    LOG_INFO("handshake", "slot=%d reached LOBBY state", peer_slot);

    /* --- Late-join data: existing ships, scores, DeletePlayerUI --- */

    /* Send Score (0x37) for each active player -- stock format sends one
     * message per player: [0x37][player_id:i32][kills:i32][deaths:i32][score:i32] */
    {
        int sent = 0;
        for (int i = 1; i < BC_MAX_PLAYERS; i++) {
            if (g_peers.peers[i].state >= PEER_LOBBY) {
                u8 score_buf[32];
                int slen = bc_build_score(score_buf, sizeof(score_buf),
                                           g_peers.peers[i].ship.object_id,
                                           g_peers.peers[i].kills,
                                           g_peers.peers[i].deaths,
                                           g_peers.peers[i].score);
                if (slen > 0) {
                    bc_queue_reliable(peer_slot, score_buf, slen);
                    sent++;
                }
            }
        }
        if (sent > 0) {
            LOG_DEBUG("handshake", "slot=%d sending Score for %d players",
                      peer_slot, sent);
        }
    }

    /* Forward cached ObjCreateTeam for every already-spawned ship */
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        if (i == peer_slot) continue;
        bc_peer_t *other = &g_peers.peers[i];
        if (other->state >= PEER_LOBBY && other->spawn_len > 0) {
            bc_queue_reliable(peer_slot, other->spawn_payload, other->spawn_len);
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
                bc_queue_reliable(peer_slot, del_ui, dlen);
        }
    }

    /* Flush immediately so the client gets 0x28+Settings+GameInit+join data
     * without waiting for the next 100ms tick. */
    bc_flush_peer(peer_slot);

    /* Do NOT send NewPlayerInGame (0x2A) here -- that is a CLIENT-to-SERVER
     * message.  The client sends 0x2A after receiving Settings + GameInit.
     * When we receive it, we respond with MissionInit (see handle_game_message). */
}

void bc_handle_peer_disconnect(int slot)
{
    if (g_peers.peers[slot].state == PEER_EMPTY) return;

    g_stats.disconnects++;

    /* Update player record with disconnect time */
    for (int i = 0; i < g_stats.player_count; i++) {
        if (g_stats.players[i].disconnect_time == 0 &&
            g_stats.players[i].connect_time == g_peers.peers[slot].connect_time) {
            g_stats.players[i].disconnect_time = bc_ms_now();
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
            i32 ship_id = g_peers.peers[slot].object_id;
            if (ship_id < 0) {
                /* Fallback: compute from game_slot */
                ship_id = bc_make_ship_id(slot > 0 ? slot - 1 : 0);
            }
            len = bc_build_destroy_obj(payload, sizeof(payload), ship_id);
            if (len > 0) bc_relay_to_others(slot, payload, len, true);
        }

        /* 2. DeletePlayerUI (0x17) -- remove from scoreboard */
        u8 game_slot = (u8)(slot > 0 ? slot - 1 : 0);
        len = bc_delete_player_ui_build(payload, sizeof(payload), game_slot);
        if (len > 0) bc_relay_to_others(slot, payload, len, true);

        /* 3. DeletePlayerAnim (0x18) -- "Player X has left" notification */
        len = bc_delete_player_anim_build(payload, sizeof(payload),
                                           g_peers.peers[slot].name);
        if (len > 0) bc_relay_to_others(slot, payload, len, true);

        LOG_DEBUG("net", "Sent disconnect notifications for slot %d "
                  "(DestroyObj+DeletePlayerUI+DeletePlayerAnim)", slot);
    }

    g_peers.peers[slot].respawn_timer = 0.0f;
    g_peers.peers[slot].respawn_class = -1;

    bc_peers_remove(&g_peers, slot);
    LOG_INFO("net", "Player removed: %s (slot %d), %d remaining",
             addr_str, slot, g_peers.count - 1);

    /* Notify master servers that player count changed */
    bc_master_statechanged(&g_masters, &g_socket);
}

void bc_handle_connect(const bc_addr_t *from, int len)
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
            bc_send_unreliable_direct(from, boot_payload, boot_len);
        }
        return;
    }

    /* Verify address was stored correctly (GCC -O2 dead-store workaround).
     * i686-w64-mingw32-gcc -O2 can eliminate stores after memset even with
     * volatile, especially when the struct is very large (bc_peer_t >4KB).
     * Re-write the address here to be safe. */
    {
        volatile u8 *dst = (volatile u8 *)&g_peers.peers[slot].addr;
        const u8 *src = (const u8 *)from;
        for (size_t b = 0; b < sizeof(bc_addr_t); b++)
            dst[b] = src[b];
    }

    g_peers.peers[slot].last_recv_time = bc_ms_now();
    g_peers.peers[slot].connect_time = bc_ms_now();

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
        rec->connect_time = bc_ms_now();
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
                            cs_payload, cs_len, seq, bc_ms_now());
            int msg_total = 5 + cs_len;
            pkt[pos++] = BC_TRANSPORT_RELIABLE;
            pkt[pos++] = (u8)msg_total;
            pkt[pos++] = 0x80;                    /* reliable flags */
            pkt[pos++] = (u8)(seq & 0xFF);         /* counter -> seqHi */
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

    /* Notify master servers that player count changed */
    bc_master_statechanged(&g_masters, &g_socket);

    (void)len;
}

void bc_handle_checksum_response(int peer_slot,
                                 const bc_transport_msg_t *msg)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    /* Handle 0xFF final round response */
    if (peer->state == PEER_CHECKSUMMING_FINAL) {
        /* Parse the response to verify it's well-formed */
        bc_checksum_resp_t resp;
        if (!bc_checksum_response_parse(&resp, msg->payload, msg->payload_len)) {
            /* Full hex dump for debugging */
            int dump_len = msg->payload_len < 300 ? msg->payload_len : 300;
            char *hex = (char *)alloca((size_t)(dump_len * 3 + 1));
            hex[0] = '\0';
            for (int i = 0; i < dump_len; i++)
                sprintf(hex + i * 3, "%02X ", msg->payload[i]);
            LOG_WARN("handshake", "slot=%d round 0xFF parse error (len=%d)",
                     peer_slot, msg->payload_len);
            LOG_WARN("handshake", "  hex=[%s]", hex);
            g_stats.boots_checksum++;
            u8 boot[4];
            int blen = bc_bootplayer_build(boot, sizeof(boot), BC_BOOT_CHECKSUM);
            if (blen > 0) bc_queue_reliable(peer_slot, boot, blen);
            bc_handle_peer_disconnect(peer_slot);
            return;
        }
        LOG_DEBUG("handshake", "slot=%d checksum round 0xFF validated "
                  "(%d files, %d subdirs, dir=0x%08X)",
                  peer_slot, resp.file_count, resp.subdir_count, resp.dir_hash);
        send_settings_and_gameinit(peer_slot);
        return;
    }

    if (peer->state != PEER_CHECKSUMMING) {
        /* Client retransmits checksum responses until it gets the next
         * server message.  This is normal -- just silently re-ACK. */
        LOG_TRACE("handshake", "slot=%d ignoring checksum retransmit (state=%d)",
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
            if (blen > 0) bc_queue_reliable(peer_slot, boot, blen);
            bc_handle_peer_disconnect(peer_slot);
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
            if (blen > 0) bc_queue_reliable(peer_slot, boot, blen);
            bc_handle_peer_disconnect(peer_slot);
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
         * The server sends
         * [0x20][0xFF] after rounds 0-3 complete. */
        LOG_DEBUG("handshake", "slot=%d rounds 0-3 passed, sending final round 0xFF",
                  peer_slot);
        u8 payload[256];
        int plen = bc_checksum_request_final_build(payload, sizeof(payload));
        if (plen > 0) {
            bc_queue_reliable(peer_slot, payload, plen);
            bc_flush_peer(peer_slot);
        }
        peer->state = PEER_CHECKSUMMING_FINAL;
    }
}
