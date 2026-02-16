#ifndef OPENBC_MANIFEST_H
#define OPENBC_MANIFEST_H

#include "openbc/types.h"

/*
 * Hash manifest -- in-memory representation of a vanilla-1.1.json manifest.
 *
 * The manifest stores expected StringHash/FileHash values for all files
 * checked during the 4-round checksum exchange. The server loads this at
 * startup and validates client checksum responses against it.
 *
 * Structure mirrors the JSON: 4 checksum directories, each with files
 * and optional subdirectories (e.g. scripts/ships/Hardpoints/).
 */

#define BC_MANIFEST_MAX_DIRS       4
#define BC_MANIFEST_MAX_FILES    256
#define BC_MANIFEST_MAX_SUBDIRS    8
#define BC_MANIFEST_MAX_SUB_FILES 128

/* A single file entry: name hash + content hash */
typedef struct {
    u32 name_hash;
    u32 content_hash;
} bc_manifest_file_t;

/* A subdirectory within a checksum directory */
typedef struct {
    u32                 name_hash;
    bc_manifest_file_t  files[BC_MANIFEST_MAX_SUB_FILES];
    int                 file_count;
} bc_manifest_subdir_t;

/* A top-level checksum directory (one per round) */
typedef struct {
    u32                   dir_name_hash;
    bool                  recursive;
    bc_manifest_file_t    files[BC_MANIFEST_MAX_FILES];
    int                   file_count;
    bc_manifest_subdir_t  subdirs[BC_MANIFEST_MAX_SUBDIRS];
    int                   subdir_count;
} bc_manifest_dir_t;

/* Complete manifest */
typedef struct {
    u32               version_hash;
    bc_manifest_dir_t dirs[BC_MANIFEST_MAX_DIRS];
    int               dir_count;
} bc_manifest_t;

/* Load a manifest from a JSON file on disk.
 * Returns true on success, fills 'manifest'.
 * On failure, returns false and prints error to stderr. */
bool bc_manifest_load(bc_manifest_t *manifest, const char *path);

/* Print a summary of the loaded manifest (for startup diagnostics). */
void bc_manifest_print_summary(const bc_manifest_t *manifest);

/* Look up a file by name_hash within a manifest directory.
 * Returns pointer to the file entry, or NULL if not found. */
const bc_manifest_file_t *bc_manifest_find_file(
    const bc_manifest_dir_t *dir, u32 name_hash);

/* Look up a file by name_hash within a manifest subdirectory.
 * Returns pointer to the file entry, or NULL if not found. */
const bc_manifest_file_t *bc_manifest_find_subdir_file(
    const bc_manifest_subdir_t *subdir, u32 name_hash);

/* Look up a subdirectory by name_hash within a manifest directory.
 * Returns pointer to the subdir entry, or NULL if not found. */
const bc_manifest_subdir_t *bc_manifest_find_subdir(
    const bc_manifest_dir_t *dir, u32 name_hash);

#endif /* OPENBC_MANIFEST_H */
