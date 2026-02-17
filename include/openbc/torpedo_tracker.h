#ifndef OPENBC_TORPEDO_TRACKER_H
#define OPENBC_TORPEDO_TRACKER_H

#include "openbc/types.h"
#include "openbc/ship_data.h"

#define BC_MAX_TORPEDOES 32

typedef struct {
    bool      active;
    i32       shooter_id;
    int       shooter_slot;
    i32       target_id;        /* -1 = dumbfire */
    bc_vec3_t pos;
    bc_vec3_t vel;              /* normalized direction */
    f32       speed;
    f32       damage;
    f32       damage_radius;
    f32       lifetime;         /* remaining seconds */
    f32       guidance_life;    /* remaining homing time */
    f32       max_angular;      /* homing turn rate (rad/s) */
} bc_torpedo_t;

/* Hit callback: called when a torpedo hits a target.
 * shooter_slot, target_slot, damage, impact position. */
typedef void (*bc_torpedo_hit_fn)(int shooter_slot, i32 target_id,
                                   f32 damage, f32 damage_radius,
                                   bc_vec3_t impact_pos,
                                   void *user_data);

typedef struct {
    bc_torpedo_t torpedoes[BC_MAX_TORPEDOES];
    int count;
} bc_torpedo_mgr_t;

/* Initialize torpedo manager (all slots inactive) */
void bc_torpedo_mgr_init(bc_torpedo_mgr_t *mgr);

/* Spawn a torpedo. Returns slot index, or -1 if full. */
int bc_torpedo_spawn(bc_torpedo_mgr_t *mgr,
                      i32 shooter_id, int shooter_slot,
                      i32 target_id,
                      bc_vec3_t pos, bc_vec3_t vel_dir, f32 speed,
                      f32 damage, f32 damage_radius,
                      f32 lifetime, f32 guidance_life,
                      f32 max_angular);

/* Tick all torpedoes: advance position, apply homing, check hits.
 * get_target_pos: callback to look up current position of a target object.
 * Returns NULL pos if target not found. */
typedef bool (*bc_torpedo_target_fn)(i32 target_id, bc_vec3_t *out_pos,
                                      void *user_data);

void bc_torpedo_tick(bc_torpedo_mgr_t *mgr, f32 dt,
                      f32 hit_radius,
                      bc_torpedo_target_fn get_target,
                      bc_torpedo_hit_fn on_hit,
                      void *user_data);

#endif /* OPENBC_TORPEDO_TRACKER_H */
