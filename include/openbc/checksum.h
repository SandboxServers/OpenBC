#ifndef OPENBC_CHECKSUM_H
#define OPENBC_CHECKSUM_H

#include "openbc/types.h"

/*
 * StringHash -- 4-lane Pearson hash (FUN_007202e0)
 *
 * Uses four 256-byte substitution tables to produce a 32-bit hash
 * from a string. Used to match directory names and filenames between
 * client and server during checksum exchange.
 *
 * Known value: StringHash("60") == 0x7E0CE243
 */
u32 string_hash(const char *str);

/*
 * FileHash -- rotate-XOR hash (FUN_006a62f0)
 *
 * Hashes file contents by XORing each DWORD and rotating left by 1.
 * Deliberately skips DWORD index 1 (bytes 4-7) to ignore .pyc
 * modification timestamps, so identical bytecode always produces
 * the same hash regardless of compile time.
 *
 * Remaining bytes (len % 4) are sign-extended (MOVSX) before XOR.
 */
u32 file_hash(const u8 *data, size_t len);

/*
 * Convenience: hash a file from disk.
 * Returns 0 on read failure (note: 0 is also a valid hash, but
 * file_hash_from_path sets *ok = false on failure).
 */
u32 file_hash_from_path(const char *path, bool *ok);

#endif /* OPENBC_CHECKSUM_H */
