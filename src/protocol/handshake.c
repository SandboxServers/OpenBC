#include "openbc/handshake.h"
#include "openbc/opcodes.h"
#include "openbc/log.h"
#include <string.h>
#include <stdio.h>

/* Checksum round definitions (from verified protocol specification).
 * Rounds 2-3 have NO trailing slash on the directory name -- verified
 * against stock dedi packet traces (dir bytes are "scripts/ships" not
 * "scripts/ships/").  The base "scripts/" in rounds 0-1 does keep its
 * trailing slash (also verified in traces). */
static const struct {
    const char *directory;
    const char *filter;
    bool        recursive;
} checksum_rounds[BC_CHECKSUM_ROUNDS] = {
    { "scripts/",        "App.pyc",      false },
    { "scripts/",        "Autoexec.pyc", false },
    { "scripts/ships",   "*.pyc",        true  },
    { "scripts/mainmenu","*.pyc",        false },
};

int bc_checksum_request_build(u8 *buf, int buf_size, int round)
{
    if (round < 0 || round >= BC_CHECKSUM_ROUNDS) return -1;

    const char *dir = checksum_rounds[round].directory;
    const char *filter = checksum_rounds[round].filter;
    bool recursive = checksum_rounds[round].recursive;

    u16 dir_len = (u16)strlen(dir);
    u16 filter_len = (u16)strlen(filter);

    /* Use bc_buffer_t for proper bit-packing of the recursive flag */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    /* Wire format: [opcode:u8][index:u8][dirLen:u16][dir:bytes][filterLen:u16][filter:bytes][recursive:bit] */
    if (!bc_buf_write_u8(&b, BC_OP_CHECKSUM_REQ)) return -1;
    if (!bc_buf_write_u8(&b, (u8)round)) return -1;
    if (!bc_buf_write_u16(&b, dir_len)) return -1;
    if (!bc_buf_write_bytes(&b, (const u8 *)dir, dir_len)) return -1;
    if (!bc_buf_write_u16(&b, filter_len)) return -1;
    if (!bc_buf_write_bytes(&b, (const u8 *)filter, filter_len)) return -1;
    if (!bc_buf_write_bit(&b, recursive)) return -1;

    return (int)b.pos;
}

int bc_checksum_request_final_build(u8 *buf, int buf_size)
{
    /* 5th checksum round (0xFF): stock dedi sends a full checksum request
     * for Scripts/Multiplayer, filter *.pyc, recursive=true.  Verified
     * from trace: 20 FF 13 00 "Scripts/Multiplayer" 05 00 "*.pyc" 21.
     * Note capital "S" in "Scripts" (differs from rounds 0-3). */
    const char *dir = "Scripts/Multiplayer";
    const char *filter = "*.pyc";
    u16 dir_len = (u16)strlen(dir);
    u16 filter_len = (u16)strlen(filter);

    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_CHECKSUM_REQ)) return -1;
    if (!bc_buf_write_u8(&b, 0xFF)) return -1;
    if (!bc_buf_write_u16(&b, dir_len)) return -1;
    if (!bc_buf_write_bytes(&b, (const u8 *)dir, dir_len)) return -1;
    if (!bc_buf_write_u16(&b, filter_len)) return -1;
    if (!bc_buf_write_bytes(&b, (const u8 *)filter, filter_len)) return -1;
    if (!bc_buf_write_bit(&b, true)) return -1;  /* recursive */

    return (int)b.pos;
}

int bc_settings_build(u8 *buf, int buf_size,
                      f32 game_time,
                      bool collision_dmg,
                      bool friendly_fire,
                      u8 player_slot,
                      const char *map_name)
{
    u16 map_len = (u16)strlen(map_name);

    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    /* Wire format from checksum-handshake-protocol.md:
     * [opcode:u8][gameTime:f32][collision:bit][friendly:bit][slot:u8]
     * [mapLen:u16][mapName:bytes][checksumFlag:bit] */
    if (!bc_buf_write_u8(&b, BC_OP_SETTINGS)) return -1;
    if (!bc_buf_write_f32(&b, game_time)) return -1;
    if (!bc_buf_write_bit(&b, collision_dmg)) return -1;
    if (!bc_buf_write_bit(&b, friendly_fire)) return -1;
    if (!bc_buf_write_u8(&b, player_slot)) return -1;
    if (!bc_buf_write_u16(&b, map_len)) return -1;
    if (!bc_buf_write_bytes(&b, (const u8 *)map_name, map_len)) return -1;
    if (!bc_buf_write_bit(&b, false)) return -1;  /* checksumFlag = 0 (no corrections) */

    return (int)b.pos;
}

