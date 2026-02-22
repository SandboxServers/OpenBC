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

/* === AlbyRules stream cipher tests === */

TEST(cipher_round_trip)
{
    /* Encrypt then decrypt should recover the original plaintext.
     * Byte 0 (direction flag) is preserved unchanged. */
    u8 original[] = { 0x01, 0x01, 0x1C, 0x0F, 0x00, 0x00, 0x00,
                      0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
    u8 data[13];
    memcpy(data, original, 13);

    alby_cipher_encrypt(data, 13);

    /* Byte 0 (direction) should be unchanged */
    ASSERT_EQ(data[0], 0x01);

    /* Bytes 1+ should be different from original */
    ASSERT(memcmp(data + 1, original + 1, 12) != 0);

    /* Decrypt should recover original */
    alby_cipher_decrypt(data, 13);
    ASSERT(memcmp(data, original, 13) == 0);
}

TEST(cipher_byte0_preserved)
{
    /* Direction byte (0xFF for init, 0x01 for server, 0x02 for client)
     * must never be modified by encrypt or decrypt. */
    u8 pkt1[] = { 0xFF, 0x01, 0x03, 0x08 };
    alby_cipher_encrypt(pkt1, 4);
    ASSERT_EQ(pkt1[0], 0xFF);

    u8 pkt2[] = { 0x01, 0x01, 0x00, 0x02 };
    alby_cipher_encrypt(pkt2, 4);
    ASSERT_EQ(pkt2[0], 0x01);

    u8 pkt3[] = { 0x02, 0x01, 0x00, 0x02 };
    alby_cipher_encrypt(pkt3, 4);
    ASSERT_EQ(pkt3[0], 0x02);
}

TEST(cipher_not_simple_xor)
{
    /* The real cipher is a stream cipher with plaintext feedback.
     * Unlike a simple repeating XOR, the same plaintext byte at
     * different positions produces different ciphertext. */
    u8 data[12] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    alby_cipher_encrypt(data, 12);

    /* If it were simple XOR, bytes 1 and 11 would be the same
     * (both are 0x00 XOR'd with the same key position). With the
     * real stream cipher, plaintext feedback changes the keystream. */
    ASSERT(data[1] != data[11]);
}

TEST(cipher_short_packets)
{
    /* 0-byte and 1-byte packets should not crash */
    u8 d0[1] = { 0x42 };
    alby_cipher_encrypt(d0, 0);
    ASSERT_EQ(d0[0], 0x42);

    alby_cipher_encrypt(d0, 1);
    ASSERT_EQ(d0[0], 0x42);  /* 1-byte = only direction, no payload */

    /* 2-byte packet: byte 0 preserved, byte 1 encrypted */
    u8 d2[2] = { 0xFF, 0x00 };
    u8 orig = d2[1];
    alby_cipher_encrypt(d2, 2);
    ASSERT_EQ(d2[0], 0xFF);
    /* Decrypt to recover */
    alby_cipher_decrypt(d2, 2);
    ASSERT_EQ(d2[1], orig);
}

TEST(cipher_per_packet_reset)
{
    /* Each packet starts from fresh cipher state.
     * Encrypting the same plaintext twice should produce identical ciphertext. */
    u8 pkt1[] = { 0x01, 0x01, 0x03, 0x08, 0x01, 0x00 };
    u8 pkt2[] = { 0x01, 0x01, 0x03, 0x08, 0x01, 0x00 };

    alby_cipher_encrypt(pkt1, 6);
    alby_cipher_encrypt(pkt2, 6);

    ASSERT(memcmp(pkt1, pkt2, 6) == 0);
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

    /* Verify wire format: count=1, bit0=1 → 0x21 */
    ASSERT_EQ(mem[0], 0x21);

    bc_buf_reset(&buf);

    bool v;
    ASSERT(bc_buf_read_bit(&buf, &v));
    ASSERT(v == true);
}

TEST(buffer_bit_packing_single_false)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_bit(&buf, false));

    /* Verify wire format: count=1, bit0=0 → 0x20 */
    ASSERT_EQ(mem[0], 0x20);

    bc_buf_reset(&buf);

    bool v;
    ASSERT(bc_buf_read_bit(&buf, &v));
    ASSERT(v == false);
}

