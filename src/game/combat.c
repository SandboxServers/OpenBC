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

int bc_combat_find_hit_subsystem(const bc_ship_class_t *cls,
                                 bc_vec3_t local_impact)
{
    int best = -1;
    f32 best_dist = 1e30f;

    for (int i = 0; i < cls->subsystem_count; i++) {
        const bc_subsystem_def_t *ss = &cls->subsystems[i];
        if (ss->radius <= 0.0f) continue;

        f32 dx = local_impact.x - ss->position.x;
        f32 dy = local_impact.y - ss->position.y;
        f32 dz = local_impact.z - ss->position.z;
        f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);

        if (dist <= ss->radius && dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

void bc_combat_apply_damage(bc_ship_state_t *target,
                            const bc_ship_class_t *cls,
                            f32 damage,
                            bc_vec3_t impact_dir)
{
    if (!target->alive || damage <= 0.0f) return;

    /* 1. Determine shield facing */
    int facing = bc_combat_shield_facing(target, impact_dir);

    /* 2. Shield absorbs damage */
    f32 overflow = 0.0f;
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

    /* 3. Hull damage */
    target->hull_hp -= overflow;
    if (target->hull_hp <= 0.0f) {
        target->hull_hp = 0.0f;
        target->alive = false;
    }

    /* 4. Subsystem damage: find nearest subsystem to impact
     * (use impact_dir scaled as a rough local-frame point) */
    bc_vec3_t right = bc_vec3_cross(target->fwd, target->up);
    bc_vec3_t local = {
        bc_vec3_dot(impact_dir, right),
        bc_vec3_dot(impact_dir, target->fwd),
        bc_vec3_dot(impact_dir, target->up),
    };
    int ss_idx = bc_combat_find_hit_subsystem(cls, local);
    if (ss_idx >= 0 && target->subsystem_hp[ss_idx] > 0.0f) {
        target->subsystem_hp[ss_idx] -= overflow * 0.5f;
        if (target->subsystem_hp[ss_idx] < 0.0f) {
            target->subsystem_hp[ss_idx] = 0.0f;
        }
    }
}

/* --- Shield recharge --- */

void bc_combat_shield_tick(bc_ship_state_t *ship,
                           const bc_ship_class_t *cls,
                           f32 dt)
{
    if (!ship->alive || dt <= 0.0f) return;
    if (ship->cloak_state != BC_CLOAK_DECLOAKED) return;

    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
        ship->shield_hp[i] += cls->shield_recharge[i] * dt;
        if (ship->shield_hp[i] > cls->shield_hp[i]) {
            ship->shield_hp[i] = cls->shield_hp[i];
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

    /* Shields drop immediately */
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++)
        ship->shield_hp[i] = 0.0f;

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
            /* Shields begin recharging from 0 (handled by shield_tick) */
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

    /* Find first alive tractor subsystem for damage/range stats */
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

    /* Apply drag: reduce target speed toward 0 */
    f32 drag = ss->max_damage * dt * 0.1f; /* proportional drag */
    if (target->speed > drag) {
        target->speed -= drag;
    } else {
        target->speed = 0.0f;
    }

    /* Apply low tractor damage to target */
    f32 dmg = ss->max_damage * dt * 0.02f; /* 2% of MaxDamage per second */
    if (dmg > 0.0f) {
        bc_vec3_t impact = bc_vec3_normalize(bc_vec3_sub(target->pos, ship->pos));
        bc_combat_apply_damage(target, cls, dmg, impact);
    }
}

/* --- Repair system --- */

bool bc_repair_add(bc_ship_state_t *ship, u8 subsys_idx)
{
    if (ship->repair_count >= 8) return false;

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

void bc_repair_tick(bc_ship_state_t *ship,
                    const bc_ship_class_t *cls,
                    f32 dt)
{
    if (!ship->alive || dt <= 0.0f) return;
    if (ship->repair_count == 0) return;
    if (cls->max_repair_points <= 0.0f || cls->num_repair_teams <= 0) return;

    /* Repair rate: repair_points per second, split across teams */
    f32 rate = cls->max_repair_points * (f32)cls->num_repair_teams;
    f32 heal = rate * dt;

    /* Heal top-priority subsystem */
    u8 ss_idx = ship->repair_queue[0];
    if (ss_idx < (u8)cls->subsystem_count) {
        f32 max_hp = cls->subsystems[ss_idx].max_condition;
        ship->subsystem_hp[ss_idx] += heal;
        if (ship->subsystem_hp[ss_idx] >= max_hp) {
            ship->subsystem_hp[ss_idx] = max_hp;
            /* Remove from queue when fully repaired */
            bc_repair_remove(ship, ss_idx);
        }
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
