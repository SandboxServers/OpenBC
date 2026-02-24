#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include <string.h>
#include <math.h>

/* --- Helpers to find weapon subsystem indices --- */

/* Find the N-th phaser/pulse subsystem in the ship class definition.
 * Returns subsystem index in cls->subsystems[], or -1. */
static int find_phaser_subsys(const bc_ship_class_t *cls, int bank_idx)
{
    int count = 0;
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "phaser") == 0 ||
            strcmp(cls->subsystems[i].type, "pulse_weapon") == 0) {
            if (count == bank_idx) return i;
            count++;
        }
    }
    return -1;
}

/* Find the N-th torpedo tube subsystem. */
static int find_torpedo_subsys(const bc_ship_class_t *cls, int tube_idx)
{
    int count = 0;
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "torpedo_tube") == 0) {
            if (count == tube_idx) return i;
            count++;
        }
    }
    return -1;
}

/* Find first subsystem by type string, or -1 if not found. */
static int find_subsys_by_type(const bc_ship_class_t *cls, const char *type)
{
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, type) == 0)
            return i;
    }
    return -1;
}

/* Find serialization list entry by HP slot index, or -1. */
static int find_ser_entry_by_hp_index(const bc_ship_class_t *cls, int hp_index)
{
    const bc_ss_list_t *sl = &cls->ser_list;
    for (int i = 0; i < sl->count; i++) {
        if (sl->entries[i].hp_index == hp_index)
            return i;
    }
    return -1;
}

/* --- Charge / Cooldown ticks --- */

void bc_combat_charge_tick(bc_ship_state_t *ship,
                           const bc_ship_class_t *cls,
                           f32 power_level, f32 dt)
{
    if (!ship->alive || dt <= 0.0f) return;
    if (ship->cloak_state != BC_CLOAK_DECLOAKED) return; /* no charge while cloaked */

    int bank_idx = 0;
    for (int i = 0; i < cls->subsystem_count; i++) {
        const bc_subsystem_def_t *ss = &cls->subsystems[i];
        if (strcmp(ss->type, "phaser") != 0 &&
            strcmp(ss->type, "pulse_weapon") != 0) continue;
        if (bank_idx >= BC_MAX_PHASER_BANKS) break;

        /* Only recharge if subsystem is alive */
        if (ship->subsystem_hp[i] > 0.0f) {
            f32 rate = ss->recharge_rate * power_level;
            ship->phaser_charge[bank_idx] += rate * dt;
            if (ship->phaser_charge[bank_idx] > ss->max_charge) {
                ship->phaser_charge[bank_idx] = ss->max_charge;
            }
        }
        bank_idx++;
    }
}

void bc_combat_torpedo_tick(bc_ship_state_t *ship,
                            const bc_ship_class_t *cls,
                            f32 dt)
{
    if (!ship->alive || dt <= 0.0f) return;

    /* Type switch timer */
    if (ship->torpedo_switching) {
        ship->torpedo_switch_timer -= dt;
        if (ship->torpedo_switch_timer <= 0.0f) {
            ship->torpedo_switching = false;
            ship->torpedo_switch_timer = 0.0f;
        }
    }

    int tube_idx = 0;
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "torpedo_tube") != 0) continue;
        if (tube_idx >= BC_MAX_TORPEDO_TUBES) break;

        if (ship->torpedo_cooldown[tube_idx] > 0.0f) {
            ship->torpedo_cooldown[tube_idx] -= dt;
            if (ship->torpedo_cooldown[tube_idx] < 0.0f) {
                ship->torpedo_cooldown[tube_idx] = 0.0f;
            }
        }
        tube_idx++;
    }
}

/* --- Phaser fire --- */

