#include "openbc/client_transport.h"
#include "openbc/transport.h"
#include "openbc/opcodes.h"
#include "openbc/buffer.h"
#include "openbc/checksum.h"
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#  include <windows.h>
#  define bc_stricmp _stricmp
#else
#  include <strings.h>
#  include <dirent.h>
#  define bc_stricmp strcasecmp
#endif

int bc_client_build_connect(u8 *out, int out_size, u32 local_ip)
{
    /* Wire: [dir=0xFF][count=1][type=0x03][totalLen=8][flags=0x01][pad:3][ip:4] */
    if (out_size < 10) return -1;

    out[0] = BC_DIR_INIT;     /* 0xFF */
    out[1] = 1;               /* 1 message */
    out[2] = BC_TRANSPORT_CONNECT; /* 0x03 */
    out[3] = 8;               /* totalLen */
    out[4] = 0x01;            /* flags */
    out[5] = 0x00;            /* pad */
    out[6] = 0x00;            /* pad */
    out[7] = 0x00;            /* pad */
    out[8] = (u8)(local_ip & 0xFF);
    out[9] = (u8)((local_ip >> 8) & 0xFF);

    return 10;
}

int bc_client_build_keepalive_name(u8 *out, int out_size, u8 slot,
                                    u32 local_ip, const char *name)
{
    /* Wire: [dir=0x02+slot][count=1][type=0x00][totalLen][flags=0x80]
     *   [pad:2][slot:1][ip:4][name_utf16le...] */
    int name_len = (int)strlen(name);
    int name_bytes = name_len * 2 + 2; /* UTF-16LE + null terminator */
    int payload_len = 1 + 2 + 1 + 4 + name_bytes; /* flags+pad+slot+ip+name */
    int msg_len = 2 + payload_len; /* type + totalLen + payload */
    int pkt_len = 2 + msg_len;     /* dir + count + msg */

    if (pkt_len > out_size || msg_len > 255) return -1;

    out[0] = BC_DIR_CLIENT + slot;
    out[1] = 1;
    out[2] = BC_TRANSPORT_KEEPALIVE;
    out[3] = (u8)msg_len;
    out[4] = 0x80;            /* flags */
    out[5] = 0x00;            /* pad */
    out[6] = 0x00;            /* pad */
    out[7] = slot;
    out[8] = (u8)(local_ip & 0xFF);
    out[9] = (u8)((local_ip >> 8) & 0xFF);
    out[10] = (u8)((local_ip >> 16) & 0xFF);
    out[11] = (u8)((local_ip >> 24) & 0xFF);

    /* UTF-16LE encode name */
    int pos = 12;
    for (int i = 0; i < name_len; i++) {
        out[pos++] = (u8)name[i]; /* low byte */
        out[pos++] = 0x00;        /* high byte (ASCII) */
    }
    /* Null terminator */
    out[pos++] = 0x00;
    out[pos++] = 0x00;

    return pkt_len;
}

int bc_client_build_reliable(u8 *out, int out_size,
                              u8 slot, const u8 *payload, int payload_len, u16 seq)
{
    /* Wire: [dir=0x02+slot][count=1][0x32][totalLen][flags=0x80][seqHi][0x00][payload] */
    int total_msg_len = 5 + payload_len;
    int pkt_len = 2 + total_msg_len;

    if (pkt_len > out_size || total_msg_len > 255) return -1;

    out[0] = BC_DIR_CLIENT + slot;
    out[1] = 1;
    out[2] = BC_TRANSPORT_RELIABLE;
    out[3] = (u8)total_msg_len;
    out[4] = 0x80;
    out[5] = (u8)(seq & 0xFF);
    out[6] = 0;
    memcpy(out + 7, payload, (size_t)payload_len);

    return pkt_len;
}

