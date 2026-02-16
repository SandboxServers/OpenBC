#include "test_util.h"
#include "openbc/cipher.h"
#include "openbc/buffer.h"
#include "openbc/transport.h"
#include "openbc/reliable.h"
#include "openbc/manifest.h"
#include "openbc/handshake.h"
#include "openbc/opcodes.h"
#include <string.h>
#include <math.h>

/* === AlbyRules cipher tests === */

TEST(cipher_known_key)
{
    /* Verify the cipher XORs with "AlbyRules!" */
    u8 data[10] = {0};
    alby_rules_cipher(data, 10);
    /* XOR of 0 with key = key itself */
    ASSERT_EQ(data[0], 0x41); /* 'A' */
    ASSERT_EQ(data[1], 0x6C); /* 'l' */
    ASSERT_EQ(data[2], 0x62); /* 'b' */
    ASSERT_EQ(data[3], 0x79); /* 'y' */
    ASSERT_EQ(data[4], 0x52); /* 'R' */
    ASSERT_EQ(data[5], 0x75); /* 'u' */
    ASSERT_EQ(data[6], 0x6C); /* 'l' */
    ASSERT_EQ(data[7], 0x65); /* 'e' */
    ASSERT_EQ(data[8], 0x73); /* 's' */
    ASSERT_EQ(data[9], 0x21); /* '!' */
}

TEST(cipher_round_trip)
{
    u8 original[] = "Hello, Bridge Commander!";
    u8 data[24];
    memcpy(data, original, 24);

    alby_rules_cipher(data, 24);

    /* Should be different from original */
    ASSERT(memcmp(data, original, 24) != 0);

    /* Decrypt (same operation) */
    alby_rules_cipher(data, 24);

    /* Should match original */
    ASSERT(memcmp(data, original, 24) == 0);
}

TEST(cipher_key_wraps)
{
    /* Key wraps around every 10 bytes */
    u8 data[20] = {0};
    alby_rules_cipher(data, 20);

    /* Bytes 0-9 and 10-19 should be identical (both XOR with key) */
    ASSERT(memcmp(data, data + 10, 10) == 0);
}

TEST(cipher_empty)
{
    /* Zero-length should be safe */
    u8 data[1] = { 0x42 };
    alby_rules_cipher(data, 0);
    ASSERT_EQ(data[0], 0x42);  /* Unchanged */
}

/* === Buffer stream tests === */

TEST(buffer_write_read_u8)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_u8(&buf, 0x42));
    ASSERT(bc_buf_write_u8(&buf, 0xFF));

    bc_buf_reset(&buf);

    u8 v1, v2;
    ASSERT(bc_buf_read_u8(&buf, &v1));
    ASSERT(bc_buf_read_u8(&buf, &v2));
    ASSERT_EQ(v1, 0x42);
    ASSERT_EQ(v2, 0xFF);
}

TEST(buffer_write_read_u16)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_u16(&buf, 0x5655));  /* BC default port */

    /* Verify little-endian in memory */
    ASSERT_EQ(mem[0], 0x55);
    ASSERT_EQ(mem[1], 0x56);

    bc_buf_reset(&buf);

    u16 v;
    ASSERT(bc_buf_read_u16(&buf, &v));
    ASSERT_EQ(v, 0x5655);
}

TEST(buffer_write_read_i32)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_i32(&buf, 0x3FFFFFFF));  /* Player 0 base ID */

    bc_buf_reset(&buf);

    i32 v;
    ASSERT(bc_buf_read_i32(&buf, &v));
    ASSERT_EQ((u32)v, 0x3FFFFFFF);
}

TEST(buffer_write_read_i32_negative)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_i32(&buf, -1));

    bc_buf_reset(&buf);

    i32 v;
    ASSERT(bc_buf_read_i32(&buf, &v));
    ASSERT_EQ_INT(v, -1);
}

TEST(buffer_write_read_f32)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_f32(&buf, 360.0f));  /* Reliable timeout */

    bc_buf_reset(&buf);

    f32 v;
    ASSERT(bc_buf_read_f32(&buf, &v));
    ASSERT(v == 360.0f);
}

TEST(buffer_write_read_bytes)
{
    u8 mem[32];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    u8 src[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    ASSERT(bc_buf_write_bytes(&buf, src, 4));

    bc_buf_reset(&buf);

    u8 dst[4] = {0};
    ASSERT(bc_buf_read_bytes(&buf, dst, 4));
    ASSERT(memcmp(src, dst, 4) == 0);
}

TEST(buffer_overflow_protection)
{
    u8 mem[2];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_u8(&buf, 0x01));
    ASSERT(bc_buf_write_u8(&buf, 0x02));
    ASSERT(!bc_buf_write_u8(&buf, 0x03));  /* Should fail */
}

