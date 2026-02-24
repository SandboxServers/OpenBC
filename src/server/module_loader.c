/*
 * module_loader.c -- DLL module loading, engine API wiring, lifecycle management.
 *
 * Loads C DLL modules via LoadLibrary (Windows) or dlopen (POSIX),
 * resolves the obc_module_load entry point, calls it with the engine API
 * table, and manages shutdown in reverse order.
 */

#include "openbc/module_loader.h"
#include "openbc/event_bus.h"
#include "openbc/server_state.h"
#include "openbc/server_send.h"
#include "openbc/combat.h"
#include "openbc/log.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

/* =========================================================================
 * Section A: Platform DL helpers (static)
 * ========================================================================= */

static void *dl_open(const char *path)
{
#ifdef _WIN32
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

/*
 * dl_sym returns a function pointer via void*.  ISO C forbids direct casts
 * between function and object pointer types, but POSIX dlsym and Win32
 * GetProcAddress both return through object-pointer-typed APIs by convention.
 * We use memcpy to avoid the -Wpedantic diagnostic.
 */
static void *dl_sym(void *handle, const char *symbol)
{
#ifdef _WIN32
    FARPROC fp = GetProcAddress((HMODULE)handle, symbol);
    void *result;
    memcpy(&result, &fp, sizeof(result));
    return result;
#else
    return dlsym(handle, symbol);
#endif
}

static void dl_close(void *handle)
{
#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
}

static const char *dl_error(void)
{
#ifdef _WIN32
    static char buf[256];
    DWORD err = GetLastError();
    if (err == 0) return "unknown error";
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, buf, sizeof(buf), NULL);
    /* Strip trailing newline from FormatMessage output */
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
        buf[--len] = '\0';
    return buf;
#else
    const char *msg = dlerror();
    return msg ? msg : "unknown error";
#endif
}

/* =========================================================================
 * Section B: Path validation
 * ========================================================================= */

