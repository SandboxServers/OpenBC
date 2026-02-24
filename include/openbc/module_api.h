#ifndef OPENBC_MODULE_API_H
#define OPENBC_MODULE_API_H

/*
 * OpenBC Engine API  --  module_api.h
 *
 * Every module (C DLL or future Lua script) receives a pointer to
 * obc_engine_api_t when it is loaded, and the same pointer is passed
 * into every event handler.  Modules must not cache raw function pointers;
 * call through the api table every time.
 *
 * API versioning:
 *   - api_version is currently 1.
 *   - New function pointers are ALWAYS appended at the end of the struct.
 *   - Existing pointers are never moved or removed.
 *   - A module should check api_version >= MIN_REQUIRED at load time.
 *
 * This header is intentionally standalone: it only includes the event bus
 * (for obc_event_handler_fn / obc_event_ctx_t) and the two public ship
 * headers that define the opaque data types exposed through the API.
 */

#include "openbc/event_bus.h"
#include "openbc/ship_state.h"
#include "openbc/ship_data.h"

/* -------------------------------------------------------------------------
 * Cross-platform DLL export macro.
 * Module entry points (obc_module_load) must be tagged with this.
 * ---------------------------------------------------------------------- */

#ifdef _WIN32
#  define OBC_MODULE_EXPORT __declspec(dllexport)
#else
#  define OBC_MODULE_EXPORT __attribute__((visibility("default")))
#endif

/* -------------------------------------------------------------------------
 * obc_ship_state_t / obc_ship_class_t  --  public API name aliases.
 *
 * The internal engine uses bc_ship_state_t / bc_ship_class_t.  These
 * typedefs give modules a stable obc_-prefixed name that documents "you
 * received this through the API table" without introducing a distinct type.
 * ---------------------------------------------------------------------- */

typedef bc_ship_state_t obc_ship_state_t;
typedef bc_ship_class_t obc_ship_class_t;

/* -------------------------------------------------------------------------
 * obc_module_t  --  per-module handle.
 *
 * The engine fills name before calling obc_module_load; the module owns
 * user_data and shutdown.  The same pointer is passed to every API call
 * that is module-specific (e.g. config_string).
 * ---------------------------------------------------------------------- */

typedef struct obc_module {
    const char *name;   /* Module name from [[modules]] TOML; engine-owned */
    void       *user_data; /* Module-private data; set however the module wishes */

    /*
     * Optional shutdown callback.  Called in reverse-load order when the
     * server shuts down or the module is unloaded.  May be NULL.
     */
    void (*shutdown)(const struct obc_engine_api *api, struct obc_module *self);
} obc_module_t;

/* -------------------------------------------------------------------------
 * obc_engine_api_t  --  the complete engine interface.
 *
 * APPEND ONLY: add new function pointers at the bottom.  Never reorder,
 * rename, or remove existing pointers -- that would break binary compat.
 * ---------------------------------------------------------------------- */

