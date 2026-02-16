#include "test_util.h"
#include "openbc/cipher.h"
#include "openbc/buffer.h"
#include <string.h>

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
TEST_MAIN_END()
