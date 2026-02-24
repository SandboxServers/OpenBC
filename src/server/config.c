#include "openbc/config.h"
#include "toml/toml.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

#define OBC_CFG_MAX_FILE_BYTES (1024L * 1024L)

static void str_copy(char *dst, size_t dstsz, const char *src)
{
    if (!dst || dstsz == 0 || !src) return;

    size_t src_len = strlen(src);
    size_t copy_len = src_len;
    if (copy_len >= dstsz) {
        fprintf(stderr,
                "config: warning: truncating value to %zu chars: '%.32s...'\n",
                dstsz - 1, src);
        copy_len = dstsz - 1;
    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static void warn_invalid_i64(const char *field, int64_t value, const char *range_desc)
{
    fprintf(stderr,
            "config: warning: invalid %s=%lld (expected %s); keeping existing value\n",
            field, (long long)value, range_desc);
}

static void warn_capacity_overflow(const char *field, int value, int max_value)
{
    fprintf(stderr,
            "config: warning: %s has %d entries (max %d); extras ignored\n",
            field, value, max_value);
}

static int clamp_with_warning(const char *field, int value, int max_value)
{
    if (value > max_value) {
        warn_capacity_overflow(field, value, max_value);
        return max_value;
    }
    return value;
}

static bool parse_i64_for_int(int64_t value, int *out)
{
    if (value < INT_MIN || value > INT_MAX) return false;
    *out = (int)value;
    return true;
}

static bool parse_i64_for_int_range(int64_t value, int min_value, int max_value, int *out)
{
    if (!parse_i64_for_int(value, out)) return false;
    if (*out < min_value || *out > max_value) return false;
    return true;
}

static bool parse_i64_for_u16_range(int64_t value, uint16_t min_value, uint16_t max_value,
                                    uint16_t *out)
{
    if (value < min_value || value > max_value) return false;
    *out = (uint16_t)value;
    return true;
}

static void process_server_section(toml_table_t *root, obc_server_cfg_t *cfg)
{
    toml_table_t *server = toml_table_table(root, "server");
    if (!server) return;

    toml_value_t value = toml_table_int(server, "port");
    if (value.ok) {
        uint16_t parsed_port = 0;
        if (parse_i64_for_u16_range(value.u.i, 1, 65535, &parsed_port))
            cfg->port = parsed_port;
        else
            warn_invalid_i64("[server].port", value.u.i, "1..65535");
    }

    value = toml_table_int(server, "max_players");
    if (value.ok) {
        int parsed_max_players = 0;
        if (parse_i64_for_int(value.u.i, &parsed_max_players))
            cfg->max_players = parsed_max_players;
        else
            warn_invalid_i64("[server].max_players", value.u.i, "32-bit signed integer");
    }

    value = toml_table_string(server, "name");
    if (value.ok) {
        str_copy(cfg->name, sizeof(cfg->name), value.u.s);
        free(value.u.s);
    }

    value = toml_table_string(server, "log_level");
    if (value.ok) {
        str_copy(cfg->log_level, sizeof(cfg->log_level), value.u.s);
        free(value.u.s);
    }

    value = toml_table_string(server, "log_file");
    if (value.ok) {
        str_copy(cfg->log_file, sizeof(cfg->log_file), value.u.s);
        free(value.u.s);
    }
}

static void process_game_section(toml_table_t *root, obc_server_cfg_t *cfg)
{
    toml_table_t *game = toml_table_table(root, "game");
    if (!game) return;

    toml_value_t value = toml_table_string(game, "map");
    if (value.ok) {
        str_copy(cfg->map, sizeof(cfg->map), value.u.s);
        free(value.u.s);
    }

    value = toml_table_int(game, "system");
    if (value.ok) {
        int parsed_system = 0;
        if (parse_i64_for_int(value.u.i, &parsed_system))
            cfg->system = parsed_system;
        else
            warn_invalid_i64("[game].system", value.u.i, "32-bit signed integer");
    }

    value = toml_table_int(game, "time_limit");
    if (value.ok) {
        int parsed_time_limit = 0;
        if (parse_i64_for_int(value.u.i, &parsed_time_limit))
            cfg->time_limit = parsed_time_limit;
        else
            warn_invalid_i64("[game].time_limit", value.u.i, "32-bit signed integer");
    }

    value = toml_table_int(game, "frag_limit");
    if (value.ok) {
        int parsed_frag_limit = 0;
        if (parse_i64_for_int(value.u.i, &parsed_frag_limit))
            cfg->frag_limit = parsed_frag_limit;
        else
            warn_invalid_i64("[game].frag_limit", value.u.i, "32-bit signed integer");
    }

    value = toml_table_bool(game, "collision_damage");
    if (value.ok) cfg->collision_damage = value.u.b;

    value = toml_table_bool(game, "friendly_fire");
    if (value.ok) cfg->friendly_fire = value.u.b;

    value = toml_table_int(game, "difficulty");
    if (value.ok) {
        int parsed_difficulty = 0;
        if (parse_i64_for_int_range(value.u.i, 0, 2, &parsed_difficulty))
            cfg->difficulty = parsed_difficulty;
        else
            warn_invalid_i64("[game].difficulty", value.u.i, "0..2");
    }

    value = toml_table_int(game, "respawn_time");
    if (value.ok) {
        int parsed_respawn_time = 0;
        if (parse_i64_for_int_range(value.u.i, 0, 3600, &parsed_respawn_time))
            cfg->respawn_time = parsed_respawn_time;
        else
            warn_invalid_i64("[game].respawn_time", value.u.i, "0..3600");
    }

    value = toml_table_string(game, "mode_file");
    if (value.ok) {
        str_copy(cfg->mode_file, sizeof(cfg->mode_file), value.u.s);
        free(value.u.s);
    }
}

static void process_data_section(toml_table_t *root, obc_server_cfg_t *cfg)
{
    toml_table_t *data = toml_table_table(root, "data");
    if (!data) return;

    toml_value_t value = toml_table_string(data, "registry");
    if (value.ok) {
        str_copy(cfg->registry, sizeof(cfg->registry), value.u.s);
        free(value.u.s);
    }

    value = toml_table_string(data, "manifest");
    if (value.ok) {
        str_copy(cfg->manifest_path, sizeof(cfg->manifest_path), value.u.s);
        free(value.u.s);
    }

    toml_array_t *packs = toml_table_array(data, "mod_packs");
    if (!packs) return;

    int pack_count = toml_array_len(packs);
    pack_count = clamp_with_warning("[data].mod_packs", pack_count, OBC_CFG_MOD_PACKS_MAX);

    cfg->mod_pack_count = 0;
    for (int i = 0; i < pack_count; i++) {
        toml_value_t string_value = toml_array_string(packs, i);
        if (!string_value.ok) continue;

        str_copy(cfg->mod_packs[cfg->mod_pack_count],
                 sizeof(cfg->mod_packs[0]), string_value.u.s);
        free(string_value.u.s);
        cfg->mod_pack_count++;
    }
}

static void process_gamespy_section(toml_table_t *root, obc_server_cfg_t *cfg)
{
    toml_table_t *gamespy = toml_table_table(root, "gamespy");
    if (!gamespy) return;

    toml_value_t value = toml_table_bool(gamespy, "enabled");
    if (value.ok) cfg->gamespy_enabled = value.u.b;

    value = toml_table_bool(gamespy, "lan_discovery");
    if (value.ok) cfg->lan_discovery = value.u.b;

    toml_array_t *master_array = toml_table_array(gamespy, "master");
    if (!master_array) return;

    int master_count = toml_array_len(master_array);
    master_count = clamp_with_warning("[gamespy].master", master_count, OBC_CFG_MASTERS_MAX);

    cfg->master_count = 0;
    for (int i = 0; i < master_count; i++) {
        toml_value_t string_value = toml_array_string(master_array, i);
        if (!string_value.ok) continue;

        str_copy(cfg->masters[cfg->master_count],
                 sizeof(cfg->masters[0]), string_value.u.s);
        free(string_value.u.s);
        cfg->master_count++;
    }
}

static void process_master_section(toml_table_t *root, obc_server_cfg_t *cfg)
{
    toml_table_t *master = toml_table_table(root, "master");
    if (!master) return;

    toml_value_t value = toml_table_int(master, "heartbeat_interval");
    if (!value.ok) return;

    int parsed_interval = 0;
    if (parse_i64_for_int_range(value.u.i, 10, 3600, &parsed_interval))
        cfg->heartbeat_interval = parsed_interval;
    else
        warn_invalid_i64("[master].heartbeat_interval", value.u.i, "10..3600");
}

static void process_module_table(toml_table_t *module, obc_module_cfg_t *out_module)
{
    toml_value_t value = toml_table_string(module, "name");
    if (value.ok) {
        str_copy(out_module->name, sizeof(out_module->name), value.u.s);
        free(value.u.s);
    }

    value = toml_table_string(module, "dll");
    if (value.ok) {
        str_copy(out_module->dll, sizeof(out_module->dll), value.u.s);
        free(value.u.s);
    }

    value = toml_table_string(module, "lua");
    if (value.ok) {
        str_copy(out_module->lua, sizeof(out_module->lua), value.u.s);
        free(value.u.s);
    }

    toml_table_t *module_config = toml_table_table(module, "config");
    if (!module_config) return;

    int key_count = toml_table_len(module_config);
    if (key_count > OBC_CFG_MODCFG_MAX)
        warn_capacity_overflow("[modules.config]", key_count, OBC_CFG_MODCFG_MAX);

    for (int key_index = 0;
         key_index < key_count && out_module->kv_count < OBC_CFG_MODCFG_MAX;
         key_index++) {
        int key_length = 0;
        const char *key = toml_table_key(module_config, key_index, &key_length);
        (void)key_length;
        if (!key) continue;

        obc_mod_kv_t *kv = &out_module->kv[out_module->kv_count];
        str_copy(kv->key, sizeof(kv->key), key);

        toml_value_t string_value = toml_table_string(module_config, key);
        if (string_value.ok) {
            str_copy(kv->val, sizeof(kv->val), string_value.u.s);
            free(string_value.u.s);
            out_module->kv_count++;
            continue;
        }

        toml_value_t int_value = toml_table_int(module_config, key);
        if (int_value.ok) {
            snprintf(kv->val, sizeof(kv->val), "%lld", (long long)int_value.u.i);
            out_module->kv_count++;
            continue;
        }

        toml_value_t bool_value = toml_table_bool(module_config, key);
        if (bool_value.ok) {
            str_copy(kv->val, sizeof(kv->val), bool_value.u.b ? "true" : "false");
            out_module->kv_count++;
            continue;
        }

        toml_value_t double_value = toml_table_double(module_config, key);
        if (double_value.ok) {
            snprintf(kv->val, sizeof(kv->val), "%.17g", double_value.u.d);
            out_module->kv_count++;
            continue;
        }

        /* Nested arrays/tables inside [modules.config] are unsupported and
         * intentionally skipped. */
    }
}

static void process_modules_section(toml_table_t *root, obc_server_cfg_t *cfg)
{
    toml_array_t *modules = toml_table_array(root, "modules");
    if (!modules) return;

    int module_count = toml_array_len(modules);
    module_count = clamp_with_warning("[[modules]]", module_count, OBC_CFG_MODULES_MAX);

    cfg->module_count = 0;
    for (int i = 0; i < module_count; i++) {
        toml_table_t *module = toml_array_table(modules, i);
        if (!module) continue;

        obc_module_cfg_t *out_module = &cfg->modules[cfg->module_count];
        memset(out_module, 0, sizeof(*out_module));
        process_module_table(module, out_module);
        cfg->module_count++;
    }
}

/*
 * Process a parsed TOML root table into cfg.
 * All fields present in root override the current cfg values; absent
 * fields are left untouched.
 */
static void process_toml(toml_table_t *root, obc_server_cfg_t *cfg)
{
    process_server_section(root, cfg);
    process_game_section(root, cfg);
    process_data_section(root, cfg);
    process_gamespy_section(root, cfg);
    process_master_section(root, cfg);
    process_modules_section(root, cfg);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void obc_config_defaults(obc_server_cfg_t *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    /* [server] */
    cfg->port        = 22101;
    cfg->max_players = 6;
    str_copy(cfg->name,      sizeof(cfg->name),      "OpenBC Server");
    str_copy(cfg->log_level, sizeof(cfg->log_level), "info");
    /* log_file: empty = auto-generate timestamped name */

    /* [game] */
    str_copy(cfg->map, sizeof(cfg->map),
             "Multiplayer.Episode.Mission1.Mission1");
    cfg->system           = 1;
    cfg->time_limit       = -1;
    cfg->frag_limit       = -1;
    cfg->collision_damage = true;
    cfg->friendly_fire    = false;
    cfg->difficulty       = 1;
    cfg->respawn_time     = 10;

    /* [data]: empty = auto-detect */

    /* [gamespy] */
    cfg->gamespy_enabled = true;
    cfg->lan_discovery   = true;

    /* [master] */
    cfg->heartbeat_interval = 60;
}

bool obc_config_load(const char *path, obc_server_cfg_t *cfg)
{
    if (!path || !cfg) return false;

    FILE *fp = fopen(path, "r");
    if (!fp) return false;  /* Missing file is silently OK */

    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "config: failed to inspect '%s' (must be a regular file)\n", path);
        fclose(fp);
        return false;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        fprintf(stderr, "config: failed to inspect '%s'\n", path);
        fclose(fp);
        return false;
    }
    if (file_size > OBC_CFG_MAX_FILE_BYTES) {
        fprintf(stderr, "config: '%s' too large (%ld bytes, max %ld)\n",
                path, file_size, OBC_CFG_MAX_FILE_BYTES);
        fclose(fp);
        return false;
    }
    rewind(fp);

    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);

    if (!root) {
        fprintf(stderr, "config: failed to parse '%s': %s\n", path, errbuf);
        return false;
    }

    process_toml(root, cfg);
    toml_free(root);
    return true;
}

