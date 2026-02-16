#include "openbc/manifest.h"
#include "openbc/json_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Convert a hex string like "0x7E0CE243" to u32. */
static u32 hex_to_u32(const char *s)
{
    if (!s) return 0;
    return (u32)strtoul(s, NULL, 16);
}

/* Parse a JSON file array into manifest file entries.
 * Returns number of files parsed, or -1 on error. */
static int parse_files(const json_value_t *arr,
                       bc_manifest_file_t *out, int max_files)
{
    size_t count = json_array_len(arr);
    if ((int)count > max_files) {
        fprintf(stderr, "manifest: too many files (%d, max %d)\n",
                (int)count, max_files);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        const json_value_t *f = json_array_get(arr, i);
        out[i].name_hash    = hex_to_u32(json_string(json_get(f, "name_hash")));
        out[i].content_hash = hex_to_u32(json_string(json_get(f, "content_hash")));
    }
    return (int)count;
}

/* Parse a JSON subdirs array. */
static int parse_subdirs(const json_value_t *arr,
                         bc_manifest_subdir_t *out, int max_subdirs)
{
    size_t count = json_array_len(arr);
    if ((int)count > max_subdirs) {
        fprintf(stderr, "manifest: too many subdirs (%d, max %d)\n",
                (int)count, max_subdirs);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        const json_value_t *sd = json_array_get(arr, i);
        out[i].name_hash = hex_to_u32(json_string(json_get(sd, "name_hash")));

        const json_value_t *files = json_get(sd, "files");
        int fc = parse_files(files, out[i].files, BC_MANIFEST_MAX_SUB_FILES);
        if (fc < 0) return -1;
        out[i].file_count = fc;
    }
    return (int)count;
}

bool bc_manifest_load(bc_manifest_t *manifest, const char *path)
{
    memset(manifest, 0, sizeof(*manifest));

    /* Read entire file into memory */
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "manifest: cannot open '%s'\n", path);
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {
        fprintf(stderr, "manifest: invalid file size (%ld)\n", size);
        fclose(fp);
        return false;
    }

    char *text = malloc((size_t)size + 1);
    if (!text) {
        fclose(fp);
        return false;
    }

    size_t nread = fread(text, 1, (size_t)size, fp);
    fclose(fp);
    text[nread] = '\0';

    /* Parse JSON */
    json_value_t *root = json_parse(text);
    free(text);

    if (!root) {
        fprintf(stderr, "manifest: JSON parse error in '%s'\n", path);
        return false;
    }

    /* Extract version hash */
    const char *vh = json_string(json_get(root, "version_string_hash"));
    manifest->version_hash = hex_to_u32(vh);

    /* Parse directories array */
    const json_value_t *dirs = json_get(root, "directories");
    size_t dir_count = json_array_len(dirs);
    if ((int)dir_count > BC_MANIFEST_MAX_DIRS) {
        fprintf(stderr, "manifest: too many directories (%d)\n", (int)dir_count);
        json_free(root);
        return false;
    }
    manifest->dir_count = (int)dir_count;

    for (size_t i = 0; i < dir_count; i++) {
        const json_value_t *d = json_array_get(dirs, i);
        bc_manifest_dir_t *md = &manifest->dirs[i];

        md->dir_name_hash = hex_to_u32(json_string(json_get(d, "dir_name_hash")));
        md->recursive = json_bool(json_get(d, "recursive"));

        /* Parse files */
        const json_value_t *files = json_get(d, "files");
        int fc = parse_files(files, md->files, BC_MANIFEST_MAX_FILES);
        if (fc < 0) { json_free(root); return false; }
        md->file_count = fc;

        /* Parse subdirs */
        const json_value_t *subdirs = json_get(d, "subdirs");
        int sc = parse_subdirs(subdirs, md->subdirs, BC_MANIFEST_MAX_SUBDIRS);
        if (sc < 0) { json_free(root); return false; }
        md->subdir_count = sc;
    }

    json_free(root);
    return true;
}

void bc_manifest_print_summary(const bc_manifest_t *manifest)
{
    int total_files = 0;
    printf("Manifest: version_hash=0x%08X, %d directories\n",
           manifest->version_hash, manifest->dir_count);

    for (int i = 0; i < manifest->dir_count; i++) {
        const bc_manifest_dir_t *d = &manifest->dirs[i];
        int dir_total = d->file_count;
        for (int s = 0; s < d->subdir_count; s++) {
            dir_total += d->subdirs[s].file_count;
        }
        printf("  Round %d: dir_hash=0x%08X, %d files, %d subdirs%s\n",
               i, d->dir_name_hash, d->file_count, d->subdir_count,
               d->recursive ? " (recursive)" : "");
        total_files += dir_total;
    }
    printf("  Total: %d files tracked\n", total_files);
}

const bc_manifest_file_t *bc_manifest_find_file(
    const bc_manifest_dir_t *dir, u32 name_hash)
{
    for (int i = 0; i < dir->file_count; i++) {
        if (dir->files[i].name_hash == name_hash)
            return &dir->files[i];
    }
    return NULL;
}

const bc_manifest_file_t *bc_manifest_find_subdir_file(
    const bc_manifest_subdir_t *subdir, u32 name_hash)
{
    for (int i = 0; i < subdir->file_count; i++) {
        if (subdir->files[i].name_hash == name_hash)
            return &subdir->files[i];
    }
    return NULL;
}

const bc_manifest_subdir_t *bc_manifest_find_subdir(
    const bc_manifest_dir_t *dir, u32 name_hash)
{
    for (int i = 0; i < dir->subdir_count; i++) {
        if (dir->subdirs[i].name_hash == name_hash)
            return &dir->subdirs[i];
    }
    return NULL;
}
