#include "test_util.h"
#include "openbc/cipher.h"
#include "openbc/buffer.h"
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
TEST_MAIN_END()