bool obc_config_load_str(const char *src, obc_server_cfg_t *cfg)
{
    if (!src || !cfg) return false;

    /* toml_parse() modifies the input buffer, so we must copy it. */
    char *buf = strdup(src);
    if (!buf) return false;

    char errbuf[256];
    toml_table_t *root = toml_parse(buf, errbuf, sizeof(errbuf));
    free(buf);

    if (!root) {
        fprintf(stderr, "config: failed to parse TOML string: %s\n", errbuf);
        return false;
    }

    process_toml(root, cfg);
    toml_free(root);
    return true;
}

void obc_config_free(obc_server_cfg_t *cfg)
{
    (void)cfg;
    /* All strings are fixed-size arrays; nothing to release. */
}

/* -------------------------------------------------------------------------
 * Per-module config accessors
 * ---------------------------------------------------------------------- */

static const obc_module_cfg_t *find_module(const obc_server_cfg_t *cfg,
                                            const char *module_name)
{
    if (!cfg || !module_name) return NULL;
    for (int i = 0; i < cfg->module_count; i++) {
        if (strcmp(cfg->modules[i].name, module_name) == 0)
            return &cfg->modules[i];
    }
    return NULL;
}

static const char *find_kv(const obc_module_cfg_t *mod, const char *key)
{
    if (!mod || !key) return NULL;
    for (int i = 0; i < mod->kv_count; i++) {
        if (strcmp(mod->kv[i].key, key) == 0)
            return mod->kv[i].val;
    }
    return NULL;
}

