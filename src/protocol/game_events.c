#include "openbc/game_events.h"
#include "openbc/buffer.h"
#include "openbc/opcodes.h"
#include <string.h>

/*
 * Object ID -> player slot mapping.
 *
 * BC assigns object IDs starting at base 0x3FFFFFFF.
 * Each player slot owns 2^18 (262144) consecutive IDs.
 *   Slot 0: 0x3FFFFFFF .. 0x4003FFFE
 *   Slot 1: 0x4003FFFF .. 0x4007FFFE
 *   etc.
 */
int bc_object_id_to_slot(i32 object_id)
{
    i32 offset = object_id - 0x3FFFFFFF;
    if (offset < 0) return -1;

    int slot = (int)((u32)offset >> 18);
    if (slot >= BC_MAX_PLAYERS) return -1;

    return slot;
}

/*
 * TorpedoFire (opcode 0x19)
 *
 * Wire format:
 *   [0x19][object_id:i32][flags1:u8][flags2:u8][velocity:cv3]
 *   if flags2 bit 1: [target_id:i32][impact_point:cv4]
 *
 * Minimum: 1+4+1+1+3 = 10 bytes
 * With target: 10+4+5 = 19 bytes
 */
bool bc_parse_torpedo_fire(const u8 *payload, int len, bc_torpedo_event_t *out)
{
    memset(out, 0, sizeof(*out));

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)payload, (size_t)len);

    u8 opcode;
    if (!bc_buf_read_u8(&buf, &opcode)) return false;
    if (opcode != BC_OP_TORPEDO_FIRE) return false;

    if (!bc_buf_read_i32(&buf, &out->shooter_id)) return false;
    if (!bc_buf_read_u8(&buf, &out->subsys_index)) return false;
    if (!bc_buf_read_u8(&buf, &out->flags)) return false;
    if (!bc_buf_read_cv3(&buf, &out->vel_x, &out->vel_y, &out->vel_z)) return false;

    /* flags bit 1 = has_target */
    out->has_target = (out->flags & 0x02) != 0;
    if (out->has_target) {
        if (!bc_buf_read_i32(&buf, &out->target_id)) return false;
        if (!bc_buf_read_cv4(&buf, &out->impact_x, &out->impact_y, &out->impact_z))
            return false;
    }

    return true;
}

/*
 * BeamFire (opcode 0x1A)
 *
 * Wire format:
 *   [0x1A][object_id:i32][flags:u8][target_pos:cv3][more_flags:u8]
 *   if more_flags bit 0: [target_id:i32]
 *
 * Minimum: 1+4+1+3+1 = 10 bytes
 * With target: 10+4 = 14 bytes
 */
bool bc_parse_beam_fire(const u8 *payload, int len, bc_beam_event_t *out)
{
    memset(out, 0, sizeof(*out));

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)payload, (size_t)len);

    u8 opcode;
    if (!bc_buf_read_u8(&buf, &opcode)) return false;
    if (opcode != BC_OP_BEAM_FIRE) return false;

    if (!bc_buf_read_i32(&buf, &out->shooter_id)) return false;
    if (!bc_buf_read_u8(&buf, &out->flags)) return false;
    if (!bc_buf_read_cv3(&buf, &out->dir_x, &out->dir_y, &out->dir_z)) return false;
    if (!bc_buf_read_u8(&buf, &out->more_flags)) return false;

    /* more_flags bit 0 = has_target_id */
    out->has_target = (out->more_flags & 0x01) != 0;
    if (out->has_target) {
        if (!bc_buf_read_i32(&buf, &out->target_id)) return false;
    }

    return true;
}

/*
 * Explosion (opcode 0x29)
 *
 * Wire format:
 *   [0x29][object_id:i32][impact:cv4][damage:cf16][radius:cf16]
 *
 * Total: 1+4+5+2+2 = 14 bytes
 */
bool bc_parse_explosion(const u8 *payload, int len, bc_explosion_event_t *out)
{
    memset(out, 0, sizeof(*out));

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)payload, (size_t)len);

    u8 opcode;
    if (!bc_buf_read_u8(&buf, &opcode)) return false;
    if (opcode != BC_OP_EXPLOSION) return false;

    if (!bc_buf_read_i32(&buf, &out->object_id)) return false;
    if (!bc_buf_read_cv4(&buf, &out->impact_x, &out->impact_y, &out->impact_z))
        return false;
    if (!bc_buf_read_cf16(&buf, &out->damage)) return false;
    if (!bc_buf_read_cf16(&buf, &out->radius)) return false;

    return true;
}

