#include "test_util.h"
#include "openbc/reliable.h"
#include "openbc/transport.h"
#include <string.h>

/*
 * Unit tests for reliable_out-based outbound packet bundling (issue #195).
 *
 * Wire-protocol trace analysis of a stock dedicated server session shows
 * outbound datagrams carry MULTIPLE transport messages (up to 255) in one
 * 512-byte datagram rather than one message per datagram.  OpenBC reproduces
 * that by accumulating ACKs / unreliable data into the per-peer outbox and, at
 * flush time, packing every *due* reliable_out entry into the SAME datagram via
 * bc_reliable_pack_due().
 *
 * The two correctness properties the old parallel-queue design lacked, proven
 * here, are:
 *   1. Idempotency within a tick -- packing a peer twice at the same now_ms
 *      does not duplicate a reliable message on the wire (send_time is stamped).
 *   2. ACK removal -- an ACKed reliable entry is pruned from reliable_out and is
 *      never packed again (bundling and ACK removal share one queue).
 */

/* Build one bundled datagram the way bc_flush_peer does: pack due reliable
 * entries into a fresh outbox, then serialize the outbox.  Returns datagram
 * length (0 if nothing pending), and sets *count to the parsed message count. */
static int build_bundle(bc_reliable_queue_t *q, bc_outbox_t *outbox,
                        u32 now_ms, u8 *out, int out_size, int *count)
{
    bc_reliable_pack_due(q, outbox, now_ms, NULL);
    if (count) *count = 0;
    if (!bc_outbox_pending(outbox)) return 0;
    int len = bc_outbox_flush_to_buf(outbox, out, out_size);
    if (len <= 0) return len;
    bc_packet_t pkt;
    if (bc_transport_parse(out, len, &pkt)) {
        if (count) *count = pkt.msg_count;
    } else if (count) {
        *count = -1;
    }
    return len;
}

/* === Multi-message bundling === */

TEST(bundle_packs_multiple_reliables_in_one_datagram)
{
    /* Several reliable messages queued before a flush all ride in one datagram
     * (multi-msg, not one-per-datagram). */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    u8 body[] = { 0x1C, 0xAA, 0xBB };
    for (int i = 0; i < 5; i++)
        ASSERT(bc_reliable_add(&q, body, sizeof(body), (u16)i, 1000));

    u8 out[BC_MAX_PACKET_SIZE];
    int count = 0;
    int len = build_bundle(&q, &outbox, 1000, out, sizeof(out), &count);
    ASSERT(len > 0);
    ASSERT_EQ(out[0], BC_DIR_SERVER);  /* sender id (server) */
    ASSERT_EQ(out[1], 5);              /* message count header */
    ASSERT_EQ_INT(count, 5);
    /* All five remain in reliable_out until ACKed (still tracked). */
    ASSERT_EQ_INT(q.count, 5);
}

TEST(bundle_mixes_acks_and_reliables_in_one_datagram)
{
    /* ACKs accumulated in the outbox bundle together with due reliable_out
     * entries in a single datagram. */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    /* Two pending ACKs already in the outbox (like incoming-reliable ACKs). */
    ASSERT(bc_outbox_add_ack(&outbox, 0x0500, 0x00));
    ASSERT(bc_outbox_add_ack(&outbox, 0x0600, 0x00));
    /* One due reliable message. */
    u8 body[] = { 0x06, 0x01 };
    ASSERT(bc_reliable_add(&q, body, sizeof(body), 1, 1000));

    u8 out[BC_MAX_PACKET_SIZE];
    int count = 0;
    int len = build_bundle(&q, &outbox, 1000, out, sizeof(out), &count);
    ASSERT(len > 0);
    ASSERT_EQ_INT(count, 3);  /* 2 ACKs + 1 reliable, one datagram */
}

/* === Synchronous-send idiom === */

TEST(bundle_fresh_reliable_is_due_immediately)
{
    /* A just-queued reliable (never sent) is due on the very next pack, even
     * when its send_time equals "now" -- this is what makes the synchronous
     * idiom bc_queue_reliable(); bc_flush_peer(); send immediately. */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    u8 body[] = { 0x28 };
    ASSERT(bc_reliable_add(&q, body, sizeof(body), 7, 5000));  /* send_time=5000 */

    /* Pack at the SAME now_ms it was added.  Without the `sent` flag this would
     * be (now - send_time == 0) < interval -> skipped; the first-send rule
     * makes it due. */
    int packed = bc_reliable_pack_due(&q, &outbox, 5000, NULL);
    ASSERT_EQ_INT(packed, 1);
    ASSERT(bc_outbox_pending(&outbox));
}

