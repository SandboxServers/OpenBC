#include "test_util.h"
#include "openbc/client_transport.h"
#include "openbc/transport.h"
#include "openbc/handshake.h"
#include "openbc/opcodes.h"
#include "openbc/checksum.h"
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

/* === Parse checksum request === */

TEST(parse_checksum_request_round0)
{
    /* Build a checksum request for round 0 (scripts/, App.pyc, non-recursive) */
    u8 req_buf[128];
    int req_len = bc_checksum_request_build(req_buf, sizeof(req_buf), 0);
    ASSERT(req_len > 0);

    bc_checksum_request_t req;
    ASSERT(bc_client_parse_checksum_request(req_buf, req_len, &req));
    ASSERT_EQ(req.round, 0);
    ASSERT(strcmp(req.directory, "scripts/") == 0);
    ASSERT(strcmp(req.filter, "App.pyc") == 0);
    ASSERT(!req.recursive);
}

TEST(parse_checksum_request_round2_recursive)
{
    u8 req_buf[128];
    int req_len = bc_checksum_request_build(req_buf, sizeof(req_buf), 2);
    ASSERT(req_len > 0);

    bc_checksum_request_t req;
    ASSERT(bc_client_parse_checksum_request(req_buf, req_len, &req));
    ASSERT_EQ(req.round, 2);
    ASSERT(strcmp(req.directory, "scripts/ships/") == 0);
    ASSERT(strcmp(req.filter, "*.pyc") == 0);
    ASSERT(req.recursive);
}

/* === Wire-accurate checksum response === */

TEST(checksum_resp_roundtrip)
{
    /* Build a response with 2 files */
    bc_client_file_hash_t files[2] = {
        { .name_hash = 0x12345678, .content_hash = 0xAABBCCDD },
        { .name_hash = 0x87654321, .content_hash = 0x11223344 },
    };

    u8 buf[512];
    int len = bc_client_build_checksum_resp(buf, sizeof(buf), 0,
                                             0xDEADBEEF, 0xCAFEBABE,
                                             files, 2);
    ASSERT(len > 0);
    /* Header: [0x21][0][ref:4][dir:4] = 10, file_count:2 = 2, 2 files * 8 = 16 */
    ASSERT_EQ(len, 10 + 2 + 16);

    /* Parse it back with the server's parser */
    bc_checksum_resp_t resp;
    ASSERT(bc_checksum_response_parse(&resp, buf, len));
    ASSERT_EQ(resp.round_index, 0);
    ASSERT_EQ(resp.ref_hash, 0xDEADBEEF);
    ASSERT_EQ(resp.dir_hash, 0xCAFEBABE);
    ASSERT_EQ(resp.file_count, 2);
    ASSERT_EQ(resp.files[0].name_hash, 0x12345678);
    ASSERT_EQ(resp.files[0].content_hash, 0xAABBCCDD);
    ASSERT_EQ(resp.files[1].name_hash, 0x87654321);
    ASSERT_EQ(resp.files[1].content_hash, 0x11223344);
}

TEST(checksum_resp_recursive_roundtrip)
{
    bc_client_file_hash_t top_files[1] = {
        { .name_hash = 0xAAAAAAAA, .content_hash = 0xBBBBBBBB },
    };
    bc_client_subdir_hash_t subdirs[1];
    subdirs[0].name_hash = 0xCCCCCCCC;
    subdirs[0].file_count = 1;
    subdirs[0].files[0].name_hash = 0xDDDDDDDD;
    subdirs[0].files[0].content_hash = 0xEEEEEEEE;

    u8 buf[512];
    int len = bc_client_build_checksum_resp_recursive(
        buf, sizeof(buf), 2,
        0x11111111, 0x22222222,
        top_files, 1, subdirs, 1);
    ASSERT(len > 0);

    bc_checksum_resp_t resp;
    ASSERT(bc_checksum_response_parse(&resp, buf, len));
    ASSERT_EQ(resp.round_index, 2);
    ASSERT_EQ(resp.ref_hash, 0x11111111);
    ASSERT_EQ(resp.dir_hash, 0x22222222);
    ASSERT_EQ(resp.file_count, 1);
    ASSERT_EQ(resp.files[0].name_hash, 0xAAAAAAAA);
    ASSERT_EQ(resp.subdir_count, 1);
    ASSERT_EQ(resp.subdirs[0].name_hash, 0xCCCCCCCC);
    ASSERT_EQ(resp.subdirs[0].data.file_count, 1);
    ASSERT_EQ(resp.subdirs[0].data.files[0].name_hash, 0xDDDDDDDD);
    ASSERT_EQ(resp.subdirs[0].data.files[0].content_hash, 0xEEEEEEEE);
}