int bc_gameinit_build(u8 *buf, int buf_size)
{
    if (buf_size < 1) return -1;
    buf[0] = BC_OP_GAME_INIT;
    return 1;
}

int bc_mission_init_build(u8 *buf, int buf_size,
                          int system_index, int current_player_count,
                          int time_limit, i32 end_time, int frag_limit)
{
    /* Wire format (from Mission1.py InitNetwork):
     * [opcode:u8][current_player_count:u8][system_index:u8]
     * [time_limit:u8]  -- 255 = no limit
     *   [end_time:i32]  -- only present if time_limit != 255
     * [frag_limit:u8]  -- 255 = no limit */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_MSG_MISSION_INIT)) return -1;
    if (!bc_buf_write_u8(&b, (u8)(current_player_count & 0xFF))) return -1;
    if (!bc_buf_write_u8(&b, (u8)(system_index & 0xFF))) return -1;

    if (time_limit < 0) {
        if (!bc_buf_write_u8(&b, 0xFF)) return -1;
    } else {
        if (!bc_buf_write_u8(&b, (u8)(time_limit & 0xFF))) return -1;
        if (!bc_buf_write_i32(&b, end_time)) return -1;
    }

    if (frag_limit < 0) {
        if (!bc_buf_write_u8(&b, 0xFF)) return -1;
    } else {
        if (!bc_buf_write_u8(&b, (u8)(frag_limit & 0xFF))) return -1;
    }

    return (int)b.pos;
}

int bc_ui_collision_build(u8 *buf, int buf_size, bool collision_enabled)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_UI_SETTINGS)) return -1;
    if (!bc_buf_write_bit(&b, collision_enabled)) return -1;

    return (int)b.pos;
}

int bc_bootplayer_build(u8 *buf, int buf_size, u8 reason)
{
    if (buf_size < 2) return -1;
    buf[0] = BC_OP_BOOT_PLAYER;
    buf[1] = reason;
    return 2;
}

int bc_delete_player_ui_build(u8 *buf, int buf_size, u8 game_slot)
{
    /* Wire format from trace (18 bytes):
     *   [0x17][connection_data:17]
     * Observed at join time: 17 66 08 00 00 F1 00 80 00 00 00 00 00 91 07 00 00 02
     * The exact meaning of bytes 1-16 is not fully decoded, but the last byte
     * appears to be the game slot.  We emit a minimal but correct packet:
     *   [0x17][game_slot:u8] */
    if (buf_size < 2) return -1;
    buf[0] = BC_OP_DELETE_PLAYER_UI;
    buf[1] = game_slot;
    return 2;
}

int bc_delete_player_anim_build(u8 *buf, int buf_size,
                                 const char *player_name)
{
    /* Wire format: [0x18][name_len:u16][name:bytes]
     * Triggers a "Player X has left" floating notification.
     * The exact animation fields after the name are unknown (no trace
     * captures exist), so we send name only -- the client may accept
     * just the name length for its template. */
    int name_len = player_name ? (int)strlen(player_name) : 0;
    int total = 3 + name_len;  /* opcode + u16 len + name bytes */
    if (total > buf_size) return -1;

    buf[0] = BC_OP_DELETE_PLAYER_ANIM;
    buf[1] = (u8)(name_len & 0xFF);
    buf[2] = (u8)((name_len >> 8) & 0xFF);
    if (name_len > 0)
        memcpy(buf + 3, player_name, (size_t)name_len);
    return total;
}

/* --- Checksum response parsing --- */

/* Parse a recursive file tree from a buffer.
 * Wire format (self-describing, same at every nesting level):
 *   [file_count:u16 LE][files × {name_hash:u32, content_hash:u32}]
 *   [subdir_count:u8]
 *   [name_hash_0:u32][name_hash_1:u32]...[name_hash_N:u32]   (all names first)
 *   [tree_0][tree_1]...[tree_N]                                (then all trees)
 *
 * If files/file_count are non-NULL, stores file entries (up to max_files).
 * If resp is non-NULL, stores first-level subdirectory data in resp->subdirs.
 * Nested subdirectories (level 2+) are consumed without storing. */
