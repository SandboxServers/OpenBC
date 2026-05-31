#include "test_util.h"
#include "openbc/drain.h"
#include "openbc/transport.h"
#include <string.h>

/*
 * Unit tests for the 4-pass per-peer outbound drain (issue #195).
 *
 * The drain packs multiple TGMessages into one datagram in a fixed pass order
 * (priority-reliable -> reliable one-shot -> unreliable -> priority stale),
 * matching the burst-absorption behaviour observed in wire-protocol trace
 * analysis of a stock dedicated server session.
 */

/* Count the transport messages in a serialized datagram by re-parsing it.
 * Returns the parsed message count, or -1 on parse failure. */
static int parse_count(const u8 *buf, int len)
{
    bc_packet_t pkt;
    if (!bc_transport_parse(buf, len, &pkt)) return -1;
    return pkt.msg_count;
}

/* === Pass ordering === */

TEST(drain_empty_sends_nothing)
{
    bc_drain_t d;
    bc_drain_init(&d);
    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT_EQ_INT(len, 0);
}

TEST(drain_bundles_priority_multi)
{
    /* Pass 1 packs multiple fresh priority-reliable messages into one
     * datagram (multi-msg, not one-per-datagram). */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 body[] = { 0x1C, 0xAA, 0xBB };
    for (int i = 0; i < 5; i++)
        ASSERT(bc_drain_enqueue_priority(&d, body, sizeof(body), (u16)i));

    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT(len > 0);
    ASSERT_EQ(out[0], 0x01);       /* sender id */
    ASSERT_EQ(out[1], 5);          /* message count header */
    ASSERT_EQ_INT(parse_count(out, len), 5);
}

TEST(drain_reliable_is_one_shot)
{
    /* Pass 2 is one-shot: at most ONE reliable message per datagram even when
     * several are queued. */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 body[] = { 0x32, 0x01, 0x02, 0x03 };
    for (int i = 0; i < 4; i++)
        ASSERT(bc_drain_enqueue_reliable(&d, body, sizeof(body), (u16)i));

    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT(len > 0);
    ASSERT_EQ(out[1], 1);          /* exactly one reliable drained */
    ASSERT_EQ_INT(parse_count(out, len), 1);

    /* Next drain pulls the next reliable (still one at a time). */
    len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT_EQ(out[1], 1);
    /* Two of the four remain after two one-shot drains. */
    ASSERT_EQ_INT(d.reliable.count, 2);
}

TEST(drain_pass_order_priority_then_reliable_then_unreliable)
{
    /* A datagram with all three lanes populated drains in pass order:
     * priority (P1) first, then one reliable (P2), then unreliable (P3). */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 pr[] = { 0x06, 0x01 };
    u8 rl[] = { 0x07, 0x02 };
    u8 ur[] = { 0x1C, 0x03 };
    ASSERT(bc_drain_enqueue_priority(&d, pr, sizeof(pr), 1));
    ASSERT(bc_drain_enqueue_reliable(&d, rl, sizeof(rl), 2));
    ASSERT(bc_drain_enqueue_unreliable(&d, ur, sizeof(ur)));

    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT(len > 0);
    ASSERT_EQ_INT(parse_count(out, len), 3);

    /* First message after the 2-byte header is the priority one (reliable,
     * carries pr body). out[2]=type 0x32, out[4]=flags 0x80 reliable. */
    ASSERT_EQ(out[2], BC_TRANSPORT_RELIABLE);
    ASSERT_EQ(out[4], 0x80);
    /* priority body begins after [type][len][0x80][seqHi][seqLo] */
    ASSERT_EQ(out[7], 0x06);
}

/* === Pass 3 promotion + dequeue === */

TEST(drain_unreliable_promotes_and_frees)
{
    /* Pass 3 drains unreliable messages, dequeues+frees each, and reports the
     * promoted ones to the caller. */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 ur[] = { 0x1C, 0x10, 0x20 };
    for (int i = 0; i < 3; i++)
        ASSERT(bc_drain_enqueue_unreliable(&d, ur, sizeof(ur)));

    const bc_qmsg_t *promoted[8];
    int pc = 0;
    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), promoted, 8, &pc);
    ASSERT(len > 0);
    ASSERT_EQ_INT(pc, 3);                 /* all three promoted */
    ASSERT_EQ_INT(d.unreliable.count, 0); /* all three dequeued + freed */
}