/* === Within-tick idempotency === */

TEST(bundle_idempotent_within_a_tick)
{
    /* Packing the same peer twice at the same now_ms must NOT re-send a reliable
     * message: the first pack stamps send_time = now, so the second pack sees it
     * as not-yet-due.  This is the key property the parallel-queue design lacked
     * (it re-sent on every drain). */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);

    u8 body[] = { 0x06, 0x42 };
    ASSERT(bc_reliable_add(&q, body, sizeof(body), 3, 2000));

    /* First pack at t=2000: the message is due (first send). */
    bc_outbox_t ob1;
    bc_outbox_init(&ob1);
    int packed1 = bc_reliable_pack_due(&q, &ob1, 2000, NULL);
    ASSERT_EQ_INT(packed1, 1);

    /* Second pack at the SAME t=2000 (e.g. synchronous flush + per-tick flush):
     * nothing due, nothing packed -- no duplicate on the wire. */
    bc_outbox_t ob2;
    bc_outbox_init(&ob2);
    int packed2 = bc_reliable_pack_due(&q, &ob2, 2000, NULL);
    ASSERT_EQ_INT(packed2, 0);
    ASSERT(!bc_outbox_pending(&ob2));

    /* The entry is still tracked (awaiting ACK), not dropped. */
    ASSERT_EQ_INT(q.count, 1);
}

TEST(bundle_retransmits_after_interval_elapses)
{
    /* Once the retransmit interval elapses since the last send, the entry
     * becomes due again and is re-packed (retransmit). */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);

    u8 body[] = { 0x06, 0x99 };
    ASSERT(bc_reliable_add(&q, body, sizeof(body), 4, 0));

    bc_outbox_t ob1;
    bc_outbox_init(&ob1);
    ASSERT_EQ_INT(bc_reliable_pack_due(&q, &ob1, 0, NULL), 1);  /* first send */

    /* Still within the interval -> not due. */
    bc_outbox_t ob2;
    bc_outbox_init(&ob2);
    ASSERT_EQ_INT(
        bc_reliable_pack_due(&q, &ob2, BC_RELIABLE_RETRANSMIT_MS - 1, NULL), 0);

    /* Interval elapsed -> due again (retransmit), retransmit count reported. */
    bc_outbox_t ob3;
    bc_outbox_init(&ob3);
    int retx = 0;
    int packed = bc_reliable_pack_due(&q, &ob3, BC_RELIABLE_RETRANSMIT_MS, &retx);
    ASSERT_EQ_INT(packed, 1);
    ASSERT_EQ_INT(retx, 1);          /* counted as a retransmit, not first send */
    ASSERT_EQ_INT(q.entries[0].retries, 1);
}

/* === ACK removal === */

TEST(bundle_ack_removes_message_so_it_is_not_resent)
{
    /* An incoming ACK prunes the matching reliable_out entry; after the
     * retransmit interval elapses it is NOT re-packed because it is gone.  ACK
     * removal and bundling share the single reliable_out queue. */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);

    u8 a[] = { 0x06, 0x01 };
    u8 b[] = { 0x07, 0x02 };
    ASSERT(bc_reliable_add(&q, a, sizeof(a), 10, 0));
    ASSERT(bc_reliable_add(&q, b, sizeof(b), 11, 0));

    /* First send of both at t=0. */
    bc_outbox_t ob1;
    bc_outbox_init(&ob1);
    ASSERT_EQ_INT(bc_reliable_pack_due(&q, &ob1, 0, NULL), 2);

    /* Wire seq is (counter << 8); the ACK references the high byte (counter).
     * bc_reliable_ack matches on the stored seq directly, so ACK seq 10. */
    ASSERT(bc_reliable_ack(&q, 10));
    ASSERT_EQ_INT(q.count, 1);  /* one pruned, one remains */

    /* After the interval, only the un-ACKed entry (seq 11) is re-packed. */
    bc_outbox_t ob2;
    bc_outbox_init(&ob2);
    int packed = bc_reliable_pack_due(&q, &ob2, BC_RELIABLE_RETRANSMIT_MS, NULL);
    ASSERT_EQ_INT(packed, 1);

    /* And the surviving entry is the un-ACKed one. */
    bool found_11 = false, found_10 = false;
    for (int i = 0; i < BC_RELIABLE_QUEUE_SIZE; i++) {
        if (!q.entries[i].active) continue;
        if (q.entries[i].seq == 11) found_11 = true;
        if (q.entries[i].seq == 10) found_10 = true;
    }
    ASSERT(found_11);
    ASSERT(!found_10);
}