int bc_client_build_unreliable(u8 *out, int out_size,
                                u8 slot, const u8 *payload, int payload_len)
{
    /* Wire: [dir=0x02+slot][count=1][0x32][totalLen][0x00][payload]
     * Real BC clients send unreliable data as type 0x32 with flags=0x00
     * (no seq bytes), not as bare type 0x00 keepalive. */
    int total_msg_len = 3 + payload_len;  /* type + totalLen + flags + payload */
    int pkt_len = 2 + total_msg_len;

    if (pkt_len > out_size || total_msg_len > 255) return -1;

    out[0] = BC_DIR_CLIENT + slot;
    out[1] = 1;
    out[2] = BC_TRANSPORT_RELIABLE;  /* 0x32 */
    out[3] = (u8)total_msg_len;
    out[4] = 0x00;  /* flags = unreliable */
    memcpy(out + 5, payload, (size_t)payload_len);

    return pkt_len;
}

int bc_client_build_ack(u8 *out, int out_size, u8 slot, u16 seq, u8 flags)
{
    /* Wire: [dir=0x02+slot][count=1][0x01][counter][0x00][flags] */
    if (out_size < 6) return -1;

    out[0] = BC_DIR_CLIENT + slot;
    out[1] = 1;
    out[2] = BC_TRANSPORT_ACK;
    out[3] = (u8)(seq >> 8); /* counter = high byte of wire seq */
    out[4] = 0x00;
    out[5] = flags;

    return 6;
}

/* --- Wire-accurate checksum response builders --- */

bool bc_client_parse_checksum_request(const u8 *payload, int payload_len,
                                       bc_checksum_request_t *req)
{
    memset(req, 0, sizeof(*req));
    if (payload_len < 2) return false;

    bc_buffer_t b;
    bc_buf_init(&b, (u8 *)payload, (size_t)payload_len);

    u8 opcode;
    if (!bc_buf_read_u8(&b, &opcode)) return false;
    if (opcode != BC_OP_CHECKSUM_REQ) return false;

    u8 round;
    if (!bc_buf_read_u8(&b, &round)) return false;
    req->round = round;

    /* Round 0xFF uses the same request format as rounds 0-3 */

    /* Read directory path */
    u16 dir_len;
    if (!bc_buf_read_u16(&b, &dir_len)) return false;
    if (dir_len >= sizeof(req->directory)) return false;
    if (!bc_buf_read_bytes(&b, (u8 *)req->directory, dir_len)) return false;
    req->directory[dir_len] = '\0';

    /* Read file filter */
    u16 filter_len;
    if (!bc_buf_read_u16(&b, &filter_len)) return false;
    if (filter_len >= sizeof(req->filter)) return false;
    if (!bc_buf_read_bytes(&b, (u8 *)req->filter, filter_len)) return false;
    req->filter[filter_len] = '\0';

    /* Read recursive flag (bit-packed) */
    bool recursive;
    if (!bc_buf_read_bit(&b, &recursive)) return false;
    req->recursive = recursive;

    return true;
}

int bc_client_build_checksum_resp(u8 *buf, int buf_size, u8 round,
                                   u32 ref_hash, u32 dir_hash,
                                   const bc_client_file_hash_t *files, int file_count)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_CHECKSUM_RESP)) return -1;
    if (!bc_buf_write_u8(&b, round)) return -1;
    /* Only round 0 includes ref_hash (StringHash of gamever "60").
     * Other rounds start directly with dir_hash. */
    if (round == 0) {
        if (!bc_buf_write_u32(&b, ref_hash)) return -1;
    }
    if (!bc_buf_write_u32(&b, dir_hash)) return -1;

    /* File tree: [file_count:u16][files × 8][subdir_count:u8=0] */
    if (!bc_buf_write_u16(&b, (u16)file_count)) return -1;
    for (int i = 0; i < file_count; i++) {
        if (!bc_buf_write_u32(&b, files[i].name_hash)) return -1;
        if (!bc_buf_write_u32(&b, files[i].content_hash)) return -1;
    }
    if (!bc_buf_write_u8(&b, 0)) return -1;  /* subdir_count = 0 */

    return (int)b.pos;
}

