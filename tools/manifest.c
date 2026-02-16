#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "openbc/checksum.h"
#include "openbc/json_parse.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s generate <game_dir> -o <output.json>\n"
        "  %s verify <manifest.json> <game_dir>\n"
        "  %s hash-string <string>\n"
        "  %s hash-file <path>\n",
        prog, prog, prog, prog);
}

/* Forward declarations */
static int cmd_hash_string(int argc, char **argv);
static int cmd_hash_file(int argc, char **argv);
static int cmd_generate(int argc, char **argv);
static int cmd_verify(int argc, char **argv);

/* --- JSON writing helpers (minimal, no dependency) --- */

typedef struct {
    FILE *fp;
    int   indent;
    bool  first_item;  /* tracks comma placement */
} json_writer_t;

static void json_indent(json_writer_t *w)
{
    for (int i = 0; i < w->indent; i++) fprintf(w->fp, "  ");
}

static void json_comma(json_writer_t *w)
{
    if (!w->first_item) fprintf(w->fp, ",");
    fprintf(w->fp, "\n");
    w->first_item = false;
}

static void json_key_str(json_writer_t *w, const char *key, const char *val)
{
    json_comma(w);
    json_indent(w);
    fprintf(w->fp, "\"%s\": \"%s\"", key, val);
}

static void json_key_hex(json_writer_t *w, const char *key, u32 val)
{
    json_comma(w);
    json_indent(w);
    fprintf(w->fp, "\"%s\": \"0x%08X\"", key, val);
}

static void json_key_bool(json_writer_t *w, const char *key, bool val)
{
    json_comma(w);
    json_indent(w);
    fprintf(w->fp, "\"%s\": %s", key, val ? "true" : "false");
}

static void json_key_int(json_writer_t *w, const char *key, int val)
{
    json_comma(w);
    json_indent(w);
    fprintf(w->fp, "\"%s\": %d", key, val);
}

static void json_begin_obj(json_writer_t *w, const char *key)
{
    if (key) {
        json_comma(w);
        json_indent(w);
        fprintf(w->fp, "\"%s\": {", key);
    } else {
        fprintf(w->fp, "{");
    }
    w->indent++;
    w->first_item = true;
}

static void json_end_obj(json_writer_t *w)
{
    w->indent--;
    fprintf(w->fp, "\n");
    json_indent(w);
    fprintf(w->fp, "}");
    w->first_item = false;
}

static void json_begin_arr(json_writer_t *w, const char *key)
{
    json_comma(w);
    json_indent(w);
    fprintf(w->fp, "\"%s\": [", key);
    w->indent++;
    w->first_item = true;
}

static void json_begin_arr_obj(json_writer_t *w)
{
    json_comma(w);
    json_indent(w);
    fprintf(w->fp, "{");
    w->indent++;
    w->first_item = true;
}

static void json_end_arr(json_writer_t *w)
{
    w->indent--;
    fprintf(w->fp, "\n");
    json_indent(w);
    fprintf(w->fp, "]");
    w->first_item = false;
}

/* --- Checksum directory definitions (stock BC 1.1) --- */

typedef struct {
    int         index;
    const char *path;
    const char *filter;
    bool        recursive;
} checksum_dir_t;

static const checksum_dir_t CHECKSUM_DIRS[] = {
    { 0, "scripts",          "App.pyc",      false },
    { 1, "scripts",          "Autoexec.pyc", false },
    { 2, "scripts/ships",    "*.pyc",        true  },
    { 3, "scripts/mainmenu", "*.pyc",        false },
};
#define NUM_CHECKSUM_DIRS 4

/* Check if a filename matches a filter pattern (simple: "*" or exact match) */
static bool match_filter(const char *filename, const char *filter)
{
    if (strcmp(filter, "*.pyc") == 0) {
        size_t len = strlen(filename);
        return len >= 4 && strcmp(filename + len - 4, ".pyc") == 0;
    }
    return strcmp(filename, filter) == 0;
}