TEST(buffer_remaining)
{
    u8 mem[8];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT_EQ_INT((int)bc_buf_remaining(&buf), 8);
    bc_buf_write_u8(&buf, 0);
    ASSERT_EQ_INT((int)bc_buf_remaining(&buf), 7);
    bc_buf_write_i32(&buf, 0);
    ASSERT_EQ_INT((int)bc_buf_remaining(&buf), 3);
}

TEST(buffer_bit_packing_single)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_bit(&buf, true));

    bc_buf_reset(&buf);

    bool v;
    ASSERT(bc_buf_read_bit(&buf, &v));
    ASSERT(v == true);
}

TEST(buffer_bit_packing_two_bits)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_bit(&buf, true));
    ASSERT(bc_buf_write_bit(&buf, false));

    bc_buf_reset(&buf);

    bool v1, v2;
    ASSERT(bc_buf_read_bit(&buf, &v1));
    ASSERT(bc_buf_read_bit(&buf, &v2));
    ASSERT(v1 == true);
    ASSERT(v2 == false);
}

TEST(buffer_settings_packet_bits)
{
    /* Simulate the Settings packet (0x00) bit fields:
     * collision_damage = true, friendly_fire = false */
    u8 mem[32];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    bc_buf_write_u8(&buf, 0x00);       /* opcode */
    bc_buf_write_f32(&buf, 120.5f);    /* game_time */
    bc_buf_write_bit(&buf, true);      /* collision_damage */
    bc_buf_write_bit(&buf, false);     /* friendly_fire */
    bc_buf_write_u8(&buf, 3);          /* player_slot */

    bc_buf_reset(&buf);

    u8 opcode;
    f32 game_time;
    bool collision, friendly;
    u8 slot;

    ASSERT(bc_buf_read_u8(&buf, &opcode));
    ASSERT(bc_buf_read_f32(&buf, &game_time));
    ASSERT(bc_buf_read_bit(&buf, &collision));
    ASSERT(bc_buf_read_bit(&buf, &friendly));
    ASSERT(bc_buf_read_u8(&buf, &slot));

    ASSERT_EQ(opcode, 0x00);
    ASSERT(game_time == 120.5f);
    ASSERT(collision == true);
    ASSERT(friendly == false);
    ASSERT_EQ(slot, 3);
}

TEST(buffer_alloc_free)
{
    bc_buffer_t buf;
    ASSERT(bc_buf_alloc(&buf, 64));
    ASSERT(buf.data != NULL);
    ASSERT_EQ_INT((int)buf.capacity, 64);

    bc_buf_write_u8(&buf, 0x42);
    bc_buf_reset(&buf);

    u8 v;
    bc_buf_read_u8(&buf, &v);
    ASSERT_EQ(v, 0x42);

    bc_buf_free(&buf);
    ASSERT(buf.data == NULL);
}

/* === CompressedFloat16 tests === */

TEST(cf16_zero)
{
    u16 enc = bc_cf16_encode(0.0f);
    f32 dec = bc_cf16_decode(enc);
    ASSERT(fabsf(dec) < 1e-6f);
}

TEST(cf16_small_positive)
{
    /* 0.0005 should encode in scale 0 [0, 0.001) */
    u16 enc = bc_cf16_encode(0.0005f);
    f32 dec = bc_cf16_decode(enc);
    ASSERT(fabsf(dec - 0.0005f) < 0.0001f);
}

TEST(cf16_medium_value)
{
    /* 42.0 should encode in scale 5 [10, 100) */
    u16 enc = bc_cf16_encode(42.0f);
    f32 dec = bc_cf16_decode(enc);
    ASSERT(fabsf(dec - 42.0f) < 0.1f);
}

TEST(cf16_large_value)
{
    /* 5000 should encode in scale 7 [1000, 10000) */
    u16 enc = bc_cf16_encode(5000.0f);
    f32 dec = bc_cf16_decode(enc);
    ASSERT(fabsf(dec - 5000.0f) < 5.0f);
}

TEST(cf16_negative)
{
    u16 enc = bc_cf16_encode(-7.5f);
    f32 dec = bc_cf16_decode(enc);
    ASSERT(dec < 0.0f);
    ASSERT(fabsf(dec - (-7.5f)) < 0.05f);
}

TEST(cf16_sign_bit)
{
    /* Sign is in bit 15 */
    u16 pos = bc_cf16_encode(1.0f);
    u16 neg = bc_cf16_encode(-1.0f);
    ASSERT((pos & 0x8000) == 0);
    ASSERT((neg & 0x8000) != 0);
    /* Magnitude should be the same */
    ASSERT((pos & 0x7FFF) == (neg & 0x7FFF));
}