bool bc_combat_can_fire_phaser(const bc_ship_state_t *ship,
                               const bc_ship_class_t *cls,
                               int bank_idx)
{
    if (!ship->alive) return false;
    if (ship->cloak_state != BC_CLOAK_DECLOAKED) return false;
    if (bank_idx < 0 || bank_idx >= BC_MAX_PHASER_BANKS) return false;

    int ss_idx = find_phaser_subsys(cls, bank_idx);
    if (ss_idx < 0) return false;

    /* Subsystem must be alive */
    if (ship->subsystem_hp[ss_idx] <= 0.0f) return false;

    /* Parent container must be alive (if any) */
    int parent = cls->subsystems[ss_idx].parent_idx;
    if (parent >= 0 && ship->subsystem_hp[parent] <= 0.0f) return false;

    /* Check disabled threshold */
    const bc_subsystem_def_t *ss = &cls->subsystems[ss_idx];
    if (ss->disabled_pct > 0.0f) {
        f32 threshold = ss->max_condition * (1.0f - ss->disabled_pct);
        if (ship->subsystem_hp[ss_idx] < threshold) return false;
    }

    /* Must have minimum charge */
    if (ship->phaser_charge[bank_idx] < ss->min_firing_charge) return false;

    return true;
}

int bc_combat_fire_phaser(bc_ship_state_t *shooter,
                          const bc_ship_class_t *cls,
                          int bank_idx,
                          i32 target_id,
                          u8 *buf, int buf_size)
{
    if (!bc_combat_can_fire_phaser(shooter, cls, bank_idx)) return -1;

    /* Reset charge */
    shooter->phaser_charge[bank_idx] = 0.0f;

    /* Build BeamFire packet with shooter's forward as direction */
    return bc_build_beam_fire(buf, buf_size,
                               shooter->object_id, 0,
                               shooter->fwd.x, shooter->fwd.y, shooter->fwd.z,
                               (target_id >= 0), target_id);
}

/* --- Torpedo fire --- */

bool bc_combat_can_fire_torpedo(const bc_ship_state_t *ship,
                                const bc_ship_class_t *cls,
                                int tube_idx)
{
    if (!ship->alive) return false;
    if (ship->cloak_state != BC_CLOAK_DECLOAKED) return false;
    if (ship->torpedo_switching) return false;
    if (tube_idx < 0 || tube_idx >= BC_MAX_TORPEDO_TUBES) return false;

    int ss_idx = find_torpedo_subsys(cls, tube_idx);
    if (ss_idx < 0) return false;
    if (ship->subsystem_hp[ss_idx] <= 0.0f) return false;

    /* Parent container must be alive (if any) */
    int parent = cls->subsystems[ss_idx].parent_idx;
    if (parent >= 0 && ship->subsystem_hp[parent] <= 0.0f) return false;

    if (ship->torpedo_cooldown[tube_idx] > 0.0f) return false;

    return true;
}

int bc_combat_fire_torpedo(bc_ship_state_t *shooter,
                           const bc_ship_class_t *cls,
                           int tube_idx,
                           i32 target_id,
                           bc_vec3_t direction,
                           u8 *buf, int buf_size)
{
    if (!bc_combat_can_fire_torpedo(shooter, cls, tube_idx)) return -1;

    int ss_idx = find_torpedo_subsys(cls, tube_idx);
    if (ss_idx < 0) return -1;

    /* Set cooldown */
    shooter->torpedo_cooldown[tube_idx] = cls->subsystems[ss_idx].reload_delay;

    /* Build TorpedoFire packet */
    return bc_build_torpedo_fire(buf, buf_size,
                                  shooter->object_id, (u8)ss_idx,
                                  direction.x, direction.y, direction.z,
                                  (target_id >= 0), target_id,
                                  0, 0, 0);
}

void bc_combat_switch_torpedo_type(bc_ship_state_t *ship,
                                   const bc_ship_class_t *cls,
                                   u8 new_type)
{
    if (ship->torpedo_type == new_type) return;
    ship->torpedo_type = new_type;
    ship->torpedo_switching = true;

    /* Find max reload delay among all tubes */
    f32 max_delay = 0.0f;
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "torpedo_tube") == 0) {
            if (cls->subsystems[i].reload_delay > max_delay) {
                max_delay = cls->subsystems[i].reload_delay;
            }
        }
    }
    ship->torpedo_switch_timer = max_delay;
}

/* --- Damage system --- */

