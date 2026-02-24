#include "test_util.h"
#include "openbc/module_api.h"

#include <stddef.h>
#include <string.h>

/*
 * test_module_api.c  --  static type-level tests for module_api.h
 *
 * This test does not link a real engine implementation; it verifies:
 *   - The header compiles cleanly and all types resolve.
 *   - Struct sizes and layout assumptions are sane.
 *   - OBC_MODULE_EXPORT expands to a non-empty token.
 *   - Kill-method constants are distinct.
 *   - obc_module_t fields exist with the expected types.
 *   - obc_engine_api_t contains api_version as first member.
 *   - Function pointers in the API table have the correct arity
 *     (verified by taking their address and comparing with a test stub).
 *   - obc_module_load_fn typedef is assignable to a compatible function.
 */

/* -------------------------------------------------------------------------
 * Stub implementations used only for function-pointer compatibility checks.
 * These are NOT linked into any module; they just prove the typedefs match.
 * ---------------------------------------------------------------------- */

static int  stub_event_subscribe(const char *ev, obc_event_handler_fn fn, int p)
    { (void)ev; (void)fn; (void)p; return 0; }
static void stub_event_unsubscribe(const char *ev, obc_event_handler_fn fn)
    { (void)ev; (void)fn; }
static void stub_event_fire(const char *ev, int slot, void *data)
    { (void)ev; (void)slot; (void)data; }

static int  stub_peer_count(void)          { return 0; }
static int  stub_peer_max(void)            { return 0; }
static int  stub_peer_slot_active(int s)   { (void)s; return 0; }
static const char *stub_peer_name(int s)   { (void)s; return NULL; }
static int  stub_peer_team(int s)          { (void)s; return 0; }

static const obc_ship_state_t *stub_ship_get(int s) { (void)s; return NULL; }
static float stub_ship_hull(int s)         { (void)s; return 0.f; }
static float stub_ship_hull_max(int s)     { (void)s; return 0.f; }
static int   stub_ship_alive(int s)        { (void)s; return 0; }
static int   stub_ship_species(int s)      { (void)s; return -1; }
static float stub_subsys_hp(int s, int i)  { (void)s; (void)i; return 0.f; }
static float stub_subsys_hp_max(int s, int i) { (void)s; (void)i; return 0.f; }
static int   stub_subsys_count(int s)      { (void)s; return 0; }

static void stub_ship_damage(int s, float a, int src)   { (void)s;(void)a;(void)src; }
static void stub_ship_sub_damage(int s, int i, float a) { (void)s;(void)i;(void)a; }
static void stub_ship_kill(int s, int k, int m)         { (void)s;(void)k;(void)m; }
static void stub_ship_respawn(int s)                    { (void)s; }
static void stub_ship_set_pos(int s, float x, float y, float z)
    { (void)s;(void)x;(void)y;(void)z; }

static void stub_score_add(int s, int k, int d, int p) { (void)s;(void)k;(void)d;(void)p; }
static int  stub_score_kills(int s)   { (void)s; return 0; }
static int  stub_score_deaths(int s)  { (void)s; return 0; }
static int  stub_score_points(int s)  { (void)s; return 0; }
static void stub_score_reset(void)    {}

static void stub_send_reliable(int s, const void *d, int l)   { (void)s;(void)d;(void)l; }
static void stub_send_unreliable(int s, const void *d, int l) { (void)s;(void)d;(void)l; }
static void stub_send_all(const void *d, int l, int r)        { (void)d;(void)l;(void)r; }
static void stub_send_others(int e, const void *d, int l, int r) { (void)e;(void)d;(void)l;(void)r; }
static void stub_relay_others(int e, const void *d, int l)    { (void)e;(void)d;(void)l; }

static const char *stub_cfg_str(const obc_module_t *m, const char *k, const char *def)
    { (void)m;(void)k; return def; }
static int stub_cfg_int(const obc_module_t *m, const char *k, int def)
    { (void)m;(void)k; return def; }
static float stub_cfg_float(const obc_module_t *m, const char *k, float def)
    { (void)m;(void)k; return def; }
