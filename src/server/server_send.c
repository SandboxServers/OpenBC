#include "openbc/server_state.h"
#include "openbc/server_send.h"
#include "openbc/transport.h"
#include "openbc/cipher.h"
#include "openbc/reliable.h"
#include "openbc/log.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#endif

/*
 * Outbound bundling -- corrected reliable_out-based design.
 *
 * Wire-protocol trace analysis of a stock dedicated server session shows
 * outbound datagrams carry MULTIPLE transport messages (up to 255) in one
 * 512-byte datagram, not one message per datagram.  OpenBC reproduces that by
 * accumulating ACKs / unreliable game data into the per-peer outbox AND, at
 * flush time, packing every *due* reliable_out entry into the same datagram.
 *
 * Reliable traffic lives in ONE place: peer->reliable_out.  Incoming ACKs prune
 * it (bc_reliable_ack), so an ACKed message is never packed again.  bc_flush_peer
 * stamps send_time on each packed entry, so flushing a peer multiple times in
 * the same tick (synchronous handshake sends + the per-tick flush) never
 * re-sends a reliable message within that tick.
 */

void bc_queue_reliable(int peer_slot, const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    u16 seq = peer->reliable_seq_out++;

    /* reliable_out is the single source of truth for reliable traffic: it is
     * ACK-pruned, retransmit-timed, and the source bc_flush_peer bundles from.
     * The entry is queued as "not yet sent" (due on the next flush). */
    if (!bc_reliable_add(&peer->reliable_out, payload, payload_len,
                         seq, bc_ms_now())) {
        LOG_WARN("send", "reliable queue full or oversized payload "
                 "(slot=%d len=%d) -- message dropped",
                 peer_slot, payload_len);
    }
}

void bc_queue_unreliable(int peer_slot, const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    /* Unreliable game traffic accumulates in the outbox and ships once on the
     * next flush, bundled with any ACKs and due reliable messages.  No
     * retransmit tracking (fire-and-forget). */
    if (!bc_outbox_add_unreliable(&peer->outbox, payload, payload_len)) {
        bc_flush_peer(peer_slot);  /* outbox full -- ship what we have, retry */
        if (!bc_outbox_add_unreliable(&peer->outbox, payload, payload_len)) {
            LOG_WARN("send", "outbox still full after flush, unreliable msg "
                     "dropped (slot=%d len=%d)", peer_slot, payload_len);
        }
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

    /* Bundle every *due* reliable_out entry (never-sent, or its retransmit
     * interval elapsed) into the outbox so it ships in the same datagram as any
     * pending ACKs / unreliable data.  Stamping send_time=now inside
     * bc_reliable_pack_due makes this idempotent within a tick: a second
     * bc_flush_peer at the same millisecond re-packs nothing.  This is what
     * makes the synchronous idiom `bc_queue_reliable(); bc_flush_peer();` send
     * the just-queued reliable message immediately. */
    int retransmits = 0;
    int packed = bc_reliable_pack_due(&peer->reliable_out, &peer->outbox,
                                      bc_ms_now(), &retransmits);
    if (retransmits > 0) g_stats.reliable_retransmits += (u32)retransmits;

    if (!bc_outbox_pending(&peer->outbox)) {
        LOG_TRACE("flush", "slot=%d nothing to send (outbox empty, reliable "
                  "packed=%d)", slot, packed);
        return;
    }

    LOG_TRACE("flush", "slot=%d flushing outbox (msg_count=%d pos=%d, reliable "
              "packed=%d)", slot, peer->outbox.msg_count, peer->outbox.pos,
              packed);

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

/*
 * Per-tick send: flush every active peer.  Each flush bundles that peer's
 * pending ACKs, unreliable data, and every due reliable_out entry into a single
 * datagram (see bc_flush_peer).  Because every peer gets its own datagram there
 * is no shared per-tick datagram budget to ration, so no round-robin cursor is
 * needed -- the old parallel-queue drain's fairness cursor was only meaningful
 * when peers competed for one buffer.
 */
void bc_flush_all_peers(void)
{
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {  /* slot 0 = dedi host */
        if (g_peers.peers[i].state == PEER_EMPTY) continue;
        bc_flush_peer(i);
    }
}