/* Hash files in a directory, write JSON */
static void hash_directory_files(json_writer_t *w, const char *dirpath,
                                  const char *filter, bool recursive)
{
    DIR *d = opendir(dirpath);
    if (!d) return;

    struct dirent *ent;
    bool has_files = false;
    bool has_subdirs = false;

    /* First pass: files */
    json_begin_arr(w, "files");
    rewinddir(d);
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, ent->d_name);

        struct stat st;
        if (stat(fullpath, &st) != 0) continue;

        if (S_ISREG(st.st_mode) && match_filter(ent->d_name, filter)) {
            bool ok;
            u32 name_h = string_hash(ent->d_name);
            u32 content_h = file_hash_from_path(fullpath, &ok);

            json_begin_arr_obj(w);
            json_key_str(w, "filename", ent->d_name);
            json_key_hex(w, "name_hash", name_h);
            if (ok) {
                json_key_hex(w, "content_hash", content_h);
            } else {
                json_key_str(w, "content_hash", "ERROR");
            }
            json_end_obj(w);
            has_files = true;
        }
    }
    json_end_arr(w);
    (void)has_files;

    /* Second pass: subdirectories (if recursive) */
    json_begin_arr(w, "subdirs");
    if (recursive) {
        rewinddir(d);
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;

            char fullpath[1024];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, ent->d_name);

            struct stat st;
            if (stat(fullpath, &st) != 0) continue;

            if (S_ISDIR(st.st_mode)) {
                json_begin_arr_obj(w);
                json_key_str(w, "name", ent->d_name);
                json_key_hex(w, "name_hash", string_hash(ent->d_name));
                hash_directory_files(w, fullpath, filter, true);
                json_end_obj(w);
                has_subdirs = true;
            }
        }
    }
    json_end_arr(w);
    (void)has_subdirs;

    closedir(d);
}

/* --- Command implementations --- */

static int cmd_hash_string(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Error: missing string argument\n");
        return 1;
    }
    u32 h = string_hash(argv[0]);
    printf("0x%08X\n", h);
    return 0;
}

static int cmd_hash_file(int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Error: missing file path argument\n");
        return 1;
    }
    bool ok;
    u32 h = file_hash_from_path(argv[0], &ok);
    if (!ok) {
        fprintf(stderr, "Error: could not read file '%s'\n", argv[0]);
        return 1;
    }
    printf("0x%08X\n", h);
    return 0;
}

static int cmd_generate(int argc, char **argv)
{
    const char *game_dir = NULL;
    const char *output   = NULL;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (!game_dir) {
            game_dir = argv[i];
        }
    }

    if (!game_dir || !output) {
        fprintf(stderr, "Error: generate requires <game_dir> -o <output.json>\n");
        return 1;
    }

    FILE *fp = fopen(output, "w");
    if (!fp) {
        fprintf(stderr, "Error: cannot open '%s' for writing\n", output);
        return 1;
    }

    json_writer_t w = { .fp = fp, .indent = 0, .first_item = true };

    json_begin_obj(&w, NULL);

    /* Metadata */
    json_begin_obj(&w, "meta");
    json_key_str(&w, "name", "Star Trek: Bridge Commander 1.1");
    json_key_str(&w, "generator", "openbc-hash");
    json_key_str(&w, "generator_version", "0.1.0");
    json_end_obj(&w);

    /* Version string */
    json_key_str(&w, "version_string", "60");
    json_key_hex(&w, "version_string_hash", string_hash("60"));

    /* Directories */
    json_begin_arr(&w, "directories");

    for (int i = 0; i < NUM_CHECKSUM_DIRS; i++) {
        const checksum_dir_t *cd = &CHECKSUM_DIRS[i];

        char dirpath[1024];
        snprintf(dirpath, sizeof(dirpath), "%s/%s", game_dir, cd->path);

        /* Extract the last component of the path for dir_name_hash */
        const char *dirname = strrchr(cd->path, '/');
        dirname = dirname ? dirname + 1 : cd->path;

        json_begin_arr_obj(&w);
        json_key_int(&w, "index", cd->index);
        json_key_str(&w, "path", cd->path);
        json_key_str(&w, "filter", cd->filter);
        json_key_bool(&w, "recursive", cd->recursive);
        json_key_hex(&w, "dir_name_hash", string_hash(dirname));

        hash_directory_files(&w, dirpath, cd->filter, cd->recursive);

        json_end_obj(&w);
    }

    json_end_arr(&w);
    json_end_obj(&w);

    fprintf(fp, "\n");
    fclose(fp);

    printf("Manifest written to %s\n", output);
    return 0;
}

/* Parse a "0xNNNNNNNN" hex string to u32. Returns 0 on failure. */
static u32 parse_hex(const char *s)
{
    if (!s) return 0;
    return (u32)strtoul(s, NULL, 16);
}

/* Read an entire file into a malloc'd buffer. Sets *out_len. Returns NULL on error. */
static char *read_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (len < 0) { fclose(fp); return NULL; }

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(fp); return NULL; }

    size_t read = fread(buf, 1, (size_t)len, fp);
    fclose(fp);

    buf[read] = '\0';
    if (out_len) *out_len = read;
    return buf;
}