TEST(buffer_bit_packing_two_bits)
{
    u8 mem[16];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    ASSERT(bc_buf_write_bit(&buf, true));
    ASSERT(bc_buf_write_bit(&buf, false));

    /* Verify wire format: count=2, bit0=1, bit1=0 → 0x41 */
    ASSERT_EQ(mem[0], 0x41);

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

    /* Verify bit byte wire format: count=2, bit0=1, bit1=0 → 0x41
     * (only 2 bits written before the u8, the 3rd bit (checksumFlag)
     * comes later after the map name -- that starts a new group). */
    ASSERT_EQ(mem[5], 0x41);

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

TEST(buffer_settings_three_bits_wire_format)
{
    /* Verify that the full Settings bit byte matches stock dedi traces.
     * Stock dedi trace shows 0x61 = (3<<5) | 0x01 for:
     * collision=1, friendlyFire=0, checksumCorrection=0 */
    u8 mem[32];
    bc_buffer_t buf;
    bc_buf_init(&buf, mem, sizeof(mem));

    bc_buf_write_bit(&buf, true);   /* collision_damage */
    bc_buf_write_bit(&buf, false);  /* friendly_fire */
    bc_buf_write_bit(&buf, false);  /* checksum_correction */

    ASSERT_EQ(mem[0], 0x61);  /* count=3, bits=0b00001 */

    bc_buf_reset(&buf);

    bool v1, v2, v3;
    ASSERT(bc_buf_read_bit(&buf, &v1));
    ASSERT(bc_buf_read_bit(&buf, &v2));
    ASSERT(bc_buf_read_bit(&buf, &v3));
    ASSERT(v1 == true);
    ASSERT(v2 == false);
    ASSERT(v3 == false);
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

    /* Directory: "scripts/ships" (no trailing slash per trace) */
    u16 dir_len;
    ASSERT(bc_buf_read_u16(&b, &dir_len));
    ASSERT_EQ_INT(dir_len, 13);
    u8 dir[64];
    ASSERT(bc_buf_read_bytes(&b, dir, dir_len));
    ASSERT(memcmp(dir, "scripts/ships", 13) == 0);

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
                                120.5f, true, false, 3,
                                "Multiplayer.Episode.Mission1.Mission1");
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
    ASSERT_EQ_INT(map_len, 37);  /* "Multiplayer.Episode.Mission1.Mission1" */

    u8 map[64];
    ASSERT(bc_buf_read_bytes(&b, map, map_len));
    ASSERT(memcmp(map, "Multiplayer.Episode.Mission1.Mission1", 37) == 0);

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

/* === MissionInit packet tests === */

TEST(mission_init_no_limits)
{
    u8 buf[32];
    int len = bc_mission_init_build(buf, sizeof(buf), 3, 6, -1, 0, -1);
    ASSERT(len > 0);

    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)len);

    u8 opcode;
    ASSERT(bc_buf_read_u8(&b, &opcode));
    ASSERT_EQ(opcode, BC_MSG_MISSION_INIT);

    u8 current_player_count;
    ASSERT(bc_buf_read_u8(&b, &current_player_count));
    ASSERT_EQ(current_player_count, 6);

    u8 system_index;
    ASSERT(bc_buf_read_u8(&b, &system_index));
    ASSERT_EQ(system_index, 3);

    u8 time_limit;
    ASSERT(bc_buf_read_u8(&b, &time_limit));
    ASSERT_EQ(time_limit, 0xFF);  /* no limit */

    u8 frag_limit;
    ASSERT(bc_buf_read_u8(&b, &frag_limit));
    ASSERT_EQ(frag_limit, 0xFF);  /* no limit */

    /* Should be exactly 5 bytes with no limits */
    ASSERT_EQ_INT(len, 5);
}