int bc_combat_shield_facing(const bc_ship_state_t *target,
                            bc_vec3_t impact_dir)
{
    /* Transform impact direction to ship-local frame.
     * Local axes: fwd = +Y, up = +Z, right = cross(fwd, up) = +X */
    bc_vec3_t right = bc_vec3_cross(target->fwd, target->up);

    f32 local_x = bc_vec3_dot(impact_dir, right);
    f32 local_y = bc_vec3_dot(impact_dir, target->fwd);
    f32 local_z = bc_vec3_dot(impact_dir, target->up);

    f32 ax = fabsf(local_x);
    f32 ay = fabsf(local_y);
    f32 az = fabsf(local_z);

    /* Find dominant axis */
    if (ay >= ax && ay >= az) {
        return (local_y > 0) ? BC_SHIELD_FRONT : BC_SHIELD_REAR;
    } else if (az >= ax) {
        return (local_z > 0) ? BC_SHIELD_TOP : BC_SHIELD_BOTTOM;
    } else {
        return (local_x > 0) ? BC_SHIELD_LEFT : BC_SHIELD_RIGHT;
    }
}

/* Bug 1: AABB overlap test — find ALL subsystems whose bounding box overlaps
 * the damage volume, not just the nearest point-sphere hit.
 * search_radius scales each subsystem's effective bounding radius in the test,
 * expanding the set of eligible subsystems (1.5 = 50% wider search per subsystem). */
int bc_combat_find_hit_subsystems(const bc_ship_class_t *cls,
                                  bc_vec3_t local_impact, f32 damage_radius,
                                  f32 search_radius,
                                  int *out_indices, int max_out)
{
    int count = 0;

    for (int i = 0; i < cls->subsystem_count && count < max_out; i++) {
        const bc_subsystem_def_t *ss = &cls->subsystems[i];
        if (ss->radius <= 0.0f) continue;

        /* Subsystem AABB expanded by search_radius: [pos - r*sr, pos + r*sr] */
        /* Damage AABB: [impact - damage_radius, impact + damage_radius]       */
        /* Overlap requires all 3 axes to overlap                               */
        f32 ss_r = ss->radius * search_radius;
        bool overlap_x = (local_impact.x - damage_radius) <= (ss->position.x + ss_r) &&
                          (local_impact.x + damage_radius) >= (ss->position.x - ss_r);
        bool overlap_y = (local_impact.y - damage_radius) <= (ss->position.y + ss_r) &&
                          (local_impact.y + damage_radius) >= (ss->position.y - ss_r);
        bool overlap_z = (local_impact.z - damage_radius) <= (ss->position.z + ss_r) &&
                          (local_impact.z + damage_radius) >= (ss->position.z - ss_r);

        if (overlap_x && overlap_y && overlap_z) {
            out_indices[count++] = i;
        }
    }
    return count;
}

/* Bug 7: area-effect vs directed shield absorption.
 * Bug 9: skip shield absorption when cloaked.
 * Bug 10: damage_radius scaled by target's damage_radius_multiplier.
 * Issue #87: two-step pipeline — subsystems absorb before hull; each hit
 * subsystem independently absorbs up to min(overflow, ss_hp); hull gets
 * max(0, overflow - total_sub_absorbed). search_radius expands each
 * subsystem's effective AABB radius for the hit test (1.5 = collision path). */