TEST(cf16_buffer_round_trip)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_cf16(&buf, 120.5f));
    bc_buf_reset(&buf);

    f32 v;
    ASSERT(bc_buf_read_cf16(&buf, &v));
    ASSERT(fabsf(v - 120.5f) < 0.5f);
}

/* === CompressedVector3 tests === */

TEST(cv3_unit_x)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_cv3(&buf, 1.0f, 0.0f, 0.0f));
    ASSERT_EQ_INT((int)buf.pos, 3);  /* 3 bytes */

    bc_buf_reset(&buf);

    f32 x, y, z;
    ASSERT(bc_buf_read_cv3(&buf, &x, &y, &z));
    ASSERT(fabsf(x - 1.0f) < 0.01f);
    ASSERT(fabsf(y) < 0.01f);
    ASSERT(fabsf(z) < 0.01f);
}

TEST(cv3_diagonal)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    /* Diagonal direction: (1,1,1) normalized = (0.577, 0.577, 0.577) */
    ASSERT(bc_buf_write_cv3(&buf, 5.0f, 5.0f, 5.0f));
    bc_buf_reset(&buf);

    f32 x, y, z;
    ASSERT(bc_buf_read_cv3(&buf, &x, &y, &z));
    /* All components should be roughly equal */
    ASSERT(fabsf(x - y) < 0.02f);
    ASSERT(fabsf(y - z) < 0.02f);
    ASSERT(fabsf(x - 0.577f) < 0.02f);
}

TEST(cv3_zero_vector)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_cv3(&buf, 0.0f, 0.0f, 0.0f));
    bc_buf_reset(&buf);

    f32 x, y, z;
    ASSERT(bc_buf_read_cv3(&buf, &x, &y, &z));
    ASSERT(fabsf(x) < 0.01f);
    ASSERT(fabsf(y) < 0.01f);
    ASSERT(fabsf(z) < 0.01f);
}

/* === CompressedVector4 tests === */

TEST(cv4_simple)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    /* (100, 0, 0) -> direction (1,0,0), magnitude 100 */
    ASSERT(bc_buf_write_cv4(&buf, 100.0f, 0.0f, 0.0f));
    ASSERT_EQ_INT((int)buf.pos, 5);  /* 3 dir bytes + 2 CF16 bytes */

    bc_buf_reset(&buf);

    f32 x, y, z;
    ASSERT(bc_buf_read_cv4(&buf, &x, &y, &z));
    ASSERT(fabsf(x - 100.0f) < 1.0f);
    ASSERT(fabsf(y) < 1.0f);
    ASSERT(fabsf(z) < 1.0f);
}

TEST(cv4_diagonal)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    /* (30, 40, 0) -> magnitude 50 */
    ASSERT(bc_buf_write_cv4(&buf, 30.0f, 40.0f, 0.0f));
    bc_buf_reset(&buf);

    f32 x, y, z;
    ASSERT(bc_buf_read_cv4(&buf, &x, &y, &z));
    ASSERT(fabsf(x - 30.0f) < 1.5f);
    ASSERT(fabsf(y - 40.0f) < 1.5f);
    ASSERT(fabsf(z) < 1.0f);
}

TEST(cv4_zero)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_cv4(&buf, 0.0f, 0.0f, 0.0f));
    bc_buf_reset(&buf);

    f32 x, y, z;
    ASSERT(bc_buf_read_cv4(&buf, &x, &y, &z));
    ASSERT(fabsf(x) < 0.01f);
    ASSERT(fabsf(y) < 0.01f);
    ASSERT(fabsf(z) < 0.01f);
}

/* === Handshake / Checksum request tests === */

TEST(checksum_request_round0)
{
    u8 buf[256];
    int len = bc_checksum_request_build(buf, sizeof(buf), 0);
    ASSERT(len > 0);

    /* Parse it back */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)len);

    u8 opcode;
    ASSERT(bc_buf_read_u8(&b, &opcode));
    ASSERT_EQ(opcode, BC_OP_CHECKSUM_REQ);

    u8 idx;
    ASSERT(bc_buf_read_u8(&b, &idx));
    ASSERT_EQ(idx, 0);

    /* Directory: "scripts/" */
    u16 dir_len;
    ASSERT(bc_buf_read_u16(&b, &dir_len));
    ASSERT_EQ_INT(dir_len, 8);
    u8 dir[64];
    ASSERT(bc_buf_read_bytes(&b, dir, dir_len));
    ASSERT(memcmp(dir, "scripts/", 8) == 0);

    /* Filter: "App.pyc" */
    u16 filt_len;
    ASSERT(bc_buf_read_u16(&b, &filt_len));
    ASSERT_EQ_INT(filt_len, 7);
    u8 filt[64];
    ASSERT(bc_buf_read_bytes(&b, filt, filt_len));
    ASSERT(memcmp(filt, "App.pyc", 7) == 0);

    /* Recursive: false */
    bool recursive;
    ASSERT(bc_buf_read_bit(&b, &recursive));
    ASSERT(recursive == false);
}