/* Verify files in a manifest directory entry against disk.
 * Returns number of mismatches found. */
static int verify_files(const json_value_t *files_arr, const char *dirpath,
                        int *checked)
{
    int mismatches = 0;
    size_t nfiles = json_array_len(files_arr);

    for (size_t i = 0; i < nfiles; i++) {
        const json_value_t *entry = json_array_get(files_arr, i);
        const char *filename = json_string(json_get(entry, "filename"));
        const char *expected_str = json_string(json_get(entry, "content_hash"));
        if (!filename || !expected_str) continue;

        u32 expected = parse_hex(expected_str);

        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, filename);

        bool ok;
        u32 actual = file_hash_from_path(fullpath, &ok);
        (*checked)++;

        if (!ok) {
            printf("  MISSING  %s/%s\n", dirpath, filename);
            mismatches++;
        } else if (actual != expected) {
            printf("  MISMATCH %s/%s  expected=0x%08X actual=0x%08X\n",
                   dirpath, filename, expected, actual);
            mismatches++;
        }
    }
    return mismatches;
}

/* Verify subdirectories recursively. Returns number of mismatches. */
static int verify_subdirs(const json_value_t *subdirs_arr, const char *dirpath,
                          int *checked)
{
    int mismatches = 0;
    size_t nsubs = json_array_len(subdirs_arr);

    for (size_t i = 0; i < nsubs; i++) {
        const json_value_t *sub = json_array_get(subdirs_arr, i);
        const char *name = json_string(json_get(sub, "name"));
        if (!name) continue;

        char subpath[1024];
        snprintf(subpath, sizeof(subpath), "%s/%s", dirpath, name);

        mismatches += verify_files(json_get(sub, "files"), subpath, checked);
        mismatches += verify_subdirs(json_get(sub, "subdirs"), subpath, checked);
    }
    return mismatches;
}

static int cmd_verify(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Error: verify requires <manifest.json> <game_dir>\n");
        return 1;
    }

    const char *manifest_path = argv[0];
    const char *game_dir = argv[1];

    /* Load manifest file */
    size_t len;
    char *text = read_file(manifest_path, &len);
    if (!text) {
        fprintf(stderr, "Error: cannot read '%s'\n", manifest_path);
        return 1;
    }

    /* Parse JSON */
    json_value_t *root = json_parse(text);
    free(text);
    if (!root) {
        fprintf(stderr, "Error: failed to parse JSON from '%s'\n", manifest_path);
        return 1;
    }

    int total_checked = 0;
    int total_mismatches = 0;

    /* Verify version string hash */
    const char *ver_str = json_string(json_get(root, "version_string"));
    const char *ver_hash_str = json_string(json_get(root, "version_string_hash"));
    if (ver_str && ver_hash_str) {
        u32 expected = parse_hex(ver_hash_str);
        u32 actual = string_hash(ver_str);
        if (actual != expected) {
            printf("  MISMATCH version_string \"%s\"  expected=0x%08X actual=0x%08X\n",
                   ver_str, expected, actual);
            total_mismatches++;
        }
        total_checked++;
    }

    /* Verify each directory */
    const json_value_t *dirs = json_get(root, "directories");
    size_t ndirs = json_array_len(dirs);

    for (size_t i = 0; i < ndirs; i++) {
        const json_value_t *dir = json_array_get(dirs, i);
        const char *path = json_string(json_get(dir, "path"));
        if (!path) continue;

        char dirpath[1024];
        snprintf(dirpath, sizeof(dirpath), "%s/%s", game_dir, path);

        printf("Checking %s ...\n", path);
        total_mismatches += verify_files(json_get(dir, "files"), dirpath,
                                         &total_checked);
        total_mismatches += verify_subdirs(json_get(dir, "subdirs"), dirpath,
                                           &total_checked);
    }

    json_free(root);

    printf("\n=== %d files checked, %d mismatches ===\n",
           total_checked, total_mismatches);

    return total_mismatches > 0 ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "generate") == 0) {
        return cmd_generate(argc - 2, argv + 2);
    } else if (strcmp(cmd, "verify") == 0) {
        return cmd_verify(argc - 2, argv + 2);
    } else if (strcmp(cmd, "hash-string") == 0) {
        return cmd_hash_string(argc - 2, argv + 2);
    } else if (strcmp(cmd, "hash-file") == 0) {
        return cmd_hash_file(argc - 2, argv + 2);
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage(argv[0]);
        return 1;
    }
}