void bc_combat_apply_damage(bc_ship_state_t *target,
                            const bc_ship_class_t *cls,
                            f32 damage, f32 damage_radius,
                            bc_vec3_t impact_dir,
                            bool area_effect,
                            f32 search_radius)
{
    if (!target->alive || damage <= 0.0f) return;

    f32 overflow = 0.0f;

    /* Bug 9: shields don't absorb while cloaked */
    if (!bc_cloak_shields_active(target)) {
        overflow = damage;
    } else if (area_effect) {
        /* Bug 7: area-effect — damage/6 per facing, each independently absorbs */
        f32 per_facing = damage / 6.0f;
        f32 total_absorbed = 0.0f;
        for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
            f32 absorbed = per_facing;
            if (absorbed > target->shield_hp[i])
                absorbed = target->shield_hp[i];
            target->shield_hp[i] -= absorbed;
            total_absorbed += absorbed;
        }
        overflow = damage - total_absorbed;
    } else {
        /* Directed: single facing absorbs */
        int facing = bc_combat_shield_facing(target, impact_dir);
        if (target->shield_hp[facing] > 0.0f) {
            if (damage <= target->shield_hp[facing]) {
                target->shield_hp[facing] -= damage;
                return; /* fully absorbed */
            }
            overflow = damage - target->shield_hp[facing];
            target->shield_hp[facing] = 0.0f;
        } else {
            overflow = damage;
        }
    }

    if (overflow <= 0.0f) return;

    /* Step 1: Subsystem absorption — subsystems absorb from overflow BEFORE hull.
     * Each hit subsystem independently absorbs up to min(overflow, ss_hp).
     * Bug 10: scale damage_radius by target's multiplier. */
    f32 total_sub_absorbed = 0.0f;
    f32 effective_radius = damage_radius * cls->damage_radius_multiplier;
    if (effective_radius > 0.0f) {
        bc_vec3_t right = bc_vec3_cross(target->fwd, target->up);
        bc_vec3_t local = {
            bc_vec3_dot(impact_dir, right),
            bc_vec3_dot(impact_dir, target->fwd),
            bc_vec3_dot(impact_dir, target->up),
        };

        /* Bug 1: find ALL overlapping subsystems (search_radius scales ss AABB) */
        int hit_indices[BC_MAX_SUBSYSTEMS];
        int hit_count = bc_combat_find_hit_subsystems(cls, local,
                                                       effective_radius,
                                                       search_radius,
                                                       hit_indices,
                                                       BC_MAX_SUBSYSTEMS);

        for (int h = 0; h < hit_count; h++) {
            int ss_idx = hit_indices[h];
            if (target->subsystem_hp[ss_idx] > 0.0f) {
                /* Each subsystem absorbs up to the full overflow independently. */
                f32 absorbed = overflow;
                if (absorbed > target->subsystem_hp[ss_idx])
                    absorbed = target->subsystem_hp[ss_idx];
                target->subsystem_hp[ss_idx] -= absorbed;
                if (target->subsystem_hp[ss_idx] < 0.0f)
                    target->subsystem_hp[ss_idx] = 0.0f;
                total_sub_absorbed += absorbed;
            }
        }
    }

    /* Step 2: Hull gets overflow minus what subsystems absorbed. */
    f32 hull_damage = overflow - total_sub_absorbed;
    if (hull_damage <= 0.0f) return;

    target->hull_hp -= hull_damage;
    if (target->hull_hp <= 0.0f) {
        target->hull_hp = 0.0f;
        target->alive = false;
    }
}

/* Path 1 — Direct collision (multi-contact, fractional).
 * Output: 0.1 to 0.5 (fraction of radius). */
f32 bc_combat_collision_damage_path1(f32 collision_energy, f32 ship_mass,
                                      int contact_count)
{
    if (ship_mass <= 0.0f || contact_count <= 0) return 0.0f;
    f32 raw = (collision_energy / ship_mass) / (f32)contact_count;
    f32 scaled = raw * 0.1f + 0.1f;
    if (scaled > 0.5f) scaled = 0.5f;
    if (scaled < 0.0f) scaled = 0.0f;
    return scaled;
}

/* Path 2 — Collision effect handler (shield-first, absolute HP).
 * Dead zone: raw <= 0.01 ignored. Output: 500.0+ absolute HP. */
f32 bc_combat_collision_damage_path2(f32 collision_energy, f32 ship_mass,
                                      int contact_count)
{
    if (ship_mass <= 0.0f || contact_count <= 0) return 0.0f;
    f32 raw = (collision_energy / ship_mass) / (f32)contact_count;
    if (raw <= 0.01f) return 0.0f;   /* dead zone */
    return raw * 900.0f + 500.0f;
}

/* --- Shield recharge --- */

