#include "openbc/ship_power.h"
#include "openbc/buffer.h"
#include "openbc/game_builders.h"
#include "openbc/opcodes.h"

/* --- Hierarchical health serializer (flag 0x20) --- */

/* Encode condition as u8: truncate(current / max * 255) */
static u8 encode_condition(f32 current, f32 max)
{
    if (max <= 0.0f) return 0;
    f32 ratio = current / max;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return (u8)(ratio * 255.0f);
}

int bc_ship_build_health_update(const bc_ship_state_t *ship,
                                 const bc_ship_class_t *cls,
                                 f32 game_time,
                                 u8 start_idx, u8 *next_idx,
                                 bool is_own_ship,
                                 u8 *buf, int buf_size)
{
    const bc_ss_list_t *sl = &cls->ser_list;
    if (!ship->alive || sl->count == 0) {
        *next_idx = 0;
        return 0;
    }

    /* Clamp start_idx */
    if ((int)start_idx >= sl->count)
        start_idx = 0;

    u8 field[128];
    bc_buffer_t fb;
    bc_buf_init(&fb, field, sizeof(field));

    /* Write start_index byte (counts toward 10-byte budget) */
    bc_buf_write_u8(&fb, start_idx);

    int cursor = (int)start_idx;
    int initial = cursor;
    bool first = true;

    while (1) {
        const bc_ss_entry_t *e = &sl->entries[cursor];

        /* Write condition byte */
        bc_buf_write_u8(&fb, encode_condition(
            ship->subsystem_hp[e->hp_index], e->max_condition));

        /* Write children condition bytes */
        for (int c = 0; c < e->child_count; c++) {
            bc_buf_write_u8(&fb, encode_condition(
                ship->subsystem_hp[e->child_hp_index[c]],
                e->child_max_condition[c]));
        }

        /* Format-specific extras.
         * Consecutive Powered entries share their has_power_data bits
         * in a single [count:3][values:5] byte.  Only reset bit_count
         * when transitioning OUT of the Powered format (Base or Power). */
        if (e->format == BC_SS_FORMAT_POWERED) {
            if (is_own_ship) {
                /* Owner's client has local power state; send false
                 * (no power_pct byte follows). */
                bc_buf_write_bit(&fb, false);
            } else {
                /* Remote observers need the power allocation data.
                 * Sign-bit encoding: positive = ON, negative = OFF.
                 * Disabled subsystem at pct%: write -(i8)pct so the
                 * client recovers both the slider position and the
                 * off state.  See power-system.md §Sign Bit. */
                bc_buf_write_bit(&fb, true);
                u8 pct = ship->power_pct[cursor];
                if (!ship->subsys_enabled[cursor])
                    pct = (u8)(-(i8)pct);
                bc_buf_write_u8(&fb, pct);
            }
        } else if (e->format == BC_SS_FORMAT_POWER) {
            /* Non-Powered entry: flush any accumulated Powered bits */
            fb.bit_count = 0;
            /* Battery percentages: truncate(current / limit * 255) */
            u8 main_pct = (cls->main_battery_limit > 0.0f)
                ? (u8)(ship->main_battery / cls->main_battery_limit * 255.0f)
                : 0;
            u8 backup_pct = (cls->backup_battery_limit > 0.0f)
                ? (u8)(ship->backup_battery / cls->backup_battery_limit * 255.0f)
                : 0;
            bc_buf_write_u8(&fb, main_pct);
            bc_buf_write_u8(&fb, backup_pct);
        } else {
            /* BC_SS_FORMAT_BASE: flush any accumulated Powered bits */
            fb.bit_count = 0;
        }

        /* Advance cursor */
        cursor++;
        if (cursor >= sl->count) cursor = 0;

        /* Stop conditions */
        if (cursor == initial) break; /* full cycle */

        if (!first && (int)fb.pos >= 10) break; /* budget exhausted */
        first = false;
    }

    *next_idx = (u8)cursor;

    return bc_build_state_update(buf, buf_size,
                                  ship->object_id, game_time, 0x20,
                                  field, (int)fb.pos);
}

