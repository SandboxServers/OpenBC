#include "openbc/config.h"
#include "toml/toml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static void str_copy(char *dst, size_t dstsz, const char *src)
{
    if (!dst || dstsz == 0 || !src) return;
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

/*
 * Process a parsed TOML root table into cfg.
 * All fields present in root override the current cfg values; absent
 * fields are left untouched.
 */
static void process_toml(toml_table_t *root, obc_server_cfg_t *cfg)
{
    toml_value_t v;

    /* [server] ---------------------------------------------------------- */
    toml_table_t *server = toml_table_table(root, "server");
    if (server) {
        v = toml_table_int(server, "port");
        if (v.ok) cfg->port = (uint16_t)v.u.i;

        v = toml_table_int(server, "max_players");
        if (v.ok) cfg->max_players = (int)v.u.i;

        v = toml_table_string(server, "name");
        if (v.ok) { str_copy(cfg->name, sizeof(cfg->name), v.u.s); free(v.u.s); }

        v = toml_table_string(server, "log_level");
        if (v.ok) { str_copy(cfg->log_level, sizeof(cfg->log_level), v.u.s); free(v.u.s); }

        v = toml_table_string(server, "log_file");
        if (v.ok) { str_copy(cfg->log_file, sizeof(cfg->log_file), v.u.s); free(v.u.s); }
    }

    /* [game] ------------------------------------------------------------ */
    toml_table_t *game = toml_table_table(root, "game");
    if (game) {
        v = toml_table_string(game, "map");
        if (v.ok) { str_copy(cfg->map, sizeof(cfg->map), v.u.s); free(v.u.s); }

        v = toml_table_int(game, "system");
        if (v.ok) cfg->system = (int)v.u.i;

        v = toml_table_int(game, "time_limit");
        if (v.ok) cfg->time_limit = (int)v.u.i;

        v = toml_table_int(game, "frag_limit");
        if (v.ok) cfg->frag_limit = (int)v.u.i;

        v = toml_table_bool(game, "collision_damage");
        if (v.ok) cfg->collision_damage = v.u.b;

        v = toml_table_bool(game, "friendly_fire");
        if (v.ok) cfg->friendly_fire = v.u.b;

        v = toml_table_int(game, "difficulty");
        if (v.ok) cfg->difficulty = (int)v.u.i;

        v = toml_table_int(game, "respawn_time");
        if (v.ok) cfg->respawn_time = (int)v.u.i;

        v = toml_table_string(game, "mode_file");
        if (v.ok) { str_copy(cfg->mode_file, sizeof(cfg->mode_file), v.u.s); free(v.u.s); }
    }

    /* [data] ------------------------------------------------------------ */
    toml_table_t *data = toml_table_table(root, "data");
    if (data) {
        v = toml_table_string(data, "registry");
        if (v.ok) { str_copy(cfg->registry, sizeof(cfg->registry), v.u.s); free(v.u.s); }

        v = toml_table_string(data, "manifest");
        if (v.ok) { str_copy(cfg->manifest_path, sizeof(cfg->manifest_path), v.u.s); free(v.u.s); }

        toml_array_t *packs = toml_table_array(data, "mod_packs");
        if (packs) {
            int n = toml_array_len(packs);
            if (n > OBC_CFG_MOD_PACKS_MAX) n = OBC_CFG_MOD_PACKS_MAX;
            cfg->mod_pack_count = 0;
            for (int i = 0; i < n; i++) {
                toml_value_t sv = toml_array_string(packs, i);
                if (sv.ok) {
                    str_copy(cfg->mod_packs[cfg->mod_pack_count],
                             sizeof(cfg->mod_packs[0]), sv.u.s);
                    free(sv.u.s);
                    cfg->mod_pack_count++;
                }
            }
        }
    }

    /* [gamespy] --------------------------------------------------------- */
    toml_table_t *gamespy = toml_table_table(root, "gamespy");
    if (gamespy) {
        v = toml_table_bool(gamespy, "enabled");
        if (v.ok) cfg->gamespy_enabled = v.u.b;

        v = toml_table_bool(gamespy, "lan_discovery");
        if (v.ok) cfg->lan_discovery = v.u.b;

        toml_array_t *marr = toml_table_array(gamespy, "master");
        if (marr) {
            int n = toml_array_len(marr);
            if (n > OBC_CFG_MASTERS_MAX) n = OBC_CFG_MASTERS_MAX;
            cfg->master_count = 0;
            for (int i = 0; i < n; i++) {
                toml_value_t sv = toml_array_string(marr, i);
                if (sv.ok) {
                    str_copy(cfg->masters[cfg->master_count],
                             sizeof(cfg->masters[0]), sv.u.s);
                    free(sv.u.s);
                    cfg->master_count++;
                }
            }
        }
    }

    /* [master] ---------------------------------------------------------- */
    toml_table_t *master = toml_table_table(root, "master");
    if (master) {
        v = toml_table_int(master, "heartbeat_interval");
        if (v.ok) cfg->heartbeat_interval = (int)v.u.i;
    }

    /* [[modules]] ------------------------------------------------------- */
    toml_array_t *modules = toml_table_array(root, "modules");
    if (modules) {
        int n = toml_array_len(modules);
        if (n > OBC_CFG_MODULES_MAX) n = OBC_CFG_MODULES_MAX;
        cfg->module_count = 0;
        for (int i = 0; i < n; i++) {
            toml_table_t *mod = toml_array_table(modules, i);
            if (!mod) continue;

            obc_module_cfg_t *m = &cfg->modules[cfg->module_count];
            memset(m, 0, sizeof(*m));

            v = toml_table_string(mod, "name");
            if (v.ok) { str_copy(m->name, sizeof(m->name), v.u.s); free(v.u.s); }

            v = toml_table_string(mod, "dll");
            if (v.ok) { str_copy(m->dll,  sizeof(m->dll),  v.u.s); free(v.u.s); }

            v = toml_table_string(mod, "lua");
            if (v.ok) { str_copy(m->lua,  sizeof(m->lua),  v.u.s); free(v.u.s); }

            /* [modules.config] sub-table: store all values as strings */
            toml_table_t *mcfg = toml_table_table(mod, "config");
            if (mcfg) {
                int nkeys = toml_table_len(mcfg);
                for (int ki = 0; ki < nkeys && m->kv_count < OBC_CFG_MODCFG_MAX; ki++) {
                    int keylen;
                    const char *key = toml_table_key(mcfg, ki, &keylen);
                    if (!key) continue;

                    obc_mod_kv_t *kv = &m->kv[m->kv_count];
                    str_copy(kv->key, sizeof(kv->key), key);

                    /* Try each TOML type in order; convert to string for storage. */
                    toml_value_t sv = toml_table_string(mcfg, key);
                    if (sv.ok) {
                        str_copy(kv->val, sizeof(kv->val), sv.u.s);
                        free(sv.u.s);
                        m->kv_count++;
                        continue;
                    }
                    toml_value_t iv = toml_table_int(mcfg, key);
                    if (iv.ok) {
                        snprintf(kv->val, sizeof(kv->val), "%lld",
                                 (long long)iv.u.i);
                        m->kv_count++;
                        continue;
                    }
                    toml_value_t bv = toml_table_bool(mcfg, key);
                    if (bv.ok) {
                        str_copy(kv->val, sizeof(kv->val),
                                 bv.u.b ? "true" : "false");
                        m->kv_count++;
                        continue;
                    }
                    toml_value_t dv = toml_table_double(mcfg, key);
                    if (dv.ok) {
                        snprintf(kv->val, sizeof(kv->val), "%.17g", dv.u.d);
                        m->kv_count++;
                        continue;
                    }
                }
            }

            cfg->module_count++;
        }
    }
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
    return (int)strtol(v, NULL, 10);
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
    return strtod(v, NULL);
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
    return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
}