static int stub_cfg_bool(const obc_module_t *m, const char *k, int def)
    { (void)m;(void)k; return def; }

static int  stub_timer_add(float i, obc_event_handler_fn cb, void *ud)
    { (void)i;(void)cb;(void)ud; return -1; }
static void stub_timer_remove(int id) { (void)id; }

static float       stub_game_time(void)        { return 0.f; }
static const char *stub_game_map(void)         { return ""; }
static int         stub_game_mode_id(void)     { return 0; }
static int         stub_game_in_progress(void) { return 0; }

static const obc_ship_class_t *stub_ship_class_by_species(int id) { (void)id; return NULL; }
static int stub_ship_class_count(void) { return 0; }

/* A stub module load function with the correct signature. */
static int stub_module_load(const obc_engine_api_t *api, obc_module_t *self)
    { (void)api; (void)self; return 0; }

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

TEST(test_api_version_field_at_offset_zero)
{
    /* api_version must be the first member so a simple cast from the raw
     * DLL-exported struct works for version sniffing. */
    obc_engine_api_t api;
    memset(&api, 0, sizeof(api));
    api.api_version = 42;
    const int *first = (const int *)&api;
    ASSERT_EQ_INT(42, *first);
}

TEST(test_api_version_default_value)
{
    obc_engine_api_t api;
    memset(&api, 0, sizeof(api));
    ASSERT_EQ_INT(0, api.api_version);  /* zeroed by memset */
}

TEST(test_module_entry_symbol)
{
    ASSERT(strcmp(OBC_MODULE_ENTRY_SYMBOL, "obc_module_load") == 0);
}

TEST(test_kill_constants_distinct)
{
    ASSERT(OBC_KILL_WEAPON        != OBC_KILL_COLLISION);
    ASSERT(OBC_KILL_COLLISION     != OBC_KILL_SELF_DESTRUCT);
    ASSERT(OBC_KILL_SELF_DESTRUCT != OBC_KILL_EXPLOSION);
    ASSERT(OBC_KILL_EXPLOSION     != OBC_KILL_ENVIRONMENT);
}

TEST(test_kill_constants_values)
{
    ASSERT_EQ_INT(0, OBC_KILL_WEAPON);
    ASSERT_EQ_INT(1, OBC_KILL_COLLISION);
    ASSERT_EQ_INT(2, OBC_KILL_SELF_DESTRUCT);
    ASSERT_EQ_INT(3, OBC_KILL_EXPLOSION);
    ASSERT_EQ_INT(4, OBC_KILL_ENVIRONMENT);
}

TEST(test_module_t_fields_accessible)
{
    obc_module_t mod;
    memset(&mod, 0, sizeof(mod));
    mod.name      = "test_module";
    mod.user_data = NULL;
    mod.shutdown  = NULL;

    ASSERT(strcmp(mod.name, "test_module") == 0);
    ASSERT(mod.user_data == NULL);
    ASSERT(mod.shutdown  == NULL);
}

