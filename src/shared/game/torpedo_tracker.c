#include "openbc/torpedo_tracker.h"
#include "openbc/movement.h"
#include <string.h>
#include <math.h>

void bc_torpedo_mgr_init(bc_torpedo_mgr_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
}

int bc_torpedo_spawn(bc_torpedo_mgr_t *mgr,
                      i32 shooter_id, int shooter_slot,
                      i32 target_id,
                      bc_vec3_t pos, bc_vec3_t vel_dir, f32 speed,
                      f32 damage, f32 damage_radius,
                      f32 lifetime, f32 guidance_life,
                      f32 max_angular)
{
    /* Find free slot */
    for (int i = 0; i < BC_MAX_TORPEDOES; i++) {
        if (!mgr->torpedoes[i].active) {
            bc_torpedo_t *t = &mgr->torpedoes[i];
            t->active = true;
            t->shooter_id = shooter_id;
            t->shooter_slot = shooter_slot;
            t->target_id = target_id;
            t->pos = pos;
            t->vel = vel_dir;
            t->speed = speed;
            t->damage = damage;
            t->damage_radius = damage_radius;
            t->lifetime = lifetime;
            t->guidance_life = guidance_life;
            t->max_angular = max_angular;
            mgr->count++;
            return i;
        }
    }
    return -1; /* full */
}

void bc_torpedo_tick(bc_torpedo_mgr_t *mgr, f32 dt,
                      f32 hit_radius,
                      bc_torpedo_target_fn get_target,
                      bc_torpedo_hit_fn on_hit,
                      void *user_data)
{
    if (dt <= 0.0f) return;

    for (int i = 0; i < BC_MAX_TORPEDOES; i++) {
        bc_torpedo_t *t = &mgr->torpedoes[i];
        if (!t->active) continue;

        /* Homing: turn velocity toward target */
        if (t->target_id >= 0 && t->guidance_life > 0.0f && get_target) {
            bc_vec3_t target_pos;
            if (get_target(t->target_id, &target_pos, user_data)) {
                bc_vec3_t to_target = bc_vec3_normalize(
                    bc_vec3_sub(target_pos, t->pos));

                /* Blend velocity toward target by max angular rate */
                f32 max_turn = t->max_angular * dt;
                bc_vec3_t desired = bc_vec3_normalize(
                    bc_vec3_add(
                        bc_vec3_scale(t->vel, 1.0f),
                        bc_vec3_scale(to_target, max_turn)));
                t->vel = desired;
            }
            t->guidance_life -= dt;
        }

        /* Advance position */
        t->pos = bc_vec3_add(t->pos, bc_vec3_scale(t->vel, t->speed * dt));

        /* Check hit: distance to target */
        if (t->target_id >= 0 && get_target) {
            bc_vec3_t target_pos;
            if (get_target(t->target_id, &target_pos, user_data)) {
                f32 dist = bc_vec3_dist(t->pos, target_pos);
                if (dist < hit_radius) {
                    /* HIT */
                    if (on_hit) {
                        on_hit(t->shooter_slot, t->target_id,
                               t->damage, t->damage_radius,
                               t->pos, user_data);
                    }
                    t->active = false;
                    mgr->count--;
                    continue;
                }
            }
        }

        /* Decrement lifetime */
        t->lifetime -= dt;
        if (t->lifetime <= 0.0f) {
            t->active = false;
            mgr->count--;
        }
    }
}