/* Bug 6: power budget with overflow redistribution */
void bc_combat_shield_tick(bc_ship_state_t *ship,
                           const bc_ship_class_t *cls,
                           f32 power_level, f32 dt)
{
    if (!ship->alive || dt <= 0.0f) return;
    if (ship->cloak_state != BC_CLOAK_DECLOAKED) return;

    /* Special recovery path: if shield subsystem is destroyed/disabled,
     * recharge surviving facings using backup battery directly. */
    int shield_ss = find_subsys_by_type(cls, "shield");
    bool shield_alive = true;
    bool shield_enabled = true;
    if (shield_ss >= 0) {
        shield_alive = ship->subsystem_hp[shield_ss] > 0.0f;

        /* subsys_enabled[] is indexed by serialization entry, not by
         * flat subsystem index, so map shield_ss -> ser entry first. */
        int shield_entry = find_ser_entry_by_hp_index(cls, shield_ss);
        if (shield_entry >= 0 && shield_entry < BC_SS_MAX_ENTRIES)
            shield_enabled = ship->subsys_enabled[shield_entry];
    }

    if (!shield_alive || !shield_enabled) {
        bool can_recharge[BC_MAX_SHIELD_FACINGS];
        bool is_full[BC_MAX_SHIELD_FACINGS];
        f32 requested = 0.0f;
        f32 total_before = 0.0f;
        f32 overflow = 0.0f;

        for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
            total_before += ship->shield_hp[i];
            can_recharge[i] = (ship->shield_hp[i] > 0.0f &&
                               ship->shield_hp[i] < cls->shield_hp[i]);
            if (can_recharge[i]) {
                requested += cls->shield_recharge[i] * power_level * dt;
            }
        }

        if (requested <= 0.0f || ship->backup_battery <= 0.0f) return;

        f32 scale = 1.0f;
        if (requested > ship->backup_battery)
            scale = ship->backup_battery / requested;

        for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
            if (!can_recharge[i]) {
                is_full[i] = true;
                continue;
            }

            f32 gain = cls->shield_recharge[i] * power_level * dt * scale;
            f32 new_hp = ship->shield_hp[i] + gain;
            if (new_hp >= cls->shield_hp[i]) {
                overflow += new_hp - cls->shield_hp[i];
                ship->shield_hp[i] = cls->shield_hp[i];
                is_full[i] = true;
            } else {
                ship->shield_hp[i] = new_hp;
                is_full[i] = false;
            }
        }

        /* Reuse normal overflow redistribution, but only for facings that
         * were eligible in the recovery path. */
        if (overflow > 0.0f) {
            f32 total_capacity = 0.0f;
            for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
                if (can_recharge[i] && !is_full[i]) {
                    total_capacity += cls->shield_hp[i] - ship->shield_hp[i];
                }
            }
            if (total_capacity > 0.0f) {
                f32 to_distribute = overflow;
                if (to_distribute > total_capacity) to_distribute = total_capacity;
                for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
                    if (!can_recharge[i] || is_full[i]) continue;
                    f32 room = cls->shield_hp[i] - ship->shield_hp[i];
                    f32 share = to_distribute * (room / total_capacity);
                    ship->shield_hp[i] += share;
                    if (ship->shield_hp[i] > cls->shield_hp[i])
                        ship->shield_hp[i] = cls->shield_hp[i];
                }
            }
        }

        /* Net draw = shield HP gained. Any capped-out excess is returned. */
        f32 total_after = 0.0f;
        for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
            total_after += ship->shield_hp[i];
        }

        f32 consumed = total_after - total_before;
        if (consumed < 0.0f) consumed = 0.0f;
        if (consumed > ship->backup_battery) consumed = ship->backup_battery;
        ship->backup_battery -= consumed;
        if (ship->backup_battery < 0.0f)
            ship->backup_battery = 0.0f;
        return;
    }

    /* Pass 1: compute raw gain, cap at max, accumulate overflow */
    f32 overflow = 0.0f;
    f32 raw_gain[BC_MAX_SHIELD_FACINGS];
    bool is_full[BC_MAX_SHIELD_FACINGS];

    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
        raw_gain[i] = cls->shield_recharge[i] * power_level * dt;
        f32 new_hp = ship->shield_hp[i] + raw_gain[i];
        if (new_hp >= cls->shield_hp[i]) {
            overflow += new_hp - cls->shield_hp[i];
            ship->shield_hp[i] = cls->shield_hp[i];
            is_full[i] = true;
        } else {
            ship->shield_hp[i] = new_hp;
            is_full[i] = false;
        }
    }

    /* Pass 2: redistribute overflow to non-full facings proportionally */
    if (overflow > 0.0f) {
        f32 total_capacity = 0.0f;
        for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
            if (!is_full[i]) {
                total_capacity += cls->shield_hp[i] - ship->shield_hp[i];
            }
        }
        if (total_capacity > 0.0f) {
            f32 to_distribute = overflow;
            if (to_distribute > total_capacity) to_distribute = total_capacity;
            for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
                if (is_full[i]) continue;
                f32 room = cls->shield_hp[i] - ship->shield_hp[i];
                f32 share = to_distribute * (room / total_capacity);
                ship->shield_hp[i] += share;
                if (ship->shield_hp[i] > cls->shield_hp[i])
                    ship->shield_hp[i] = cls->shield_hp[i];
            }
        }
    }
}

