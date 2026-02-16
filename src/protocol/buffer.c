#include "openbc/buffer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void bc_buf_init(bc_buffer_t *buf, u8 *data, size_t capacity)
{
    buf->data         = data;
    buf->capacity     = capacity;
    buf->pos          = 0;
    buf->bit_bookmark = 0;
    buf->bit_count    = 0;
}

bool bc_buf_alloc(bc_buffer_t *buf, size_t capacity)
{
    u8 *data = (u8 *)calloc(1, capacity);
    if (!data) return false;
    bc_buf_init(buf, data, capacity);
    return true;
}

void bc_buf_free(bc_buffer_t *buf)
{
    free(buf->data);
    buf->data     = NULL;
    buf->capacity = 0;
    buf->pos      = 0;
}

void bc_buf_reset(bc_buffer_t *buf)
{
    buf->pos          = 0;
    buf->bit_bookmark = 0;
    buf->bit_count    = 0;
}

size_t bc_buf_remaining(const bc_buffer_t *buf)
{
    return (buf->pos < buf->capacity) ? buf->capacity - buf->pos : 0;
}

/* --- Write primitives --- */

bool bc_buf_write_u8(bc_buffer_t *buf, u8 val)
{
    if (buf->pos + 1 > buf->capacity) return false;
    buf->data[buf->pos++] = val;
    return true;
}

bool bc_buf_write_u16(bc_buffer_t *buf, u16 val)
{
    if (buf->pos + 2 > buf->capacity) return false;
    buf->data[buf->pos++] = (u8)(val & 0xFF);
    buf->data[buf->pos++] = (u8)((val >> 8) & 0xFF);
    return true;
}

bool bc_buf_write_u32(bc_buffer_t *buf, u32 val)
{
    if (buf->pos + 4 > buf->capacity) return false;
    buf->data[buf->pos++] = (u8)(val & 0xFF);
    buf->data[buf->pos++] = (u8)((val >> 8) & 0xFF);
    buf->data[buf->pos++] = (u8)((val >> 16) & 0xFF);
    buf->data[buf->pos++] = (u8)((val >> 24) & 0xFF);
    return true;
}

bool bc_buf_write_i32(bc_buffer_t *buf, i32 val)
{
    return bc_buf_write_u32(buf, (u32)val);
}

bool bc_buf_write_f32(bc_buffer_t *buf, f32 val)
{
    u32 bits;
    memcpy(&bits, &val, 4);
    return bc_buf_write_i32(buf, (i32)bits);
}

bool bc_buf_write_bytes(bc_buffer_t *buf, const u8 *src, size_t len)
{
    if (buf->pos + len > buf->capacity) return false;
    memcpy(buf->data + buf->pos, src, len);
    buf->pos += len;
    return true;
}

/*
 * WriteBit -- pack booleans into a shared byte.
 *
 * Original TGBufferStream packs up to 5 bits per byte:
 *   Byte layout: [count:3][bits:5]
 *   count (bits 7-5): number of booleans packed (stored as count-1)
 *   bits  (bits 4-0): boolean values, one per bit position
 *
 * First WriteBit in a group: write a placeholder byte, record bookmark.
 * Subsequent WriteBit calls: update the byte at the bookmark position.
 * After 5 bits or the next non-bit write: the pack is finalized.
 */
bool bc_buf_write_bit(bc_buffer_t *buf, bool val)
{
    if (buf->bit_count == 0) {
        /* Start a new bit-pack group */
        if (buf->pos + 1 > buf->capacity) return false;
        buf->bit_bookmark = buf->pos;
        buf->data[buf->pos++] = 0;
        buf->bit_count = 1;
        /* Store: count=0 (meaning 1 bit), value in bit 0 */
        buf->data[buf->bit_bookmark] = (0 << 5) | (val ? 1 : 0);
    } else if (buf->bit_count < 5) {
        /* Add to existing bit-pack group */
        u8 byte = buf->data[buf->bit_bookmark];
        if (val) {
            byte |= (1 << buf->bit_count);
        }
        buf->bit_count++;
        byte = (byte & 0x1F) | (((buf->bit_count - 1) & 0x7) << 5);
        buf->data[buf->bit_bookmark] = byte;
    } else {
        /* Current group is full (5 bits), start a new one */
        buf->bit_count = 0;
        return bc_buf_write_bit(buf, val);
    }
    return true;
}

/* --- Read primitives --- */

bool bc_buf_read_u8(bc_buffer_t *buf, u8 *out)
{
    if (buf->pos + 1 > buf->capacity) return false;
    *out = buf->data[buf->pos++];
    return true;
}

