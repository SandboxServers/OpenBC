#ifndef OPENBC_SHIP_DATA_H
#define OPENBC_SHIP_DATA_H

#include "openbc/types.h"

/* Maximum limits */
#define BC_MAX_SHIPS          16
#define BC_MAX_PROJECTILES    16
#define BC_MAX_SUBSYSTEMS     64
#define BC_MAX_SHIELD_FACINGS  6

typedef struct { f32 x, y, z; } bc_vec3_t;

typedef struct {
    char    name[64];
    char    type[32];       /* "hull", "phaser", "torpedo_tube", "shield", etc. */
    bc_vec3_t position;
    f32     radius;
    f32     max_condition;
    f32     disabled_pct;
    bool    is_critical;
    bool    is_targetable;
    f32     repair_complexity;

    /* Weapon-specific (zero if N/A) */
    f32     max_damage;
    f32     max_charge;
    f32     min_firing_charge;
    f32     recharge_rate;
    f32     discharge_rate;
    f32     max_damage_distance;
    u8      weapon_id;

    /* Phaser/pulse orientation */
    bc_vec3_t forward;
    bc_vec3_t up;
    f32     arc_width[2];   /* min, max angles */
    f32     arc_height[2];

    /* Torpedo tube */
    f32     reload_delay;
    i32     max_ready;
    f32     immediate_delay;
    bc_vec3_t direction;

    /* Tractor beam */
    f32     normal_power;

    /* Cloak */
    f32     cloak_strength;

    /* Repair */
    f32     max_repair_points;
    i32     num_repair_teams;
} bc_subsystem_def_t;

typedef struct {
    char    name[32];
    u16     species_id;
    char    faction[32];
    f32     hull_hp;
    f32     mass;
    f32     rotational_inertia;
    f32     max_speed;
    f32     max_accel;
    f32     max_angular_accel;
    f32     max_angular_velocity;
    f32     shield_hp[BC_MAX_SHIELD_FACINGS];
    f32     shield_recharge[BC_MAX_SHIELD_FACINGS];
    bool    can_cloak;
    bool    has_tractor;
    u8      torpedo_tubes;
    u8      phaser_banks;
    u8      pulse_weapons;
    u8      tractor_beams;
    f32     max_repair_points;
    i32     num_repair_teams;
    int     subsystem_count;
    bc_subsystem_def_t subsystems[BC_MAX_SUBSYSTEMS];
} bc_ship_class_t;

typedef struct {
    char    name[32];
    char    script[32];
    u8      net_type_id;
    f32     damage;
    f32     launch_speed;
    f32     power_cost;
    f32     guidance_lifetime;
    f32     max_angular_accel;
    f32     lifetime;
    f32     damage_radius_factor;
} bc_projectile_def_t;

typedef struct {
    bc_ship_class_t     ships[BC_MAX_SHIPS];
    int                 ship_count;
    bc_projectile_def_t projectiles[BC_MAX_PROJECTILES];
    int                 projectile_count;
    bool                loaded;
} bc_game_registry_t;

/* Load registry from JSON file. Returns true on success. */
bool bc_registry_load(bc_game_registry_t *reg, const char *path);

/* Lookup by index (0-based). Returns NULL if out of range. */
const bc_ship_class_t *bc_registry_get_ship(const bc_game_registry_t *reg, int index);

/* Lookup by species_id. Returns NULL if not found. */
const bc_ship_class_t *bc_registry_find_ship(const bc_game_registry_t *reg, u16 species_id);

/* Lookup by species_id, returning index. Returns -1 if not found. */
int bc_registry_find_ship_index(const bc_game_registry_t *reg, u16 species_id);

/* Lookup projectile by net_type_id. Returns NULL if not found. */
const bc_projectile_def_t *bc_registry_get_projectile(const bc_game_registry_t *reg, u8 net_type_id);

#endif /* OPENBC_SHIP_DATA_H */