TEST(checksum_request_round2_recursive)
{
    u8 buf[256];
    int len = bc_checksum_request_build(buf, sizeof(buf), 2);
    ASSERT(len > 0);

    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)len);

    u8 opcode, idx;
    ASSERT(bc_buf_read_u8(&b, &opcode));
    ASSERT(bc_buf_read_u8(&b, &idx));
    ASSERT_EQ(opcode, BC_OP_CHECKSUM_REQ);
    ASSERT_EQ(idx, 2);

    /* Directory: "scripts/ships/" */
    u16 dir_len;
    ASSERT(bc_buf_read_u16(&b, &dir_len));
    ASSERT_EQ_INT(dir_len, 14);
    u8 dir[64];
    ASSERT(bc_buf_read_bytes(&b, dir, dir_len));
    ASSERT(memcmp(dir, "scripts/ships/", 14) == 0);

    /* Filter: "*.pyc" */
    u16 filt_len;
    ASSERT(bc_buf_read_u16(&b, &filt_len));
    ASSERT_EQ_INT(filt_len, 5);
    u8 filt[64];
    ASSERT(bc_buf_read_bytes(&b, filt, filt_len));
    ASSERT(memcmp(filt, "*.pyc", 5) == 0);

    /* Recursive: true */
    bool recursive;
    ASSERT(bc_buf_read_bit(&b, &recursive));
    ASSERT(recursive == true);
}

TEST(checksum_request_invalid_round)
{
    u8 buf[256];
    ASSERT_EQ_INT(bc_checksum_request_build(buf, sizeof(buf), -1), -1);
    ASSERT_EQ_INT(bc_checksum_request_build(buf, sizeof(buf), 4), -1);
}

TEST(checksum_request_all_rounds)
{
    /* All 4 rounds should build successfully */
    u8 buf[256];
    for (int i = 0; i < BC_CHECKSUM_ROUNDS; i++) {
        int len = bc_checksum_request_build(buf, sizeof(buf), i);
        ASSERT(len > 0);
        ASSERT_EQ(buf[0], BC_OP_CHECKSUM_REQ);
        ASSERT_EQ(buf[1], (u8)i);
    }
}

/* === Settings packet tests === */

TEST(settings_build_basic)
{
    u8 buf[256];
    int len = bc_settings_build(buf, sizeof(buf),
                                120.5f, true, false, 3, "DeepSpace9");
    ASSERT(len > 0);

    /* Parse it back */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)len);

    u8 opcode;
    ASSERT(bc_buf_read_u8(&b, &opcode));
    ASSERT_EQ(opcode, BC_OP_SETTINGS);

    f32 game_time;
    ASSERT(bc_buf_read_f32(&b, &game_time));
    ASSERT(game_time == 120.5f);

    bool collision, friendly;
    ASSERT(bc_buf_read_bit(&b, &collision));
    ASSERT(bc_buf_read_bit(&b, &friendly));
    ASSERT(collision == true);
    ASSERT(friendly == false);

    u8 slot;
    ASSERT(bc_buf_read_u8(&b, &slot));
    ASSERT_EQ(slot, 3);

    u16 map_len;
    ASSERT(bc_buf_read_u16(&b, &map_len));
    ASSERT_EQ_INT(map_len, 10);  /* "DeepSpace9" */

    u8 map[64];
    ASSERT(bc_buf_read_bytes(&b, map, map_len));
    ASSERT(memcmp(map, "DeepSpace9", 10) == 0);

    bool checksum_flag;
    ASSERT(bc_buf_read_bit(&b, &checksum_flag));
    ASSERT(checksum_flag == false);
}

TEST(settings_build_different_values)
{
    u8 buf[256];
    int len = bc_settings_build(buf, sizeof(buf),
                                0.0f, false, true, 0, "TestMap");
    ASSERT(len > 0);

    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)len);

    u8 opcode;
    bc_buf_read_u8(&b, &opcode);
    ASSERT_EQ(opcode, BC_OP_SETTINGS);

    f32 game_time;
    bc_buf_read_f32(&b, &game_time);
    ASSERT(game_time == 0.0f);

    bool collision, friendly;
    bc_buf_read_bit(&b, &collision);
    bc_buf_read_bit(&b, &friendly);
    ASSERT(collision == false);
    ASSERT(friendly == true);

    u8 slot;
    bc_buf_read_u8(&b, &slot);
    ASSERT_EQ(slot, 0);
}

/* === GameInit packet tests === */

TEST(gameinit_build)
{
    u8 buf[16];
    int len = bc_gameinit_build(buf, sizeof(buf));
    ASSERT_EQ_INT(len, 1);
    ASSERT_EQ(buf[0], BC_OP_GAME_INIT);
}

