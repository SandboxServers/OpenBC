#ifndef OPENBC_CONFIG_H
#define OPENBC_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* Maximum counts for array fields in obc_server_cfg_t. */
#define OBC_CFG_MODULES_MAX    16
#define OBC_CFG_MASTERS_MAX    16
#define OBC_CFG_MOD_PACKS_MAX   8
#define OBC_CFG_MODCFG_MAX     32

/*
 * A single key=value pair in a module's [modules.config] sub-table.
 * All values are stored as strings regardless of their TOML type.
 */
typedef struct {
    char key[64];
    char val[256];
} obc_mod_kv_t;

/*
 * Configuration for one [[modules]] entry in server.toml.
 * Exactly one of dll[] or lua[] will be non-empty for a valid module.
 */
typedef struct {
    char        name[64];
    /* SECURITY: dll/lua paths are admin-controlled in server.toml today.
     * When module loading is added, enforce allowlisted base directories and
     * reject traversal sequences (e.g. "../"). */
    char        dll[256];
    char        lua[256];
    obc_mod_kv_t kv[OBC_CFG_MODCFG_MAX];
    int          kv_count;
} obc_module_cfg_t;

/*
 * Complete server configuration parsed from server.toml.
 * All string fields use fixed-size arrays — no heap allocation.
 *
 * Layering:  obc_config_defaults() → obc_config_load() → CLI args
 * Fields absent from the TOML file keep their default values.
 */
typedef struct {
    /* [server] */
    uint16_t port;
    int      max_players;
    char     name[64];        /* Capped at GameSpy hostname field size */
    char     log_level[16];   /* quiet|error|warn|info|debug|trace */
    char     log_file[256];   /* empty = auto-generate */

    /* [game] */
    char map[64];             /* Capped at GameSpy missionscript field size */
    int  system;              /* Star system index 1-9 */
    int  time_limit;          /* Minutes; -1 = no limit */
    int  frag_limit;          /* Kill count; -1 = no limit */
    bool collision_damage;
    bool friendly_fire;
    int  difficulty;          /* 0=Easy, 1=Normal, 2=Hard */
    int  respawn_time;        /* Seconds */
    char mode_file[256];      /* Optional game-mode TOML path */

    /* [data] */
    char registry[256];       /* Ship data directory; empty = auto-detect */
    char manifest_path[256];  /* Hash manifest JSON; empty = auto-detect */
    char mod_packs[OBC_CFG_MOD_PACKS_MAX][256];
    int  mod_pack_count;

    /* [gamespy] */
    bool gamespy_enabled;
    bool lan_discovery;
    char masters[OBC_CFG_MASTERS_MAX][256];  /* host:port strings */
    int  master_count;

    /* [master] */
    int heartbeat_interval;   /* Seconds */

    /* [[modules]] */
    obc_module_cfg_t modules[OBC_CFG_MODULES_MAX];
    int              module_count;
} obc_server_cfg_t;

/*
 * Fill cfg with hardcoded defaults matching stock server.toml.
 * Always call this before obc_config_load() or obc_config_load_str().
 */
void obc_config_defaults(obc_server_cfg_t *cfg);

/*
 * Load a TOML file at path into cfg.  Fields present in the file override
 * current values; absent fields are left untouched.
 *
 * Returns true on success.
 * Returns false (silently) if the file cannot be opened — the file is
 * optional; a missing server.toml is not an error.
 * Returns false (with stderr message) if TOML parsing fails.
 */
bool obc_config_load(const char *path, obc_server_cfg_t *cfg);

/*
 * Parse TOML from a NUL-terminated string into cfg.
 * Useful for unit tests and embedded configuration.
 * Fields present in src override cfg; absent fields are left untouched.
 *
 * Returns true on success, false on parse failure.
 */
bool obc_config_load_str(const char *src, obc_server_cfg_t *cfg);

/*
 * Release resources held by cfg.  Currently a no-op (all strings are
 * fixed-size arrays).  Reserved for future dynamically-allocated fields.
 */
void obc_config_free(obc_server_cfg_t *cfg);

/*
 * Per-module config accessors.
 * All return default_val if the module or key is not found.
 * obc_config_mod_string() returns a pointer into cfg — do not free it.
 */
const char *obc_config_mod_string(const obc_server_cfg_t *cfg,
                                   const char *module_name,
                                   const char *key,
                                   const char *default_val);
int         obc_config_mod_int   (const obc_server_cfg_t *cfg,
                                   const char *module_name,
                                   const char *key,
                                   int         default_val);
double      obc_config_mod_float (const obc_server_cfg_t *cfg,
                                   const char *module_name,
                                   const char *key,
                                   double      default_val);
bool        obc_config_mod_bool  (const obc_server_cfg_t *cfg,
                                   const char *module_name,
                                   const char *key,
                                   bool        default_val);

#endif /* OPENBC_CONFIG_H */
