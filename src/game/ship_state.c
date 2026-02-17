#include "openbc/ship_state.h"
#include "openbc/buffer.h"
#include "openbc/game_builders.h"
#include <string.h>
#include <math.h>

void bc_ship_init(bc_ship_state_t *ship,
                  const bc_ship_class_t *cls,
                  int class_index,
                  i32 object_id,
                  u8 owner_slot,
                  u8 team_id)
{
    memset(ship, 0, sizeof(*ship));
    ship->class_index = class_index;
    ship->object_id = object_id;
    ship->owner_slot = owner_slot;
    ship->team_id = team_id;
    ship->alive = true;
    ship->tractor_target_id = -1;

    /* Full HP */
    ship->hull_hp = cls->hull_hp;
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
        ship->shield_hp[i] = cls->shield_hp[i];
    }
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        ship->subsystem_hp[i] = cls->subsystems[i].max_condition;
    }

    /* Default orientation: facing forward (+Y), up (+Z) */
    ship->fwd = (bc_vec3_t){0.0f, 1.0f, 0.0f};
    ship->up  = (bc_vec3_t){0.0f, 0.0f, 1.0f};
    ship->quat[0] = 1.0f; /* w=1, identity rotation */

    /* Weapons at full charge */
    int phaser_idx = 0, tube_idx = 0;
    for (int i = 0; i < cls->subsystem_count; i++) {
        const bc_subsystem_def_t *ss = &cls->subsystems[i];
        if (strcmp(ss->type, "phaser") == 0 || strcmp(ss->type, "pulse_weapon") == 0) {
            if (phaser_idx < BC_MAX_PHASER_BANKS) {
                ship->phaser_charge[phaser_idx++] = ss->max_charge;
            }
        } else if (strcmp(ss->type, "torpedo_tube") == 0) {
            if (tube_idx < BC_MAX_TORPEDO_TUBES) {
                ship->torpedo_cooldown[tube_idx++] = 0.0f; /* ready */
            }
        }
    }
}

int bc_ship_serialize(const bc_ship_state_t *ship,
                      const bc_ship_class_t *cls,
                      u8 *buf, int buf_size)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    /* Object ID */
    if (!bc_buf_write_i32(&b, ship->object_id)) return -1;

    /* Species ID (u16) */
    if (!bc_buf_write_u16(&b, cls->species_id)) return -1;

    /* Position (3x f32) */
    if (!bc_buf_write_f32(&b, ship->pos.x)) return -1;
    if (!bc_buf_write_f32(&b, ship->pos.y)) return -1;
    if (!bc_buf_write_f32(&b, ship->pos.z)) return -1;

    /* Quaternion (4x f32) */
    if (!bc_buf_write_f32(&b, ship->quat[0])) return -1;
    if (!bc_buf_write_f32(&b, ship->quat[1])) return -1;
    if (!bc_buf_write_f32(&b, ship->quat[2])) return -1;
    if (!bc_buf_write_f32(&b, ship->quat[3])) return -1;

    /* Forward + Up (6x f32) */
    if (!bc_buf_write_f32(&b, ship->fwd.x)) return -1;
    if (!bc_buf_write_f32(&b, ship->fwd.y)) return -1;
    if (!bc_buf_write_f32(&b, ship->fwd.z)) return -1;
    if (!bc_buf_write_f32(&b, ship->up.x)) return -1;
    if (!bc_buf_write_f32(&b, ship->up.y)) return -1;
    if (!bc_buf_write_f32(&b, ship->up.z)) return -1;

    /* Speed */
    if (!bc_buf_write_f32(&b, ship->speed)) return -1;

    /* Hull HP */
    if (!bc_buf_write_f32(&b, ship->hull_hp)) return -1;

    /* Shield HP (6 facings) */
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
        if (!bc_buf_write_f32(&b, ship->shield_hp[i])) return -1;
    }

    /* Subsystem count + HP per subsystem */
    if (!bc_buf_write_u16(&b, (u16)cls->subsystem_count)) return -1;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (!bc_buf_write_f32(&b, ship->subsystem_hp[i])) return -1;
    }

    /* Cloak state */
    if (!bc_buf_write_u8(&b, ship->cloak_state)) return -1;

    /* Torpedo type */
    if (!bc_buf_write_u8(&b, ship->torpedo_type)) return -1;

    return (int)b.pos;
}

int bc_ship_build_create_packet(const bc_ship_state_t *ship,
                                const bc_ship_class_t *cls,
                                u8 *buf, int buf_size)
{
    /* Serialize the ship blob first into a temp buffer */
    u8 blob[1024];
    int blob_len = bc_ship_serialize(ship, cls, blob, (int)sizeof(blob));
    if (blob_len < 0) return -1;

    return bc_build_object_create_team(buf, buf_size,
                                       ship->owner_slot, ship->team_id,
                                       blob, blob_len);
}

int bc_ship_build_health_update(const bc_ship_state_t *ship,
                                 const bc_ship_class_t *cls,
                                 f32 game_time,
                                 u8 start_idx, int batch_size,
                                 u8 *buf, int buf_size)
{
    if (!ship->alive || cls->subsystem_count == 0) return 0;

    /* Build field data: [startIdx:u8][health_bytes...][6 shield bytes][1 hull byte] */
    u8 field[128];
    int fpos = 0;

    field[fpos++] = start_idx;

    /* Subsystem health bytes (round-robin window) */
    int count = cls->subsystem_count;
    if (batch_size > count) batch_size = count;
    for (int i = 0; i < batch_size; i++) {
        int idx = ((int)start_idx + i) % count;
        f32 max_hp = cls->subsystems[idx].max_condition;
        f32 ratio = (max_hp > 0.0f) ? (ship->subsystem_hp[idx] / max_hp) : 0.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        field[fpos++] = (u8)(ratio * 255.0f);
    }

    /* Shield HP: 6 bytes, each 0-255 mapped to 0.0 - max_shield_hp */
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
        f32 max_sh = cls->shield_hp[i];
        f32 ratio = (max_sh > 0.0f) ? (ship->shield_hp[i] / max_sh) : 0.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        field[fpos++] = (u8)(ratio * 255.0f);
    }

    /* Hull HP: 1 byte, 0-255 mapped to 0.0 - max hull */
    {
        f32 ratio = (cls->hull_hp > 0.0f) ? (ship->hull_hp / cls->hull_hp) : 0.0f;
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        field[fpos++] = (u8)(ratio * 255.0f);
    }

    return bc_build_state_update(buf, buf_size,
                                  ship->object_id, game_time, 0x20,
                                  field, fpos);
}