TEST(gameinit_build_too_small)
{
    u8 buf[1];
    int len = bc_gameinit_build(buf, 0);
    ASSERT_EQ_INT(len, -1);
}

/* === Checksum response tests === */

/* Helper: build a minimal checksum response for round 0 (scripts/, App.pyc) */
static int build_round0_response(u8 *buf, int buf_size,
                                  u32 dir_hash, u32 name_hash, u32 content_hash)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    bc_buf_write_u8(&b, BC_OP_CHECKSUM_RESP);  /* opcode */
    bc_buf_write_u8(&b, 0);                     /* round index */
    bc_buf_write_u32(&b, 0x12345678);           /* ref_hash */
    bc_buf_write_u32(&b, dir_hash);             /* dir_hash */
    bc_buf_write_u16(&b, 1);                    /* file_count */
    bc_buf_write_u32(&b, name_hash);            /* file name_hash */
    bc_buf_write_u32(&b, content_hash);         /* file content_hash */

    return (int)b.pos;
}

TEST(checksum_resp_parse_round0)
{
    u8 buf[256];
    int len = build_round0_response(buf, sizeof(buf),
                                     0x4DAFCB2F, 0x373EB677, 0xF8A0A740);

    bc_checksum_resp_t resp;
    ASSERT(bc_checksum_response_parse(&resp, buf, len));
    ASSERT_EQ(resp.round_index, 0);
    ASSERT_EQ(resp.dir_hash, 0x4DAFCB2F);
    ASSERT_EQ_INT(resp.file_count, 1);
    ASSERT_EQ(resp.files[0].name_hash, 0x373EB677);
    ASSERT_EQ(resp.files[0].content_hash, 0xF8A0A740);
}

TEST(checksum_resp_validate_ok)
{
    /* Build a manifest dir matching round 0 */
    bc_manifest_dir_t dir;
    memset(&dir, 0, sizeof(dir));
    dir.dir_name_hash = 0x4DAFCB2F;
    dir.file_count = 1;
    dir.files[0].name_hash = 0x373EB677;
    dir.files[0].content_hash = 0xF8A0A740;

    u8 buf[256];
    int len = build_round0_response(buf, sizeof(buf),
                                     0x4DAFCB2F, 0x373EB677, 0xF8A0A740);
    bc_checksum_resp_t resp;
    ASSERT(bc_checksum_response_parse(&resp, buf, len));
    ASSERT_EQ_INT(bc_checksum_response_validate(&resp, &dir), CHECKSUM_OK);
}

TEST(checksum_resp_validate_content_mismatch)
{
    bc_manifest_dir_t dir;
    memset(&dir, 0, sizeof(dir));
    dir.dir_name_hash = 0x4DAFCB2F;
    dir.file_count = 1;
    dir.files[0].name_hash = 0x373EB677;
    dir.files[0].content_hash = 0xF8A0A740;

    /* Send wrong content hash */
    u8 buf[256];
    int len = build_round0_response(buf, sizeof(buf),
                                     0x4DAFCB2F, 0x373EB677, 0xDEADBEEF);
    bc_checksum_resp_t resp;
    ASSERT(bc_checksum_response_parse(&resp, buf, len));
    ASSERT_EQ_INT(bc_checksum_response_validate(&resp, &dir), CHECKSUM_FILE_MISMATCH);
}

TEST(checksum_resp_validate_dir_mismatch)
{
    bc_manifest_dir_t dir;
    memset(&dir, 0, sizeof(dir));
    dir.dir_name_hash = 0x4DAFCB2F;
    dir.file_count = 1;
    dir.files[0].name_hash = 0x373EB677;
    dir.files[0].content_hash = 0xF8A0A740;

    /* Send wrong dir hash */
    u8 buf[256];
    int len = build_round0_response(buf, sizeof(buf),
                                     0xBADBAD00, 0x373EB677, 0xF8A0A740);
    bc_checksum_resp_t resp;
    ASSERT(bc_checksum_response_parse(&resp, buf, len));
    ASSERT_EQ_INT(bc_checksum_response_validate(&resp, &dir), CHECKSUM_DIR_MISMATCH);
}

TEST(checksum_resp_validate_file_missing)
{
    bc_manifest_dir_t dir;
    memset(&dir, 0, sizeof(dir));
    dir.dir_name_hash = 0x4DAFCB2F;
    dir.file_count = 1;
    dir.files[0].name_hash = 0x373EB677;
    dir.files[0].content_hash = 0xF8A0A740;

    /* Send response with no files */
    u8 buf[256];
    bc_buffer_t b;
    bc_buf_init(&b, buf, sizeof(buf));
    bc_buf_write_u8(&b, BC_OP_CHECKSUM_RESP);
    bc_buf_write_u8(&b, 0);
    bc_buf_write_u32(&b, 0x12345678);
    bc_buf_write_u32(&b, 0x4DAFCB2F);
    bc_buf_write_u16(&b, 0);  /* 0 files */

    bc_checksum_resp_t resp;
    ASSERT(bc_checksum_response_parse(&resp, buf, (int)b.pos));
    ASSERT_EQ_INT(bc_checksum_response_validate(&resp, &dir), CHECKSUM_FILE_MISSING);
}