int bc_client_build_checksum_resp_recursive(
    u8 *buf, int buf_size, u8 round,
    u32 ref_hash, u32 dir_hash,
    const bc_client_file_hash_t *files, int file_count,
    const bc_client_subdir_hash_t *subdirs, int subdir_count)
{
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_CHECKSUM_RESP)) return -1;
    if (!bc_buf_write_u8(&b, round)) return -1;
    if (round == 0) {
        if (!bc_buf_write_u32(&b, ref_hash)) return -1;
    }
    if (!bc_buf_write_u32(&b, dir_hash)) return -1;

    /* Top-level files */
    if (!bc_buf_write_u16(&b, (u16)file_count)) return -1;
    for (int i = 0; i < file_count; i++) {
        if (!bc_buf_write_u32(&b, files[i].name_hash)) return -1;
        if (!bc_buf_write_u32(&b, files[i].content_hash)) return -1;
    }

    /* Subdirectories: [subdir_count:u8][name_0:u32..name_N:u32][tree_0..tree_N]
     * All name hashes first, then all trees sequentially. */
    if (!bc_buf_write_u8(&b, (u8)subdir_count)) return -1;
    for (int s = 0; s < subdir_count; s++) {
        if (!bc_buf_write_u32(&b, subdirs[s].name_hash)) return -1;
    }
    for (int s = 0; s < subdir_count; s++) {
        /* Each subdir tree: [file_count:u16][files × 8][subdir_count:u8=0] */
        if (!bc_buf_write_u16(&b, (u16)subdirs[s].file_count)) return -1;
        for (int i = 0; i < subdirs[s].file_count; i++) {
            if (!bc_buf_write_u32(&b, subdirs[s].files[i].name_hash)) return -1;
            if (!bc_buf_write_u32(&b, subdirs[s].files[i].content_hash)) return -1;
        }
        if (!bc_buf_write_u8(&b, 0)) return -1;  /* no sub-subdirs */
    }

    return (int)b.pos;
}

int bc_client_build_checksum_final(u8 *buf, int buf_size, u32 dir_hash)
{
    /* Build an empty recursive response for round 0xFF (Scripts/Multiplayer).
     * Wire: [0x21][0xFF][dir_hash:u32][file_count:u16=0][subdir_count:u8=0]
     * Round 0xFF has no ref_hash (only round 0 includes ref_hash). */
    bc_buffer_t b;
    bc_buf_init(&b, buf, (size_t)buf_size);

    if (!bc_buf_write_u8(&b, BC_OP_CHECKSUM_RESP)) return -1;
    if (!bc_buf_write_u8(&b, 0xFF)) return -1;
    if (!bc_buf_write_u32(&b, dir_hash)) return -1;
    if (!bc_buf_write_u16(&b, 0)) return -1;  /* file_count = 0 */
    if (!bc_buf_write_u8(&b, 0)) return -1;   /* subdir_count = 0 */

    return (int)b.pos;
}

/* --- Directory scanning for checksum computation --- */

/* Check if a filename matches a filter pattern.
 * Supports exact match ("App.pyc") and wildcard ("*.pyc"). */
static bool filter_match(const char *filename, const char *filter)
{
    if (filter[0] == '*' && filter[1] == '.') {
        /* Wildcard: match file extension */
        const char *ext = filter + 1; /* ".pyc" */
        size_t ext_len = strlen(ext);
        size_t name_len = strlen(filename);
        if (name_len < ext_len) return false;
        return bc_stricmp(filename + name_len - ext_len, ext) == 0;
    }
    /* Exact match (case-insensitive) */
    return bc_stricmp(filename, filter) == 0;
}

/* Scan a single directory (non-recursive) for matching files.
 * full_path must end with a path separator ('/' or '\\' on Windows). */
static bool scan_single_dir(const char *full_path, const char *filter,
                             bc_client_file_hash_t *files, int *file_count,
                             int max_files)
{
    *file_count = 0;

#ifdef _WIN32
    char search_path[260];
    snprintf(search_path, sizeof(search_path), "%s*", full_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return true; /* Empty dir is OK */

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!filter_match(fd.cFileName, filter)) continue;
        if (*file_count >= max_files) break;

        files[*file_count].name_hash = string_hash(fd.cFileName);

        char file_path[260];
        snprintf(file_path, sizeof(file_path), "%s%s", full_path, fd.cFileName);

        bool ok;
        files[*file_count].content_hash = file_hash_from_path(file_path, &ok);
        if (!ok) continue;

        (*file_count)++;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
#else
    DIR *dir = opendir(full_path); /* trailing '/' is accepted by opendir */
    if (!dir)
        return true; /* Empty/missing dir is OK */

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_DIR) continue;
        if (!filter_match(ent->d_name, filter)) continue;
        if (*file_count >= max_files) break;

        files[*file_count].name_hash = string_hash(ent->d_name);

        /* full_path already ends with '/', so just append filename */
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s%s", full_path, ent->d_name);

        bool ok;
        files[*file_count].content_hash = file_hash_from_path(file_path, &ok);
        if (!ok) continue;

        (*file_count)++;
    }

    closedir(dir);