typedef struct obc_engine_api {
    /* ------------------------------------------------------------------ */
    /* Version.  Modules must verify api_version >= their minimum.         */
    /* ------------------------------------------------------------------ */
    int api_version; /* currently 1 */

    /* ------------------------------------------------------------------ */
    /* Event System                                                         */
    /* ------------------------------------------------------------------ */

    /*
     * Subscribe fn to event_name at the given priority (0=highest).
     * Returns 0 on success, -1 if capacity is exceeded.
     * Safe to call from within a handler (deferred until fire completes).
     */
    int  (*event_subscribe)(const char *event_name,
                             obc_event_handler_fn fn,
                             int priority);

    /*
     * Remove fn from event_name.  Safe to call from within a handler.
     * No-op if not found.
     */
    void (*event_unsubscribe)(const char *event_name,
                               obc_event_handler_fn fn);

    /*
     * Fire event_name.  All subscribers run in priority order.
     * sender_slot is the player slot that caused the event, or -1 for
     * engine/module-initiated events.
     * data is a typed payload; cast based on event_name.
     *
     * Returns an obc_event_result_t so the caller can observe whether any
     * handler cancelled the event or suppressed network relay.
     *
     * Note: the standalone obc_event_fire() also takes an api pointer as
     * its first argument; the wrapper closes over it so modules do not
     * need to pass api back.
     */
    obc_event_result_t (*event_fire)(const char *event_name, int sender_slot,
                                      const void *data);

    /* ------------------------------------------------------------------ */
    /* Peer / Player                                                         */
    /* ------------------------------------------------------------------ */

    /* Number of currently connected players (excludes empty slots). */
    int  (*peer_count)(void);

    /* Server's maximum player capacity. */
    int  (*peer_max)(void);

    /* Returns 1 if slot is active, 0 otherwise. */
    int  (*peer_slot_active)(int slot);

    /* Returns the player's name string, or NULL if slot is inactive. */
    const char *(*peer_name)(int slot);

    /* Returns the player's team index (0 = FFA / no team). */
    int  (*peer_team)(int slot);

    /* ------------------------------------------------------------------ */
    /* Ship State (Read)                                                    */
    /* ------------------------------------------------------------------ */

    /*
     * Read-only pointer to the full ship state for slot.
     * Returns NULL if the slot has no ship.
     * Do NOT store across handler calls -- may be invalidated.
     */
    const obc_ship_state_t *(*ship_get)(int slot);

    /* Current hull HP for slot (0.0 if no ship). */
    float (*ship_hull)(int slot);

    /* Maximum hull HP for slot (0.0 if no ship). */
    float (*ship_hull_max)(int slot);

    /* Returns 1 if ship exists and hull > 0, else 0. */
    int   (*ship_alive)(int slot);

    /* Returns the ship's species ID, or -1 if no ship. */
    int   (*ship_species)(int slot);

    /* Current HP for subsystem subsys_index on slot (0.0 if invalid). */
    float (*subsystem_hp)(int slot, int subsys_index);

    /* Maximum HP for subsystem subsys_index on slot (0.0 if invalid). */
    float (*subsystem_hp_max)(int slot, int subsys_index);

    /* Number of subsystems on ship at slot (0 if no ship). */
    int   (*subsystem_count)(int slot);

    /* ------------------------------------------------------------------ */
    /* Ship State (Write)                                                   */
    /* ------------------------------------------------------------------ */

    /*
     * Apply environmental / directionless damage to hull.  Damage is
     * spread equally across all shield facings (area-effect).
     * source_slot identifies the attacker for kill attribution; pass -1
     * for environmental damage.
     */
    void (*ship_apply_damage)(int slot, float amount, int source_slot);

    /*
     * Apply directed damage at a specific impact direction.  The engine
     * uses dir to select the shield facing and spatially target subsystems.
     * radius controls the subsystem search area (0.0 = hull only).
     */
    void (*ship_apply_damage_at)(int slot, float amount,
                                   float dir_x, float dir_y, float dir_z,
                                   float radius, int source_slot);

    /* Apply damage to a specific subsystem. HP floored at 0. */
    void (*ship_apply_subsystem_damage)(int slot, int subsys_index, float amount);

    /*
     * Immediately kill a ship.  Fires the "ship_killed" event.
     * method: KILL_WEAPON=0, KILL_COLLISION=1, KILL_SELF_DESTRUCT=2,
     *         KILL_EXPLOSION=3, KILL_ENVIRONMENT=4.
     */
    void (*ship_kill)(int slot, int killer_slot, int method);

    /* Respawn a dead ship.  Restores HP, fires "ship_respawned". */
    void (*ship_respawn)(int slot);

    /* Teleport the ship to (x, y, z).  No-op if slot has no ship. */
    void (*ship_set_position)(int slot, float x, float y, float z);

    /*
     * Set the ship's orientation via forward and up vectors.
     * Both vectors should be unit-length.  No-op if slot has no ship.
     */
    void (*ship_set_orientation)(int slot, float fx, float fy, float fz,
                                  float ux, float uy, float uz);

    /* ------------------------------------------------------------------ */
    /* Scoring                                                              */
    /* ------------------------------------------------------------------ */

    /*
     * Add deltas to a player's score (all values may be negative).
     * Fires "score_changed".
     */
    void (*score_add)(int slot, int kills, int deaths, int points);

    /* Read current score values. */
    int  (*score_kills)(int slot);
    int  (*score_deaths)(int slot);
    int  (*score_points)(int slot);

    /* Reset all player scores to zero (used on game restart). */
    void (*score_reset_all)(void);

    /* ------------------------------------------------------------------ */
    /* Messaging                                                            */
    /* ------------------------------------------------------------------ */

    /* Send a pre-built wire message to a single player. */
    void (*send_reliable)(int to_slot, const void *data, int len);
    void (*send_unreliable)(int to_slot, const void *data, int len);

    /* Broadcast to all connected players. reliable=1 for ACK'd delivery. */
    void (*send_to_all)(const void *data, int len, int reliable);

    /* Send to all players except one. */
    void (*send_to_others)(int except_slot, const void *data, int len,
                            int reliable);

    /* Relay an incoming message as-is to all players except the sender. */
    void (*relay_to_others)(int except_slot, const void *original_data,
                             int len);

    /* ------------------------------------------------------------------ */
    /* Per-module Config (reads from [modules.config] TOML sub-table)      */
    /* ------------------------------------------------------------------ */

    /* Returns def if the key is absent. String pointer valid until server exit. */
    const char *(*config_string)(const obc_module_t *self, const char *key,
                                  const char *def);
    int         (*config_int)   (const obc_module_t *self, const char *key,
                                  int def);
    float       (*config_float) (const obc_module_t *self, const char *key,
                                  float def);
    /* Returns def (as int 0/1) if absent. */
    int         (*config_bool)  (const obc_module_t *self, const char *key,
                                  int def);

    /* ------------------------------------------------------------------ */
    /* Timers                                                               */
    /* ------------------------------------------------------------------ */

    /*
     * Register a periodic callback.  The callback fires with a synthetic
     * obc_event_ctx_t where event_data = user_data.  Returns a timer ID,
     * or -1 on failure.
     */
    int  (*timer_add)(float interval_sec, obc_event_handler_fn callback,
                      void *user_data);

    /* Cancel a timer.  No-op for invalid IDs. */
    void (*timer_remove)(int timer_id);

    /* ------------------------------------------------------------------ */
    /* Logging                                                              */
    /* ------------------------------------------------------------------ */

    /* Printf-style logging.  Module name is prepended automatically. */
    void (*log_info) (const char *fmt, ...);
    void (*log_warn) (const char *fmt, ...);
    void (*log_debug)(const char *fmt, ...);
    void (*log_error)(const char *fmt, ...);

    /* ------------------------------------------------------------------ */
    /* Game State                                                           */
    /* ------------------------------------------------------------------ */

    /* Elapsed game time in seconds since the current round started. */
    float       (*game_time)(void);

    /* Current map name string (e.g. "Multiplayer.Episode.Mission1.Mission1"). */
    const char *(*game_map)(void);

    /*
     * Returns a numeric game-mode ID: 0 = none/default, higher values
     * correspond to loaded TOML mode files (A16+).
     */
    int         (*game_mode_id)(void);

    /* Returns 1 if a round is active, 0 if in lobby or post-game. */
    int         (*game_in_progress)(void);

    /* ------------------------------------------------------------------ */
    /* Data Registry (read-only ship class data)                           */
    /* ------------------------------------------------------------------ */

    /* Look up ship class by species ID.  Returns NULL if not in registry. */
    const obc_ship_class_t *(*ship_class_by_species)(int species_id);

    /* Total number of ship classes in the registry. */
    int (*ship_class_count)(void);

    /* Look up ship class by zero-based index.  Returns NULL if out of range. */
    const obc_ship_class_t *(*ship_class_by_index)(int index);

    /* ------------------------------------------------------------------ */
    /* Shield State                                                         */
    /* ------------------------------------------------------------------ */

    /* Current shield HP for a facing (0-5).  Returns 0.0 if invalid. */
    float (*ship_shield_hp)(int slot, int facing);

    /* Maximum shield HP for a facing (0-5).  Returns 0.0 if invalid. */
    float (*ship_shield_hp_max)(int slot, int facing);

    /*
     * --- APPEND NEW POINTERS BELOW THIS LINE ---
     * Incrementing api_version when adding pointers is not required for
     * backward compat (old modules ignore unknown trailing fields), but
     * is recommended when adding a batch of new functionality.
     */

} obc_engine_api_t;