/* === UICollisionSetting tests === */

TEST(ui_collision_build)
{
    u8 buf[16];
    int len = bc_ui_collision_build(buf, sizeof(buf), true);
    ASSERT(len > 0);
    ASSERT_EQ(buf[0], BC_OP_UI_SETTINGS);

    /* Parse back */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)len);

    u8 opcode;
    ASSERT(bc_buf_read_u8(&b, &opcode));
    ASSERT_EQ(opcode, BC_OP_UI_SETTINGS);

    bool collision;
    ASSERT(bc_buf_read_bit(&b, &collision));
    ASSERT(collision == true);
}

/* === BootPlayer and disconnect tests === */

TEST(bootplayer_build_server_full)
{
    u8 buf[16];
    int len = bc_bootplayer_build(buf, sizeof(buf), BC_BOOT_SERVER_FULL);
    ASSERT_EQ_INT(len, 2);
    ASSERT_EQ(buf[0], BC_OP_BOOT_PLAYER);
    ASSERT_EQ(buf[1], BC_BOOT_SERVER_FULL);
}

TEST(bootplayer_build_checksum_fail)
{
    u8 buf[16];
    int len = bc_bootplayer_build(buf, sizeof(buf), BC_BOOT_CHECKSUM);
    ASSERT_EQ_INT(len, 2);
    ASSERT_EQ(buf[0], BC_OP_BOOT_PLAYER);
    ASSERT_EQ(buf[1], BC_BOOT_CHECKSUM);
}

TEST(delete_player_build)
{
    u8 buf[16];
    int len;

    len = bc_delete_player_ui_build(buf, sizeof(buf));
    ASSERT_EQ_INT(len, 1);
    ASSERT_EQ(buf[0], BC_OP_DELETE_PLAYER_UI);

    len = bc_delete_player_anim_build(buf, sizeof(buf));
    ASSERT_EQ_INT(len, 1);
    ASSERT_EQ(buf[0], BC_OP_DELETE_PLAYER_ANIM);
}

/* === Reliable delivery tests === */

TEST(reliable_add_and_ack)
{
    bc_reliable_queue_t q;
    bc_reliable_init(&q);

    u8 payload[] = { 0x20, 0x01 };  /* Checksum request */
    ASSERT(bc_reliable_add(&q, payload, 2, 0x0001, 1000));
    ASSERT_EQ_INT(q.count, 1);

    /* ACK it */
    ASSERT(bc_reliable_ack(&q, 0x0001));
    ASSERT_EQ_INT(q.count, 0);

    /* Double ACK returns false */
    ASSERT(!bc_reliable_ack(&q, 0x0001));
}

TEST(reliable_timeout_detection)
{
    bc_reliable_queue_t q;
    bc_reliable_init(&q);

    u8 payload[] = { 0x00 };
    bc_reliable_add(&q, payload, 1, 0x0001, 1000);

    /* Not timed out initially */
    ASSERT(!bc_reliable_check_timeout(&q));

    /* Simulate max retries */
    q.entries[0].retries = BC_RELIABLE_MAX_RETRIES;
    ASSERT(bc_reliable_check_timeout(&q));
}

TEST(reliable_retransmit)
{
    bc_reliable_queue_t q;
    bc_reliable_init(&q);

    u8 payload[] = { 0x20, 0x00 };
    bc_reliable_add(&q, payload, 2, 0x0005, 1000);

    /* No retransmit needed yet */
    ASSERT_EQ_INT(bc_reliable_check_retransmit(&q, 1500), -1);

    /* After 2 seconds, should trigger retransmit */
    int idx = bc_reliable_check_retransmit(&q, 3001);
    ASSERT(idx >= 0);
    ASSERT_EQ(q.entries[idx].seq, 0x0005);
    ASSERT_EQ_INT(q.entries[idx].retries, 1);
}

/* === Fragment reassembly tests === */

