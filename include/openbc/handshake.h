#ifndef OPENBC_HANDSHAKE_H
#define OPENBC_HANDSHAKE_H

#include "openbc/types.h"
#include "openbc/buffer.h"
#include "openbc/manifest.h"

/*
 * Connection handshake -- checksum exchange and Settings/GameInit delivery.
 *
 * After a client connects, the server runs 4 checksum rounds:
 *   Round 0: scripts/         App.pyc          non-recursive
 *   Round 1: scripts/         Autoexec.pyc     non-recursive
 *   Round 2: scripts/ships/   *.pyc            recursive
 *   Round 3: scripts/mainmenu/ *.pyc           non-recursive
 *
 * After all 4 pass, the server sends Settings (0x00) then GameInit (0x01).
 *
 * Reference: docs/phase1-verified-protocol.md Section 4 + 7
 */

#define BC_CHECKSUM_ROUNDS  4

/* Build a checksum request payload (opcode 0x20) for the given round.
 * Writes into buf. Returns bytes written, or -1 on error. */
int bc_checksum_request_build(u8 *buf, int buf_size, int round);

/* Build the final checksum request (round 0xFF).
 * This is the 5th round observed in traces, sent after rounds 0-3 pass.
 * Stock dedi sends a full request for Scripts/Multiplayer, *.pyc (recursive).
 * Returns bytes written, or -1 on error. */
int bc_checksum_request_final_build(u8 *buf, int buf_size);

/* Build the Settings payload (opcode 0x00).
 * Parameters:
 *   game_time       - current game clock value
 *   collision_dmg   - collision damage enabled
 *   friendly_fire   - friendly fire enabled
 *   player_slot     - assigned player slot (0-5)
 *   map_name        - mission module path (e.g. "Multiplayer.Episode.Mission1.Mission1")
 * Returns bytes written, or -1 on error. */
int bc_settings_build(u8 *buf, int buf_size,
                      f32 game_time,
                      bool collision_dmg,
                      bool friendly_fire,
                      u8 player_slot,
                      const char *map_name);

/* Build the GameInit payload (opcode 0x01).
 * Returns bytes written (always 1), or -1 on error. */
int bc_gameinit_build(u8 *buf, int buf_size);

/* Build the MissionInit payload (opcode 0x35).
 * Sent to each client after NewPlayerInGame to tell them which star system
 * to load, along with player limit and match rules.
 *   system_index  - star system (1-9, see SpeciesToSystem.py)
 *   player_limit  - max players in the match
 *   time_limit    - time limit in minutes (-1 = no limit)
 *   frag_limit    - frag/kill limit (-1 = no limit)
 * Returns bytes written, or -1 on error. */
int bc_mission_init_build(u8 *buf, int buf_size,
                          int system_index, int player_limit,
                          int time_limit, int frag_limit);

/* --- UICollisionSetting (opcode 0x16) --- */

/* Build a UICollisionSetting payload (opcode 0x16).
 * Sends the collision damage enabled flag to the client UI.
 * Returns bytes written, or -1 on error. */
int bc_ui_collision_build(u8 *buf, int buf_size, bool collision_enabled);

/* --- BootPlayer and disconnect messages --- */

/* BootPlayer reasons (opcode 0x04) */
#define BC_BOOT_GENERIC     0   /* Generic kick */
#define BC_BOOT_VERSION     1   /* Version mismatch */
#define BC_BOOT_SERVER_FULL 2   /* Server is full */
#define BC_BOOT_BANNED      3   /* Player is banned */
#define BC_BOOT_CHECKSUM    4   /* Checksum validation failed */

/* Build a BootPlayer payload (opcode 0x04).
 * reason: one of BC_BOOT_* constants.
 * Returns bytes written, or -1 on error. */
int bc_bootplayer_build(u8 *buf, int buf_size, u8 reason);

/* Build a DeletePlayerUI payload (opcode 0x17).
 * Returns bytes written (always 1), or -1 on error. */
int bc_delete_player_ui_build(u8 *buf, int buf_size);

/* Build a DeletePlayerAnim payload (opcode 0x18).
 * Returns bytes written (always 1), or -1 on error. */
int bc_delete_player_anim_build(u8 *buf, int buf_size);

/* --- Checksum response parsing and validation --- */

/* Result of checksum validation */
typedef enum {
    CHECKSUM_OK,           /* All hashes match */
    CHECKSUM_EMPTY_DIR,    /* Client reported empty directory (0xFF index) */
    CHECKSUM_DIR_MISMATCH, /* Directory name hash mismatch */
    CHECKSUM_FILE_MISSING, /* File in manifest not found in response */
    CHECKSUM_FILE_MISMATCH,/* File content hash mismatch */
    CHECKSUM_PARSE_ERROR,  /* Malformed response */
} bc_checksum_result_t;

/* Parsed checksum file entry from client response */
typedef struct {
    u32 name_hash;
    u32 content_hash;
} bc_checksum_file_t;

/* Parsed checksum response */
#define BC_CHECKSUM_MAX_RESP_FILES  256
#define BC_CHECKSUM_MAX_RESP_SUBDIRS 8
#define BC_CHECKSUM_MAX_SUB_FILES   128

typedef struct {
    int file_count;
    bc_checksum_file_t files[BC_CHECKSUM_MAX_SUB_FILES];
} bc_checksum_subdir_resp_t;

typedef struct {
    u8   round_index;      /* 0-3, or 0xFF for empty */
    u32  ref_hash;         /* Reference hash from client */
    u32  dir_hash;         /* Directory name hash */
    bool empty;            /* True if client reported empty dir (index=0xFF) */
    int  file_count;
    bc_checksum_file_t files[BC_CHECKSUM_MAX_RESP_FILES];
    int  subdir_count;
    struct {
        u32 name_hash;
        bc_checksum_subdir_resp_t data;
    } subdirs[BC_CHECKSUM_MAX_RESP_SUBDIRS];
} bc_checksum_resp_t;

/* Parse a checksum response payload (opcode 0x21) into a structured form.
 * Returns true on success. */
bool bc_checksum_response_parse(bc_checksum_resp_t *resp,
                                const u8 *payload, int payload_len);

/* Validate a parsed checksum response against a manifest directory.
 * Returns CHECKSUM_OK if all hashes match, otherwise the first error. */
bc_checksum_result_t bc_checksum_response_validate(
    const bc_checksum_resp_t *resp,
    const bc_manifest_dir_t *manifest_dir);

/* Return a human-readable name for a checksum result. */
const char *bc_checksum_result_name(bc_checksum_result_t result);

#endif /* OPENBC_HANDSHAKE_H */