TEST(checksum_final_roundtrip)
{
    u8 buf[32];
    int len = bc_client_build_checksum_final(buf, sizeof(buf),
                                              0xFACEFACE, 42);
    ASSERT(len == 10);

    bc_checksum_resp_t resp;
    ASSERT(bc_checksum_response_parse(&resp, buf, len));
    ASSERT_EQ(resp.round_index, 0xFF);
    ASSERT(resp.empty);
    ASSERT_EQ(resp.dir_hash, 0xFACEFACE);
    ASSERT_EQ(resp.file_count, 42);
}

/* === Directory scanning === */

TEST(scan_directory_round0)
{
    /* Scan test fixtures for round 0: scripts/, App.pyc, non-recursive */
    bc_client_dir_scan_t scan;
    ASSERT(bc_client_scan_directory("tests\\fixtures\\", "scripts/",
                                     "App.pyc", false, &scan));
    ASSERT_EQ(scan.file_count, 1);
    ASSERT_EQ(scan.files[0].name_hash, string_hash("App.pyc"));
    ASSERT(scan.files[0].content_hash != 0);
}

TEST(scan_directory_round2_recursive)
{
    /* Scan test fixtures for round 2: scripts/ships/, *.pyc, recursive */
    bc_client_dir_scan_t scan;
    ASSERT(bc_client_scan_directory("tests\\fixtures\\", "scripts/ships/",
                                     "*.pyc", true, &scan));
    ASSERT_EQ(scan.file_count, 1);  /* Galaxy.pyc */
    ASSERT_EQ(scan.files[0].name_hash, string_hash("Galaxy.pyc"));
    ASSERT_EQ(scan.subdir_count, 1); /* Klingon/ */
    ASSERT_EQ(scan.subdirs[0].name_hash, string_hash("Klingon"));
    ASSERT_EQ(scan.subdirs[0].file_count, 1); /* BirdOfPrey.pyc */
}

TEST(scan_and_build_roundtrip)
{
    /* Full roundtrip: scan directory -> build response -> parse -> validate */
    bc_client_dir_scan_t scan;
    ASSERT(bc_client_scan_directory("tests\\fixtures\\", "scripts/",
                                     "App.pyc", false, &scan));
    ASSERT_EQ(scan.file_count, 1);

    /* ref_hash = scan.dir_hash (already stripped trailing slash) */
    u32 ref_hash = scan.dir_hash;

    u8 buf[512];
    int len = bc_client_build_checksum_resp(buf, sizeof(buf), 0,
                                             ref_hash, scan.dir_hash,
                                             scan.files, scan.file_count);
    ASSERT(len > 0);

    /* Parse with server parser */
    bc_checksum_resp_t resp;
    ASSERT(bc_checksum_response_parse(&resp, buf, len));
    ASSERT_EQ(resp.round_index, 0);
    /* dir_hash uses only the leaf directory name (e.g. "scripts") */
    ASSERT_EQ(resp.ref_hash, string_hash("scripts"));
    ASSERT_EQ(resp.dir_hash, string_hash("scripts"));
    ASSERT_EQ(resp.file_count, 1);
    ASSERT_EQ(resp.files[0].name_hash, string_hash("App.pyc"));
    /* Content hash should be a real FileHash of the fixture file */
    bool ok;
    u32 expected = file_hash_from_path("tests\\fixtures\\scripts\\App.pyc", &ok);
    ASSERT(ok);
    ASSERT_EQ(resp.files[0].content_hash, expected);
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    RUN(connect_packet_format);
    RUN(keepalive_name_utf16);
    RUN(client_reliable_direction);
    RUN(client_ack_direction);
    RUN(parse_checksum_request_round0);
    RUN(parse_checksum_request_round2_recursive);
    RUN(checksum_resp_roundtrip);
    RUN(checksum_resp_recursive_roundtrip);
    RUN(checksum_final_roundtrip);
    RUN(scan_directory_round0);
    RUN(scan_directory_round2_recursive);
    RUN(scan_and_build_roundtrip);
TEST_MAIN_END()