TEST(mission_init_with_limits)
{
    u8 buf[32];
    int len = bc_mission_init_build(buf, sizeof(buf), 8, 4, 10, 1234, 25);
    ASSERT(len > 0);

    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)len);

    u8 opcode;
    ASSERT(bc_buf_read_u8(&b, &opcode));
    ASSERT_EQ(opcode, BC_MSG_MISSION_INIT);

    u8 current_player_count;
    ASSERT(bc_buf_read_u8(&b, &current_player_count));
    ASSERT_EQ(current_player_count, 4);

    u8 system_index;
    ASSERT(bc_buf_read_u8(&b, &system_index));
    ASSERT_EQ(system_index, 8);  /* Albirea */

    u8 time_limit;
    ASSERT(bc_buf_read_u8(&b, &time_limit));
    ASSERT_EQ(time_limit, 10);

    /* end_time i32 follows when time_limit != 255 */
    i32 end_time;
    ASSERT(bc_buf_read_i32(&b, &end_time));
    ASSERT_EQ_INT(end_time, 1234);

    u8 frag_limit;
    ASSERT(bc_buf_read_u8(&b, &frag_limit));
    ASSERT_EQ(frag_limit, 25);

    /* 1+1+1+1+4+1 = 9 bytes with both limits */
    ASSERT_EQ_INT(len, 9);
}

TEST(mission_init_too_small)
{
    u8 buf[2];
    int len = bc_mission_init_build(buf, sizeof(buf), 1, 6, -1, 0, -1);
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
    bc_buf_write_u8(&b, 0);                     /* subdir_count = 0 */

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
    bc_buf_write_u8(&b, 0);   /* 0 subdirs */

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
    u8 buf[64];
    int len;

    /* DeletePlayerUI: [0x17][game_slot] */
    len = bc_delete_player_ui_build(buf, sizeof(buf), 2);
    ASSERT_EQ_INT(len, 2);
    ASSERT_EQ(buf[0], BC_OP_DELETE_PLAYER_UI);
    ASSERT_EQ(buf[1], 2);

    /* DeletePlayerAnim: [0x18][name_len:u16][name:bytes] */
    len = bc_delete_player_anim_build(buf, sizeof(buf), "Cady");
    ASSERT_EQ_INT(len, 7);  /* 1 + 2 + 4 */
    ASSERT_EQ(buf[0], BC_OP_DELETE_PLAYER_ANIM);
    ASSERT_EQ(buf[1], 4);  /* name_len low byte */
    ASSERT_EQ(buf[2], 0);  /* name_len high byte */
    ASSERT_EQ(buf[3], 'C');
}

/* === Reliable delivery tests === */

TEST(reliable_queue_full)
{
    /* bc_reliable_add returns false when all BC_RELIABLE_QUEUE_SIZE slots
     * are occupied -- verified to surface the LOG_WARN path in bc_queue_reliable */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);

    u8 payload[] = { 0x20, 0x01 };
    for (int i = 0; i < BC_RELIABLE_QUEUE_SIZE; i++) {
        ASSERT(bc_reliable_add(&q, payload, 2, (u16)i, 1000));
    }
    ASSERT_EQ_INT(q.count, BC_RELIABLE_QUEUE_SIZE);

    /* One more should fail -- queue is full */
    ASSERT(!bc_reliable_add(&q, payload, 2, BC_RELIABLE_QUEUE_SIZE, 1000));
    ASSERT_EQ_INT(q.count, BC_RELIABLE_QUEUE_SIZE);  /* count unchanged */
}

TEST(reliable_oversized_payload)
{
    /* bc_reliable_add rejects payloads > BC_RELIABLE_MAX_PAYLOAD */
    bc_reliable_queue_t q;
    bc_reliable_init(&q);

    u8 big[BC_RELIABLE_MAX_PAYLOAD + 1];
    memset(big, 0xBB, sizeof(big));
    ASSERT(!bc_reliable_add(&q, big, BC_RELIABLE_MAX_PAYLOAD + 1, 0, 1000));
    ASSERT_EQ_INT(q.count, 0);

    /* Exactly at the limit should succeed */
    ASSERT(bc_reliable_add(&q, big, BC_RELIABLE_MAX_PAYLOAD, 1, 1000));
    ASSERT_EQ_INT(q.count, 1);
}

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