static bool skip_stateupdate_prefix_to_subsystems(bc_buffer_t *buf, u8 *dirty_out)
{
    u8 opcode;
    i32 object_id;
    f32 game_time;
    u8 dirty;

    if (!bc_buf_read_u8(buf, &opcode)) return false;
    if (opcode != BC_OP_STATE_UPDATE) return false;
    if (!bc_buf_read_i32(buf, &object_id)) return false;
    if (!bc_buf_read_f32(buf, &game_time)) return false;
    if (!bc_buf_read_u8(buf, &dirty)) return false;
    (void)object_id;
    (void)game_time;
    if (dirty_out) *dirty_out = dirty;

    if (dirty & BC_DIRTY_POSITION_ABS) {
        bool has_hash = false;
        u16 hash16;
        f32 x, y, z;
        if (!bc_buf_read_f32(buf, &x)) return false;
        if (!bc_buf_read_f32(buf, &y)) return false;
        if (!bc_buf_read_f32(buf, &z)) return false;
        if (!bc_buf_read_bit(buf, &has_hash)) return false;
        if (has_hash) {
            if (!bc_buf_read_u16(buf, &hash16)) return false;
        }
    }
    if (dirty & BC_DIRTY_POSITION_DELTA) {
        f32 dx, dy, dz;
        if (!bc_buf_read_cv4(buf, &dx, &dy, &dz)) return false;
    }
    if (dirty & BC_DIRTY_ORIENT_FWD) {
        f32 fx, fy, fz;
        if (!bc_buf_read_cv3(buf, &fx, &fy, &fz)) return false;
    }
    if (dirty & BC_DIRTY_ORIENT_UP) {
        f32 ux, uy, uz;
        if (!bc_buf_read_cv3(buf, &ux, &uy, &uz)) return false;
    }
    if (dirty & BC_DIRTY_SPEED) {
        f32 speed;
        if (!bc_buf_read_cf16(buf, &speed)) return false;
    }

    return true;
}

int bc_ship_apply_remote_power_state(const u8 *state_update,
                                     int state_update_len,
                                     const bc_ship_class_t *cls,
                                     bc_ship_state_t *ship)
{
    if (!state_update || state_update_len <= 0 || !cls || !ship)
        return 0;

    const bc_ss_list_t *sl = &cls->ser_list;
    if (sl->count <= 0) return 0;

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)state_update, (size_t)state_update_len);

    u8 dirty = 0;
    if (!skip_stateupdate_prefix_to_subsystems(&buf, &dirty))
        return 0;

    if (!(dirty & BC_DIRTY_SUBSYSTEM_STATES))
        return 0;

    /* The 0x20 block has no explicit length field. Keep parsing constrained
     * to cases where no unknown variable-length tail (0x80 weapon block) can
     * follow it in the same packet. */
    if (dirty & BC_DIRTY_WEAPON_STATES)
        return 0;

    if (dirty & BC_DIRTY_CLOAK_STATE)
        return 0;

    u8 start_idx;
    if (!bc_buf_read_u8(&buf, &start_idx))
        return 0;

    int cursor = (int)start_idx;
    if (cursor < 0 || cursor >= sl->count)
        cursor = 0;

    int updated = 0;

    while (bc_buf_remaining(&buf) > 0) {
        const bc_ss_entry_t *e = &sl->entries[cursor];
        u8 condition_byte;

        if (!bc_buf_read_u8(&buf, &condition_byte))
            break;

        for (int c = 0; c < e->child_count; c++) {
            if (!bc_buf_read_u8(&buf, &condition_byte))
                return updated;
        }

        if (e->format == BC_SS_FORMAT_POWERED) {
            bool has_power_data = false;
            if (!bc_buf_read_bit(&buf, &has_power_data))
                return updated;

            if (has_power_data) {
                u8 raw_pct;
                if (!bc_buf_read_u8(&buf, &raw_pct))
                    return updated;

                i8 signed_pct = (i8)raw_pct;
                bool enabled = signed_pct > 0;
                u8 pct = enabled ? raw_pct : (u8)(-signed_pct);

                if (signed_pct == 0) {
                    enabled = false;
                    pct = 0;
                }

                ship->power_pct[cursor] = pct;
                ship->subsys_enabled[cursor] = enabled;
                updated++;
            }
        } else if (e->format == BC_SS_FORMAT_POWER) {
            /* Battery percentages follow the power entry; consume for
             * stream alignment but keep server battery authority local. */
            u8 skip;
            if (!bc_buf_read_u8(&buf, &skip)) return updated;
            if (!bc_buf_read_u8(&buf, &skip)) return updated;
        }

        cursor++;
        if (cursor >= sl->count) cursor = 0;
    }

    return updated;
}