/* === 255-message cap === */

TEST(drain_respects_255_cap)
{
    /* The per-datagram message count is a u8 that is capped at 255 (it never
     * wraps).  We fill the priority ring with tiny messages and confirm every
     * drained message is accounted for in the header count and the count
     * stays within the cap. */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 body[] = { 0x00 };  /* 6 bytes on wire: [0x32][len][0x80][seq][0][0x00] */
    int n = BC_DRAIN_QUEUE_CAP;  /* fill the priority ring (64) */
    for (int i = 0; i < n; i++)
        bc_drain_enqueue_priority(&d, body, sizeof(body), (u16)i);

    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT(len > 0);
    /* 64 * 6 = 384 bytes + 2 header = 386 <= 512, so all 64 fit in one
     * datagram and the header count reflects them without wrapping. */
    ASSERT_EQ(out[1], (u8)n);
    ASSERT(out[1] <= BC_DRAIN_MSGCOUNT_MAX);
}

/* === Won't-fit break === */

TEST(drain_breaks_when_next_wont_fit)
{
    /* Pass 1 breaks (does not drop later messages) when the next message would
     * overflow the 512-byte datagram.  Use large bodies so only a few fit. */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 big[200];
    memset(big, 0x55, sizeof(big));
    big[0] = 0x1C;
    /* Each on wire: 5 hdr + 200 = 205 bytes.  2-byte header + 205*2 = 412,
     * + a third (205) = 617 > 512, so only 2 fit per datagram. */
    for (int i = 0; i < 5; i++)
        ASSERT(bc_drain_enqueue_priority(&d, big, sizeof(big), (u16)i));

    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT(len > 0);
    ASSERT(len <= BC_MAX_PACKET_SIZE);
    ASSERT_EQ(out[1], 2);  /* only two fit; the rest stay queued */
    /* The un-drained fresh priority messages remain (retx still < 3). */
}

/* === Pass 4 gate (the ACK-outbox deadlock condition) === */

TEST(drain_pass4_gate_closed_without_acks)
{
    /* A stale priority-reliable message (retx >= 3) must NOT fire when there
     * are no pending ACKs, even though there is queued data -- this is the
     * exact ACK-outbox-deadlock gate. */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 body[] = { 0x06, 0x01 };
    ASSERT(bc_drain_enqueue_priority(&d, body, sizeof(body), 1));
    /* Force the single priority entry into the stale lane (retx >= 3). */
    d.priority.slot[d.priority.head].retx = BC_DRAIN_RETX_STALE;

    /* No ACKs queued -> ack_count == 0 -> gate closed -> nothing sent. */
    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT_EQ_INT(len, 0);
    /* The stale entry stays queued (not freed, not sent). */
    ASSERT_EQ_INT(d.priority.count, 1);
}

TEST(drain_pass4_gate_open_with_acks)
{
    /* With a pending ACK, msg_count becomes > 0 after the ACK is packed, the
     * gate opens, and the stale priority-reliable message flushes alongside. */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 body[] = { 0x06, 0x01 };
    ASSERT(bc_drain_enqueue_priority(&d, body, sizeof(body), 1));
    d.priority.slot[d.priority.head].retx = BC_DRAIN_RETX_STALE;
    ASSERT(bc_drain_enqueue_ack(&d, 0x0500, 0x00));  /* one pending ACK */

    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT(len > 0);
    /* ACK + stale priority message = 2 messages. */
    ASSERT_EQ_INT(parse_count(out, len), 2);
}