/* === Fragment reassembly error-path tests === */

TEST(fragment_invalid_total_frags)
{
    /* total_frags < 2 on the first fragment must be rejected and reset the buffer */
    bc_fragment_buf_t frag;
    bc_fragment_reset(&frag);

    /* Claim only 1 total fragment -- invalid, fragmentation implies >= 2 */
    u8 f0[] = { 0, 1, 0xAA, 0xBB };
    ASSERT(!bc_fragment_receive(&frag, f0, 4));
    ASSERT(!frag.active);        /* Buffer must have been reset */
    ASSERT_EQ_INT(frag.buf_len, 0);

    /* total_frags == 0 also invalid */
    u8 f1[] = { 0, 0, 0xCC };
    ASSERT(!bc_fragment_receive(&frag, f1, 3));
    ASSERT(!frag.active);
}

TEST(fragment_first_too_large)
{
    /* First fragment data exceeding BC_FRAGMENT_BUF_SIZE must be rejected */
    bc_fragment_buf_t frag;
    bc_fragment_reset(&frag);

    /* Build a first-fragment with data_len = BC_FRAGMENT_BUF_SIZE + 1 */
    int oversized = BC_FRAGMENT_BUF_SIZE + 1;
    u8 *f0 = malloc((size_t)(oversized + 2));
    ASSERT(f0 != NULL);
    f0[0] = 0;  /* frag_idx */
    f0[1] = 3;  /* total_frags -- valid, but data itself is too large */
    memset(f0 + 2, 0xCC, (size_t)oversized);

    ASSERT(!bc_fragment_receive(&frag, f0, oversized + 2));
    ASSERT(!frag.active);   /* Buffer reset on error */
    ASSERT_EQ_INT(frag.buf_len, 0);
    free(f0);

    /* Exactly at the limit should succeed */
    u8 *f1 = malloc((size_t)(BC_FRAGMENT_BUF_SIZE + 2));
    ASSERT(f1 != NULL);
    f1[0] = 0;
    f1[1] = 2;
    memset(f1 + 2, 0xDD, BC_FRAGMENT_BUF_SIZE);
    ASSERT(!bc_fragment_receive(&frag, f1, BC_FRAGMENT_BUF_SIZE + 2));
    ASSERT(frag.active);  /* Accepted -- waiting for fragment 1 */
    ASSERT_EQ_INT(frag.buf_len, BC_FRAGMENT_BUF_SIZE);
    free(f1);
}

TEST(fragment_overflow_on_continuation)
{
    /* Continuation fragment that would overflow BC_FRAGMENT_BUF_SIZE
     * must be rejected and reset the buffer */
    bc_fragment_buf_t frag;
    bc_fragment_reset(&frag);

    /* First fragment: nearly fills the buffer */
    int first_data = BC_FRAGMENT_BUF_SIZE - 10;
    u8 *f0 = malloc((size_t)(first_data + 2));
    ASSERT(f0 != NULL);
    f0[0] = 0;  /* frag_idx */
    f0[1] = 2;  /* total_frags */
    memset(f0 + 2, 0xAA, (size_t)first_data);
    ASSERT(!bc_fragment_receive(&frag, f0, first_data + 2));
    ASSERT(frag.active);
    ASSERT_EQ_INT(frag.buf_len, first_data);
    free(f0);

    /* Continuation fragment: 11 bytes of data would push buf_len over limit */
    u8 f1[12];
    f1[0] = 1;  /* frag_idx */
    memset(f1 + 1, 0xBB, 11);
    ASSERT(!bc_fragment_receive(&frag, f1, 12));
    ASSERT(!frag.active);   /* Buffer reset on overflow */
    ASSERT_EQ_INT(frag.buf_len, 0);
}