#endif
    return true;
}

bool bc_client_scan_directory(const char *base_dir, const char *sub_dir,
                               const char *filter, bool recursive,
                               bc_client_dir_scan_t *scan)
{
    memset(scan, 0, sizeof(*scan));

    /* Compute directory name hash.
     * The real BC client hashes only the LEAF directory name (e.g. "ships"
     * not "scripts/ships/"). Strip trailing separators, then find the last
     * path component. */
    char dir_for_hash[64];
    strncpy(dir_for_hash, sub_dir, sizeof(dir_for_hash) - 1);
    dir_for_hash[sizeof(dir_for_hash) - 1] = '\0';
    size_t dlen = strlen(dir_for_hash);
    while (dlen > 0 && (dir_for_hash[dlen - 1] == '/' || dir_for_hash[dlen - 1] == '\\')) {
        dir_for_hash[--dlen] = '\0';
    }
    /* Find last path component */
    const char *leaf = dir_for_hash;
    for (size_t i = 0; i < dlen; i++) {
        if (dir_for_hash[i] == '/' || dir_for_hash[i] == '\\') {
            leaf = dir_for_hash + i + 1;
        }
    }
    scan->dir_hash = string_hash(leaf);

    /* Build full path: base_dir + sub_dir */
    char full_path[260];
    snprintf(full_path, sizeof(full_path), "%s%s", base_dir, sub_dir);

    /* Ensure path ends with separator */
    size_t plen = strlen(full_path);
    if (plen > 0 && full_path[plen - 1] != '\\' && full_path[plen - 1] != '/') {
        if (plen + 1 < sizeof(full_path)) {
#ifdef _WIN32
            full_path[plen] = '\\';
#else
            full_path[plen] = '/';
#endif
            full_path[plen + 1] = '\0';
        }
    }

    /* Scan top-level files */
    if (!scan_single_dir(full_path, filter,
                          scan->files, &scan->file_count, 256))
        return false;

    /* If recursive, scan subdirectories */
    if (recursive) {
#ifdef _WIN32
        char search_path[260];
        snprintf(search_path, sizeof(search_path), "%s*", full_path);

        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search_path, &fd);
        if (hFind == INVALID_HANDLE_VALUE) return true;

        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue; /* Skip . and .. */
            if (scan->subdir_count >= 8) break;

            bc_client_subdir_hash_t *sd = &scan->subdirs[scan->subdir_count];
            sd->name_hash = string_hash(fd.cFileName);

            char subdir_path[260];
            snprintf(subdir_path, sizeof(subdir_path), "%s%s\\",
                     full_path, fd.cFileName);

            if (!scan_single_dir(subdir_path, filter,
                                  sd->files, &sd->file_count, 128))
                continue;

            if (sd->file_count > 0) {
                scan->subdir_count++;
            }
        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);
#else
        DIR *rdir = opendir(full_path);
        if (!rdir) return true;

        struct dirent *rent;
        while ((rent = readdir(rdir)) != NULL) {
            if (rent->d_type != DT_DIR) continue;
            if (rent->d_name[0] == '.') continue; /* Skip . and .. */
            if (scan->subdir_count >= 8) break;

            bc_client_subdir_hash_t *sd = &scan->subdirs[scan->subdir_count];
            sd->name_hash = string_hash(rent->d_name);

            /* full_path ends with '/' */
            char subdir_path[512];
            snprintf(subdir_path, sizeof(subdir_path), "%s%s/",
                     full_path, rent->d_name);

            if (!scan_single_dir(subdir_path, filter,
                                  sd->files, &sd->file_count, 128))
                continue;

            if (sd->file_count > 0) {
                scan->subdir_count++;
            }
        }

        closedir(rdir);
#endif
    }

    return true;
}
