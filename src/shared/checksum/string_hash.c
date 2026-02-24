#include "openbc/checksum.h"

/* Lookup tables defined in hash_tables.c */
extern const u8 HASH_TABLE_0[256];
extern const u8 HASH_TABLE_1[256];
extern const u8 HASH_TABLE_2[256];
extern const u8 HASH_TABLE_3[256];

/*
 * StringHash -- 4-lane Pearson hash.
 *
 * Uses four 256-byte substitution tables forming Mutually Orthogonal
 * Latin Squares (MOLS). Verified: StringHash("60") == 0x7E0CE243.
 */
u32 string_hash(const char *str)
{
    u8 h0 = 0, h1 = 0, h2 = 0, h3 = 0;

    while (*str) {
        u8 c = (u8)*str++;
        h0 = HASH_TABLE_0[c ^ h0];
        h1 = HASH_TABLE_1[c ^ h1];
        h2 = HASH_TABLE_2[c ^ h2];
        h3 = HASH_TABLE_3[c ^ h3];
    }

    return ((u32)h0 << 24) | ((u32)h1 << 16) | ((u32)h2 << 8) | (u32)h3;
}
