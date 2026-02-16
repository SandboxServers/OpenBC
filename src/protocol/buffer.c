#include "openbc/buffer.h"
#include <stdlib.h>
#include <string.h>

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

bool bc_buf_write_i32(bc_buffer_t *buf, i32 val)
{
    if (buf->pos + 4 > buf->capacity) return false;
    u32 v = (u32)val;
    buf->data[buf->pos++] = (u8)(v & 0xFF);
    buf->data[buf->pos++] = (u8)((v >> 8) & 0xFF);
    buf->data[buf->pos++] = (u8)((v >> 16) & 0xFF);
    buf->data[buf->pos++] = (u8)((v >> 24) & 0xFF);
    return true;
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

bool bc_buf_read_i32(bc_buffer_t *buf, i32 *out)
{
    if (buf->pos + 4 > buf->capacity) return false;
    u32 v = (u32)buf->data[buf->pos]
          | ((u32)buf->data[buf->pos + 1] << 8)
          | ((u32)buf->data[buf->pos + 2] << 16)
          | ((u32)buf->data[buf->pos + 3] << 24);
    buf->pos += 4;
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
