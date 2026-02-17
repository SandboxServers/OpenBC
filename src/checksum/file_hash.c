#include "openbc/checksum.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * FileHash -- rotate-XOR over file contents.
 *
 * Algorithm:
 *   hash = 0
 *   for each DWORD i in file (little-endian):
 *       if i == 1: skip (bytes 4-7 = .pyc timestamp)
 *       hash ^= dword[i]
 *       hash = ROL(hash, 1)
 *   for remaining bytes (len % 4):
 *       hash ^= MOVSX(byte)   // sign-extend byte to 32 bits
 *       hash = ROL(hash, 1)
 *   return hash
 */
u32 file_hash(const u8 *data, size_t len)
{
    u32 hash = 0;
    size_t dword_count = len / 4;

    for (size_t i = 0; i < dword_count; i++) {
        if (i == 1) continue;  /* Skip DWORD index 1 (bytes 4-7) */

        /* Read little-endian DWORD */
        u32 dword = (u32)data[i * 4]
                  | ((u32)data[i * 4 + 1] << 8)
                  | ((u32)data[i * 4 + 2] << 16)
                  | ((u32)data[i * 4 + 3] << 24);

        hash ^= dword;
        hash = (hash << 1) | (hash >> 31);  /* ROL 1 */
    }

    /* Handle remaining bytes with MOVSX sign-extension */
    size_t remainder = len % 4;
    if (remainder > 0) {
        const u8 *tail = data + (dword_count * 4);
        for (size_t i = 0; i < remainder; i++) {
            i32 extended = (i8)tail[i];  /* MOVSX: sign-extend byte */
            hash ^= (u32)extended;
            hash = (hash << 1) | (hash >> 31);  /* ROL 1 */
        }
    }

    return hash;
}

u32 file_hash_from_path(const char *path, bool *ok)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (ok) *ok = false;
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        if (ok) *ok = (size == 0);
        return 0;
    }

    u8 *buf = (u8 *)malloc((size_t)size);
    if (!buf) {
        fclose(f);
        if (ok) *ok = false;
        return 0;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    fclose(f);

    if (nread != (size_t)size) {
        free(buf);
        if (ok) *ok = false;
        return 0;
    }

    u32 result = file_hash(buf, (size_t)size);
    free(buf);
    if (ok) *ok = true;
    return result;
}
