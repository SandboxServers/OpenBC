#ifndef OPENBC_COMBAT_H
#define OPENBC_COMBAT_H

#include "openbc/types.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/movement.h"

/* --- Weapon charge/cooldown ticks --- */

/* Weapon tick interval (seconds). Stock ticks WeaponSystem children at 3 Hz
 * (~0.33s), not per-frame. Per-frame ticking would advance phaser charge and
 * torpedo reload ~10x faster than stock (~30 Hz main loop). The simulation
 * loop accumulates dt and steps the charge/cooldown ticks at this interval,
 * mirroring the power-system 1 Hz gate (bc_ship_power_tick). */
#define BC_WEAPON_TICK_INTERVAL  (1.0f / 3.0f)  /* exactly 3 Hz (stock WeaponSystem cadence) */

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
 * Each subsystem AABB expanded by search_radius: [pos - r*sr, pos + r*sr].
 * Damage AABB = [impact - damage_radius, impact + damage_radius].
 * search_radius scales each subsystem's effective bounding radius (1.0 = normal,
 * 1.5 = find subsystems within 1.5x their radius from the damage origin).
 * Returns count of overlapping subsystems written to out_indices. */
int bc_combat_find_hit_subsystems(const bc_ship_class_t *cls,
                                  bc_vec3_t local_impact, f32 damage_radius,
                                  f32 search_radius,
                                  int *out_indices, int max_out);

/* Apply damage to target with shield absorption.
 * impact_dir = normalized direction from attacker to target.
 * area_effect: true = damage/6 per shield facing, false = single facing.
 * damage_radius: used for subsystem AABB overlap test (scaled by target's
 * damage_radius_multiplier; pass 0.0 to skip subsystem damage).
 * search_radius: spatial search expansion factor for subsystem hit detection.
 *   Each subsystem's bounding radius is scaled by this value in the AABB test.
 *   1.0 = normal (weapons); 1.5 = collision path (wider spatial search).
 * Pipeline: shields absorb first; hit subsystems absorb from overflow
 * independently (each up to min(overflow, ss_hp)); hull gets remainder. */
void bc_combat_apply_damage(bc_ship_state_t *target,
                            const bc_ship_class_t *cls,
                            f32 damage, f32 damage_radius,
                            bc_vec3_t impact_dir,
                            bool area_effect,
                            f32 search_radius);

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
 * Binary-confirmed: DAT_008E4E1C in stbc.exe = 5.0f (raw bytes 00 00 A0 40).
 * Settable per-ship via SWIG CloakingSubsystem_SetCloakTime in stock;
 * not exposed via hardpoint script API. */
#define BC_CLOAK_TRANSITION_TIME  5.0f

/* Cloak energy threshold: if the cloaking device's power efficiency
 * drops below this, the cloak fails and decloaking begins. */
#define BC_CLOAK_ENERGY_THRESHOLD 0.5f

/* Shield-delay window (seconds) for cloak transitions (Issue #192).
 * Wire-protocol trace analysis of a stock dedicated server session shows the
 * shield-active state change is deferred by this delay relative to the cloak
 * transition: shields stay up ~1.0s after cloak-up begins (vulnerability
 * window) and re-enable ~1.0s after decloak completes (grace window).
 * This is a class-level global shared by all cloaking devices. */
#define BC_CLOAK_SHIELD_DELAY 1.0f

/* Begin cloaking. Shields functionally disabled (stop absorbing/recharging)
 * but HP preserved. Weapons disabled.
 * Returns false if ship cannot cloak (no device, dead, already cloaking/cloaked). */
bool bc_cloak_start(bc_ship_state_t *ship, const bc_ship_class_t *cls);

/* Begin decloaking. Ship becomes visible but shields/weapons stay offline
 * until transition completes (vulnerability window).
 * Returns false if not cloaked/cloaking. */
bool bc_cloak_stop(bc_ship_state_t *ship);

/* Advance cloak state machine timer. Call each tick.
 * cloak_efficiency: 0.0-1.0 power efficiency for the cloaking device.
 *   If below BC_CLOAK_ENERGY_THRESHOLD while CLOAKED, auto-decloak begins.
 *   Pass 1.0f to disable the energy-failure check.
 * On DECLOAKING->DECLOAKED transition: any shield facing at 0 HP set to 1.0. */
void bc_cloak_tick(bc_ship_state_t *ship, f32 cloak_efficiency, f32 dt);

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

/* Tick tractor beam: apply multiplicative drag to SOURCE ship's engine stats.
 * Reduces speed directly and sets tractor_drag for angular velocity scaling.
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

/* --- Friendly-fire tracking (Issue #203) --- */

/* Friendly-fire tracking mode.
 *  PERMISSIVE: no tracking, no warnings (legacy OpenBC behavior).
 *  WARNING:    track + warn at warning_points threshold; never end game (stock
 *              default — mission startup typically warns at 100 points).
 *  STRICT:     track + warn + end game once tolerance is exceeded. */
typedef enum {
    BC_FF_MODE_PERMISSIVE = 0,
    BC_FF_MODE_WARNING    = 1,
    BC_FF_MODE_STRICT     = 2
} bc_ff_mode_t;

/* Per-server friendly-fire accumulator state. Mirrors the stock three-float
 * model (tolerance / current / warning_points) plus the game-over toggle. */
typedef struct {
    bc_ff_mode_t mode;
    f32  tolerance;              /* cumulative FF damage that crosses threshold */
    f32  current;               /* running FF damage accumulator */
    f32  warning_points;        /* threshold at which a warning fires */
    bool game_over_on_threshold;/* end game when current >= tolerance */
    bool warned;                /* one-shot latch: warning already emitted */
} bc_friendly_fire_t;

/* Outcome flags from a single FF accumulation step (bc_ff_step). */
typedef struct {
    bool warned;     /* warning threshold crossed on THIS step (one-shot) */
    bool game_over;  /* tolerance exceeded AND game_over_on_threshold set */
} bc_ff_outcome_t;

/* Pure FF accumulation/decision step (no I/O, no globals).
 * Mutates ff->current and ff->warned; returns what reactions are due so the
 * caller can perform the wire-side emits. No-op (zero outcome) in PERMISSIVE
 * mode or for non-positive damage. Unit-testable in isolation. */
bc_ff_outcome_t bc_ff_step(bc_friendly_fire_t *ff, f32 damage_amount);

#endif /* OPENBC_COMBAT_H */
