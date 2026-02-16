#ifndef OPENBC_COMBAT_H
#define OPENBC_COMBAT_H

#include "openbc/types.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/movement.h"

/* --- Weapon charge/cooldown ticks --- */

/* Tick phaser/pulse weapon charge (recharge toward max_charge).
 * power_level: 0.0-1.0, affects recharge rate. */
void bc_combat_charge_tick(bc_ship_state_t *ship,
                           const bc_ship_class_t *cls,
                           f32 power_level, f32 dt);

/* Tick torpedo tube cooldowns (count down toward 0). */
void bc_combat_torpedo_tick(bc_ship_state_t *ship,
                            const bc_ship_class_t *cls,
                            f32 dt);

/* --- Phaser / Pulse fire --- */

/* Check if phaser bank can fire (charge >= min, not cloaked, subsystem alive). */
bool bc_combat_can_fire_phaser(const bc_ship_state_t *ship,
                               const bc_ship_class_t *cls,
                               int bank_idx);

/* Fire phaser. Builds BeamFire packet. Resets charge to 0.
 * Returns bytes written to buf, or -1 on error / cannot fire. */
int bc_combat_fire_phaser(bc_ship_state_t *shooter,
                          const bc_ship_class_t *cls,
                          int bank_idx,
                          i32 target_id,
                          u8 *buf, int buf_size);

/* --- Torpedo fire --- */

/* Check if torpedo tube can fire. */
bool bc_combat_can_fire_torpedo(const bc_ship_state_t *ship,
                                const bc_ship_class_t *cls,
                                int tube_idx);

/* Fire torpedo. Builds TorpedoFire packet. Sets cooldown.
 * direction = normalized velocity direction for the torpedo.
 * Returns bytes written, or -1. */
int bc_combat_fire_torpedo(bc_ship_state_t *shooter,
                           const bc_ship_class_t *cls,
                           int tube_idx,
                           i32 target_id,
                           bc_vec3_t direction,
                           u8 *buf, int buf_size);

/* Switch torpedo type. Imposes reload delay on all tubes. */
void bc_combat_switch_torpedo_type(bc_ship_state_t *ship,
                                   const bc_ship_class_t *cls,
                                   u8 new_type);

/* --- Damage --- */

/* Determine which shield facing an impact comes from.
 * impact_dir = normalized direction FROM attacker TO target in world space.
 * Returns shield index (0-5). */
int bc_combat_shield_facing(const bc_ship_state_t *target,
                            bc_vec3_t impact_dir);

/* Find nearest subsystem to a local-frame impact point.
 * Returns subsystem index, or -1 if none in range. */
int bc_combat_find_hit_subsystem(const bc_ship_class_t *cls,
                                 bc_vec3_t local_impact);

/* Apply damage to target with directional shield absorption.
 * impact_dir = normalized direction from attacker to target. */
void bc_combat_apply_damage(bc_ship_state_t *target,
                            const bc_ship_class_t *cls,
                            f32 damage,
                            bc_vec3_t impact_dir);

/* --- Shield recharge --- */

/* Tick shield recharge (only when not cloaked). */
void bc_combat_shield_tick(bc_ship_state_t *ship,
                           const bc_ship_class_t *cls,
                           f32 dt);

#endif /* OPENBC_COMBAT_H */
