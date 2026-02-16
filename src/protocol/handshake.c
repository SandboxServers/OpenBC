#include "openbc/handshake.h"
#include "openbc/opcodes.h"
#include <string.h>

/* Checksum round definitions (from RE of FUN_006a39b0) */
static const struct {
    const char *directory;
    const char *filter;
    bool        recursive;
} checksum_rounds[BC_CHECKSUM_ROUNDS] = {
    { "scripts/",         "App.pyc",      false },
    { "scripts/",         "Autoexec.pyc", false },
    { "scripts/ships/",   "*.pyc",        true  },
    { "scripts/mainmenu/","*.pyc",        false },
};

int bc_checksum_request_build(u8 *buf, int buf_size, int round)
{
    if (round < 0 || round >= BC_CHECKSUM_ROUNDS) return -1;

    const char *dir = checksum_rounds[round].directory;
    const char *filter = checksum_rounds[round].filter;
    bool recursive = checksum_rounds[round].recursive;

    u16 dir_len = (u16)strlen(dir);
    u16 filter_len = (u16)strlen(filter);

    /* Use bc_buffer_t for proper bit-packing of the recursive flag */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    /* Wire format: [opcode:u8][index:u8][dirLen:u16][dir:bytes][filterLen:u16][filter:bytes][recursive:bit] */
    if (!bc_buf_write_u8(&b, BC_OP_CHECKSUM_REQ)) return -1;
    if (!bc_buf_write_u8(&b, (u8)round)) return -1;
    if (!bc_buf_write_u16(&b, dir_len)) return -1;
    if (!bc_buf_write_bytes(&b, (const u8 *)dir, dir_len)) return -1;
    if (!bc_buf_write_u16(&b, filter_len)) return -1;
    if (!bc_buf_write_bytes(&b, (const u8 *)filter, filter_len)) return -1;
    if (!bc_buf_write_bit(&b, recursive)) return -1;

    return (int)b.pos;
}

int bc_settings_build(u8 *buf, int buf_size,
                      f32 game_time,
                      bool collision_dmg,
                      bool friendly_fire,
                      u8 player_slot,
                      const char *map_name)
{
    u16 map_len = (u16)strlen(map_name);

    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    /* Wire format from ChecksumCompleteHandler (FUN_006a1b10):
     * [opcode:u8][gameTime:f32][collision:bit][friendly:bit][slot:u8]
     * [mapLen:u16][mapName:bytes][checksumFlag:bit] */
    if (!bc_buf_write_u8(&b, BC_OP_SETTINGS)) return -1;
    if (!bc_buf_write_f32(&b, game_time)) return -1;
    if (!bc_buf_write_bit(&b, collision_dmg)) return -1;
    if (!bc_buf_write_bit(&b, friendly_fire)) return -1;
    if (!bc_buf_write_u8(&b, player_slot)) return -1;
    if (!bc_buf_write_u16(&b, map_len)) return -1;
    if (!bc_buf_write_bytes(&b, (const u8 *)map_name, map_len)) return -1;
    if (!bc_buf_write_bit(&b, false)) return -1;  /* checksumFlag = 0 (no corrections) */

    return (int)b.pos;
}

int bc_gameinit_build(u8 *buf, int buf_size)
{
    if (buf_size < 1) return -1;
    buf[0] = BC_OP_GAME_INIT;
    return 1;
}
