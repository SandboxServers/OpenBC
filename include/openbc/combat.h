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

/* Find all subsystems whose AABB overlaps the damage volume.
 * Each subsystem AABB = [pos - radius, pos + radius] per axis.
 * Damage AABB = [impact - damage_radius, impact + damage_radius].
 * Returns count of overlapping subsystems written to out_indices. */
int bc_combat_find_hit_subsystems(const bc_ship_class_t *cls,
                                  bc_vec3_t local_impact, f32 damage_radius,
                                  int *out_indices, int max_out);

/* Apply damage to target with shield absorption.
 * impact_dir = normalized direction from attacker to target.
 * area_effect: true = damage/6 per shield facing, false = single facing.
 * damage_radius: used for subsystem AABB overlap test (scaled by target's
 * damage_radius_multiplier; if multiplier is 0.0, subsystem damage skipped).
 * shield_scale: multiplier on shield absorption capacity (1.0 = normal,
 * 1.5 = collision shields absorb 50% more before overflow). */
void bc_combat_apply_damage(bc_ship_state_t *target,
                            const bc_ship_class_t *cls,
                            f32 damage, f32 damage_radius,
                            bc_vec3_t impact_dir,
                            bool area_effect,
                            f32 shield_scale);

/* Path 1 — Direct collision: raw * 0.1 + 0.1, cap 0.5 (fractional). */
f32 bc_combat_collision_damage_path1(f32 collision_energy, f32 ship_mass,
                                      int contact_count);

/* Path 2 — Collision effect handler: raw * 900 + 500 (absolute HP).
 * Dead zone at raw <= 0.01. Used by server collision handler. */
f32 bc_combat_collision_damage_path2(f32 collision_energy, f32 ship_mass,
                                      int contact_count);

/* --- Shield recharge --- */

/* Tick shield recharge with power budget and overflow redistribution.
 * power_level: 0.0-1.0, scales recharge rate. Only when not cloaked. */
void bc_combat_shield_tick(bc_ship_state_t *ship,
                           const bc_ship_class_t *cls,
                           f32 power_level, f32 dt);

/* --- Cloaking device --- */

/* Default cloak transition time (seconds).
 * BC engine uses ~3s; not exposed in hardpoint scripts. */
#define BC_CLOAK_TRANSITION_TIME  3.0f

/* Begin cloaking. Shields functionally disabled (stop absorbing/recharging)
 * but HP preserved. Weapons disabled.
 * Returns false if ship cannot cloak (no device, dead, already cloaking/cloaked). */
bool bc_cloak_start(bc_ship_state_t *ship, const bc_ship_class_t *cls);

/* Begin decloaking. Ship becomes visible but shields/weapons stay offline
 * until transition completes (vulnerability window).
 * Returns false if not cloaked/cloaking. */
bool bc_cloak_stop(bc_ship_state_t *ship);

/* Advance cloak state machine timer. Call each tick.
 * On DECLOAKING->DECLOAKED transition: any shield facing at 0 HP set to 1.0. */
void bc_cloak_tick(bc_ship_state_t *ship, f32 dt);

/* Check if ship can fire weapons (only when fully DECLOAKED). */
bool bc_cloak_can_fire(const bc_ship_state_t *ship);

/* Check if shields are active (only when fully DECLOAKED). */
bool bc_cloak_shields_active(const bc_ship_state_t *ship);

/* --- Tractor beams --- */

/* Check if tractor beam can engage (has charge, not cloaked, subsystem alive). */
bool bc_combat_can_tractor(const bc_ship_state_t *ship,
                            const bc_ship_class_t *cls,
                            int beam_idx);

/* Engage tractor beam on target. Sets tractor_target_id.
 * Returns the tractor subsystem index, or -1 on failure. */
int bc_combat_tractor_engage(bc_ship_state_t *ship,
                              const bc_ship_class_t *cls,
                              int beam_idx,
                              i32 target_id);

/* Release tractor beam. */
void bc_combat_tractor_disengage(bc_ship_state_t *ship);

/* Tick tractor beam: apply multiplicative drag to target's speed.
 * No damage applied (spec: tractor beams do NOT apply direct damage). */
void bc_combat_tractor_tick(bc_ship_state_t *ship,
                             bc_ship_state_t *target,
                             const bc_ship_class_t *cls,
                             f32 dt);

/* --- Repair system --- */

/* Add a subsystem to the repair queue.
 * Returns true if added, false if queue full or already queued. */
bool bc_repair_add(bc_ship_state_t *ship, u8 subsys_idx);

/* Remove a subsystem from the repair queue. */
void bc_repair_remove(bc_ship_state_t *ship, u8 subsys_idx);

/* Tick repair: heal up to num_repair_teams subsystems simultaneously.
 * raw_repair = max_repair_points * repair_system_health_pct * dt
 * per_sub = raw_repair / min(queue_count, num_repair_teams)
 * condition_gain = per_sub / repair_complexity
 * Destroyed subsystems (0 HP) are skipped but remain in queue. */
void bc_repair_tick(bc_ship_state_t *ship,
                    const bc_ship_class_t *cls,
                    f32 dt);

/* Auto-queue any subsystem below its disabled threshold. */
void bc_repair_auto_queue(bc_ship_state_t *ship,
                           const bc_ship_class_t *cls);

#endif /* OPENBC_COMBAT_H */