static bool parse_file_tree(bc_buffer_t *b,
                            bc_checksum_file_t *files, int *file_count, int max_files,
                            bc_checksum_resp_t *resp)
{
    u16 fc;
    if (!bc_buf_read_u16(b, &fc)) {
        LOG_DEBUG("checksum", "tree: failed to read file_count at pos=%d",
                  (int)b->pos);
        return false;
    }
    if (files && (int)fc > max_files) {
        LOG_DEBUG("checksum", "tree: file_count=%d exceeds max=%d at pos=%d",
                  (int)fc, max_files, (int)b->pos);
        return false;
    }
    if (file_count) *file_count = (int)fc;

    for (int i = 0; i < (int)fc; i++) {
        u32 nh, ch;
        if (!bc_buf_read_u32(b, &nh)) return false;
        if (!bc_buf_read_u32(b, &ch)) return false;
        if (files && i < max_files) {
            files[i].name_hash = nh;
            files[i].content_hash = ch;
        }
    }

    /* subdir_count is ALWAYS present (u8, NOT u16).
     * The tree format is self-describing: subdir_count=0 means leaf node. */
    u8 sc;
    if (!bc_buf_read_u8(b, &sc)) {
        LOG_DEBUG("checksum", "tree: failed to read subdir_count(u8) at pos=%d",
                  (int)b->pos);
        return false;
    }

    if (sc == 0) return true;

    if (resp) {
        resp->subdir_count = (int)sc > BC_CHECKSUM_MAX_RESP_SUBDIRS
                           ? BC_CHECKSUM_MAX_RESP_SUBDIRS : (int)sc;
    }

    /* Read ALL name hashes first, store directly into resp if available */
    for (int i = 0; i < (int)sc; i++) {
        u32 nh;
        if (!bc_buf_read_u32(b, &nh)) return false;
        if (resp && i < BC_CHECKSUM_MAX_RESP_SUBDIRS) {
            resp->subdirs[i].name_hash = nh;
        }
    }

    /* Then parse each tree sequentially */
    for (int i = 0; i < (int)sc; i++) {
        if (resp && i < BC_CHECKSUM_MAX_RESP_SUBDIRS) {
            if (!parse_file_tree(b,
                    resp->subdirs[i].data.files,
                    &resp->subdirs[i].data.file_count,
                    BC_CHECKSUM_MAX_SUB_FILES,
                    NULL))
                return false;
        } else {
            /* Consume bytes without storing */
            if (!parse_file_tree(b, NULL, NULL, 0, NULL))
                return false;
        }
    }

    return true;
}

bool bc_checksum_response_parse(bc_checksum_resp_t *resp,
                                const u8 *payload, int payload_len)
{
    memset(resp, 0, sizeof(*resp));

    if (payload_len < 2) return false;

    bc_buffer_t b;
    bc_buf_init(&b, (u8 *)payload, (size_t)payload_len);

    u8 opcode;
    if (!bc_buf_read_u8(&b, &opcode)) return false;
    if (opcode != BC_OP_CHECKSUM_RESP) {
        LOG_DEBUG("checksum", "parse: bad opcode 0x%02X (expected 0x21)", opcode);
        return false;
    }

    u8 index;
    if (!bc_buf_read_u8(&b, &index)) return false;
    resp->round_index = index;

    /* Response format (verified from live client trace data):
     *   Round 0:    [0x21][round:u8][ref_hash:u32][dir_hash:u32][file_tree...]
     *   Other rounds: [0x21][round:u8][dir_hash:u32][file_tree...]
     *
     * Only round 0 includes ref_hash (StringHash of gamever "60" = 0x7E0CE243).
     * Rounds 1-3 and 0xFF skip ref_hash and start directly with dir_hash.
     * The checksum-handshake-protocol.md incorrectly generalized round 0's
     * format to all rounds. */
    u32 rh = 0, dh;
    if (index == 0) {
        if (!bc_buf_read_u32(&b, &rh)) {
            LOG_DEBUG("checksum", "parse: round=%d failed to read ref_hash", index);
            return false;
        }
    }
    if (!bc_buf_read_u32(&b, &dh)) {
        LOG_DEBUG("checksum", "parse: round=%d failed to read dir_hash", index);
        return false;
    }
    resp->ref_hash = rh;
    resp->dir_hash = dh;

    LOG_DEBUG("checksum", "parse: round=0x%02X ref=0x%08X dir=0x%08X "
              "remaining=%d", index, rh, dh, (int)(b.capacity - b.pos));

    /* File tree format is self-describing (subdir_count:u8 always present).
     * No need for a recursive flag — the parser recurses when subdir_count > 0. */
    if (!parse_file_tree(&b,
            resp->files, &resp->file_count, BC_CHECKSUM_MAX_RESP_FILES,
            resp)) {
        LOG_DEBUG("checksum", "parse: round=%d file_tree parse failed "
                  "at pos=%d", index, (int)b.pos);
        return false;
    }

    return true;
}