TEST(fragment_three_part_reassembly)
{
    bc_fragment_buf_t frag;
    bc_fragment_reset(&frag);

    /* Fragment 0: [total_frags=3][data: 0xAA 0xBB] */
    u8 f0[] = { 3, 0xAA, 0xBB };
    ASSERT(!bc_fragment_receive(&frag, f0, 3));
    ASSERT(frag.active);
    ASSERT_EQ_INT(frag.frags_expected, 3);
    ASSERT_EQ_INT(frag.frags_received, 1);

    /* Fragment 1: [frag_idx=1][data: 0xCC 0xDD] */
    u8 f1[] = { 1, 0xCC, 0xDD };
    ASSERT(!bc_fragment_receive(&frag, f1, 3));
    ASSERT_EQ_INT(frag.frags_received, 2);

    /* Fragment 2: [frag_idx=2][data: 0xEE] */
    u8 f2[] = { 2, 0xEE };
    ASSERT(bc_fragment_receive(&frag, f2, 2));  /* Complete! */
    ASSERT_EQ_INT(frag.buf_len, 5);
    ASSERT_EQ(frag.buf[0], 0xAA);
    ASSERT_EQ(frag.buf[1], 0xBB);
    ASSERT_EQ(frag.buf[2], 0xCC);
    ASSERT_EQ(frag.buf[3], 0xDD);
    ASSERT_EQ(frag.buf[4], 0xEE);
}

TEST(fragment_two_part_reassembly)
{
    bc_fragment_buf_t frag;
    bc_fragment_reset(&frag);

    /* Fragment 0: [total_frags=2][data: 0x21 0x02 ...] -- simulating checksum resp */
    u8 f0[256];
    f0[0] = 2;  /* total frags */
    f0[1] = 0x21;  /* opcode (checksum response) */
    for (int i = 2; i < 200; i++) f0[i] = (u8)(i & 0xFF);
    ASSERT(!bc_fragment_receive(&frag, f0, 200));

    /* Fragment 1: [frag_idx=1][more data] */
    u8 f1[100];
    f1[0] = 1;
    for (int i = 1; i < 80; i++) f1[i] = (u8)((i + 100) & 0xFF);
    ASSERT(bc_fragment_receive(&frag, f1, 80));
    ASSERT_EQ_INT(frag.buf_len, 199 + 79);  /* 278 total */
}

TEST(fragment_reset)
{
    bc_fragment_buf_t frag;
    bc_fragment_reset(&frag);
    ASSERT(!frag.active);
    ASSERT_EQ_INT(frag.buf_len, 0);
    ASSERT_EQ_INT(frag.frags_expected, 0);
    ASSERT_EQ_INT(frag.frags_received, 0);
}

/* === Transport wire format tests === */

TEST(transport_reliable_seq_wire_format)
{
    u8 pkt[64];
    u8 payload[] = { 0x20, 0x00 };  /* Checksum request round 0 */

    /* seq=0 → wire [seqHi=0x00][seqLo=0x00] */
    int len = bc_transport_build_reliable(pkt, sizeof(pkt), payload, 2, 0);
    ASSERT(len > 0);
    ASSERT_EQ(pkt[0], BC_DIR_SERVER);
    ASSERT_EQ(pkt[1], 1);
    ASSERT_EQ(pkt[2], BC_TRANSPORT_RELIABLE);
    ASSERT_EQ(pkt[4], 0x80);  /* flags */
    ASSERT_EQ(pkt[5], 0x00);  /* seqHi = counter 0 */
    ASSERT_EQ(pkt[6], 0x00);  /* seqLo = 0 */

    /* seq=1 → wire [seqHi=0x01][seqLo=0x00] */
    len = bc_transport_build_reliable(pkt, sizeof(pkt), payload, 2, 1);
    ASSERT(len > 0);
    ASSERT_EQ(pkt[5], 0x01);  /* seqHi = counter 1 */
    ASSERT_EQ(pkt[6], 0x00);  /* seqLo = 0 */

    /* seq=5 → wire [seqHi=0x05][seqLo=0x00] */
    len = bc_transport_build_reliable(pkt, sizeof(pkt), payload, 2, 5);
    ASSERT(len > 0);
    ASSERT_EQ(pkt[5], 0x05);  /* seqHi = counter 5 */
    ASSERT_EQ(pkt[6], 0x00);  /* seqLo = 0 */
}

TEST(transport_ack_references_seqhi)
{
    u8 pkt[16];

    /* ACKing a reliable msg with wire seq=0x0000 (counter 0) */
    int len = bc_transport_build_ack(pkt, sizeof(pkt), 0x0000, 0x80);
    ASSERT_EQ_INT(len, 6);
    ASSERT_EQ(pkt[3], 0x00);  /* ACK byte = 0 */

    /* ACKing a reliable msg with wire seq=0x0100 (counter 1) */
    len = bc_transport_build_ack(pkt, sizeof(pkt), 0x0100, 0x80);
    ASSERT_EQ_INT(len, 6);
    ASSERT_EQ(pkt[3], 0x01);  /* ACK byte = 1 */

    /* ACKing a reliable msg with wire seq=0x0500 (counter 5) */
    len = bc_transport_build_ack(pkt, sizeof(pkt), 0x0500, 0x80);
    ASSERT_EQ_INT(len, 6);
    ASSERT_EQ(pkt[3], 0x05);  /* ACK byte = 5 */
}