/* --- Cloaking device --- */

/* Find the cloaking subsystem index, or -1 */
static int find_cloak_subsys(const bc_ship_class_t *cls)
{
    return find_subsys_by_type(cls, "cloak");
}

/* Bug 9: cloak preserves shield HP (does NOT zero them) */
bool bc_cloak_start(bc_ship_state_t *ship, const bc_ship_class_t *cls)
{
    if (!ship->alive) return false;
    if (!cls->can_cloak) return false;
    if (ship->cloak_state != BC_CLOAK_DECLOAKED) return false;

    /* Cloaking device subsystem must be alive */
    int ss = find_cloak_subsys(cls);
    if (ss >= 0 && ship->subsystem_hp[ss] <= 0.0f) return false;

    ship->cloak_state = BC_CLOAK_CLOAKING;
    ship->cloak_timer = BC_CLOAK_TRANSITION_TIME;

    /* Shield HP preserved — shields are functionally disabled via
     * bc_cloak_shields_active() returning false, which causes
     * apply_damage to skip absorption and shield_tick to skip recharge. */

    return true;
}

bool bc_cloak_stop(bc_ship_state_t *ship)
{
    if (ship->cloak_state == BC_CLOAK_DECLOAKED) return false;
    if (ship->cloak_state == BC_CLOAK_DECLOAKING) return false;

    ship->cloak_state = BC_CLOAK_DECLOAKING;
    ship->cloak_timer = BC_CLOAK_TRANSITION_TIME;
    return true;
}

/* Bug 9: on DECLOAKING->DECLOAKED, reset any 0 HP facing to 1.0 */
void bc_cloak_tick(bc_ship_state_t *ship, f32 cloak_efficiency, f32 dt)
{
    if (dt <= 0.0f) { return; }

    /* Sanitize efficiency: NaN/inf → 0, then clamp to [0,1] */
    if (!isfinite(cloak_efficiency) || cloak_efficiency < 0.0f) {
        cloak_efficiency = 0.0f;
    } else if (cloak_efficiency > 1.0f) {
        cloak_efficiency = 1.0f;
    }

    switch (ship->cloak_state) {
    case BC_CLOAK_CLOAKING:
        ship->cloak_timer -= dt;
        if (ship->cloak_timer <= 0.0f) {
            ship->cloak_timer = 0.0f;
            ship->cloak_state = BC_CLOAK_CLOAKED;
        }
        break;
    case BC_CLOAK_CLOAKED:
        /* Energy failure: if cloak can't get enough power, force decloak */
        if (cloak_efficiency < BC_CLOAK_ENERGY_THRESHOLD) {
            ship->cloak_state = BC_CLOAK_DECLOAKING;
            ship->cloak_timer = BC_CLOAK_TRANSITION_TIME;
        }
        break;
    case BC_CLOAK_DECLOAKING:
        ship->cloak_timer -= dt;
        if (ship->cloak_timer <= 0.0f) {
            ship->cloak_timer = 0.0f;
            ship->cloak_state = BC_CLOAK_DECLOAKED;
            /* Any shield facing at 0 HP gets reset to 1.0 */
            for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
                if (ship->shield_hp[i] <= 0.0f)
                    ship->shield_hp[i] = 1.0f;
            }
        }
        break;
    default:
        break;
    }
}

bool bc_cloak_can_fire(const bc_ship_state_t *ship)
{
    return ship->cloak_state == BC_CLOAK_DECLOAKED;
}

bool bc_cloak_shields_active(const bc_ship_state_t *ship)
{
    return ship->cloak_state == BC_CLOAK_DECLOAKED;
}

/* --- Tractor beams --- */

/* Find the N-th tractor beam subsystem. */
static int find_tractor_subsys(const bc_ship_class_t *cls, int beam_idx)
{
    int count = 0;
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "tractor_beam") == 0) {
            if (count == beam_idx) return i;
            count++;
        }
    }
    return -1;
}