/*
 * DestroyObject (opcode 0x14)
 *
 * Wire format:
 *   [0x14][object_id:i32]
 *
 * Total: 5 bytes
 */
bool bc_parse_destroy_obj(const u8 *payload, int len, bc_destroy_event_t *out)
{
    memset(out, 0, sizeof(*out));

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)payload, (size_t)len);

    u8 opcode;
    if (!bc_buf_read_u8(&buf, &opcode)) return false;
    if (opcode != BC_OP_DESTROY_OBJ) return false;

    if (!bc_buf_read_i32(&buf, &out->object_id)) return false;

    return true;
}

/*
 * ObjectCreate / ObjectCreateTeam (opcode 0x02 / 0x03)
 *
 * Wire format (header only -- we don't parse serialized object data):
 *   type_tag=2: [type_tag:u8][owner_slot:u8][serialized_data...]
 *   type_tag=3: [type_tag:u8][owner_slot:u8][team_id:u8][serialized_data...]
 */
bool bc_parse_object_create_header(const u8 *payload, int len,
                                   bc_object_create_header_t *out)
{
    memset(out, 0, sizeof(*out));

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)payload, (size_t)len);

    if (!bc_buf_read_u8(&buf, &out->type_tag)) return false;
    if (out->type_tag != 2 && out->type_tag != 3) return false;

    if (!bc_buf_read_u8(&buf, &out->owner_slot)) return false;

    if (out->type_tag == 3) {
        if (!bc_buf_read_u8(&buf, &out->team_id)) return false;
        out->has_team = true;
    } else {
        out->has_team = false;
    }

    return true;
}

/*
 * CollisionEffect (opcode 0x15)
 *
 * Wire format (from docs/collision-effect-wire-format.md):
 *   [0x15][class_id:i32][code:i32][source_obj:i32][target_obj:i32]
 *   [contact_count:u8][contacts:4*N][force:f32]
 *
 * Minimum: 1+4+4+4+4+1+4 = 22 bytes (0 contacts)
 */
bool bc_parse_collision_effect(const u8 *payload, int len,
                                bc_collision_event_t *out)
{
    memset(out, 0, sizeof(*out));
    if (len < 22) return false;

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)payload, (size_t)len);

    u8 opcode;
    i32 class_id, code;
    if (!bc_buf_read_u8(&buf, &opcode)) return false;
    if (opcode != 0x15) return false;

    /* Skip constant header fields (event class + code) */
    if (!bc_buf_read_i32(&buf, &class_id)) return false;
    if (!bc_buf_read_i32(&buf, &code)) return false;

    if (!bc_buf_read_i32(&buf, &out->source_object_id)) return false;
    if (!bc_buf_read_i32(&buf, &out->target_object_id)) return false;

    if (!bc_buf_read_u8(&buf, &out->contact_count)) return false;

    /* Skip contact_count * 4 bytes of contact point data */
    u8 skip;
    for (int i = 0; i < (int)out->contact_count * 4; i++) {
        if (!bc_buf_read_u8(&buf, &skip)) return false;
    }

    /* Last 4 bytes: collision force */
    if (!bc_buf_read_f32(&buf, &out->collision_force)) return false;

    return true;
}

/*
 * Ship blob header (inside ObjCreateTeam)
 *
 * Wire format (from packet captures of real BC 1.1 client):
 *   [prefix:4 bytes][object_id:i32][species_id:u8][pos:3xf32]...
 *
 * The 4-byte prefix (observed: 08 80 00 00) purpose is unknown.
 * species_id is a u8 (observed values 0x01, 0x05 matching registry indices 1-16).
 *
 * Minimum: 4+4+1+12 = 21 bytes
 */
bool bc_parse_ship_blob_header(const u8 *blob, int len,
                                bc_ship_blob_header_t *out)
{
    memset(out, 0, sizeof(*out));

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)blob, (size_t)len);

    /* Skip 4-byte prefix */
    u8 skip;
    for (int i = 0; i < 4; i++) {
        if (!bc_buf_read_u8(&buf, &skip)) return false;
    }

    if (!bc_buf_read_i32(&buf, &out->object_id)) return false;

    /* species_id is a single byte on the wire */
    u8 species_u8;
    if (!bc_buf_read_u8(&buf, &species_u8)) return false;
    out->species_id = (u16)species_u8;

    if (!bc_buf_read_f32(&buf, &out->pos_x)) return false;
    if (!bc_buf_read_f32(&buf, &out->pos_y)) return false;
    if (!bc_buf_read_f32(&buf, &out->pos_z)) return false;

    return true;
}