TEST(bundle_ack_before_first_send_prevents_send)
{
    /* If an ACK arrives before a queued reliable is ever packed (it was
     * delivered via some other coalesced datagram), the entry is removed and
     * never sent. */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);

    u8 body[] = { 0x06, 0x01 };
    ASSERT(bc_reliable_add(&q, body, sizeof(body), 20, 0));
    ASSERT(bc_reliable_ack(&q, 20));
    ASSERT_EQ_INT(q.count, 0);

    bc_outbox_t ob;
    bc_outbox_init(&ob);
    ASSERT_EQ_INT(bc_reliable_pack_due(&q, &ob, 0, NULL), 0);
    ASSERT(!bc_outbox_pending(&ob));
}

/* === MTU / 255 cap behaviour === */

TEST(bundle_respects_mtu_leaves_remainder_queued)
{
    /* Packing breaks (does not drop) when the next reliable would overflow the
     * 512-byte datagram.  The remainder stays due for the next flush. */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    /* Large bodies: each on wire is 5 (reliable hdr) + 200 = 205 bytes.
     * 2-byte datagram header + 205*2 = 412 fits; a third (205) -> 617 > 512.
     * So only 2 fit; the third must remain queued. */
    u8 big[200];
    memset(big, 0x55, sizeof(big));
    for (int i = 0; i < 3; i++)
        ASSERT(bc_reliable_add(&q, big, sizeof(big), (u16)i, 1000));

    int packed = bc_reliable_pack_due(&q, &outbox, 1000, NULL);
    ASSERT_EQ_INT(packed, 2);          /* only two fit this datagram */
    ASSERT_EQ_INT(outbox.msg_count, 2);

    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(&outbox, out, sizeof(out));
    ASSERT(len > 0 && len <= BC_MAX_PACKET_SIZE);

    /* The third entry is still queued (active), not sent, not dropped. */
    ASSERT_EQ_INT(q.count, 3);  /* all three tracked; only two marked sent */
    int sent_count = 0;
    for (int i = 0; i < BC_RELIABLE_QUEUE_SIZE; i++)
        if (q.entries[i].active && q.entries[i].sent) sent_count++;
    ASSERT_EQ_INT(sent_count, 2);
}

/* === MTU / header constants (wire facts) === */

TEST(bundle_wire_constants)
{
    ASSERT_EQ_INT(BC_MAX_PACKET_SIZE, 512);            /* MTU */
    ASSERT_EQ_INT(BC_RELIABLE_MAX_PAYLOAD, 512);
    /* Datagram header is [direction:1][msg_count:1] = 2 bytes (outbox starts
     * its write cursor at 2). */
    bc_outbox_t ob;
    bc_outbox_init(&ob);
    ASSERT_EQ_INT(ob.pos, 2);
    ASSERT_EQ_INT(ob.msg_count, 0);
}

TEST_MAIN_BEGIN()
    RUN(bundle_packs_multiple_reliables_in_one_datagram);
    RUN(bundle_mixes_acks_and_reliables_in_one_datagram);
    RUN(bundle_fresh_reliable_is_due_immediately);
    RUN(bundle_idempotent_within_a_tick);
    RUN(bundle_retransmits_after_interval_elapses);
    RUN(bundle_ack_removes_message_so_it_is_not_resent);
    RUN(bundle_ack_before_first_send_prevents_send);
    RUN(bundle_respects_mtu_leaves_remainder_queued);
    RUN(bundle_wire_constants);
TEST_MAIN_END()