bool bc_buf_read_u16(bc_buffer_t *buf, u16 *out)
{
    if (buf->pos + 2 > buf->capacity) return false;
    *out = (u16)buf->data[buf->pos]
         | ((u16)buf->data[buf->pos + 1] << 8);
    buf->pos += 2;
    return true;
}

bool bc_buf_read_u32(bc_buffer_t *buf, u32 *out)
{
    if (buf->pos + 4 > buf->capacity) return false;
    u32 v = (u32)buf->data[buf->pos]
          | ((u32)buf->data[buf->pos + 1] << 8)
          | ((u32)buf->data[buf->pos + 2] << 16)
          | ((u32)buf->data[buf->pos + 3] << 24);
    buf->pos += 4;
    *out = v;
    return true;
}

bool bc_buf_read_i32(bc_buffer_t *buf, i32 *out)
{
    u32 v;
    if (!bc_buf_read_u32(buf, &v)) return false;
    *out = (i32)v;
    return true;
}

bool bc_buf_read_f32(bc_buffer_t *buf, f32 *out)
{
    i32 bits;
    if (!bc_buf_read_i32(buf, &bits)) return false;
    memcpy(out, &bits, 4);
    return true;
}

bool bc_buf_read_bytes(bc_buffer_t *buf, u8 *dst, size_t len)
{
    if (buf->pos + len > buf->capacity) return false;
    memcpy(dst, buf->data + buf->pos, len);
    buf->pos += len;
    return true;
}

bool bc_buf_read_bit(bc_buffer_t *buf, bool *out)
{
    if (buf->bit_count == 0) {
        /* Read a new bit-pack byte */
        if (buf->pos + 1 > buf->capacity) return false;
        u8 byte = buf->data[buf->pos++];
        buf->bit_bookmark = buf->pos - 1;
        u8 count = ((byte >> 5) & 0x7) + 1;  /* stored as count-1 */
        buf->bit_count = count;
        /* Read bit 0 */
        *out = (byte & 1) != 0;
        buf->bit_count--;  /* consumed one bit */
    } else {
        /* Read next bit from current group */
        u8 byte = buf->data[buf->bit_bookmark];
        u8 total = ((byte >> 5) & 0x7) + 1;
        u8 bit_idx = total - buf->bit_count;
        *out = ((byte >> bit_idx) & 1) != 0;
        buf->bit_count--;
    }
    return true;
}

/* --- CompressedFloat16 (logarithmic 16-bit float) ---
 *
 * Extracted from FUN_006d3a90 (encode) and FUN_006d3b30 (decode).
 * Constants from stbc.exe .rdata section:
 *   BASE = 0.001f  (DAT_00888b4c)
 *   MULT = 10.0f   (DAT_0088c548)
 *
 * Scale ranges (8 decades):
 *   0: [0,      0.001)
 *   1: [0.001,  0.01)
 *   2: [0.01,   0.1)
 *   3: [0.1,    1.0)
 *   4: [1.0,    10.0)
 *   5: [10.0,   100.0)
 *   6: [100.0,  1000.0)
 *   7: [1000.0, 10000.0)
 */

#define CF16_BASE  0.001f
#define CF16_MULT  10.0f

u16 bc_cf16_encode(f32 value)
{
    u32 sign_flag = 0;
    if (value < 0.0f) {
        sign_flag = 8;
        value = -value;
    }

    /* Find scale: smallest s such that value < BASE * MULT^s.
     * Track both lo (previous threshold) and hi (current threshold)
     * so we can compute the mantissa relative to the sub-range. */
    u32 scale = 0;
    f32 lo = 0.0f;
    f32 hi = CF16_BASE;
    while (scale < 8) {
        if (value < hi) break;
        lo = hi;
        hi *= CF16_MULT;
        scale++;
    }

    /* Overflow: clamp to scale=7 max mantissa */
    if (scale >= 8) {
        return (u16)((sign_flag | 7) * 0x1000 + 0xFFF);
    }

    /* Compute mantissa relative to sub-range [lo, hi).
     * Matches decode: result = lo + mantissa/4095 * (hi - lo) */
    f32 range = hi - lo;
    i32 mantissa = (range > 0.0f) ? (i32)((value - lo) / range * 4096.0f) : 0;
    if (mantissa > 0xFFF) mantissa = 0xFFF;
    if (mantissa < 0) mantissa = 0;

    return (u16)((sign_flag | scale) * 0x1000 + mantissa);
}

