#include "openbc/movement.h"
#include "openbc/buffer.h"
#include "openbc/game_builders.h"
#include <math.h>
#include <string.h>

/* --- Vec3 helpers --- */

f32 bc_vec3_dot(bc_vec3_t a, bc_vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

f32 bc_vec3_len(bc_vec3_t v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

bc_vec3_t bc_vec3_normalize(bc_vec3_t v)
{
    f32 len = bc_vec3_len(v);
    if (len < 1e-8f) return (bc_vec3_t){0, 0, 0};
    return (bc_vec3_t){v.x / len, v.y / len, v.z / len};
}

bc_vec3_t bc_vec3_cross(bc_vec3_t a, bc_vec3_t b)
{
    return (bc_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

bc_vec3_t bc_vec3_sub(bc_vec3_t a, bc_vec3_t b)
{
    return (bc_vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

bc_vec3_t bc_vec3_add(bc_vec3_t a, bc_vec3_t b)
{
    return (bc_vec3_t){a.x + b.x, a.y + b.y, a.z + b.z};
}

bc_vec3_t bc_vec3_scale(bc_vec3_t v, f32 s)
{
    return (bc_vec3_t){v.x * s, v.y * s, v.z * s};
}

f32 bc_vec3_dist(bc_vec3_t a, bc_vec3_t b)
{
    return bc_vec3_len(bc_vec3_sub(a, b));
}

/* --- Movement --- */

void bc_ship_move_tick(bc_ship_state_t *ship, f32 engine_efficiency, f32 dt)
{
    if (!ship->alive || dt <= 0.0f) return;
    ship->pos = bc_vec3_add(ship->pos, bc_vec3_scale(ship->fwd, ship->speed * engine_efficiency * dt));
}

void bc_ship_turn_toward(bc_ship_state_t *ship,
                         const bc_ship_class_t *cls,
                         bc_vec3_t target, f32 dt)
{
    if (!ship->alive) return;

    bc_vec3_t to_target = bc_vec3_sub(target, ship->pos);
    f32 dist = bc_vec3_len(to_target);
    if (dist < 1e-4f) return;

    bc_vec3_t desired = bc_vec3_normalize(to_target);

    /* Angle between current forward and desired */
    f32 dot = bc_vec3_dot(ship->fwd, desired);
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    f32 angle = acosf(dot);

    if (angle < 1e-5f) return; /* already facing target */

    /* Max turn this tick */
    f32 max_turn = cls->max_angular_velocity * dt;
    if (max_turn <= 0.0f) return;

    f32 t = (angle <= max_turn) ? 1.0f : (max_turn / angle);

    /* Rotation axis */
    bc_vec3_t axis = bc_vec3_cross(ship->fwd, desired);
    f32 axis_len = bc_vec3_len(axis);

    if (axis_len < 1e-8f) {
        /* Vectors are parallel (same or opposite). If opposite, pick arbitrary axis. */
        if (dot < 0.0f) {
            /* 180 degree turn: use up vector as axis */
            axis = ship->up;
        } else {
            return; /* Same direction */
        }
    }
    axis = bc_vec3_normalize(axis);

    /* Slerp-like rotation: rotate fwd by t * angle around axis */
    f32 rot_angle = t * angle;
    f32 c = cosf(rot_angle);
    f32 s = sinf(rot_angle);

    /* Rodrigues' rotation formula: v' = v*cos(a) + (k x v)*sin(a) + k*(k.v)*(1-cos(a)) */
    bc_vec3_t kxv = bc_vec3_cross(axis, ship->fwd);
    f32 kdv = bc_vec3_dot(axis, ship->fwd);

    ship->fwd = bc_vec3_normalize((bc_vec3_t){
        ship->fwd.x * c + kxv.x * s + axis.x * kdv * (1.0f - c),
        ship->fwd.y * c + kxv.y * s + axis.y * kdv * (1.0f - c),
        ship->fwd.z * c + kxv.z * s + axis.z * kdv * (1.0f - c),
    });

    /* Also rotate up vector */
    bc_vec3_t kxu = bc_vec3_cross(axis, ship->up);
    f32 kdu = bc_vec3_dot(axis, ship->up);
    ship->up = bc_vec3_normalize((bc_vec3_t){
        ship->up.x * c + kxu.x * s + axis.x * kdu * (1.0f - c),
        ship->up.y * c + kxu.y * s + axis.y * kdu * (1.0f - c),
        ship->up.z * c + kxu.z * s + axis.z * kdu * (1.0f - c),
    });
}

void bc_ship_set_speed(bc_ship_state_t *ship,
                       const bc_ship_class_t *cls,
                       f32 speed)
{
    if (speed < 0.0f) speed = 0.0f;
    if (speed > cls->max_speed) speed = cls->max_speed;
    ship->speed = speed;
}

/* --- StateUpdate --- */

int bc_ship_build_state_update(const bc_ship_state_t *cur,
                               const bc_ship_state_t *prev,
                               f32 game_time,
                               u8 *buf, int buf_size)
{
    u8 dirty = 0;

    /* Compare fields to determine dirty flags */
    if (fabsf(cur->pos.x - prev->pos.x) > 0.01f ||
        fabsf(cur->pos.y - prev->pos.y) > 0.01f ||
        fabsf(cur->pos.z - prev->pos.z) > 0.01f) {
        dirty |= BC_DIRTY_POS_ABS;
    }
    if (fabsf(cur->fwd.x - prev->fwd.x) > 0.001f ||
        fabsf(cur->fwd.y - prev->fwd.y) > 0.001f ||
        fabsf(cur->fwd.z - prev->fwd.z) > 0.001f) {
        dirty |= BC_DIRTY_FWD;
    }
    if (fabsf(cur->up.x - prev->up.x) > 0.001f ||
        fabsf(cur->up.y - prev->up.y) > 0.001f ||
        fabsf(cur->up.z - prev->up.z) > 0.001f) {
        dirty |= BC_DIRTY_UP;
    }
    if (fabsf(cur->speed - prev->speed) > 0.01f) {
        dirty |= BC_DIRTY_SPEED;
    }
    if (cur->cloak_state != prev->cloak_state) {
        dirty |= BC_DIRTY_CLOAK;
    }

    if (dirty == 0) return 0; /* nothing changed */

    /* Build field data */
    u8 field_data[128];
    bc_buffer_t fb;
    bc_buf_init(&fb, field_data, sizeof(field_data));

    if (dirty & BC_DIRTY_POS_ABS) {
        bc_buf_write_f32(&fb, cur->pos.x);
        bc_buf_write_f32(&fb, cur->pos.y);
        bc_buf_write_f32(&fb, cur->pos.z);
        /* No hash bit for now */
        bc_buf_write_bit(&fb, false);
    }
    if (dirty & BC_DIRTY_FWD) {
        bc_buf_write_cv3(&fb, cur->fwd.x, cur->fwd.y, cur->fwd.z);
    }
    if (dirty & BC_DIRTY_UP) {
        bc_buf_write_cv3(&fb, cur->up.x, cur->up.y, cur->up.z);
    }
    if (dirty & BC_DIRTY_SPEED) {
        bc_buf_write_cf16(&fb, cur->speed);
    }
    if (dirty & BC_DIRTY_CLOAK) {
        bc_buf_write_u8(&fb, cur->cloak_state);
    }

    return bc_build_state_update(buf, buf_size,
                                  cur->object_id, game_time, dirty,
                                  field_data, (int)fb.pos);
}
