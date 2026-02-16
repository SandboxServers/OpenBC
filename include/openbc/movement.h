#ifndef OPENBC_MOVEMENT_H
#define OPENBC_MOVEMENT_H

#include "openbc/types.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"

/* StateUpdate dirty flags */
#define BC_DIRTY_POS_ABS     0x01
#define BC_DIRTY_POS_DELTA   0x02
#define BC_DIRTY_FWD         0x04
#define BC_DIRTY_UP          0x08
#define BC_DIRTY_SPEED       0x10
#define BC_DIRTY_SUBSYS      0x20
#define BC_DIRTY_CLOAK       0x40
#define BC_DIRTY_WEAPON      0x80

/* Advance ship position: pos += fwd * speed * dt */
void bc_ship_move_tick(bc_ship_state_t *ship, f32 dt);

/* Rotate ship forward vector toward a target position.
 * Turn rate clamped by cls->max_angular_velocity. */
void bc_ship_turn_toward(bc_ship_state_t *ship,
                         const bc_ship_class_t *cls,
                         bc_vec3_t target, f32 dt);

/* Set ship speed, clamped to [0, cls->max_speed] */
void bc_ship_set_speed(bc_ship_state_t *ship,
                       const bc_ship_class_t *cls,
                       f32 speed);

/* Build a StateUpdate packet by comparing current vs prev state.
 * Returns bytes written, or -1 on error. */
int bc_ship_build_state_update(const bc_ship_state_t *cur,
                               const bc_ship_state_t *prev,
                               f32 game_time,
                               u8 *buf, int buf_size);

/* --- Vec3 math helpers --- */
f32       bc_vec3_dot(bc_vec3_t a, bc_vec3_t b);
f32       bc_vec3_len(bc_vec3_t v);
bc_vec3_t bc_vec3_normalize(bc_vec3_t v);
bc_vec3_t bc_vec3_cross(bc_vec3_t a, bc_vec3_t b);
bc_vec3_t bc_vec3_sub(bc_vec3_t a, bc_vec3_t b);
bc_vec3_t bc_vec3_add(bc_vec3_t a, bc_vec3_t b);
bc_vec3_t bc_vec3_scale(bc_vec3_t v, f32 s);
f32       bc_vec3_dist(bc_vec3_t a, bc_vec3_t b);

#endif /* OPENBC_MOVEMENT_H */
