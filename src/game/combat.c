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