/* === Fragment reassembly tests === */

TEST(fragment_three_part_reassembly)
{
    bc_fragment_buf_t frag;
    bc_fragment_reset(&frag);

    /* Fragment 0: [frag_idx=0][total_frags=3][data: 0xAA 0xBB] */
    u8 f0[] = { 0, 3, 0xAA, 0xBB };
    ASSERT(!bc_fragment_receive(&frag, f0, 4));
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

    /* Fragment 0: [frag_idx=0][total_frags=2][data: 0x21 0x02 ...] -- simulating checksum resp */
    u8 f0[256];
    f0[0] = 0;     /* frag_idx */
    f0[1] = 2;     /* total frags */
    f0[2] = 0x21;  /* opcode (checksum response) */
    for (int i = 3; i < 200; i++) f0[i] = (u8)(i & 0xFF);
    ASSERT(!bc_fragment_receive(&frag, f0, 200));

    /* Fragment 1: [frag_idx=1][more data] */
    u8 f1[100];
    f1[0] = 1;
    for (int i = 1; i < 80; i++) f1[i] = (u8)((i + 100) & 0xFF);
    ASSERT(bc_fragment_receive(&frag, f1, 80));
    ASSERT_EQ_INT(frag.buf_len, 198 + 79);  /* 277 total */
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

/* === Outbox tests === */

TEST(outbox_single_unreliable)
{
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    u8 payload[] = { 0x1C, 0x01, 0x02 };  /* StateUpdate */
    ASSERT(bc_outbox_add_unreliable(&outbox, payload, 3));
    ASSERT(bc_outbox_pending(&outbox));

    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(&outbox, pkt, sizeof(pkt));
    ASSERT(len > 0);

    /* Parse back */
    bc_packet_t parsed;
    ASSERT(bc_transport_parse(pkt, len, &parsed));
    ASSERT_EQ(parsed.direction, BC_DIR_SERVER);
    ASSERT_EQ_INT(parsed.msg_count, 1);
    ASSERT_EQ(parsed.msgs[0].type, BC_TRANSPORT_RELIABLE);  /* 0x32 with flags=0x00 = unreliable */
    ASSERT_EQ(parsed.msgs[0].flags, 0x00);
    ASSERT_EQ_INT(parsed.msgs[0].payload_len, 3);
    ASSERT_EQ(parsed.msgs[0].payload[0], 0x1C);
}

TEST(outbox_multi_message)
{
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    /* Add unreliable */
    u8 unreliable[] = { 0x1C, 0xAA };
    ASSERT(bc_outbox_add_unreliable(&outbox, unreliable, 2));

    /* Add reliable */
    u8 reliable[] = { 0x00, 0x01 };
    ASSERT(bc_outbox_add_reliable(&outbox, reliable, 2, 5));

    /* Add ACK (stock dedi uses flags=0x00 for standard ACKs) */
    ASSERT(bc_outbox_add_ack(&outbox, 0x0300, 0x00));

    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(&outbox, pkt, sizeof(pkt));
    ASSERT(len > 0);

    /* Parse back */
    bc_packet_t parsed;
    ASSERT(bc_transport_parse(pkt, len, &parsed));
    ASSERT_EQ_INT(parsed.msg_count, 3);

    /* Message 0: unreliable (type 0x32 flags=0x00) */
    ASSERT_EQ(parsed.msgs[0].type, BC_TRANSPORT_RELIABLE);
    ASSERT_EQ(parsed.msgs[0].flags, 0x00);
    ASSERT_EQ_INT(parsed.msgs[0].payload_len, 2);

    /* Message 1: reliable with seq=5 */
    ASSERT_EQ(parsed.msgs[1].type, BC_TRANSPORT_RELIABLE);
    ASSERT_EQ_INT(parsed.msgs[1].payload_len, 2);
    ASSERT_EQ(parsed.msgs[1].payload[0], 0x00);
    /* Wire seq: seqHi=5, seqLo=0 → parsed as (5<<8)|0 = 0x0500 */
    ASSERT_EQ_INT(parsed.msgs[1].seq, 0x0500);

    /* Message 2: ACK for wire seq 0x0300 → counter=3 */
    ASSERT_EQ(parsed.msgs[2].type, BC_TRANSPORT_ACK);
    ASSERT_EQ_INT(parsed.msgs[2].seq, 3);
    ASSERT_EQ(parsed.msgs[2].flags, 0x00);
}

TEST(outbox_overflow)
{
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    /* Fill outbox with large unreliable messages until it's full */
    u8 big_payload[200];
    memset(big_payload, 0xAA, sizeof(big_payload));

    /* 200 + 3 header = 203 per message. With 2 byte packet header, ~2.5 messages fit */
    ASSERT(bc_outbox_add_unreliable(&outbox, big_payload, 200));
    ASSERT(bc_outbox_add_unreliable(&outbox, big_payload, 200));
    /* Third should fail (2 + 203 + 203 + 203 = 611 > 512) */
    ASSERT(!bc_outbox_add_unreliable(&outbox, big_payload, 200));

    /* But a small one should still fit */
    u8 small[] = { 0x01 };
    ASSERT(bc_outbox_add_unreliable(&outbox, small, 1));
}

TEST(outbox_flush_resets_for_retry)
{
    /* After bc_outbox_flush_to_buf the outbox is empty and a fresh add
     * succeeds -- this is the flush-and-retry contract that bc_queue_reliable
     * and bc_queue_unreliable depend on */
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    /* Fill outbox with two large messages */
    u8 payload[200];
    memset(payload, 0xAA, sizeof(payload));
    ASSERT(bc_outbox_add_unreliable(&outbox, payload, 200));
    ASSERT(bc_outbox_add_unreliable(&outbox, payload, 200));
    ASSERT(!bc_outbox_add_unreliable(&outbox, payload, 200));  /* now full */

    /* Flush */
    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(&outbox, pkt, sizeof(pkt));
    ASSERT(len > 0);
    ASSERT(!bc_outbox_pending(&outbox));  /* outbox is now empty */

    /* Retry add must now succeed */
    ASSERT(bc_outbox_add_unreliable(&outbox, payload, 200));
}

TEST(outbox_oversized_payload_rejected)
{
    /* The wire format stores totalLen as a u8, so msg_len > 255 is rejected
     * regardless of available outbox space.  For unreliable: 3 + len > 255
     * means len > 252.  For reliable: 5 + len > 255 means len > 250. */
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    u8 big[256];
    memset(big, 0xBB, sizeof(big));

    /* Unreliable: 253 bytes → msg_len=256 > 255 → rejected */
    ASSERT(!bc_outbox_add_unreliable(&outbox, big, 253));
    ASSERT(!bc_outbox_pending(&outbox));

    /* Unreliable: 252 bytes → msg_len=255 → accepted */
    ASSERT(bc_outbox_add_unreliable(&outbox, big, 252));

    bc_outbox_init(&outbox);  /* reset */

    /* Reliable: 251 bytes → msg_len=256 > 255 → rejected */
    ASSERT(!bc_outbox_add_reliable(&outbox, big, 251, 0));
    ASSERT(!bc_outbox_pending(&outbox));

    /* Reliable: 250 bytes → msg_len=255 → accepted */
    ASSERT(bc_outbox_add_reliable(&outbox, big, 250, 0));
}

TEST(outbox_empty_flush)
{
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(&outbox, pkt, sizeof(pkt));
    ASSERT_EQ_INT(len, 0);
    ASSERT(!bc_outbox_pending(&outbox));
}

TEST(outbox_reliable_seq_format)
{
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    u8 payload[] = { 0x20, 0x00 };  /* Checksum request */
    ASSERT(bc_outbox_add_reliable(&outbox, payload, 2, 7));

    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(&outbox, pkt, sizeof(pkt));
    ASSERT(len > 0);

    /* Parse and verify seq wire format */
    bc_packet_t parsed;
    ASSERT(bc_transport_parse(pkt, len, &parsed));
    ASSERT_EQ_INT(parsed.msg_count, 1);
    ASSERT_EQ(parsed.msgs[0].type, BC_TRANSPORT_RELIABLE);
    /* seq=7 → seqHi=7, seqLo=0 → parsed as 0x0700 */
    ASSERT_EQ_INT(parsed.msgs[0].seq, 0x0700);
    ASSERT_EQ(parsed.msgs[0].flags, 0x80);
    ASSERT_EQ_INT(parsed.msgs[0].payload_len, 2);
    ASSERT_EQ(parsed.msgs[0].payload[0], 0x20);
}

/* === Shutdown notify tests === */

TEST(shutdown_notify_format)
{
    u8 pkt[16];
    /* Slot 1, IP 10.10.10.239 = 0x0A0A0AEF in host order,
     * but bc_addr_t.ip is network byte order: 0xEF0A0A0A */
    int len = bc_transport_build_shutdown_notify(pkt, sizeof(pkt), 1, 0xEF0A0A0A);
    ASSERT_EQ_INT(len, 12);
    ASSERT_EQ(pkt[0], BC_DIR_SERVER);    /* direction */
    ASSERT_EQ(pkt[1], 1);                /* msg count */
    ASSERT_EQ(pkt[2], BC_TRANSPORT_CONNECT_ACK);  /* 0x05 */
    ASSERT_EQ(pkt[3], 0x0A);             /* totalLen */
    ASSERT_EQ(pkt[4], 0xC0);             /* flags */
    ASSERT_EQ(pkt[7], 1);                /* slot */
    /* IP bytes in network order */
    ASSERT_EQ(pkt[8],  0x0A);            /* 10 */
    ASSERT_EQ(pkt[9],  0x0A);            /* 10 */
    ASSERT_EQ(pkt[10], 0x0A);            /* 10 */
    ASSERT_EQ(pkt[11], 0xEF);            /* 239 */
}

TEST(shutdown_notify_ip_encoding)
{
    u8 pkt[16];
    /* IP 192.168.0.196 = network byte order 0xC4A8C0 ... wait.
     * network byte order for 192.168.0.196 is bytes: C0 A8 00 C4
     * stored as u32: depends on endianness. On x86 (little-endian),
     * u32 = 0xC400A8C0 stores as bytes C0 A8 00 C4 in memory.
     * Our bc_addr_t.ip is in network byte order already. */
    u32 ip_be = 0xC400A8C0;  /* 192.168.0.196 on little-endian */
    int len = bc_transport_build_shutdown_notify(pkt, sizeof(pkt), 3, ip_be);
    ASSERT_EQ_INT(len, 12);
    ASSERT_EQ(pkt[7], 3);   /* slot */
    ASSERT_EQ(pkt[8],  0xC0);  /* 192 */
    ASSERT_EQ(pkt[9],  0xA8);  /* 168 */
    ASSERT_EQ(pkt[10], 0x00);  /* 0 */
    ASSERT_EQ(pkt[11], 0xC4);  /* 196 */
}

TEST(outbox_keepalive)
{
    bc_outbox_t outbox;
    bc_outbox_init(&outbox);

    ASSERT(bc_outbox_add_keepalive(&outbox));

    u8 pkt[BC_MAX_PACKET_SIZE];
    int len = bc_outbox_flush_to_buf(&outbox, pkt, sizeof(pkt));
    ASSERT(len > 0);

    /* Parse back: should be a single type 0x00 message */
    bc_packet_t parsed;
    ASSERT(bc_transport_parse(pkt, len, &parsed));
    ASSERT_EQ_INT(parsed.msg_count, 1);
    ASSERT_EQ(parsed.msgs[0].type, BC_TRANSPORT_KEEPALIVE);
    /* Keepalive totalLen=2 → payload_len=0 (2 - 2 header bytes) */
    ASSERT_EQ_INT(parsed.msgs[0].payload_len, 0);
}

/* === Direction byte tests === */

TEST(direction_byte_client_encoding)
{
    /* Client direction byte = BC_DIR_CLIENT + slot_index */
    ASSERT_EQ(BC_DIR_CLIENT + 0, 0x02);
    ASSERT_EQ(BC_DIR_CLIENT + 1, 0x03);
    ASSERT_EQ(BC_DIR_CLIENT + 2, 0x04);
    ASSERT_EQ(BC_DIR_CLIENT + 3, 0x05);
    ASSERT_EQ(BC_DIR_CLIENT + 4, 0x06);
    ASSERT_EQ(BC_DIR_CLIENT + 5, 0x07);
}

/* === 5th checksum round tests === */

TEST(checksum_round_0xff_build)
{
    u8 buf[64];
    int len = bc_checksum_request_final_build(buf, sizeof(buf));
    ASSERT(len > 2);  /* Full request, not minimal */

    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)len);

    u8 opcode;
    ASSERT(bc_buf_read_u8(&b, &opcode));
    ASSERT_EQ(opcode, BC_OP_CHECKSUM_REQ);

    u8 idx;
    ASSERT(bc_buf_read_u8(&b, &idx));
    ASSERT_EQ(idx, 0xFF);

    /* Directory: "Scripts/Multiplayer" (capital S) */
    u16 dir_len;
    ASSERT(bc_buf_read_u16(&b, &dir_len));
    ASSERT_EQ_INT(dir_len, 19);
    u8 dir[64];
    ASSERT(bc_buf_read_bytes(&b, dir, dir_len));
    ASSERT(memcmp(dir, "Scripts/Multiplayer", 19) == 0);

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