/* -------------------------------------------------------------------------
 * Module entry point.
 *
 * Every C DLL module must export this symbol with OBC_MODULE_EXPORT.
 *
 *   OBC_MODULE_EXPORT int obc_module_load(const obc_engine_api_t *api,
 *                                          obc_module_t           *self);
 *
 * Called once when the module is loaded.  The module should:
 *   1. Check api->api_version >= the minimum it requires.
 *   2. Subscribe to events via api->event_subscribe(...).
 *   3. Optionally set self->user_data and self->shutdown.
 *   4. Return 0 on success, -1 to abort loading (server will log and exit).
 *
 * The engine passes the same api pointer to every event handler; modules
 * must not cache the pointer themselves (use the one from the handler args).
 * ---------------------------------------------------------------------- */

typedef int (*obc_module_load_fn)(const obc_engine_api_t *api,
                                   obc_module_t           *self);

#define OBC_MODULE_ENTRY_SYMBOL "obc_module_load"

/* Kill method constants for ship_kill(). */
#define OBC_KILL_WEAPON       0
#define OBC_KILL_COLLISION    1
#define OBC_KILL_SELF_DESTRUCT 2
#define OBC_KILL_EXPLOSION    3
#define OBC_KILL_ENVIRONMENT  4

#endif /* OPENBC_MODULE_API_H */