bool bc_combat_can_tractor(const bc_ship_state_t *ship,
                            const bc_ship_class_t *cls,
                            int beam_idx)
{
    if (!ship->alive) return false;
    if (ship->cloak_state != BC_CLOAK_DECLOAKED) return false;
    if (!cls->has_tractor) return false;
    if (ship->tractor_target_id >= 0) return false; /* already locked */

    int ss_idx = find_tractor_subsys(cls, beam_idx);
    if (ss_idx < 0) return false;
    if (ship->subsystem_hp[ss_idx] <= 0.0f) return false;

    /* Parent container must be alive (if any) */
    int parent = cls->subsystems[ss_idx].parent_idx;
    if (parent >= 0 && ship->subsystem_hp[parent] <= 0.0f) return false;

    return true;
}

int bc_combat_tractor_engage(bc_ship_state_t *ship,
                              const bc_ship_class_t *cls,
                              int beam_idx,
                              i32 target_id)
{
    if (!bc_combat_can_tractor(ship, cls, beam_idx)) return -1;

    int ss_idx = find_tractor_subsys(cls, beam_idx);
    if (ss_idx < 0) return -1;

    ship->tractor_target_id = target_id;
    return ss_idx;
}

void bc_combat_tractor_disengage(bc_ship_state_t *ship)
{
    ship->tractor_target_id = -1;
    ship->tractor_drag = 1.0f;
}

/* Bug 2: tractor beams do NOT apply damage.
 * Bug 3: multiplicative drag, not additive.
 * Drag applies to SOURCE ship (the one projecting the tractor beam)
 * and reduces all 4 engine stats via tractor_drag multiplier. */
void bc_combat_tractor_tick(bc_ship_state_t *ship,
                             bc_ship_state_t *target,
                             const bc_ship_class_t *cls,
                             f32 dt)
{
    if (ship->tractor_target_id < 0 || !ship->alive || !target->alive) {
        ship->tractor_target_id = -1;
        ship->tractor_drag = 1.0f;
        return;
    }
    if (dt <= 0.0f) return;

    /* Find first alive tractor subsystem for range stats */
    int ss_idx = find_tractor_subsys(cls, 0);
    if (ss_idx < 0 || ship->subsystem_hp[ss_idx] <= 0.0f) {
        ship->tractor_target_id = -1;
        ship->tractor_drag = 1.0f;
        return;
    }

    const bc_subsystem_def_t *ss = &cls->subsystems[ss_idx];

    /* Check range */
    f32 dist = bc_vec3_dist(ship->pos, target->pos);
    if (dist > ss->max_damage_distance) {
        ship->tractor_target_id = -1; /* out of range, auto-release */
        ship->tractor_drag = 1.0f;
        return;
    }

    /* Bug 3: multiplicative drag formula from spec.
     * force = max_damage * system_condition_pct * dt
     * tractor_ratio = force / max_damage  (simplifies to sys_hp_pct * dt)
     * Drag applies to SOURCE ship's engine stats. */
    if (ss->max_condition <= 0.0f || ss->max_damage <= 0.0f) {
        ship->tractor_target_id = -1;
        ship->tractor_drag = 1.0f;
        return;
    }
    f32 sys_hp_pct = ship->subsystem_hp[ss_idx] / ss->max_condition;
    if (sys_hp_pct < 0.0f) {
        sys_hp_pct = 0.0f;
    } else if (sys_hp_pct > 1.0f) {
        sys_hp_pct = 1.0f;
    }

    /* dist is guaranteed <= max_damage_distance (early return above),
     * so force is full-strength when in range, zero when out. */
    f32 force = ss->max_damage * sys_hp_pct * dt;
    f32 tractor_ratio = force / ss->max_damage;
    if (tractor_ratio < 0.0f) {
        tractor_ratio = 0.0f;
    } else if (tractor_ratio > 1.0f) {
        tractor_ratio = 1.0f;
    }

    f32 drag = 1.0f - tractor_ratio;
    ship->speed *= drag;
    ship->tractor_drag = drag; /* affects max_angular_velocity in movement */

    /* Bug 2: NO damage applied — tractor beams do not deal direct damage */
}

/* --- Repair system --- */

