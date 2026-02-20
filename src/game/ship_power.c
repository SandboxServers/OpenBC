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

        /* Format-specific extras */
        if (e->format == BC_SS_FORMAT_POWERED) {
            /* Each powered entry gets its own bit-pack byte.
             * Reset bit_count so write_bit starts a fresh group
             * rather than sharing with a previous powered entry. */
            fb.bit_count = 0;
            if (is_own_ship) {
                /* Owner's client has local power state; send false
                 * (no power_pct byte follows). */
                bc_buf_write_bit(&fb, false);
            } else {
                /* Remote observers need the power allocation data. */
                bc_buf_write_bit(&fb, true);
                u8 pct = ship->power_pct[cursor];
                if (!ship->subsys_enabled[cursor])
                    pct |= 0x80;
                bc_buf_write_u8(&fb, pct);
            }
        } else if (e->format == BC_SS_FORMAT_POWER) {
            /* Battery percentages: truncate(current / limit * 255) */
            u8 main_pct = (cls->main_battery_limit > 0.0f)
                ? (u8)(ship->main_battery / cls->main_battery_limit * 255.0f)
                : 0;
            u8 backup_pct = (cls->backup_battery_limit > 0.0f)
                ? (u8)(ship->backup_battery / cls->backup_battery_limit * 255.0f)
                : 0;
            bc_buf_write_u8(&fb, main_pct);
            bc_buf_write_u8(&fb, backup_pct);
        }
        /* BC_SS_FORMAT_BASE: nothing extra */

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

    /* --- Per-frame consumer draw --- */
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

        /* Mode 0: main-first, fallback to backup */
        f32 from_main = demand;
        if (from_main > ship->main_conduit_remaining)
            from_main = ship->main_conduit_remaining;
        ship->main_conduit_remaining -= from_main;
        ship->main_battery -= from_main;
        if (ship->main_battery < 0.0f) ship->main_battery = 0.0f;

        f32 remaining = demand - from_main;
        f32 from_backup = 0.0f;
        if (remaining > 0.0f) {
            from_backup = remaining;
            if (from_backup > ship->backup_conduit_remaining)
                from_backup = ship->backup_conduit_remaining;
            ship->backup_conduit_remaining -= from_backup;
            ship->backup_battery -= from_backup;
            if (ship->backup_battery < 0.0f) ship->backup_battery = 0.0f;
        }

        f32 received = from_main + from_backup;
        ship->efficiency[i] = received / demand;
    }
}
