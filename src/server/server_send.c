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

void bc_queue_reliable(int peer_slot, const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    u16 seq = peer->reliable_seq_out++;

    /* Track for retransmission */
    bc_reliable_add(&peer->reliable_out, payload, payload_len,
                    seq, bc_ms_now());

    if (!bc_outbox_add_reliable(&peer->outbox, payload, payload_len, seq)) {
        bc_flush_peer(peer_slot);
        bc_outbox_add_reliable(&peer->outbox, payload, payload_len, seq);
    }
}

void bc_queue_unreliable(int peer_slot, const u8 *payload, int payload_len)
{
    bc_peer_t *peer = &g_peers.peers[peer_slot];
    if (!bc_outbox_add_unreliable(&peer->outbox, payload, payload_len)) {
        bc_flush_peer(peer_slot);
        bc_outbox_add_unreliable(&peer->outbox, payload, payload_len);
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