bc_checksum_result_t bc_checksum_response_validate(
    const bc_checksum_resp_t *resp,
    const bc_manifest_dir_t *manifest_dir)
{
    if (resp->empty) {
        /* Empty dir: fail if manifest expects files */
        if (manifest_dir->file_count > 0)
            return CHECKSUM_FILE_MISSING;
        return CHECKSUM_OK;
    }

    /* Verify directory name hash matches */
    if (resp->dir_hash != manifest_dir->dir_name_hash)
        return CHECKSUM_DIR_MISMATCH;

    /* Validate each file in the response against the manifest */
    for (int i = 0; i < resp->file_count; i++) {
        const bc_manifest_file_t *mf =
            bc_manifest_find_file(manifest_dir, resp->files[i].name_hash);
        if (!mf) {
            /* Extra file not in manifest -- that's OK (could be a mod file) */
            continue;
        }
        if (resp->files[i].content_hash != mf->content_hash)
            return CHECKSUM_FILE_MISMATCH;
    }

    /* Check all manifest files were present in the response */
    for (int i = 0; i < manifest_dir->file_count; i++) {
        bool found = false;
        for (int j = 0; j < resp->file_count; j++) {
            if (resp->files[j].name_hash == manifest_dir->files[i].name_hash) {
                found = true;
                break;
            }
        }
        if (!found) return CHECKSUM_FILE_MISSING;
    }

    /* Validate subdirectory files */
    for (int s = 0; s < resp->subdir_count; s++) {
        const bc_manifest_subdir_t *ms =
            bc_manifest_find_subdir(manifest_dir, resp->subdirs[s].name_hash);
        if (!ms) continue; /* Extra subdir not in manifest -- OK */

        const bc_checksum_subdir_resp_t *rs = &resp->subdirs[s].data;

        /* Validate each file in this subdir */
        for (int i = 0; i < rs->file_count; i++) {
            const bc_manifest_file_t *mf =
                bc_manifest_find_subdir_file(ms, rs->files[i].name_hash);
            if (!mf) continue;
            if (rs->files[i].content_hash != mf->content_hash)
                return CHECKSUM_FILE_MISMATCH;
        }

        /* Check all manifest subdir files were present */
        for (int i = 0; i < ms->file_count; i++) {
            bool found = false;
            for (int j = 0; j < rs->file_count; j++) {
                if (rs->files[j].name_hash == ms->files[i].name_hash) {
                    found = true;
                    break;
                }
            }
            if (!found) return CHECKSUM_FILE_MISSING;
        }
    }

    /* Check all manifest subdirs were present in response */
    for (int s = 0; s < manifest_dir->subdir_count; s++) {
        bool found = false;
        for (int j = 0; j < resp->subdir_count; j++) {
            if (resp->subdirs[j].name_hash == manifest_dir->subdirs[s].name_hash) {
                found = true;
                break;
            }
        }
        if (!found) return CHECKSUM_FILE_MISSING;
    }

    return CHECKSUM_OK;
}

const char *bc_checksum_result_name(bc_checksum_result_t result)
{
    switch (result) {
    case CHECKSUM_OK:            return "OK";
    case CHECKSUM_EMPTY_DIR:     return "empty directory";
    case CHECKSUM_DIR_MISMATCH:  return "directory hash mismatch";
    case CHECKSUM_FILE_MISSING:  return "required file missing";
    case CHECKSUM_FILE_MISMATCH: return "file content hash mismatch";
    case CHECKSUM_PARSE_ERROR:   return "parse error";
    }
    return "unknown";
}