int obc_module_path_validate(const char *path)
{
    if (!path || path[0] == '\0') {
        LOG_ERROR("module", "Module path is empty");
        return -1;
    }

    /* Reject absolute paths */
    if (path[0] == '/' || path[0] == '\\') {
        LOG_ERROR("module", "Module path is absolute (starts with /): %s", path);
        return -1;
    }
    /* Windows drive letter: e.g. C: or D: */
    if (((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') {
        LOG_ERROR("module", "Module path is absolute (drive letter): %s", path);
        return -1;
    }

    /* Reject .. as a path component (traversal attack).
     * Must be preceded by start-of-string, '/', or '\' and
     * followed by end-of-string, '/', or '\'. */
    const char *p = path;
    while (*p) {
        if (p[0] == '.' && p[1] == '.') {
            /* Check what precedes .. */
            bool at_start = (p == path);
            bool after_sep = (p > path && (p[-1] == '/' || p[-1] == '\\'));
            /* Check what follows .. */
            bool at_end = (p[2] == '\0');
            bool before_sep = (p[2] == '/' || p[2] == '\\');

            if ((at_start || after_sep) && (at_end || before_sep)) {
                LOG_ERROR("module", "Module path contains traversal (..): %s",
                          path);
                return -1;
            }
        }
        p++;
    }

    /* Require correct extension */
    size_t len = strlen(path);
#ifdef _WIN32
    const char *required_ext = ".dll";
#else
    const char *required_ext = ".so";
#endif
    size_t ext_len = strlen(required_ext);
    if (len < ext_len + 1 ||
        strcmp(path + len - ext_len, required_ext) != 0) {
        LOG_ERROR("module", "Module path has wrong extension (need %s): %s",
                  required_ext, path);
        return -1;
    }

    return 0;
}

/* =========================================================================
 * Section C: API wrapper functions (static, file-scoped closures)
 *
 * These close over file-scoped statics s_api_self and s_cfg set during
 * obc_module_loader_init.
 * ========================================================================= */

static const obc_engine_api_t *s_api_self;
static const obc_server_cfg_t *s_cfg;

/* --- Event system --- */

static int wrap_event_subscribe(const char *event_name,
                                obc_event_handler_fn fn, int priority)
{
    return obc_event_subscribe(event_name, fn, priority);
}

static void wrap_event_unsubscribe(const char *event_name,
                                   obc_event_handler_fn fn)
{
    obc_event_unsubscribe(event_name, fn);
}

static obc_event_result_t wrap_event_fire(const char *event_name,
                                          int sender_slot, const void *data)
{
    return obc_event_fire(s_api_self, event_name, sender_slot, data);
}

/* --- Config --- */

static const char *wrap_config_string(const obc_module_t *self,
                                      const char *key, const char *def)
{
    return obc_config_mod_string(s_cfg, self->name, key, def);
}

static int wrap_config_int(const obc_module_t *self,
                           const char *key, int def)
{
    return obc_config_mod_int(s_cfg, self->name, key, def);
}

static float wrap_config_float(const obc_module_t *self,
                               const char *key, float def)
{
    /* obc_config_mod_float returns double; truncate to float */
    return (float)obc_config_mod_float(s_cfg, self->name, key, (double)def);
}

static int wrap_config_bool(const obc_module_t *self,
                            const char *key, int def)
{
    return obc_config_mod_bool(s_cfg, self->name, key, def != 0) ? 1 : 0;
}

/* --- Logging ---
 * bc_log is variadic; we can't forward va_list to it.
 * Format into a stack buffer, then pass as "%s". */

static void wrap_log_info(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bc_log(LOG_INFO, "module", "%s", buf);
}

static void wrap_log_warn(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bc_log(LOG_WARN, "module", "%s", buf);
}

static void wrap_log_debug(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bc_log(LOG_DEBUG, "module", "%s", buf);
}

static void wrap_log_error(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bc_log(LOG_ERROR, "module", "%s", buf);
}

/* --- Peer / Player --- */

static int wrap_peer_count(void)
{
    /* g_peers.count includes slot 0 (dedi); API reports human players only */
    return g_peers.count > 0 ? g_peers.count - 1 : 0;
}

static int wrap_peer_max(void)
{
    return g_max_players;
}

static int wrap_peer_slot_active(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0;
    return g_peers.peers[slot].state != PEER_EMPTY ? 1 : 0;
}

static const char *wrap_peer_name(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return NULL;
    if (g_peers.peers[slot].state == PEER_EMPTY) return NULL;
    return g_peers.peers[slot].name;
}

static int wrap_peer_team(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0;
    return g_player_teams[slot];
}

/* --- Ship State (Read) --- */

static const obc_ship_state_t *wrap_ship_get(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return NULL;
    if (!g_peers.peers[slot].has_ship) return NULL;
    return &g_peers.peers[slot].ship;
}

static float wrap_ship_hull(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0.f;
    if (!g_peers.peers[slot].has_ship) return 0.f;
    return g_peers.peers[slot].ship.hull_hp;
}

static float wrap_ship_hull_max(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0.f;
    if (!g_peers.peers[slot].has_ship) return 0.f;
    int ci = g_peers.peers[slot].class_index;
    if (ci < 0 || !g_registry_loaded) return 0.f;
    const bc_ship_class_t *cls = bc_registry_get_ship(&g_registry, ci);
    if (!cls) return 0.f;
    return cls->hull_hp;
}

static int wrap_ship_alive(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0;
    if (!g_peers.peers[slot].has_ship) return 0;
    return g_peers.peers[slot].ship.alive ? 1 : 0;
}

static int wrap_ship_species(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return -1;
    if (!g_peers.peers[slot].has_ship) return -1;
    int ci = g_peers.peers[slot].class_index;
    if (ci < 0 || !g_registry_loaded) return -1;
    const bc_ship_class_t *cls = bc_registry_get_ship(&g_registry, ci);
    if (!cls) return -1;
    return (int)cls->species_id;
}

static float wrap_subsystem_hp(int slot, int subsys_index)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0.f;
    if (!g_peers.peers[slot].has_ship) return 0.f;
    if (subsys_index < 0 || subsys_index >= BC_MAX_SUBSYSTEMS) return 0.f;
    return g_peers.peers[slot].ship.subsystem_hp[subsys_index];
}

static float wrap_subsystem_hp_max(int slot, int subsys_index)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0.f;
    if (!g_peers.peers[slot].has_ship) return 0.f;
    int ci = g_peers.peers[slot].class_index;
    if (ci < 0 || !g_registry_loaded) return 0.f;
    const bc_ship_class_t *cls = bc_registry_get_ship(&g_registry, ci);
    if (!cls) return 0.f;
    if (subsys_index < 0 || subsys_index >= cls->subsystem_count) return 0.f;
    return cls->subsystems[subsys_index].max_condition;
}

static int wrap_subsystem_count(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0;
    if (!g_peers.peers[slot].has_ship) return 0;
    int ci = g_peers.peers[slot].class_index;
    if (ci < 0 || !g_registry_loaded) return 0;
    const bc_ship_class_t *cls = bc_registry_get_ship(&g_registry, ci);
    if (!cls) return 0;
    return cls->subsystem_count;
}