TEST(transport_reliable_parse_round_trip)
{
    u8 pkt[64];
    u8 payload[] = { 0x21, 0x00 };  /* Checksum response */

    /* Build reliable with counter=3 */
    int len = bc_transport_build_reliable(pkt, sizeof(pkt), payload, 2, 3);
    ASSERT(len > 0);

    /* Parse it back */
    bc_packet_t parsed;
    ASSERT(bc_transport_parse(pkt, len, &parsed));
    ASSERT_EQ_INT(parsed.msg_count, 1);
    ASSERT_EQ(parsed.msgs[0].type, BC_TRANSPORT_RELIABLE);
    /* Parsed seq = (seqHi << 8) | seqLo = (3 << 8) | 0 = 0x0300 = 768 */
    ASSERT_EQ_INT(parsed.msgs[0].seq, 768);
    ASSERT_EQ_INT(parsed.msgs[0].payload_len, 2);
    ASSERT_EQ(parsed.msgs[0].payload[0], 0x21);
}

TEST(transport_ack_round_trip)
{
    u8 pkt[16];

    /* Build ACK for wire seq=768 (counter 3) */
    int len = bc_transport_build_ack(pkt, sizeof(pkt), 768, 0x00);
    ASSERT_EQ_INT(len, 6);

    /* Parse it back */
    bc_packet_t parsed;
    ASSERT(bc_transport_parse(pkt, len, &parsed));
    ASSERT_EQ_INT(parsed.msg_count, 1);
    ASSERT_EQ(parsed.msgs[0].type, BC_TRANSPORT_ACK);
    /* Parsed ACK seq = 1 byte = counter 3 */
    ASSERT_EQ_INT(parsed.msgs[0].seq, 3);
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    /* Cipher */
    RUN(cipher_known_key);
    RUN(cipher_round_trip);
    RUN(cipher_key_wraps);
    RUN(cipher_empty);

    /* Buffer */
    RUN(buffer_write_read_u8);
    RUN(buffer_write_read_u16);
    RUN(buffer_write_read_i32);
    RUN(buffer_write_read_i32_negative);
    RUN(buffer_write_read_f32);
    RUN(buffer_write_read_bytes);
    RUN(buffer_overflow_protection);
    RUN(buffer_remaining);
    RUN(buffer_bit_packing_single);
    RUN(buffer_bit_packing_two_bits);
    RUN(buffer_settings_packet_bits);
    RUN(buffer_alloc_free);

    /* CompressedFloat16 */
    RUN(cf16_zero);
    RUN(cf16_small_positive);
    RUN(cf16_medium_value);
    RUN(cf16_large_value);
    RUN(cf16_negative);
    RUN(cf16_sign_bit);
    RUN(cf16_buffer_round_trip);

    /* CompressedVector3 */
    RUN(cv3_unit_x);
    RUN(cv3_diagonal);
    RUN(cv3_zero_vector);

    /* CompressedVector4 */
    RUN(cv4_simple);
    RUN(cv4_diagonal);
    RUN(cv4_zero);

    /* Handshake: Checksum requests */
    RUN(checksum_request_round0);
    RUN(checksum_request_round2_recursive);
    RUN(checksum_request_invalid_round);
    RUN(checksum_request_all_rounds);

    /* Handshake: Settings */
    RUN(settings_build_basic);
    RUN(settings_build_different_values);

    /* Handshake: GameInit */
    RUN(gameinit_build);
    RUN(gameinit_build_too_small);

    /* Checksum response */
    RUN(checksum_resp_parse_round0);
    RUN(checksum_resp_validate_ok);
    RUN(checksum_resp_validate_content_mismatch);
    RUN(checksum_resp_validate_dir_mismatch);
    RUN(checksum_resp_validate_file_missing);

    /* Fragment reassembly */
    RUN(fragment_three_part_reassembly);
    RUN(fragment_two_part_reassembly);
    RUN(fragment_reset);

    /* UICollisionSetting */
    RUN(ui_collision_build);

    /* BootPlayer and disconnect */
    RUN(bootplayer_build_server_full);
    RUN(bootplayer_build_checksum_fail);
    RUN(delete_player_build);

    /* Reliable delivery */
    RUN(reliable_add_and_ack);
    RUN(reliable_timeout_detection);
    RUN(reliable_retransmit);

    /* Transport wire format */
    RUN(transport_reliable_seq_wire_format);
    RUN(transport_ack_references_seqhi);
    RUN(transport_reliable_parse_round_trip);
    RUN(transport_ack_round_trip);
TEST_MAIN_END()
