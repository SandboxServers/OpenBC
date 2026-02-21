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
    /* Initialize flat subsystem HP */
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        ship->subsystem_hp[i] = cls->subsystems[i].max_condition;
    }
    /* Initialize container HP slots from serialization list.
     * Entry-level containers (e.g. "Torpedoes", "Phasers") and their
     * children (e.g. "Port Impulse", "Starboard Impulse") may have
     * hp_index values beyond subsystem_count -- these are virtual slots
     * allocated by load_serialization_list() for subsystem groups that
     * don't map 1:1 to a flat subsystem.  Without this, children default
     * to 0.0 HP and the client sees 0% health bars. */
    const bc_ss_list_t *sl = &cls->ser_list;
    for (int i = 0; i < sl->count; i++) {
        const bc_ss_entry_t *e = &sl->entries[i];
        /* Entry-level container */
        if (e->hp_index >= cls->subsystem_count && e->hp_index < BC_MAX_SUBSYSTEMS) {
            ship->subsystem_hp[e->hp_index] = e->max_condition;
        }
        /* Child-level containers */
        for (int c = 0; c < e->child_count; c++) {
            int cidx = e->child_hp_index[c];
            if (cidx >= cls->subsystem_count && cidx < BC_MAX_SUBSYSTEMS) {
                ship->subsystem_hp[cidx] = e->child_max_condition[c];
            }
        }
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

    /* Power allocation: all powered entries at 100%, enabled */
    for (int i = 0; i < sl->count && i < BC_SS_MAX_ENTRIES; i++) {
        ship->power_pct[i] = 100;
        ship->subsys_enabled[i] = true;
        ship->efficiency[i] = 1.0f;
    }

    /* Phaser intensity: default MEDIUM */
    ship->phaser_level = 1;

    /* Batteries: start fully charged */
    ship->main_battery = cls->main_battery_limit;
    ship->backup_battery = cls->backup_battery_limit;
    ship->main_conduit_remaining = cls->main_conduit_capacity;
    ship->backup_conduit_remaining = cls->backup_conduit_capacity;
    ship->power_tick_accum = 0.0f;
}

void bc_ship_assign_subsystem_ids(bc_ship_state_t *ship,
                                   const bc_ship_class_t *cls,
                                   i32 *counter)
{
    /* Assign IDs in ser_list order (matches hardpoint script LoadPropertySet order) */
    const bc_ss_list_t *sl = &cls->ser_list;
    for (int i = 0; i < sl->count; i++) {
        ship->subsys_obj_id[sl->entries[i].hp_index] = (*counter)++;
        for (int c = 0; c < sl->entries[i].child_count; c++) {
            int cidx = sl->entries[i].child_hp_index[c];
            if (cidx >= 0 && cidx < BC_MAX_SUBSYSTEMS)
                ship->subsys_obj_id[cidx] = (*counter)++;
        }
    }

    /* Find and record the repair subsystem's object ID */
    ship->repair_subsys_obj_id = -1;
    for (int i = 0; i < cls->subsystem_count && i < BC_MAX_SUBSYSTEMS; i++) {
        if (strcmp(cls->subsystems[i].type, "repair") == 0) {
            ship->repair_subsys_obj_id = ship->subsys_obj_id[i];
            break;
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