/* --- Reactor / power simulation --- */

void bc_ship_power_tick(bc_ship_state_t *ship,
                        const bc_ship_class_t *cls,
                        f32 dt)
{
    if (!ship->alive || dt <= 0.0f) return;

    const bc_ss_list_t *sl = &cls->ser_list;
    if (sl->count == 0) return;

    /* --- Generation (1-second interval) --- */
    ship->power_tick_accum += dt;
    while (ship->power_tick_accum >= 1.0f) {
        ship->power_tick_accum -= 1.0f;

        /* Reactor health ratio */
        f32 condition_pct = 1.0f;
        if (sl->reactor_entry_idx >= 0) {
            const bc_ss_entry_t *reactor = &sl->entries[sl->reactor_entry_idx];
            if (reactor->max_condition > 0.0f)
                condition_pct = ship->subsystem_hp[reactor->hp_index] / reactor->max_condition;
            if (condition_pct < 0.0f) condition_pct = 0.0f;
            if (condition_pct > 1.0f) condition_pct = 1.0f;
        }

        f32 generated = cls->power_output * condition_pct;

        /* Fill main battery first, overflow to backup */
        f32 main_before = ship->main_battery;
        ship->main_battery += generated;
        if (ship->main_battery > cls->main_battery_limit)
            ship->main_battery = cls->main_battery_limit;

        f32 overflow = generated - (ship->main_battery - main_before);
        if (overflow > 0.0f) {
            ship->backup_battery += overflow;
            if (ship->backup_battery > cls->backup_battery_limit)
                ship->backup_battery = cls->backup_battery_limit;
        }

        /* Recompute conduit limits */
        ship->main_conduit_remaining = cls->main_conduit_capacity * condition_pct;
        if (ship->main_conduit_remaining > ship->main_battery)
            ship->main_conduit_remaining = ship->main_battery;

        ship->backup_conduit_remaining = cls->backup_conduit_capacity;
        if (ship->backup_conduit_remaining > ship->backup_battery)
            ship->backup_conduit_remaining = ship->backup_battery;
    }

    /* --- Per-frame efficiency estimate (no drain) ---
     * Stock BC dedi doesn't run a power simulation -- batteries always
     * report 0xFF (100%).  We compute efficiency from conduit capacity
     * vs demand so server-side mechanics (movement, weapons) can use it,
     * but we never deduct from battery/conduit state. */
    f32 main_avail  = ship->main_conduit_remaining;
    f32 backup_avail = ship->backup_conduit_remaining;

    for (int i = 0; i < sl->count; i++) {
        const bc_ss_entry_t *e = &sl->entries[i];
        if (e->format != BC_SS_FORMAT_POWERED) {
            ship->efficiency[i] = 1.0f;
            continue;
        }

        if (!ship->subsys_enabled[i] || e->normal_power <= 0.0f) {
            ship->efficiency[i] = 0.0f;
            continue;
        }

        f32 demand = e->normal_power * ((f32)ship->power_pct[i] / 100.0f) * dt;
        if (demand <= 0.0f) {
            ship->efficiency[i] = 1.0f;
            continue;
        }

        /* Draw from conduits according to power mode:
         *   0 = main-first (fallback to backup)
         *   1 = backup-first (fallback to main)
         *   2 = backup-only (never touches main) */
        f32 from_primary = 0.0f, from_secondary = 0.0f;
        f32 *primary, *secondary;
        if (e->power_mode == BC_POWER_MODE_BACKUP_FIRST) {
            primary = &backup_avail;
            secondary = &main_avail;
        } else if (e->power_mode == BC_POWER_MODE_BACKUP_ONLY) {
            primary = &backup_avail;
            secondary = NULL;
        } else { /* BC_POWER_MODE_MAIN_FIRST (default) */
            primary = &main_avail;
            secondary = &backup_avail;
        }

        from_primary = (demand < *primary) ? demand : *primary;
        *primary -= from_primary;

        f32 remaining = demand - from_primary;
        if (remaining > 0.0f && secondary) {
            from_secondary = (remaining < *secondary) ? remaining : *secondary;
            *secondary -= from_secondary;
        }

        ship->efficiency[i] = (from_primary + from_secondary) / demand;
    }
}