/* === Run all tests === */

TEST_MAIN_BEGIN()
    /* Cipher */
    RUN(cipher_round_trip);
    RUN(cipher_byte0_preserved);
    RUN(cipher_not_simple_xor);
    RUN(cipher_short_packets);
    RUN(cipher_per_packet_reset);

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
    RUN(buffer_bit_packing_single_false);
    RUN(buffer_bit_packing_two_bits);
    RUN(buffer_settings_packet_bits);
    RUN(buffer_settings_three_bits_wire_format);
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

    /* MissionInit */
    RUN(mission_init_no_limits);
    RUN(mission_init_with_limits);
    RUN(mission_init_too_small);

    /* Checksum response */
    RUN(checksum_resp_parse_round0);
    RUN(checksum_resp_validate_ok);
    RUN(checksum_resp_validate_content_mismatch);
    RUN(checksum_resp_validate_dir_mismatch);
    RUN(checksum_resp_validate_file_missing);

    /* Fragment reassembly -- error paths */
    RUN(fragment_invalid_total_frags);
    RUN(fragment_first_too_large);
    RUN(fragment_overflow_on_continuation);
    /* Fragment reassembly -- happy paths */
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
    RUN(reliable_queue_full);
    RUN(reliable_oversized_payload);
    RUN(reliable_add_and_ack);
    RUN(reliable_timeout_detection);
    RUN(reliable_retransmit);

    /* Transport wire format */
    RUN(transport_reliable_seq_wire_format);
    RUN(transport_ack_references_seqhi);
    RUN(transport_reliable_parse_round_trip);
    RUN(transport_ack_round_trip);

    /* Outbox */
    RUN(outbox_single_unreliable);
    RUN(outbox_multi_message);
    RUN(outbox_overflow);
    RUN(outbox_flush_resets_for_retry);
    RUN(outbox_oversized_payload_rejected);
    RUN(outbox_empty_flush);
    RUN(outbox_reliable_seq_format);
    RUN(outbox_keepalive);

    /* Shutdown notify */
    RUN(shutdown_notify_format);
    RUN(shutdown_notify_ip_encoding);

    /* Direction byte */
    RUN(direction_byte_client_encoding);

    /* 5th checksum round */
    RUN(checksum_round_0xff_build);
TEST_MAIN_END()