f32 bc_cf16_decode(u16 encoded)
{
    u16 mantissa = encoded & 0xFFF;
    u8 raw_scale = (u8)(encoded >> 12);
    bool is_neg = (raw_scale >> 3) & 1;
    u8 scale = raw_scale & 0x7;

    /* Compute range boundaries */
    f32 range_lo = 0.0f;
    f32 range_hi = CF16_BASE;
    for (u8 i = 0; i < scale; i++) {
        range_lo = range_hi;
        range_hi *= CF16_MULT;
    }

    /* Interpolate within range */
    f32 result = range_lo + ((f32)mantissa / 4095.0f) * (range_hi - range_lo);
    return is_neg ? -result : result;
}

bool bc_buf_write_cf16(bc_buffer_t *buf, f32 value)
{
    return bc_buf_write_u16(buf, bc_cf16_encode(value));
}

bool bc_buf_read_cf16(bc_buffer_t *buf, f32 *out)
{
    u16 raw;
    if (!bc_buf_read_u16(buf, &raw)) return false;
    *out = bc_cf16_decode(raw);
    return true;
}

/* --- CompressedVector3 (direction only, 3 bytes) ---
 *
 * Each component = ftol(component / magnitude * 127.0)
 * Stored as 3 signed bytes. Decode: byte / 127.0f.
 */

bool bc_buf_write_cv3(bc_buffer_t *buf, f32 x, f32 y, f32 z)
{
    f32 mag = sqrtf(x * x + y * y + z * z);
    if (mag < 1e-6f) {
        /* Zero vector: write 0,0,0 */
        return bc_buf_write_u8(buf, 0)
            && bc_buf_write_u8(buf, 0)
            && bc_buf_write_u8(buf, 0);
    }
    i8 dx = (i8)(x / mag * 127.0f);
    i8 dy = (i8)(y / mag * 127.0f);
    i8 dz = (i8)(z / mag * 127.0f);
    return bc_buf_write_u8(buf, (u8)dx)
        && bc_buf_write_u8(buf, (u8)dy)
        && bc_buf_write_u8(buf, (u8)dz);
}

bool bc_buf_read_cv3(bc_buffer_t *buf, f32 *x, f32 *y, f32 *z)
{
    u8 raw_x, raw_y, raw_z;
    if (!bc_buf_read_u8(buf, &raw_x)) return false;
    if (!bc_buf_read_u8(buf, &raw_y)) return false;
    if (!bc_buf_read_u8(buf, &raw_z)) return false;
    *x = (f32)(i8)raw_x / 127.0f;
    *y = (f32)(i8)raw_y / 127.0f;
    *z = (f32)(i8)raw_z / 127.0f;
    return true;
}

/* --- CompressedVector4 (direction + CF16 magnitude, 5 bytes) ---
 *
 * Wire format: [dirX:i8][dirY:i8][dirZ:i8][magnitude:u16(CF16)]
 * Direction bytes encode unit vector, magnitude is CompressedFloat16.
 * Decoded: xyz = direction * magnitude.
 */

bool bc_buf_write_cv4(bc_buffer_t *buf, f32 x, f32 y, f32 z)
{
    f32 mag = sqrtf(x * x + y * y + z * z);
    if (mag < 1e-6f) {
        return bc_buf_write_u8(buf, 0)
            && bc_buf_write_u8(buf, 0)
            && bc_buf_write_u8(buf, 0)
            && bc_buf_write_u16(buf, 0);
    }
    i8 dx = (i8)(x / mag * 127.0f);
    i8 dy = (i8)(y / mag * 127.0f);
    i8 dz = (i8)(z / mag * 127.0f);
    return bc_buf_write_u8(buf, (u8)dx)
        && bc_buf_write_u8(buf, (u8)dy)
        && bc_buf_write_u8(buf, (u8)dz)
        && bc_buf_write_cf16(buf, mag);
}

bool bc_buf_read_cv4(bc_buffer_t *buf, f32 *x, f32 *y, f32 *z)
{
    u8 raw_x, raw_y, raw_z;
    if (!bc_buf_read_u8(buf, &raw_x)) return false;
    if (!bc_buf_read_u8(buf, &raw_y)) return false;
    if (!bc_buf_read_u8(buf, &raw_z)) return false;
    f32 mag;
    if (!bc_buf_read_cf16(buf, &mag)) return false;
    *x = (f32)(i8)raw_x / 127.0f * mag;
    *y = (f32)(i8)raw_y / 127.0f * mag;
    *z = (f32)(i8)raw_z / 127.0f * mag;
    return true;
}