/*
 * StateUpdate (opcode 0x1C)
 *
 * Wire format:
 *   [0x1C][object_id:i32][game_time:f32][dirty:u8][field_data...]
 *
 * Field data depends on dirty flags:
 *   0x01: [pos_x:f32][pos_y:f32][pos_z:f32]
 *   0x02: [delta:cv4] (compressed vector4)
 *   0x04: [fwd:cv3] (compressed vector3)
 *   0x08: [up:cv3]
 *   0x10: [speed:cf16]
 *   0x20: [startIdx:u8][health_bytes...]
 *   0x40: [cloak:u8]
 *   0x80: [weapon data...]
 */
bool bc_parse_state_update(const u8 *payload, int len,
                            bc_state_update_t *out)
{
    memset(out, 0, sizeof(*out));

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)payload, (size_t)len);

    u8 opcode;
    if (!bc_buf_read_u8(&buf, &opcode)) return false;
    if (opcode != BC_OP_STATE_UPDATE) return false;

    if (!bc_buf_read_i32(&buf, &out->object_id)) return false;
    if (!bc_buf_read_f32(&buf, &out->game_time)) return false;
    if (!bc_buf_read_u8(&buf, &out->dirty)) return false;

    /* Parse fields in order of dirty flag bits */
    if (out->dirty & 0x01) {
        if (!bc_buf_read_f32(&buf, &out->pos_x)) return false;
        if (!bc_buf_read_f32(&buf, &out->pos_y)) return false;
        if (!bc_buf_read_f32(&buf, &out->pos_z)) return false;
    }
    if (out->dirty & 0x02) {
        /* Delta position (cv4) -- skip 5 bytes, we only track absolute */
        f32 dx, dy, dz;
        if (!bc_buf_read_cv4(&buf, &dx, &dy, &dz)) return false;
    }
    if (out->dirty & 0x04) {
        if (!bc_buf_read_cv3(&buf, &out->fwd_x, &out->fwd_y, &out->fwd_z))
            return false;
    }
    if (out->dirty & 0x08) {
        if (!bc_buf_read_cv3(&buf, &out->up_x, &out->up_y, &out->up_z))
            return false;
    }
    if (out->dirty & 0x10) {
        if (!bc_buf_read_cf16(&buf, &out->speed)) return false;
    }
    /* Flags 0x20, 0x40, 0x80 are server-authoritative or not needed here */

    return true;
}

/*
 * Chat / Team Chat (opcode 0x2C / 0x2D)
 *
 * Wire format:
 *   [opcode:u8][sender_slot:u8][pad:3bytes][str_len:u16][ascii_text:N]
 *
 * Minimum: 7 bytes (empty message)
 */
bool bc_parse_chat_message(const u8 *payload, int len, bc_chat_event_t *out)
{
    memset(out, 0, sizeof(*out));

    bc_buffer_t buf;
    bc_buf_init(&buf, (u8 *)payload, (size_t)len);

    u8 opcode;
    if (!bc_buf_read_u8(&buf, &opcode)) return false;
    if (opcode != BC_MSG_CHAT && opcode != BC_MSG_TEAM_CHAT) return false;

    if (!bc_buf_read_u8(&buf, &out->sender_slot)) return false;

    /* Skip 3 padding bytes */
    u8 pad[3];
    if (!bc_buf_read_bytes(&buf, pad, 3)) return false;

    u16 str_len;
    if (!bc_buf_read_u16(&buf, &str_len)) return false;

    /* Clamp to buffer size */
    int copy_len = (int)str_len;
    if (copy_len > 255) copy_len = 255;
    if ((size_t)copy_len > bc_buf_remaining(&buf))
        copy_len = (int)bc_buf_remaining(&buf);

    if (copy_len > 0) {
        if (!bc_buf_read_bytes(&buf, (u8 *)out->message, (size_t)copy_len))
            return false;
    }
    out->message[copy_len] = '\0';
    out->message_len = copy_len;

    return true;
}
