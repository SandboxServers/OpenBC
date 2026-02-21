#ifndef OPENBC_SHIP_STATE_H
#define OPENBC_SHIP_STATE_H

#include "openbc/types.h"
#include "openbc/ship_data.h"

/* Maximum weapon banks/tubes per ship */
#define BC_MAX_PHASER_BANKS   16
#define BC_MAX_TORPEDO_TUBES   8

/* Cloak states */
#define BC_CLOAK_DECLOAKED    0
#define BC_CLOAK_CLOAKING     1
#define BC_CLOAK_CLOAKED      2
#define BC_CLOAK_DECLOAKING   3

/* Shield facing indices */
#define BC_SHIELD_FRONT   0
#define BC_SHIELD_REAR    1
#define BC_SHIELD_TOP     2
#define BC_SHIELD_BOTTOM  3
#define BC_SHIELD_LEFT    4
#define BC_SHIELD_RIGHT   5

typedef struct {
    int        class_index;      /* index into registry->ships[] */
    i32        object_id;
    u8         owner_slot;
    u8         team_id;
    char       player_name[32];

    /* Transform */
    bc_vec3_t  pos;
    f32        quat[4];          /* w, x, y, z */
    bc_vec3_t  fwd;
    bc_vec3_t  up;
    f32        speed;

    /* Health */
    f32        hull_hp;
    f32        shield_hp[BC_MAX_SHIELD_FACINGS];
    f32        subsystem_hp[BC_MAX_SUBSYSTEMS]; /* current HP per subsystem */

    /* Cloak */
    u8         cloak_state;
    f32        cloak_timer;

    /* Weapons */
    f32        phaser_charge[BC_MAX_PHASER_BANKS];
    f32        torpedo_cooldown[BC_MAX_TORPEDO_TUBES];
    u8         torpedo_type;
    bool       torpedo_switching;
    f32        torpedo_switch_timer;

    /* Tractor */
    i32        tractor_target_id; /* -1 = none */

    /* Power allocation (indexed by ser_list entry index) */
    u8         power_pct[BC_SS_MAX_ENTRIES];     /* 0-125, Powered entries only */
    bool       subsys_enabled[BC_SS_MAX_ENTRIES]; /* on/off state per entry */
    u8         phaser_level;                      /* 0=LOW, 1=MED, 2=HIGH */

    /* Reactor / battery state */
    f32        main_battery;
    f32        backup_battery;
    f32        main_conduit_remaining;
    f32        backup_conduit_remaining;
    f32        power_tick_accum;        /* 1-second interval accumulator */

    /* Power efficiency (computed per-frame by reactor tick) */
    f32        efficiency[BC_SS_MAX_ENTRIES];  /* 0.0-1.0 per powered entry */

    /* Systems */
    bool       alive;
    u8         repair_queue[BC_MAX_SUBSYSTEMS];
    int        repair_count;

    /* PythonEvent subsystem object IDs (sequential counter, NOT player-base IDs).
     * Assigned by bc_ship_assign_subsystem_ids() after bc_ship_init(). */
    i32        subsys_obj_id[BC_MAX_SUBSYSTEMS];
    i32        repair_subsys_obj_id;  /* the repair subsystem's object ID (-1 if none) */
} bc_ship_state_t;

/* Initialize a ship state from registry class data.
 * Sets full HP on hull, shields, all subsystems. */
void bc_ship_init(bc_ship_state_t *ship,
                  const bc_ship_class_t *cls,
                  int class_index,
                  i32 object_id,
                  u8 owner_slot,
                  u8 team_id);

/* Assign sequential object IDs to each subsystem in serialization list order.
 * Must be called after bc_ship_init(). counter is a global auto-increment
 * that persists across all ship creations. */
void bc_ship_assign_subsystem_ids(bc_ship_state_t *ship,
                                   const bc_ship_class_t *cls,
                                   i32 *counter);

/* Serialize ship state into an ObjectCreateTeam ship blob.
 * Returns bytes written, or -1 on error. */
int bc_ship_serialize(const bc_ship_state_t *ship,
                      const bc_ship_class_t *cls,
                      u8 *buf, int buf_size);

/* Build a complete ObjectCreateTeam packet (opcode 0x03 + blob).
 * Returns bytes written, or -1 on error. */
int bc_ship_build_create_packet(const bc_ship_state_t *ship,
                                const bc_ship_class_t *cls,
                                u8 *buf, int buf_size);

#endif /* OPENBC_SHIP_STATE_H */
