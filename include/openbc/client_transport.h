#ifndef OPENBC_CLIENT_TRANSPORT_H
#define OPENBC_CLIENT_TRANSPORT_H

#include "openbc/types.h"

/*
 * Client-side transport packet builders.
 *
 * The server-side transport builders (transport.h) hardcode direction=0x01.
 * Clients use direction=0xFF (init) or 0x02+slot (game traffic).
 * These builders produce client-perspective packets for test harnesses.
 *
 * All functions return packet length on success, or -1 on error.
 */

/* Build a Connect packet.
 * Wire: [dir=0xFF][count=1][type=0x03][totalLen=8][flags=0x01][pad][pad][pad][ip:4]
 * The client sends this to initiate a connection. */
int bc_client_build_connect(u8 *out, int out_size, u32 local_ip);

/* Build a Keepalive with embedded player name (UTF-16LE).
 * Wire: [dir=0x02+slot][count=1][type=0x00][totalLen][flags=0x80]
 *   [pad:2][slot?:1][ip:4][name_utf16le...]
 * Sent during handshake so the server learns the player name. */
int bc_client_build_keepalive_name(u8 *out, int out_size, u8 slot,
                                    u32 local_ip, const char *name);

/* Build a reliable game message with client direction byte.
 * Wire: [dir=0x02+slot][count=1][0x32][totalLen][flags=0x80][seqHi][0x00][payload]
 * Uses the same envelope format as server-side reliable. */
int bc_client_build_reliable(u8 *out, int out_size,
                              u8 slot, const u8 *payload, int payload_len, u16 seq);

/* Build an unreliable game message with client direction byte.
 * Wire: [dir=0x02+slot][count=1][type=0x00][totalLen][payload] */
int bc_client_build_unreliable(u8 *out, int out_size,
                                u8 slot, const u8 *payload, int payload_len);

/* Build an ACK with client direction byte.
 * Wire: [dir=0x02+slot][count=1][0x01][counter][0x00][flags] */
int bc_client_build_ack(u8 *out, int out_size, u8 slot, u16 seq, u8 flags);

/* --- Wire-accurate checksum response builders --- */

/* File hash entry for checksum response building */
typedef struct {
    u32 name_hash;     /* StringHash of filename */
    u32 content_hash;  /* FileHash of file contents */
} bc_client_file_hash_t;

/* Subdirectory entry for recursive checksum responses (round 2) */
typedef struct {
    u32 name_hash;                        /* StringHash of subdir name */
    int file_count;
    bc_client_file_hash_t files[128];
} bc_client_subdir_hash_t;

/* Parsed checksum request from the server */
typedef struct {
    u8   round;         /* Round index (0-3) */
    char directory[64]; /* Directory path (e.g. "scripts/") */
    char filter[32];    /* File filter (e.g. "*.pyc", "App.pyc") */
    bool recursive;     /* True if recursive directory scan */
} bc_checksum_request_t;

/* Parse a checksum request payload (opcode 0x20) from the server.
 * Returns true on success, fills req. Works for all rounds (0-3, 0xFF). */
bool bc_client_parse_checksum_request(const u8 *payload, int payload_len,
                                       bc_checksum_request_t *req);

/* Build a wire-accurate checksum response for rounds 0-3 (non-recursive).
 * Wire: [0x21][round][ref_hash:u32][dir_hash:u32]
 *       [file_count:u16][{name_hash:u32,content_hash:u32}...]
 * Returns payload length, or -1 on error. */
int bc_client_build_checksum_resp(u8 *buf, int buf_size, u8 round,
                                   u32 ref_hash, u32 dir_hash,
                                   const bc_client_file_hash_t *files, int file_count);

/* Build a wire-accurate checksum response for round 2 (recursive).
 * Adds subdirectory entries after the top-level file list.
 * Returns payload length, or -1 on error. */
int bc_client_build_checksum_resp_recursive(
    u8 *buf, int buf_size, u8 round,
    u32 ref_hash, u32 dir_hash,
    const bc_client_file_hash_t *files, int file_count,
    const bc_client_subdir_hash_t *subdirs, int subdir_count);

/* Build an empty checksum response for the final round (0xFF).
 * Wire: [0x21][0xFF][ref_hash:u32][dir_hash:u32][file_count:u16=0][subdir_count:u16=0]
 * Used when Scripts/Multiplayer is empty or absent.
 * Returns payload length (14), or -1 on error. */
int bc_client_build_checksum_final(u8 *buf, int buf_size,
                                    u32 ref_hash, u32 dir_hash);

/* --- Directory scanning for checksum computation --- */

/* Result of scanning a directory for checksum files */
typedef struct {
    u32 dir_hash;      /* StringHash of directory name */
    int file_count;
    bc_client_file_hash_t files[256];
    int subdir_count;
    bc_client_subdir_hash_t subdirs[8];
} bc_client_dir_scan_t;

/* Scan a directory and compute hashes for matching files.
 * base_dir: root game directory (e.g. "C:\\Games\\BC\\")
 * sub_dir:  relative subdirectory (e.g. "scripts/")
 * filter:   file filter (e.g. "*.pyc" or "App.pyc")
 * recursive: scan subdirectories
 * Returns true on success (scan->file_count may be 0 if dir is empty). */
bool bc_client_scan_directory(const char *base_dir, const char *sub_dir,
                               const char *filter, bool recursive,
                               bc_client_dir_scan_t *scan);

#endif /* OPENBC_CLIENT_TRANSPORT_H */
