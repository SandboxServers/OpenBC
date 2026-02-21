#ifndef OPENBC_SHIP_DATA_H
#define OPENBC_SHIP_DATA_H

#include "openbc/types.h"

/* Maximum limits */
#define BC_MAX_SHIPS          16
#define BC_MAX_PROJECTILES    16
#define BC_MAX_SUBSYSTEMS     64
#define BC_MAX_SHIELD_FACINGS  6

/* Serialization list formats (for flag 0x20 health round-robin) */
#define BC_SS_FORMAT_BASE     0   /* Hull, Shield Gen, Bridge: [cond:u8][children...] */
#define BC_SS_FORMAT_POWERED  1   /* Sensors, Engines, etc.: Base + [bit][power_pct:u8] */
#define BC_SS_FORMAT_POWER    2   /* Reactor only: Base + [main_batt:u8][backup_batt:u8] */
#define BC_SS_MAX_CHILDREN   12   /* Max children per serialization list entry */
#define BC_SS_MAX_ENTRIES    16   /* Max top-level entries in serialization list */

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

    /* Parent container index into subsystem_hp[] (-1 = no parent) */
    int     parent_idx;
} bc_subsystem_def_t;

/* Serialization list entry: one top-level subsystem in the flag 0x20 round-robin.
 * Each entry has a format (Base/Powered/Power), an hp_index into subsystem_hp[],
 * and optionally children that are serialized recursively. */
/* Power draw modes for powered subsystems */
#define BC_POWER_MODE_MAIN_FIRST   0  /* Draw from main conduit, fallback to backup */
#define BC_POWER_MODE_BACKUP_FIRST 1  /* Draw from backup conduit, fallback to main */
#define BC_POWER_MODE_BACKUP_ONLY  2  /* Draw only from backup conduit */

typedef struct {
    u8  format;                               /* BC_SS_FORMAT_BASE/POWERED/POWER */
    int hp_index;                             /* index into subsystem_hp[] */
    f32 max_condition;                        /* max HP for this entry */
    int child_count;
    int child_hp_index[BC_SS_MAX_CHILDREN];   /* children's indices into subsystem_hp[] */
    f32 child_max_condition[BC_SS_MAX_CHILDREN];
    f32 normal_power;                         /* power draw units/sec at 100% */
    u8  power_mode;                           /* BC_POWER_MODE_* (0=main-first default) */
} bc_ss_entry_t;

/* Complete serialization list for a ship class. Determines wire format order
 * for flag 0x20 health round-robin. */
typedef struct {
    bc_ss_entry_t entries[BC_SS_MAX_ENTRIES];
    int count;                                /* number of top-level entries */
    int total_hp_slots;                       /* subsystem_count + container slots */
    int reactor_entry_idx;                    /* which entry is the reactor (-1 if none) */
} bc_ss_list_t;

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
    f32     damage_radius_multiplier;  /* 1.0 = normal, 0.0 = immune */
    f32     damage_falloff_multiplier; /* 1.0 = normal */
    f32     bounding_extent;           /* max distance from origin to any subsystem center */
    int     subsystem_count;
    bc_subsystem_def_t subsystems[BC_MAX_SUBSYSTEMS];

    /* Hierarchical serialization list for flag 0x20 health round-robin */
    bc_ss_list_t ser_list;

    /* Reactor / power plant parameters */
    f32     power_output;             /* units/sec at full health */
    f32     main_battery_limit;
    f32     backup_battery_limit;
    f32     main_conduit_capacity;
    f32     backup_conduit_capacity;
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

/* Load registry from JSON file (monolith format). Returns true on success. */
bool bc_registry_load(bc_game_registry_t *reg, const char *path);

/* Load registry from versioned directory (manifest.json entry point).
 * dir should be the path to a directory containing manifest.json, ships/,
 * and projectiles/.  Returns true on success. */
bool bc_registry_load_dir(bc_game_registry_t *reg, const char *dir);

/* Lookup by index (0-based). Returns NULL if out of range. */
const bc_ship_class_t *bc_registry_get_ship(const bc_game_registry_t *reg, int index);

/* Lookup by species_id. Returns NULL if not found. */
const bc_ship_class_t *bc_registry_find_ship(const bc_game_registry_t *reg, u16 species_id);

/* Lookup by species_id, returning index. Returns -1 if not found. */
int bc_registry_find_ship_index(const bc_game_registry_t *reg, u16 species_id);

/* Lookup projectile by net_type_id. Returns NULL if not found. */
const bc_projectile_def_t *bc_registry_get_projectile(const bc_game_registry_t *reg, u8 net_type_id);

#endif /* OPENBC_SHIP_DATA_H */
