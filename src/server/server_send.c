#include "openbc/server_state.h"
#include "openbc/server_send.h"
#include "openbc/transport.h"
#include "openbc/drain.h"
#include "openbc/cipher.h"
#include "openbc/reliable.h"
#include "openbc/log.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#endif

void bc_queue_reliable(int peer_slot, const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    u16 seq = peer->reliable_seq_out++;

    /* Track for retransmission -- log if queue is full or payload exceeds limit */
    if (!bc_reliable_add(&peer->reliable_out, payload, payload_len,
                         seq, bc_ms_now())) {
        LOG_WARN("send", "reliable retransmit queue full or oversized payload "
                 "(slot=%d len=%d) -- message sent once, no retransmit",
                 peer_slot, payload_len);
    }

    /* Fresh reliable game traffic feeds the priority-reliable queue, drained
     * by Pass 1 of the per-peer 4-pass bundling drain (retx escalates a
     * stuck entry into the Pass-4 lane). */
    if (!bc_drain_enqueue_priority(&peer->drain, payload, payload_len, seq)) {
        LOG_WARN("send", "drain priority queue full, reliable msg dropped "
                 "(slot=%d seq=%u len=%d)",
                 peer_slot, (unsigned)seq, payload_len);
    }
}

void bc_queue_unreliable(int peer_slot, const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    /* Unreliable game traffic feeds the unreliable queue, drained by Pass 3
     * (dequeue + free each; promote to reliable tracking on first delivery). */
    if (!bc_drain_enqueue_unreliable(&peer->drain, payload, payload_len)) {
        LOG_WARN("send", "drain unreliable queue full, unreliable msg dropped "
                 "(slot=%d len=%d)",
                 peer_slot, payload_len);
    }
}

void bc_send_unreliable_direct(const bc_addr_t *to,
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

void bc_flush_peer(int slot)
{
    bc_peer_t *peer = &g_peers.peers[slot];
    if (!bc_outbox_pending(&peer->outbox)) {
        LOG_TRACE("flush", "slot=%d outbox empty (msg_count=%d pos=%d)",
                  slot, peer->outbox.msg_count, peer->outbox.pos);
        return;
    }

    LOG_TRACE("flush", "slot=%d flushing outbox (msg_count=%d pos=%d)",
              slot, peer->outbox.msg_count, peer->outbox.pos);

    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(&peer->outbox, pkt, sizeof(pkt));
    LOG_TRACE("flush", "slot=%d flush_to_buf returned len=%d", slot, len);
    if (len > 0) {
        /* Hex dump raw outbox before encryption */
        {
            char hex[256];
            int hpos = 0;
            int show = len < 80 ? len : 80;
            for (int j = 0; j < show; j++)
                hpos += snprintf(hex + hpos, (size_t)(sizeof(hex) - hpos),
                                  "%02X ", pkt[j]);
            LOG_TRACE("flush", "slot=%d raw: [%s]", slot, hex);
        }
        /* Trace-log outgoing packet before encryption */
        bc_packet_t trace;
        if (bc_transport_parse(pkt, len, &trace))
            bc_log_packet_trace(&trace, slot, "SEND");
        alby_cipher_encrypt(pkt, (size_t)len);
        int sent = bc_socket_send(&g_socket, &peer->addr, pkt, len);
        LOG_TRACE("flush", "slot=%d sent %d/%d bytes", slot, sent, len);
    }
}

void bc_relay_to_others(int sender_slot, const u8 *payload, int payload_len,
                        bool reliable)
{
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {  /* skip slot 0 = dedi */
        if (i == sender_slot) continue;
        if (g_peers.peers[i].state < PEER_LOBBY) continue;

        if (reliable) {
            bc_queue_reliable(i, payload, payload_len);
        } else {
            bc_queue_unreliable(i, payload, payload_len);
        }
    }
}

void bc_send_to_all(const u8 *payload, int payload_len, bool reliable)
{
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        if (g_peers.peers[i].state < PEER_LOBBY) continue;
        if (reliable)
            bc_queue_reliable(i, payload, payload_len);
        else
            bc_queue_unreliable(i, payload, payload_len);
    }
}

/* Round-robin cursor across peers for fairness: the drain starts from a
 * different peer each tick so no single peer is consistently first to claim
 * datagram budget under load. */
static int g_drain_cursor = 1;  /* slot 0 is the dedi host, never drained */

void bc_drain_peer(int slot)
{
    bc_peer_t *peer = &g_peers.peers[slot];
    if (!bc_drain_pending(&peer->drain)) return;

    /* Pass-4 "disconnecting" gate input. NOTE: there is currently no distinct
     * addressable disconnecting state -- a leaving peer transitions straight to
     * PEER_EMPTY, which bc_drain_all() skips -- so this is always false in the
     * live path today and the Pass-4 gate reduces to (msg_count>0 AND
     * ack_count>0). The branch is kept and unit-tested for when a disconnecting
     * state with a still-valid address is introduced. */
    peer->drain.disconnecting = (peer->state == PEER_EMPTY);

    u8 pkt[BC_MAX_PACKET_SIZE];
    const bc_qmsg_t *promoted[BC_DRAIN_QUEUE_CAP];
    int promoted_count = 0;

    int len = bc_drain_to_buf(&peer->drain, BC_DIR_SERVER, pkt, sizeof(pkt),
                              promoted, BC_DRAIN_QUEUE_CAP, &promoted_count);
    if (len <= 0) return;  /* Pass-4 gate closed / nothing to send this tick */

    /* Pass 3 promoted these unreliable messages to reliable tracking on first
     * delivery: register them so they retransmit until ACKed, matching stock
     * burst-absorption where a drained unreliable graduates to the reliable
     * path. */
    for (int p = 0; p < promoted_count; p++) {
        const bc_qmsg_t *m = promoted[p];
        /* m->bytes is the serialized unreliable frame [0x32][len][0x00][body];
         * strip the 3-byte transport header to recover the game payload. */
        if (m->len > 3) {
            u16 seq = peer->reliable_seq_out++;
            bc_reliable_add(&peer->reliable_out, m->bytes + 3, m->len - 3,
                            seq, bc_ms_now());
        }
    }

    bc_packet_t trace;
    if (bc_transport_parse(pkt, len, &trace))
        bc_log_packet_trace(&trace, slot, "DRN");
    alby_cipher_encrypt(pkt, (size_t)len);
    bc_socket_send(&g_socket, &peer->addr, pkt, len);
}

void bc_drain_all(void)
{
    /* Advance the round-robin start point so fairness rotates across ticks. */
    int start = g_drain_cursor;
    for (int n = 0; n < BC_MAX_PLAYERS - 1; n++) {
        int slot = 1 + ((start - 1 + n) % (BC_MAX_PLAYERS - 1));
        if (g_peers.peers[slot].state == PEER_EMPTY) continue;
        bc_drain_peer(slot);
    }
    g_drain_cursor = bc_drain_next_cursor(g_drain_cursor, BC_MAX_PLAYERS);
}
