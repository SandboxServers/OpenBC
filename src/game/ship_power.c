#include "openbc/ship_power.h"
#include "openbc/buffer.h"
#include "openbc/game_builders.h"

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
