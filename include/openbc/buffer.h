#ifndef OPENBC_BUFFER_H
#define OPENBC_BUFFER_H

#include "openbc/types.h"

/*
 * TGBufferStream equivalent -- position-tracked byte buffer.
 *
 * Provides read/write primitives matching the original TGBufferStream:
 * WriteByte, ReadByte, WriteShort, ReadShort, WriteInt32, ReadInt32,
 * WriteFloat, ReadFloat, WriteBit, ReadBit.
 *
 * All multi-byte values are little-endian.
 * Bit packing uses a bookmark system: up to 5 booleans packed per byte.
 */

typedef struct {
    u8    *data;
    size_t capacity;
    size_t pos;
    /* Bit-pack state: bookmark position in buffer and current bit index */
    size_t bit_bookmark;
    u8     bit_count;    /* Number of bits packed at bookmark (0 = no active pack) */
} bc_buffer_t;

/* Initialize a buffer over existing memory (does not own the memory) */
void bc_buf_init(bc_buffer_t *buf, u8 *data, size_t capacity);

/* Initialize a buffer for writing with freshly allocated memory */
bool bc_buf_alloc(bc_buffer_t *buf, size_t capacity);

/* Free allocated memory (only if created with bc_buf_alloc) */
void bc_buf_free(bc_buffer_t *buf);

/* Reset position to 0 */
void bc_buf_reset(bc_buffer_t *buf);

/* Remaining bytes available for reading */
size_t bc_buf_remaining(const bc_buffer_t *buf);

/* --- Write primitives --- */
bool bc_buf_write_u8(bc_buffer_t *buf, u8 val);
bool bc_buf_write_u16(bc_buffer_t *buf, u16 val);
bool bc_buf_write_u32(bc_buffer_t *buf, u32 val);
bool bc_buf_write_i32(bc_buffer_t *buf, i32 val);
bool bc_buf_write_f32(bc_buffer_t *buf, f32 val);
bool bc_buf_write_bytes(bc_buffer_t *buf, const u8 *src, size_t len);
bool bc_buf_write_bit(bc_buffer_t *buf, bool val);

/* --- Read primitives --- */
bool bc_buf_read_u8(bc_buffer_t *buf, u8 *out);
bool bc_buf_read_u16(bc_buffer_t *buf, u16 *out);
bool bc_buf_read_u32(bc_buffer_t *buf, u32 *out);
bool bc_buf_read_i32(bc_buffer_t *buf, i32 *out);
bool bc_buf_read_f32(bc_buffer_t *buf, f32 *out);
bool bc_buf_read_bytes(bc_buffer_t *buf, u8 *dst, size_t len);
bool bc_buf_read_bit(bc_buffer_t *buf, bool *out);

/* --- Compressed type encoders/decoders ---
 *
 * CompressedFloat16: Logarithmic 16-bit float.
 *   Format: [sign:1][scale:3][mantissa:12]
 *   Ranges: 8 decades from 0.001 to 10000, ~12 bits precision each.
 *   Used for: speed, damage, distances.
 *
 * CompressedVector3: Direction-only vector, 3 bytes.
 *   Each component = signed byte / 127.0.
 *   Used for: orientation (fwd/up), velocity direction.
 *
 * CompressedVector4: Direction + magnitude, 5 bytes.
 *   3 direction bytes + u16 magnitude (CompressedFloat16).
 *   Used for: position deltas, impact positions.
 */

/* Encode/decode a float to/from 16-bit logarithmic format */
u16 bc_cf16_encode(f32 value);
f32 bc_cf16_decode(u16 encoded);

/* Write/read CompressedFloat16 to/from buffer */
bool bc_buf_write_cf16(bc_buffer_t *buf, f32 value);
bool bc_buf_read_cf16(bc_buffer_t *buf, f32 *out);

/* Write/read CompressedVector3 (direction only, 3 bytes) */
bool bc_buf_write_cv3(bc_buffer_t *buf, f32 x, f32 y, f32 z);
bool bc_buf_read_cv3(bc_buffer_t *buf, f32 *x, f32 *y, f32 *z);

/* Write/read CompressedVector4 (direction + CF16 magnitude, 5 bytes) */
bool bc_buf_write_cv4(bc_buffer_t *buf, f32 x, f32 y, f32 z);
bool bc_buf_read_cv4(bc_buffer_t *buf, f32 *x, f32 *y, f32 *z);

#endif /* OPENBC_BUFFER_H */