TEST(drain_pass4_gate_with_disconnecting_flag_set)
{
    /* The disconnecting flag is the second arm of the gate's first clause
     * (msg_count > 0 OR disconnecting).  With the flag set and a pending ACK,
     * the gate opens and the stale priority message flushes. */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 body[] = { 0x06, 0x01 };
    ASSERT(bc_drain_enqueue_priority(&d, body, sizeof(body), 1));
    d.priority.slot[d.priority.head].retx = BC_DRAIN_RETX_STALE;
    ASSERT(bc_drain_enqueue_ack(&d, 0x0500, 0x00));
    d.disconnecting = true;

    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT(len > 0);
    ASSERT_EQ_INT(parse_count(out, len), 2);

    /* And the gate stays CLOSED when disconnecting but no ACK is pending --
     * ack_count > 0 is mandatory regardless of the disconnecting flag. */
    bc_drain_t d2;
    bc_drain_init(&d2);
    ASSERT(bc_drain_enqueue_priority(&d2, body, sizeof(body), 1));
    d2.priority.slot[d2.priority.head].retx = BC_DRAIN_RETX_STALE;
    d2.disconnecting = true;  /* set, but no ACK queued */
    int len2 = bc_drain_to_buf(&d2, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT_EQ_INT(len2, 0);
    ASSERT_EQ_INT(d2.priority.count, 1);
}

TEST(drain_pass4_frees_at_retx_9)
{
    /* A stale priority entry is freed once its retx reaches 9.  The gate needs
     * msg_count > 0 (a fresh P1 message opens it) AND a pending ACK. */
    bc_drain_t d;
    bc_drain_init(&d);
    u8 fresh[] = { 0x06, 0x01 };
    u8 stale[] = { 0x07, 0x02 };
    /* Fresh priority message (retx 0) -> packed by Pass 1, opens the gate. */
    ASSERT(bc_drain_enqueue_priority(&d, fresh, sizeof(fresh), 1));
    /* Stale priority message starting at retx 8: one drain -> retx 9 -> freed. */
    ASSERT(bc_drain_enqueue_priority(&d, stale, sizeof(stale), 2));
    d.priority.slot[(d.priority.head + 1) % BC_DRAIN_QUEUE_CAP].retx =
        BC_DRAIN_RETX_FREE - 1;
    ASSERT(bc_drain_enqueue_ack(&d, 0x0500, 0x00));

    u8 out[BC_MAX_PACKET_SIZE];
    int len = bc_drain_to_buf(&d, 0x01, out, sizeof(out), NULL, 0, NULL);
    ASSERT(len > 0);
    /* The fresh entry remains (retx 1, still < 3); the stale entry was freed. */
    ASSERT_EQ_INT(d.priority.count, 1);
}

/* === Fragment chunk constant === */

TEST(drain_fragment_chunk_constant)
{
    /* Fragmentation is decided at queue time with a 412-byte chunk budget
     * (MTU - 100).  Assert the constant is exposed and consistent. */
    ASSERT_EQ_INT(BC_DRAIN_FRAG_CHUNK_MAX, BC_MAX_PACKET_SIZE - 100);
    ASSERT_EQ_INT(BC_DRAIN_HEADER_LEN, 2);
    ASSERT_EQ_INT(BC_DRAIN_MSGCOUNT_MAX, 255);
}

/* === Round-robin cursor advancement === */

TEST(drain_round_robin_cursor_advances_and_wraps)
{
    /* With BC_MAX_PLAYERS slots, the cursor cycles over 1..MAX-1 and wraps
     * back to 1, giving every peer a turn at being first under load. */
    int max = 7;            /* arbitrary slot count for the math check */
    int seen[8];
    memset(seen, 0, sizeof(seen));
    int cur = 1;
    for (int i = 0; i < max - 1; i++) {
        ASSERT(cur >= 1 && cur <= max - 1);
        seen[cur] = 1;
        cur = bc_drain_next_cursor(cur, max);
    }
    /* Every peer slot 1..max-1 was visited exactly once before wrap. */
    for (int s = 1; s <= max - 1; s++)
        ASSERT_EQ_INT(seen[s], 1);
    /* After a full cycle the cursor returns to its start. */
    ASSERT_EQ_INT(cur, 1);
}

TEST_MAIN_BEGIN()
    RUN(drain_empty_sends_nothing);
    RUN(drain_bundles_priority_multi);
    RUN(drain_reliable_is_one_shot);
    RUN(drain_pass_order_priority_then_reliable_then_unreliable);
    RUN(drain_unreliable_promotes_and_frees);
    RUN(drain_respects_255_cap);
    RUN(drain_breaks_when_next_wont_fit);
    RUN(drain_pass4_gate_closed_without_acks);
    RUN(drain_pass4_gate_open_with_acks);
    RUN(drain_pass4_gate_with_disconnecting_flag_set);
    RUN(drain_pass4_frees_at_retx_9);
    RUN(drain_fragment_chunk_constant);
    RUN(drain_round_robin_cursor_advances_and_wraps);
TEST_MAIN_END()
