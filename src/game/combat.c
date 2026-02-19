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
 * the damage volume, not just the nearest point-sphere hit. */
int bc_combat_find_hit_subsystems(const bc_ship_class_t *cls,
                                  bc_vec3_t local_impact, f32 damage_radius,
                                  int *out_indices, int max_out)
{
    int count = 0;

    for (int i = 0; i < cls->subsystem_count && count < max_out; i++) {
        const bc_subsystem_def_t *ss = &cls->subsystems[i];
        if (ss->radius <= 0.0f) continue;

        /* Subsystem AABB: [pos - radius, pos + radius] per axis */
        /* Damage AABB:    [impact - damage_radius, impact + damage_radius] */
        /* Overlap requires all 3 axes to overlap */
        bool overlap_x = (local_impact.x - damage_radius) <= (ss->position.x + ss->radius) &&
                          (local_impact.x + damage_radius) >= (ss->position.x - ss->radius);
        bool overlap_y = (local_impact.y - damage_radius) <= (ss->position.y + ss->radius) &&
                          (local_impact.y + damage_radius) >= (ss->position.y - ss->radius);
        bool overlap_z = (local_impact.z - damage_radius) <= (ss->position.z + ss->radius) &&
                          (local_impact.z + damage_radius) >= (ss->position.z - ss->radius);

        if (overlap_x && overlap_y && overlap_z) {
            out_indices[count++] = i;
        }
    }
    return count;
}

/* Bug 7: area-effect vs directed shield absorption.
 * Bug 9: skip shield absorption when cloaked.
 * Bug 10: damage_radius scaled by target's damage_radius_multiplier. */
void bc_combat_apply_damage(bc_ship_state_t *target,
                            const bc_ship_class_t *cls,
                            f32 damage, f32 damage_radius,
                            bc_vec3_t impact_dir,
                            bool area_effect)
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

    /* Hull damage */
    target->hull_hp -= overflow;
    if (target->hull_hp <= 0.0f) {
        target->hull_hp = 0.0f;
        target->alive = false;
    }

    /* Subsystem damage: AABB overlap test */
    /* Bug 10: scale damage_radius by target's multiplier */
    f32 effective_radius = damage_radius * cls->damage_radius_multiplier;
    if (effective_radius > 0.0f) {
        bc_vec3_t right = bc_vec3_cross(target->fwd, target->up);
        bc_vec3_t local = {
            bc_vec3_dot(impact_dir, right),
            bc_vec3_dot(impact_dir, target->fwd),
            bc_vec3_dot(impact_dir, target->up),
        };

        /* Bug 1: find ALL overlapping subsystems */
        int hit_indices[BC_MAX_SUBSYSTEMS];
        int hit_count = bc_combat_find_hit_subsystems(cls, local,
                                                       effective_radius,
                                                       hit_indices,
                                                       BC_MAX_SUBSYSTEMS);

        for (int h = 0; h < hit_count; h++) {
            int ss_idx = hit_indices[h];
            if (target->subsystem_hp[ss_idx] > 0.0f) {
                f32 sub_dmg = overflow * 0.5f;
                target->subsystem_hp[ss_idx] -= sub_dmg;
                if (target->subsystem_hp[ss_idx] < 0.0f) {
                    target->subsystem_hp[ss_idx] = 0.0f;
                }
                /* Propagate 25% to parent container */
                int parent = cls->subsystems[ss_idx].parent_idx;
                if (parent >= 0 && target->subsystem_hp[parent] > 0.0f) {
                    target->subsystem_hp[parent] -= sub_dmg * 0.25f;
                    if (target->subsystem_hp[parent] < 0.0f) {
                        target->subsystem_hp[parent] = 0.0f;
                    }
                }
            }
        }
    }
}

/* Bug 8: collision damage formula per spec */
f32 bc_combat_collision_damage(f32 collision_energy, f32 ship_mass,
                               int contact_count,
                               f32 collision_scale, f32 collision_offset)
{
    if (ship_mass <= 0.0f || contact_count <= 0) return 0.0f;
    f32 raw = (collision_energy / ship_mass) / (f32)contact_count;
    f32 scaled = raw * collision_scale + collision_offset;
    if (scaled > 0.5f) scaled = 0.5f;
    if (scaled < 0.0f) scaled = 0.0f;
    return scaled;
}

/* --- Shield recharge --- */

/* Bug 6: power budget with overflow redistribution */
void bc_combat_shield_tick(bc_ship_state_t *ship,
                           const bc_ship_class_t *cls,
                           f32 power_level, f32 dt)
{
    if (!ship->alive || dt <= 0.0f) return;
    if (ship->cloak_state != BC_CLOAK_DECLOAKED) return;

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
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "cloak") == 0)
            return i;
    }
    return -1;
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
void bc_cloak_tick(bc_ship_state_t *ship, f32 dt)
{
    if (dt <= 0.0f) return;

    switch (ship->cloak_state) {
    case BC_CLOAK_CLOAKING:
        ship->cloak_timer -= dt;
        if (ship->cloak_timer <= 0.0f) {
            ship->cloak_timer = 0.0f;
            ship->cloak_state = BC_CLOAK_CLOAKED;
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
}

/* Bug 2: tractor beams do NOT apply damage.
 * Bug 3: multiplicative drag, not additive. */
void bc_combat_tractor_tick(bc_ship_state_t *ship,
                             bc_ship_state_t *target,
                             const bc_ship_class_t *cls,
                             f32 dt)
{
    if (ship->tractor_target_id < 0 || !ship->alive || !target->alive) {
        ship->tractor_target_id = -1;
        return;
    }
    if (dt <= 0.0f) return;

    /* Find first alive tractor subsystem for range stats */
    int ss_idx = find_tractor_subsys(cls, 0);
    if (ss_idx < 0 || ship->subsystem_hp[ss_idx] <= 0.0f) {
        ship->tractor_target_id = -1;
        return;
    }

    const bc_subsystem_def_t *ss = &cls->subsystems[ss_idx];

    /* Check range */
    f32 dist = bc_vec3_dist(ship->pos, target->pos);
    if (dist > ss->max_damage_distance) {
        ship->tractor_target_id = -1; /* out of range, auto-release */
        return;
    }

    /* Bug 3: multiplicative drag formula from spec.
     * force = max_damage * system_condition_pct * distance_ratio * dt
     * tractor_ratio = force / max_damage
     * effective_speed *= (1.0 - tractor_ratio) */
    f32 sys_hp_pct = ship->subsystem_hp[ss_idx] / ss->max_condition;
    f32 dist_ratio = (dist <= ss->max_damage_distance)
                     ? 1.0f
                     : ss->max_damage_distance / dist;
    f32 force = ss->max_damage * sys_hp_pct * dist_ratio * dt;
    f32 tractor_ratio = force / ss->max_damage;
    if (tractor_ratio > 1.0f) tractor_ratio = 1.0f;
    target->speed *= (1.0f - tractor_ratio);

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