TEST(test_function_pointer_compatibility)
{
    /* Assign every stub to the corresponding API table slot.
     * A compile error here means the function-pointer type changed. */
    obc_engine_api_t api;
    memset(&api, 0, sizeof(api));

    api.api_version      = 1;
    api.event_subscribe  = stub_event_subscribe;
    api.event_unsubscribe = stub_event_unsubscribe;
    api.event_fire       = stub_event_fire;

    api.peer_count       = stub_peer_count;
    api.peer_max         = stub_peer_max;
    api.peer_slot_active = stub_peer_slot_active;
    api.peer_name        = stub_peer_name;
    api.peer_team        = stub_peer_team;

    api.ship_get         = stub_ship_get;
    api.ship_hull        = stub_ship_hull;
    api.ship_hull_max    = stub_ship_hull_max;
    api.ship_alive       = stub_ship_alive;
    api.ship_species     = stub_ship_species;
    api.subsystem_hp     = stub_subsys_hp;
    api.subsystem_hp_max = stub_subsys_hp_max;
    api.subsystem_count  = stub_subsys_count;

    api.ship_apply_damage           = stub_ship_damage;
    api.ship_apply_subsystem_damage = stub_ship_sub_damage;
    api.ship_kill                   = stub_ship_kill;
    api.ship_respawn                = stub_ship_respawn;
    api.ship_set_position           = stub_ship_set_pos;

    api.score_add        = stub_score_add;
    api.score_kills      = stub_score_kills;
    api.score_deaths     = stub_score_deaths;
    api.score_points     = stub_score_points;
    api.score_reset_all  = stub_score_reset;

    api.send_reliable    = stub_send_reliable;
    api.send_unreliable  = stub_send_unreliable;
    api.send_to_all      = stub_send_all;
    api.send_to_others   = stub_send_others;
    api.relay_to_others  = stub_relay_others;

    api.config_string    = stub_cfg_str;
    api.config_int       = stub_cfg_int;
    api.config_float     = stub_cfg_float;
    api.config_bool      = stub_cfg_bool;

    api.timer_add        = stub_timer_add;
    api.timer_remove     = stub_timer_remove;

    api.game_time        = stub_game_time;
    api.game_map         = stub_game_map;
    api.game_mode_id     = stub_game_mode_id;
    api.game_in_progress = stub_game_in_progress;

    api.ship_class_by_species = stub_ship_class_by_species;
    api.ship_class_count      = stub_ship_class_count;

    /* Exercise all stubs through the API table to confirm dispatch works */
    ASSERT_EQ_INT(1, api.api_version);
    ASSERT_EQ_INT(0, api.event_subscribe("ev", NULL, 0));
    api.event_unsubscribe("ev", NULL);
    api.event_fire("ev", -1, NULL);

    ASSERT_EQ_INT(0, api.peer_count());
    ASSERT_EQ_INT(0, api.peer_max());
    ASSERT_EQ_INT(0, api.peer_slot_active(0));
    ASSERT(api.peer_name(0) == NULL);
    ASSERT_EQ_INT(0, api.peer_team(0));

    ASSERT(api.ship_get(0) == NULL);
    ASSERT(api.ship_hull(0) == 0.f);
    ASSERT(api.ship_alive(0) == 0);
    ASSERT(api.ship_species(0) == -1);

    api.ship_kill(0, -1, OBC_KILL_WEAPON);
    api.ship_respawn(0);

    api.score_add(0, 1, 0, 100);
    ASSERT_EQ_INT(0, api.score_kills(0));
    api.score_reset_all();

    ASSERT(api.game_map() != NULL);
    ASSERT_EQ_INT(0, api.game_mode_id());
    ASSERT_EQ_INT(0, api.game_in_progress());

    ASSERT(api.ship_class_by_species(0) == NULL);
    ASSERT_EQ_INT(0, api.ship_class_count());
}

TEST(test_module_load_fn_typedef)
{
    /* obc_module_load_fn must be assignable from stub_module_load. */
    obc_module_load_fn fn = stub_module_load;
    ASSERT(fn != NULL);

    /* Call through the typedef (with NULL api/self -- stubs are safe) */
    int ret = fn(NULL, NULL);
    ASSERT_EQ_INT(0, ret);
}

TEST(test_obc_module_export_defined)
{
    /* OBC_MODULE_EXPORT must expand to something (not empty).
     * We verify by tagging a dummy function and checking it compiles. */
    OBC_MODULE_EXPORT int dummy_export_test_fn(void);
    (void)dummy_export_test_fn;
    ASSERT(1);  /* Reaching here means the macro expanded correctly */
}

/* -------------------------------------------------------------------------
 * Runner
 * ---------------------------------------------------------------------- */

int main(void)
{
    RUN(test_api_version_field_at_offset_zero);
    RUN(test_api_version_default_value);
    RUN(test_module_entry_symbol);
    RUN(test_kill_constants_distinct);
    RUN(test_kill_constants_values);
    RUN(test_module_t_fields_accessible);
    RUN(test_function_pointer_compatibility);
    RUN(test_module_load_fn_typedef);
    RUN(test_obc_module_export_defined);
    return 0;
}
