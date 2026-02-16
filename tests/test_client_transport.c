#include "test_util.h"
#include "openbc/client_transport.h"
#include "openbc/transport.h"
#include "openbc/opcodes.h"
#include <string.h>

/* === Connect packet === */

TEST(connect_packet_format)
{
    u8 buf[64];
    u32 ip = 0x0100007F; /* 127.0.0.1 in network byte order */
    int len = bc_client_build_connect(buf, sizeof(buf), ip);

    ASSERT(len == 10);
    ASSERT_EQ(buf[0], BC_DIR_INIT);    /* 0xFF */
    ASSERT_EQ(buf[1], 1);              /* 1 message */
    ASSERT_EQ(buf[2], BC_TRANSPORT_CONNECT); /* 0x03 */
    ASSERT_EQ(buf[3], 8);              /* totalLen */
    ASSERT_EQ(buf[4], 0x01);           /* flags */
    ASSERT_EQ(buf[8], 0x7F);           /* IP low byte */
}

/* === Keepalive with name === */

TEST(keepalive_name_utf16)
{
    u8 buf[128];
    int len = bc_client_build_keepalive_name(buf, sizeof(buf), 0,
                                              0x0100007F, "Kirk");

    ASSERT(len > 0);
    ASSERT_EQ(buf[0], BC_DIR_CLIENT + 0); /* 0x02 */
    ASSERT_EQ(buf[1], 1);

    /* Name at offset 12: UTF-16LE "Kirk" + null */
    ASSERT_EQ(buf[12], 'K'); ASSERT_EQ(buf[13], 0x00);
    ASSERT_EQ(buf[14], 'i'); ASSERT_EQ(buf[15], 0x00);
    ASSERT_EQ(buf[16], 'r'); ASSERT_EQ(buf[17], 0x00);
    ASSERT_EQ(buf[18], 'k'); ASSERT_EQ(buf[19], 0x00);
    ASSERT_EQ(buf[20], 0x00); ASSERT_EQ(buf[21], 0x00); /* null term */
}

/* === Client reliable === */

TEST(client_reliable_direction)
{
    u8 payload[] = { 0x19, 0x01, 0x02, 0x03 }; /* fake torpedo */
    u8 buf[64];
    int len = bc_client_build_reliable(buf, sizeof(buf), 1, payload, 4, 5);

    ASSERT(len > 0);
    ASSERT_EQ(buf[0], BC_DIR_CLIENT + 1); /* 0x03 = client slot 1 */
    ASSERT_EQ(buf[1], 1);                 /* 1 message */
    ASSERT_EQ(buf[2], BC_TRANSPORT_RELIABLE); /* 0x32 */
    ASSERT_EQ(buf[4], 0x80);              /* reliable flags */
    ASSERT_EQ(buf[5], 5);                 /* seq counter */
    ASSERT_EQ(buf[6], 0);                 /* seqLo = 0 */
    /* Payload starts at offset 7 */
    ASSERT_EQ(buf[7], 0x19);
}

/* === Client ACK === */

TEST(client_ack_direction)
{
    u8 buf[16];
    /* ACK for wire seq 0x0300 (seqHi=3, seqLo=0) -> counter=3 */
    int len = bc_client_build_ack(buf, sizeof(buf), 2, 0x0300, 0x80);

    ASSERT(len == 6);
    ASSERT_EQ(buf[0], BC_DIR_CLIENT + 2); /* 0x04 */
    ASSERT_EQ(buf[1], 1);
    ASSERT_EQ(buf[2], BC_TRANSPORT_ACK);  /* 0x01 */
    ASSERT_EQ(buf[3], 3);                 /* counter = seqHi */
    ASSERT_EQ(buf[4], 0x00);
    ASSERT_EQ(buf[5], 0x80);              /* flags */
}

/* === Dummy checksum === */

TEST(dummy_checksum_format)
{
    u8 buf[32];
    int len = bc_client_build_dummy_checksum_resp(buf, sizeof(buf), 2);

    ASSERT(len == 12);
    ASSERT_EQ(buf[0], BC_OP_CHECKSUM_RESP); /* 0x21 */
    ASSERT_EQ(buf[1], 2);                   /* round */
    /* All zeros for hashes and file count */
    for (int i = 2; i < 12; i++) {
        ASSERT_EQ(buf[i], 0);
    }

    /* Final round */
    len = bc_client_build_dummy_checksum_final(buf, sizeof(buf));
    ASSERT(len == 10);
    ASSERT_EQ(buf[0], BC_OP_CHECKSUM_RESP);
    ASSERT_EQ(buf[1], 0xFF);
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    RUN(connect_packet_format);
    RUN(keepalive_name_utf16);
    RUN(client_reliable_direction);
    RUN(client_ack_direction);
    RUN(dummy_checksum_format);
TEST_MAIN_END()