/* --- Ship State (Write) --- */

static void wrap_ship_apply_damage(int slot, float amount, int source_slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return;
    if (!g_peers.peers[slot].has_ship) return;
    int ci = g_peers.peers[slot].class_index;
    if (ci < 0 || !g_registry_loaded) return;
    const bc_ship_class_t *cls = bc_registry_get_ship(&g_registry, ci);
    if (!cls) return;
    (void)source_slot; /* attribution tracked externally */
    bc_vec3_t dir = {0.f, 0.f, 1.f};
    bc_combat_apply_damage(&g_peers.peers[slot].ship, cls, amount, 0.f,
                           dir, true, 1.0f);
}

static void wrap_ship_apply_damage_at(int slot, float amount,
                                      float dir_x, float dir_y, float dir_z,
                                      float radius, int source_slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return;
    if (!g_peers.peers[slot].has_ship) return;
    int ci = g_peers.peers[slot].class_index;
    if (ci < 0 || !g_registry_loaded) return;
    const bc_ship_class_t *cls = bc_registry_get_ship(&g_registry, ci);
    if (!cls) return;
    (void)source_slot;
    bc_vec3_t dir = {dir_x, dir_y, dir_z};
    bc_combat_apply_damage(&g_peers.peers[slot].ship, cls, amount, radius,
                           dir, false, 1.0f);
}

static void wrap_ship_apply_subsystem_damage(int slot, int subsys_index,
                                             float amount)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return;
    if (!g_peers.peers[slot].has_ship) return;
    if (subsys_index < 0 || subsys_index >= BC_MAX_SUBSYSTEMS) return;
    float hp = g_peers.peers[slot].ship.subsystem_hp[subsys_index];
    hp -= amount;
    if (hp < 0.f) hp = 0.f;
    g_peers.peers[slot].ship.subsystem_hp[subsys_index] = hp;
}

static void wrap_ship_kill(int slot, int killer_slot, int method)
{
    (void)slot; (void)killer_slot; (void)method;
    LOG_WARN("module", "ship_kill: not yet available");
}

static void wrap_ship_respawn(int slot)
{
    (void)slot;
    LOG_WARN("module", "ship_respawn: not yet available");
}

static void wrap_ship_set_position(int slot, float x, float y, float z)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return;
    if (!g_peers.peers[slot].has_ship) return;
    g_peers.peers[slot].ship.pos.x = x;
    g_peers.peers[slot].ship.pos.y = y;
    g_peers.peers[slot].ship.pos.z = z;
}

static void wrap_ship_set_orientation(int slot, float fx, float fy, float fz,
                                      float ux, float uy, float uz)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return;
    if (!g_peers.peers[slot].has_ship) return;
    g_peers.peers[slot].ship.fwd.x = fx;
    g_peers.peers[slot].ship.fwd.y = fy;
    g_peers.peers[slot].ship.fwd.z = fz;
    g_peers.peers[slot].ship.up.x = ux;
    g_peers.peers[slot].ship.up.y = uy;
    g_peers.peers[slot].ship.up.z = uz;
}

/* --- Scoring --- */

static void wrap_score_add(int slot, int kills, int deaths, int points)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return;
    g_player_kills[slot] += kills;
    g_player_deaths[slot] += deaths;
    g_player_scores[slot] += points;
}

static int wrap_score_kills(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0;
    return g_player_kills[slot];
}

static int wrap_score_deaths(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0;
    return g_player_deaths[slot];
}

static int wrap_score_points(int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0;
    return g_player_scores[slot];
}

static void wrap_score_reset_all(void)
{
    memset(g_player_kills, 0, sizeof(g_player_kills));
    memset(g_player_deaths, 0, sizeof(g_player_deaths));
    memset(g_player_scores, 0, sizeof(g_player_scores));
}

/* --- Messaging --- */

static void wrap_send_reliable(int to_slot, const void *data, int len)
{
    if (to_slot < 1 || to_slot >= BC_MAX_PLAYERS) return;
    bc_queue_reliable(to_slot, (const u8 *)data, len);
}

static void wrap_send_unreliable(int to_slot, const void *data, int len)
{
    if (to_slot < 1 || to_slot >= BC_MAX_PLAYERS) return;
    bc_queue_unreliable(to_slot, (const u8 *)data, len);
}

static void wrap_send_to_all(const void *data, int len, int reliable)
{
    bc_send_to_all((const u8 *)data, len, reliable != 0);
}