const char *obc_config_mod_string(const obc_server_cfg_t *cfg,
                                   const char *module_name,
                                   const char *key,
                                   const char *default_val)
{
    const obc_module_cfg_t *mod = find_module(cfg, module_name);
    if (!mod) return default_val;
    const char *v = find_kv(mod, key);
    return v ? v : default_val;
}

int obc_config_mod_int(const obc_server_cfg_t *cfg,
                        const char *module_name,
                        const char *key,
                        int         default_val)
{
    const obc_module_cfg_t *mod = find_module(cfg, module_name);
    if (!mod) return default_val;
    const char *v = find_kv(mod, key);
    if (!v) return default_val;

    char *end = NULL;
    errno = 0;
    long parsed = strtol(v, &end, 10);
    if (end == v || *end != '\0' || errno == ERANGE)
        return default_val;
    if (parsed < INT_MIN || parsed > INT_MAX)
        return default_val;
    return (int)parsed;
}

double obc_config_mod_float(const obc_server_cfg_t *cfg,
                             const char *module_name,
                             const char *key,
                             double      default_val)
{
    const obc_module_cfg_t *mod = find_module(cfg, module_name);
    if (!mod) return default_val;
    const char *v = find_kv(mod, key);
    if (!v) return default_val;

    char *end = NULL;
    errno = 0;
    double parsed = strtod(v, &end);
    if (end == v || *end != '\0' || errno == ERANGE)
        return default_val;
    if (isnan(parsed) || isinf(parsed))
        return default_val;
    return parsed;
}

bool obc_config_mod_bool(const obc_server_cfg_t *cfg,
                          const char *module_name,
                          const char *key,
                          bool        default_val)
{
    const obc_module_cfg_t *mod = find_module(cfg, module_name);
    if (!mod) return default_val;
    const char *v = find_kv(mod, key);
    if (!v) return default_val;

    /* TOML booleans are stored as "true"/"false"; "1"/"0" are accepted too.
     * Any other string is treated as invalid and falls back to default_val. */
    if (strcmp(v, "true") == 0 || strcmp(v, "1") == 0) return true;
    if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0) return false;
    return default_val;
}