bool bc_repair_add(bc_ship_state_t *ship, u8 subsys_idx)
{
    /* Bug 4: queue size is BC_MAX_SUBSYSTEMS, not 8 */
    if (ship->repair_count >= BC_MAX_SUBSYSTEMS) return false;

    /* Check not already queued */
    for (int i = 0; i < ship->repair_count; i++) {
        if (ship->repair_queue[i] == subsys_idx) return false;
    }

    ship->repair_queue[ship->repair_count++] = subsys_idx;
    return true;
}

void bc_repair_remove(bc_ship_state_t *ship, u8 subsys_idx)
{
    for (int i = 0; i < ship->repair_count; i++) {
        if (ship->repair_queue[i] == subsys_idx) {
            /* Shift remaining items */
            for (int j = i; j < ship->repair_count - 1; j++)
                ship->repair_queue[j] = ship->repair_queue[j + 1];
            ship->repair_count--;
            return;
        }
    }
}

/* Bug 5: repair rate formula per spec.
 * raw_repair = max_repair_points * repair_sys_health_pct * dt
 * active = min(queue_count, num_repair_teams)
 * per_sub = raw_repair / active
 * condition_gain = per_sub / repair_complexity
 * Multiple subsystems repaired simultaneously; destroyed (0 HP) skipped. */
void bc_repair_tick(bc_ship_state_t *ship,
                    const bc_ship_class_t *cls,
                    f32 dt)
{
    if (!ship->alive || dt <= 0.0f) return;
    if (ship->repair_count == 0) return;
    if (cls->max_repair_points <= 0.0f || cls->num_repair_teams <= 0) return;

    /* Find the repair subsystem and its health ratio */
    f32 repair_sys_hp_pct = 1.0f;
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "repair") == 0) {
            f32 max_cond = cls->subsystems[i].max_condition;
            if (max_cond > 0.0f)
                repair_sys_hp_pct = ship->subsystem_hp[i] / max_cond;
            break;
        }
    }
    if (repair_sys_hp_pct <= 0.0f) return; /* repair system destroyed */

    f32 raw_repair = cls->max_repair_points * repair_sys_hp_pct * dt;

    /* Repair up to num_repair_teams subsystems simultaneously */
    int active = ship->repair_count;
    if (active > cls->num_repair_teams)
        active = cls->num_repair_teams;

    f32 per_sub = raw_repair / (f32)active;

    /* Process the first 'active' queue entries */
    int repaired = 0;
    for (int q = 0; q < active && q < ship->repair_count; q++) {
        u8 ss_idx = ship->repair_queue[q];
        if (ss_idx >= (u8)cls->subsystem_count) continue;

        /* Skip destroyed subsystems (0 HP) but keep in queue */
        if (ship->subsystem_hp[ss_idx] <= 0.0f) continue;

        f32 complexity = cls->subsystems[ss_idx].repair_complexity;
        if (complexity <= 0.0f) complexity = 1.0f;
        f32 gain = per_sub / complexity;

        f32 max_hp = cls->subsystems[ss_idx].max_condition;
        ship->subsystem_hp[ss_idx] += gain;
        if (ship->subsystem_hp[ss_idx] >= max_hp) {
            ship->subsystem_hp[ss_idx] = max_hp;
            /* Mark for removal (defer to avoid modifying while iterating) */
            ship->repair_queue[q] = 0xFF; /* sentinel */
            repaired++;
        }
    }

    /* Remove fully repaired entries (marked 0xFF) */
    if (repaired > 0) {
        int w = 0;
        for (int r = 0; r < ship->repair_count; r++) {
            if (ship->repair_queue[r] != 0xFF) {
                ship->repair_queue[w++] = ship->repair_queue[r];
            }
        }
        ship->repair_count = w;
    }
}

void bc_repair_auto_queue(bc_ship_state_t *ship,
                           const bc_ship_class_t *cls)
{
    for (int i = 0; i < cls->subsystem_count; i++) {
        const bc_subsystem_def_t *ss = &cls->subsystems[i];
        if (ss->disabled_pct <= 0.0f) continue;

        f32 threshold = ss->max_condition * (1.0f - ss->disabled_pct);
        if (ship->subsystem_hp[i] < threshold && ship->subsystem_hp[i] > 0.0f) {
            bc_repair_add(ship, (u8)i);
        }
    }
}