static void wrap_send_to_others(int except_slot, const void *data, int len,
                                int reliable)
{
    bc_relay_to_others(except_slot, (const u8 *)data, len, reliable != 0);
}

static void wrap_relay_to_others(int except_slot, const void *original_data,
                                 int len)
{
    bc_relay_to_others(except_slot, (const u8 *)original_data, len, false);
}

/* --- Game state --- */

static float wrap_game_time(void)
{
    return g_game_time;
}

static const char *wrap_game_map(void)
{
    return g_map_name ? g_map_name : "";
}

static int wrap_game_mode_id(void)
{
    return 0; /* No game mode system yet */
}

static int wrap_game_in_progress(void)
{
    return g_game_ended ? 0 : 1;
}

/* --- Data Registry --- */

static const obc_ship_class_t *wrap_ship_class_by_species(int species_id)
{
    if (!g_registry_loaded) return NULL;
    return bc_registry_find_ship(&g_registry, (u16)species_id);
}

static int wrap_ship_class_count(void)
{
    if (!g_registry_loaded) return 0;
    return g_registry.ship_count;
}

static const obc_ship_class_t *wrap_ship_class_by_index(int index)
{
    if (!g_registry_loaded) return NULL;
    return bc_registry_get_ship(&g_registry, index);
}

/* --- Shield State --- */

static float wrap_ship_shield_hp(int slot, int facing)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0.f;
    if (!g_peers.peers[slot].has_ship) return 0.f;
    if (facing < 0 || facing >= BC_MAX_SHIELD_FACINGS) return 0.f;
    return g_peers.peers[slot].ship.shield_hp[facing];
}

static float wrap_ship_shield_hp_max(int slot, int facing)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0.f;
    if (!g_peers.peers[slot].has_ship) return 0.f;
    if (facing < 0 || facing >= BC_MAX_SHIELD_FACINGS) return 0.f;
    int ci = g_peers.peers[slot].class_index;
    if (ci < 0 || !g_registry_loaded) return 0.f;
    const bc_ship_class_t *cls = bc_registry_get_ship(&g_registry, ci);
    if (!cls) return 0.f;
    return cls->shield_hp[facing];
}

/* =========================================================================
 * Section D: obc_module_api_build
 * ========================================================================= */

void obc_module_api_build(obc_engine_api_t *api, const obc_server_cfg_t *cfg)
{
    memset(api, 0, sizeof(*api));
    s_cfg = cfg;

    api->api_version = 1;

    /* Event system */
    api->event_subscribe   = wrap_event_subscribe;
    api->event_unsubscribe = wrap_event_unsubscribe;
    api->event_fire        = wrap_event_fire;

    /* Peer / Player */
    api->peer_count       = wrap_peer_count;
    api->peer_max         = wrap_peer_max;
    api->peer_slot_active = wrap_peer_slot_active;
    api->peer_name        = wrap_peer_name;
    api->peer_team        = wrap_peer_team;

    /* Ship State (Read) */
    api->ship_get         = wrap_ship_get;
    api->ship_hull        = wrap_ship_hull;
    api->ship_hull_max    = wrap_ship_hull_max;
    api->ship_alive       = wrap_ship_alive;
    api->ship_species     = wrap_ship_species;
    api->subsystem_hp     = wrap_subsystem_hp;
    api->subsystem_hp_max = wrap_subsystem_hp_max;
    api->subsystem_count  = wrap_subsystem_count;

    /* Ship State (Write) */
    api->ship_apply_damage           = wrap_ship_apply_damage;
    api->ship_apply_damage_at        = wrap_ship_apply_damage_at;
    api->ship_apply_subsystem_damage = wrap_ship_apply_subsystem_damage;
    api->ship_kill                   = wrap_ship_kill;
    api->ship_respawn                = wrap_ship_respawn;
    api->ship_set_position           = wrap_ship_set_position;
    api->ship_set_orientation        = wrap_ship_set_orientation;

    /* Scoring */
    api->score_add        = wrap_score_add;
    api->score_kills      = wrap_score_kills;
    api->score_deaths     = wrap_score_deaths;
    api->score_points     = wrap_score_points;
    api->score_reset_all  = wrap_score_reset_all;

    /* Messaging */
    api->send_reliable    = wrap_send_reliable;
    api->send_unreliable  = wrap_send_unreliable;
    api->send_to_all      = wrap_send_to_all;
    api->send_to_others   = wrap_send_to_others;
    api->relay_to_others  = wrap_relay_to_others;

    /* Config */
    api->config_string    = wrap_config_string;
    api->config_int       = wrap_config_int;
    api->config_float     = wrap_config_float;
    api->config_bool      = wrap_config_bool;

    /* Timers -- NULL (no timer subsystem yet) */
    api->timer_add        = NULL;
    api->timer_remove     = NULL;

    /* Logging */
    api->log_info         = wrap_log_info;
    api->log_warn         = wrap_log_warn;
    api->log_debug        = wrap_log_debug;
    api->log_error        = wrap_log_error;

    /* Game State */
    api->game_time        = wrap_game_time;
    api->game_map         = wrap_game_map;
    api->game_mode_id     = wrap_game_mode_id;
    api->game_in_progress = wrap_game_in_progress;

    /* Data Registry */
    api->ship_class_by_species = wrap_ship_class_by_species;
    api->ship_class_count      = wrap_ship_class_count;
    api->ship_class_by_index   = wrap_ship_class_by_index;

    /* Shield State */
    api->ship_shield_hp        = wrap_ship_shield_hp;
    api->ship_shield_hp_max    = wrap_ship_shield_hp_max;
}

