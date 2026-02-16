#include "test_util.h"
#include "openbc/checksum.h"

/* === StringHash tests === */

TEST(string_hash_empty)
{
    ASSERT_EQ(string_hash(""), 0x00000000);
}

TEST(string_hash_version_60)
{
    /* The critical known value from RE work */
    ASSERT_EQ(string_hash("60"), 0x7E0CE243);
}

TEST(string_hash_known_filenames)
{
    /* Verified via Python reference implementation using extracted tables */
    ASSERT_EQ(string_hash("App.pyc"), 0x373EB677);
    ASSERT_EQ(string_hash("scripts"), 0x4DAFCB2F);
    ASSERT_EQ(string_hash("Autoexec.pyc"), 0x8501E6A1);
}

TEST(string_hash_single_char)
{
    /* Single character: h0 = TABLE_0[c], h1 = TABLE_1[c], etc. */
    u32 h = string_hash("A");
    ASSERT(h != 0);  /* 'A' = 0x41, should produce non-zero result */
}

TEST(string_hash_deterministic)
{
    /* Same input always produces same output */
    u32 h1 = string_hash("ships");
    u32 h2 = string_hash("ships");
    ASSERT_EQ(h1, h2);
}

TEST(string_hash_different_inputs)
{
    /* Different inputs produce different outputs */
    u32 h1 = string_hash("ships");
    u32 h2 = string_hash("Ships");
    ASSERT(h1 != h2);  /* Case-sensitive */
}

/* === FileHash tests === */

TEST(file_hash_empty)
{
    /* Empty data produces 0 */
    ASSERT_EQ(file_hash(NULL, 0), 0x00000000);
}

TEST(file_hash_small_file)
{
    /* File smaller than 4 bytes: all bytes go through remainder path */
    u8 data[] = { 0x41, 0x42, 0x43 };
    u32 h = file_hash(data, 3);
    ASSERT(h != 0);
}

TEST(file_hash_exactly_4_bytes)
{
    /* 1 DWORD, index 0 -- NOT skipped */
    u8 data[] = { 0x01, 0x00, 0x00, 0x00 };
    u32 h = file_hash(data, 4);
    /* hash = 0 ^ 0x00000001 = 1, ROL 1 = 2 */
    ASSERT_EQ(h, 0x00000002);
}

TEST(file_hash_skip_dword1)
{
    /* 2 DWORDs: index 0 processed, index 1 SKIPPED */
    u8 data[] = {
        0x01, 0x00, 0x00, 0x00,  /* DWORD 0: 0x00000001 (processed) */
        0xFF, 0xFF, 0xFF, 0xFF,  /* DWORD 1: 0xFFFFFFFF (SKIPPED) */
    };
    u32 h = file_hash(data, 8);
    /* Only DWORD 0 matters: hash = 0 ^ 1 = 1, ROL 1 = 2 */
    ASSERT_EQ(h, 0x00000002);
}

TEST(file_hash_pyc_timestamp_insensitive)
{
    /* Changing bytes 4-7 should NOT change the hash */
    u8 data1[] = {
        0xAA, 0xBB, 0xCC, 0xDD,  /* DWORD 0: magic */
        0x00, 0x00, 0x00, 0x00,  /* DWORD 1: timestamp A (skipped) */
        0x11, 0x22, 0x33, 0x44,  /* DWORD 2: bytecode */
    };
    u8 data2[] = {
        0xAA, 0xBB, 0xCC, 0xDD,  /* DWORD 0: same magic */
        0xFF, 0xEE, 0xDD, 0xCC,  /* DWORD 1: timestamp B (skipped) */
        0x11, 0x22, 0x33, 0x44,  /* DWORD 2: same bytecode */
    };
    ASSERT_EQ(file_hash(data1, 12), file_hash(data2, 12));
}

TEST(file_hash_remainder_sign_extension)
{
    /* Test MOVSX sign-extension of remainder bytes */
    /* Byte 0x80 = -128 as signed, sign-extends to 0xFFFFFF80 */
    u8 data[] = { 0x80 };
    u32 h = file_hash(data, 1);
    /* hash = 0 ^ 0xFFFFFF80 = 0xFFFFFF80, ROL 1 = 0xFFFFFF01 */
    ASSERT_EQ(h, 0xFFFFFF01);
}

TEST(file_hash_remainder_positive_byte)
{
    /* Byte 0x7F = +127 as signed, sign-extends to 0x0000007F */
    u8 data[] = { 0x7F };
    u32 h = file_hash(data, 1);
    /* hash = 0 ^ 0x7F = 0x7F, ROL 1 = 0xFE */
    ASSERT_EQ(h, 0x000000FE);
}

TEST(file_hash_deterministic)
{
    u8 data[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                  0x09, 0x0A, 0x0B, 0x0C };
    u32 h1 = file_hash(data, 12);
    u32 h2 = file_hash(data, 12);
    ASSERT_EQ(h1, h2);
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    /* StringHash */
    RUN(string_hash_empty);
    RUN(string_hash_version_60);
    RUN(string_hash_known_filenames);
    RUN(string_hash_single_char);
    RUN(string_hash_deterministic);
    RUN(string_hash_different_inputs);

    /* FileHash */
    RUN(file_hash_empty);
    RUN(file_hash_small_file);
    RUN(file_hash_exactly_4_bytes);
    RUN(file_hash_skip_dword1);
    RUN(file_hash_pyc_timestamp_insensitive);
    RUN(file_hash_remainder_sign_extension);
    RUN(file_hash_remainder_positive_byte);
    RUN(file_hash_deterministic);
TEST_MAIN_END()
