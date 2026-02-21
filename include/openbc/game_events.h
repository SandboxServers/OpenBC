#ifndef OPENBC_GAME_EVENTS_H
#define OPENBC_GAME_EVENTS_H

#include "openbc/types.h"

/*
 * Game event parsers -- read-only extraction of gameplay data from relay
 * payloads. Used for combat logging; original bytes are relayed untouched.
 *
 * Wire formats from docs/phase1-verified-protocol.md Section 7.
 */

/* Object ID -> player slot (pure arithmetic, verified from RE docs).
 * BC assigns object IDs as: base=0x3FFFFFFF, each slot owns 2^18 IDs.
 * Returns slot index (0-5), or -1 if out of range. */
int bc_object_id_to_slot(i32 object_id);

/* --- Parsed event structs --- */

typedef struct {
    i32  shooter_id;
    u8   subsys_index;
    u8   flags;
    f32  vel_x, vel_y, vel_z;
    bool has_target;
    i32  target_id;
    f32  impact_x, impact_y, impact_z;
} bc_torpedo_event_t;

typedef struct {
    i32  shooter_id;
    u8   flags;
    f32  dir_x, dir_y, dir_z;
    u8   more_flags;
    bool has_target;
    i32  target_id;
} bc_beam_event_t;

typedef struct {
    i32  object_id;
    f32  impact_x, impact_y, impact_z;
    f32  damage;
    f32  radius;
} bc_explosion_event_t;

typedef struct {
    i32  object_id;
} bc_destroy_event_t;

/* CollisionEffect (0x15) -- client reports collision to host.
 * Wire: [0x15][class_id:i32][code:i32][source_obj:i32][target_obj:i32]
 *       [contact_count:u8][contacts:4*N][force:f32]
 * source_obj = 0 for environment collisions (asteroids). */
typedef struct {
    i32  source_object_id;  /* Other object (0 = environment) */
    i32  target_object_id;  /* Ship reporting the collision */
    u8   contact_count;
    f32  collision_force;   /* Impact force magnitude */
} bc_collision_event_t;

typedef struct {
    u8   type_tag;
    u8   owner_slot;
    u8   team_id;       /* Only valid if type_tag == 3 */
    bool has_team;
} bc_object_create_header_t;

typedef struct {
    u8   sender_slot;
    char message[256];
    int  message_len;
} bc_chat_event_t;

/* Ship blob header -- extracted from ObjCreateTeam ship data.
 * Wire format (from packet captures):
 *   [prefix:4 bytes][object_id:i32][species_id:u8][pos:3xf32]... */
typedef struct {
    i32 object_id;
    u16 species_id;     /* read as u8 from wire, widened to u16 */
    f32 pos_x, pos_y, pos_z;
} bc_ship_blob_header_t;

/* Parsed StateUpdate -- position/orientation/speed fields */
typedef struct {
    i32 object_id;
    f32 game_time;
    u8  dirty;
    f32 pos_x, pos_y, pos_z;   /* valid when dirty & 0x01 */
    f32 fwd_x, fwd_y, fwd_z;   /* valid when dirty & 0x04 */
    f32 up_x,  up_y,  up_z;    /* valid when dirty & 0x08 */
    f32 speed;                  /* valid when dirty & 0x10 */
} bc_state_update_t;

/* --- Parser functions ---
 * Each takes payload (starting at the opcode byte) and length.
 * Returns true on success, false if payload is truncated/malformed. */

bool bc_parse_torpedo_fire(const u8 *payload, int len, bc_torpedo_event_t *out);
bool bc_parse_beam_fire(const u8 *payload, int len, bc_beam_event_t *out);
bool bc_parse_explosion(const u8 *payload, int len, bc_explosion_event_t *out);
bool bc_parse_destroy_obj(const u8 *payload, int len, bc_destroy_event_t *out);
bool bc_parse_object_create_header(const u8 *payload, int len,
                                   bc_object_create_header_t *out);
bool bc_parse_chat_message(const u8 *payload, int len, bc_chat_event_t *out);

/* Parse CollisionEffect (0x15).
 * Extracts source/target object IDs, contact count, and collision force.
 * Skips contact point data (not needed for hull-only damage). */
bool bc_parse_collision_effect(const u8 *payload, int len,
                                bc_collision_event_t *out);

/* Parse ship blob header from ObjCreateTeam payload.
 * blob points to the serialized data AFTER [opcode][owner][team].
 * Extracts object_id, species_id, and initial position. */
bool bc_parse_ship_blob_header(const u8 *blob, int len,
                                bc_ship_blob_header_t *out);

/* Parse StateUpdate payload to extract position, orientation, speed.
 * Only parses dirty flags 0x01-0x10 (client-authoritative fields). */
bool bc_parse_state_update(const u8 *payload, int len,
                            bc_state_update_t *out);

/* SetPhaserLevel (0x12) -- 18-byte fixed message.
 * Carries phaser intensity toggle (LOW=0, MED=1, HIGH=2). */
typedef struct {
    i32  source_object_id;
    u8   phaser_level;       /* 0=LOW, 1=MED, 2=HIGH */
} bc_phaser_level_event_t;

/* Parse SetPhaserLevel (opcode 0x12).
 * Extracts source ship object ID and phaser level byte. */
bool bc_parse_set_phaser_level(const u8 *payload, int len,
                                bc_phaser_level_event_t *out);

/* HostMsg (0x13) -- self-destruct request (C->S only, 1 byte, no payload).
 * Sender identity comes from the transport envelope (peer_slot), not this
 * message body.  Returns true iff the single opcode byte is 0x13. */
bool bc_parse_host_msg(const u8 *payload, int len);

#endif /* OPENBC_GAME_EVENTS_H */