/* =========================================================================
 * Section E: obc_module_loader_init
 * ========================================================================= */

int obc_module_loader_init(obc_module_loader_t *loader,
                           const obc_server_cfg_t *cfg)
{
    memset(loader, 0, sizeof(*loader));

    /* Build the engine API table */
    obc_module_api_build(&loader->api, cfg);
    s_api_self = &loader->api;

    for (int i = 0; i < cfg->module_count; i++) {
        const obc_module_cfg_t *mcfg = &cfg->modules[i];

        /* Skip Lua-only modules */
        if (mcfg->lua[0] != '\0' && mcfg->dll[0] == '\0') {
            LOG_INFO("module", "Skipping Lua module: %s", mcfg->name);
            continue;
        }

        /* Reject entries with both dll and lua empty */
        if (mcfg->dll[0] == '\0' && mcfg->lua[0] == '\0') {
            LOG_ERROR("module", "Module '%s' has no dll or lua path",
                      mcfg->name);
            goto fail;
        }

        /* Validate and load the DLL */
        if (obc_module_path_validate(mcfg->dll) != 0) {
            goto fail;
        }

        LOG_INFO("module", "Loading module '%s' from %s", mcfg->name,
                 mcfg->dll);

        void *handle = dl_open(mcfg->dll);
        if (!handle) {
            LOG_ERROR("module", "Failed to load '%s': %s", mcfg->dll,
                      dl_error());
            goto fail;
        }

        obc_module_load_fn load_fn;
        {
            void *sym = dl_sym(handle, OBC_MODULE_ENTRY_SYMBOL);
            memcpy(&load_fn, &sym, sizeof(load_fn));
        }
        if (!load_fn) {
            LOG_ERROR("module", "Module '%s' missing entry point '%s': %s",
                      mcfg->name, OBC_MODULE_ENTRY_SYMBOL, dl_error());
            dl_close(handle);
            goto fail;
        }

        /* Set up the loaded module entry */
        obc_loaded_module_t *lm = &loader->modules[loader->count];
        memset(lm, 0, sizeof(*lm));
        lm->dl_handle = handle;
        snprintf(lm->dll_path, sizeof(lm->dll_path), "%s", mcfg->dll);
        lm->module.name = mcfg->name;

        /* Call the module's load function */
        int ret = load_fn(&loader->api, &lm->module);
        if (ret != 0) {
            LOG_ERROR("module", "Module '%s' obc_module_load returned %d",
                      mcfg->name, ret);
            dl_close(handle);
            memset(lm, 0, sizeof(*lm));
            goto fail;
        }

        loader->count++;
        LOG_INFO("module", "Module '%s' loaded successfully", mcfg->name);
    }

    if (loader->count > 0) {
        LOG_INFO("module", "%d module(s) loaded", loader->count);
    }
    return 0;

fail:
    obc_module_loader_shutdown(loader);
    return -1;
}

/* =========================================================================
 * Section F: obc_module_loader_shutdown
 * ========================================================================= */

void obc_module_loader_shutdown(obc_module_loader_t *loader)
{
    /* Shut down in reverse order */
    for (int i = loader->count - 1; i >= 0; i--) {
        obc_loaded_module_t *lm = &loader->modules[i];

        if (lm->module.shutdown) {
            LOG_INFO("module", "Shutting down module '%s'",
                     lm->module.name ? lm->module.name : "(unknown)");
            lm->module.shutdown(&loader->api, &lm->module);
        }

        if (lm->dl_handle) {
            dl_close(lm->dl_handle);
            lm->dl_handle = NULL;
        }
    }

    loader->count = 0;
}
