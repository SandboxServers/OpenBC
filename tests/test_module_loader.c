#include "test_util.h"
#include "openbc/module_loader.h"
#include "openbc/event_bus.h"
#include "openbc/config.h"

#include <string.h>

/*
 * test_module_loader.c -- unit tests for the module loader (A2 / issue #122).
 *
 * Links with: LIB_OBJ + module_loader.o + server_state.o
 * server_state.o provides all server globals (g_peers, g_registry, etc.).
 * Stub functions are provided for server_send.c symbols that module_loader.c
 * references through its wrapper functions.
 */

/* -------------------------------------------------------------------------
 * Stub server_send functions (not linked; module_loader wrappers reference them)
 * ---------------------------------------------------------------------- */

void bc_queue_reliable(int peer_slot, const u8 *payload, int payload_len)
{
    (void)peer_slot; (void)payload; (void)payload_len;
}

void bc_queue_unreliable(int peer_slot, const u8 *payload, int payload_len)
{
    (void)peer_slot; (void)payload; (void)payload_len;
}

void bc_send_to_all(const u8 *payload, int payload_len, bool reliable)
{
    (void)payload; (void)payload_len; (void)reliable;
}

void bc_relay_to_others(int sender_slot, const u8 *payload, int payload_len,
                        bool reliable)
{
    (void)sender_slot; (void)payload; (void)payload_len; (void)reliable;
}

/* -------------------------------------------------------------------------
 * Section 1: Path validation tests
 * ---------------------------------------------------------------------- */

TEST(path_validate_null_rejected)
{
    ASSERT_EQ_INT(-1, obc_module_path_validate(NULL));
}

TEST(path_validate_empty_rejected)
{
    ASSERT_EQ_INT(-1, obc_module_path_validate(""));
}

TEST(path_validate_traversal_prefix)
{
#ifdef _WIN32
    ASSERT_EQ_INT(-1, obc_module_path_validate("../evil.dll"));
#else
    ASSERT_EQ_INT(-1, obc_module_path_validate("../evil.so"));
#endif
}

TEST(path_validate_traversal_backslash)
{
#ifdef _WIN32
    ASSERT_EQ_INT(-1, obc_module_path_validate("..\\evil.dll"));
#else
    ASSERT_EQ_INT(-1, obc_module_path_validate("..\\evil.so"));
#endif
}

TEST(path_validate_traversal_mid)
{
#ifdef _WIN32
    ASSERT_EQ_INT(-1, obc_module_path_validate("foo/../bar.dll"));
#else
    ASSERT_EQ_INT(-1, obc_module_path_validate("foo/../bar.so"));
#endif
}

TEST(path_validate_traversal_end)
{
    /* Just ".." with no extension -- rejected for traversal */
    ASSERT_EQ_INT(-1, obc_module_path_validate("foo/.."));
}

TEST(path_validate_absolute_unix)
{
#ifdef _WIN32
    ASSERT_EQ_INT(-1, obc_module_path_validate("/usr/lib/evil.dll"));
#else
    ASSERT_EQ_INT(-1, obc_module_path_validate("/usr/lib/evil.so"));
#endif
}

TEST(path_validate_absolute_win_drive)
{
#ifdef _WIN32
    ASSERT_EQ_INT(-1, obc_module_path_validate("C:\\mods\\evil.dll"));
    ASSERT_EQ_INT(-1, obc_module_path_validate("D:/mods/evil.dll"));
#else
    ASSERT_EQ_INT(-1, obc_module_path_validate("C:\\mods\\evil.so"));
    ASSERT_EQ_INT(-1, obc_module_path_validate("D:/mods/evil.so"));
#endif
}

TEST(path_validate_wrong_extension)
{
#ifdef _WIN32
    /* .so is wrong on Windows */
    ASSERT_EQ_INT(-1, obc_module_path_validate("mymod.so"));
    ASSERT_EQ_INT(-1, obc_module_path_validate("mymod.exe"));
#else
    /* .dll is wrong on Linux */
    ASSERT_EQ_INT(-1, obc_module_path_validate("mymod.dll"));
    ASSERT_EQ_INT(-1, obc_module_path_validate("mymod.exe"));
#endif
}

TEST(path_validate_valid_simple)
{
#ifdef _WIN32
    ASSERT_EQ_INT(0, obc_module_path_validate("mymod.dll"));
#else
    ASSERT_EQ_INT(0, obc_module_path_validate("mymod.so"));
#endif
}

TEST(path_validate_valid_subdir)
{
#ifdef _WIN32
    ASSERT_EQ_INT(0, obc_module_path_validate("mods/mymod.dll"));
#else
    ASSERT_EQ_INT(0, obc_module_path_validate("mods/mymod.so"));
#endif
}

TEST(path_validate_dotdot_in_filename_ok)
{
    /* ".." inside a filename is not a traversal */
#ifdef _WIN32
    ASSERT_EQ_INT(0, obc_module_path_validate("my..mod.dll"));
#else
    ASSERT_EQ_INT(0, obc_module_path_validate("my..mod.so"));
#endif
}

/* -------------------------------------------------------------------------
 * Section 2: API table build tests
 * ---------------------------------------------------------------------- */

TEST(api_build_version)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT_EQ_INT(1, api.api_version);
}

TEST(api_build_event_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.event_subscribe   != NULL);
    ASSERT(api.event_unsubscribe != NULL);
    ASSERT(api.event_fire        != NULL);
}

TEST(api_build_config_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.config_string != NULL);
    ASSERT(api.config_int    != NULL);
    ASSERT(api.config_float  != NULL);
    ASSERT(api.config_bool   != NULL);
}

TEST(api_build_log_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.log_info  != NULL);
    ASSERT(api.log_warn  != NULL);
    ASSERT(api.log_debug != NULL);
    ASSERT(api.log_error != NULL);
}

TEST(api_build_peer_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.peer_count       != NULL);
    ASSERT(api.peer_max         != NULL);
    ASSERT(api.peer_slot_active != NULL);
    ASSERT(api.peer_name        != NULL);
    ASSERT(api.peer_team        != NULL);
}

TEST(api_build_ship_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.ship_get         != NULL);
    ASSERT(api.ship_hull        != NULL);
    ASSERT(api.ship_hull_max    != NULL);
    ASSERT(api.ship_alive       != NULL);
    ASSERT(api.ship_species     != NULL);
    ASSERT(api.subsystem_hp     != NULL);
    ASSERT(api.subsystem_hp_max != NULL);
    ASSERT(api.subsystem_count  != NULL);

    ASSERT(api.ship_apply_damage           != NULL);
    ASSERT(api.ship_apply_damage_at        != NULL);
    ASSERT(api.ship_apply_subsystem_damage != NULL);
    ASSERT(api.ship_kill                   != NULL);
    ASSERT(api.ship_respawn                != NULL);
    ASSERT(api.ship_set_position           != NULL);
    ASSERT(api.ship_set_orientation        != NULL);
}

TEST(api_build_score_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.score_add       != NULL);
    ASSERT(api.score_kills     != NULL);
    ASSERT(api.score_deaths    != NULL);
    ASSERT(api.score_points    != NULL);
    ASSERT(api.score_reset_all != NULL);
}

TEST(api_build_messaging_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.send_reliable    != NULL);
    ASSERT(api.send_unreliable  != NULL);
    ASSERT(api.send_to_all      != NULL);
    ASSERT(api.send_to_others   != NULL);
    ASSERT(api.relay_to_others  != NULL);
}

TEST(api_build_game_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.game_time        != NULL);
    ASSERT(api.game_map         != NULL);
    ASSERT(api.game_mode_id     != NULL);
    ASSERT(api.game_in_progress != NULL);
}

TEST(api_build_registry_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.ship_class_by_species != NULL);
    ASSERT(api.ship_class_count      != NULL);
    ASSERT(api.ship_class_by_index   != NULL);
}

TEST(api_build_shield_ptrs_non_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    ASSERT(api.ship_shield_hp     != NULL);
    ASSERT(api.ship_shield_hp_max != NULL);
}

TEST(api_build_timer_ptrs_null)
{
    obc_engine_api_t api;
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_module_api_build(&api, &cfg);

    /* No timer subsystem exists yet */
    ASSERT(api.timer_add    == NULL);
    ASSERT(api.timer_remove == NULL);
}

/* -------------------------------------------------------------------------
 * Section 3: Loader lifecycle tests
 * ---------------------------------------------------------------------- */

TEST(loader_zero_modules_succeeds)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    cfg.module_count = 0;

    obc_event_bus_init();

    obc_module_loader_t loader;
    int ret = obc_module_loader_init(&loader, &cfg);
    ASSERT_EQ_INT(0, ret);
    ASSERT_EQ_INT(0, loader.count);

    obc_module_loader_shutdown(&loader);
    obc_event_bus_shutdown();
}

TEST(loader_lua_only_skipped)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    cfg.module_count = 1;
    snprintf(cfg.modules[0].name, sizeof(cfg.modules[0].name), "luamod");
    cfg.modules[0].dll[0] = '\0';
    snprintf(cfg.modules[0].lua, sizeof(cfg.modules[0].lua), "mymod.lua");

    obc_event_bus_init();

    obc_module_loader_t loader;
    int ret = obc_module_loader_init(&loader, &cfg);
    ASSERT_EQ_INT(0, ret);
    ASSERT_EQ_INT(0, loader.count);

    obc_module_loader_shutdown(&loader);
    obc_event_bus_shutdown();
}

TEST(loader_invalid_path_fails)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    cfg.module_count = 1;
    snprintf(cfg.modules[0].name, sizeof(cfg.modules[0].name), "badmod");
    snprintf(cfg.modules[0].dll, sizeof(cfg.modules[0].dll),
             "../traversal.dll");
    cfg.modules[0].lua[0] = '\0';

    obc_event_bus_init();

    obc_module_loader_t loader;
    int ret = obc_module_loader_init(&loader, &cfg);
    ASSERT_EQ_INT(-1, ret);
    ASSERT_EQ_INT(0, loader.count);

    obc_event_bus_shutdown();
}

TEST(loader_missing_dll_fails)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    cfg.module_count = 1;
    snprintf(cfg.modules[0].name, sizeof(cfg.modules[0].name), "noexist");
#ifdef _WIN32
    snprintf(cfg.modules[0].dll, sizeof(cfg.modules[0].dll),
             "nonexistent_module.dll");
#else
    snprintf(cfg.modules[0].dll, sizeof(cfg.modules[0].dll),
             "nonexistent_module.so");
#endif
    cfg.modules[0].lua[0] = '\0';

    obc_event_bus_init();

    obc_module_loader_t loader;
    int ret = obc_module_loader_init(&loader, &cfg);
    ASSERT_EQ_INT(-1, ret);
    ASSERT_EQ_INT(0, loader.count);

    obc_event_bus_shutdown();
}

TEST(loader_shutdown_empty_safe)
{
    /* Shutting down a zero-initialized loader must not crash */
    obc_module_loader_t loader;
    memset(&loader, 0, sizeof(loader));
    obc_module_loader_shutdown(&loader);
    ASSERT_EQ_INT(0, loader.count);
}

TEST(loader_empty_dll_and_lua_fails)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    cfg.module_count = 1;
    snprintf(cfg.modules[0].name, sizeof(cfg.modules[0].name), "emptymod");
    cfg.modules[0].dll[0] = '\0';
    cfg.modules[0].lua[0] = '\0';

    obc_event_bus_init();

    obc_module_loader_t loader;
    int ret = obc_module_loader_init(&loader, &cfg);
    ASSERT_EQ_INT(-1, ret);
    ASSERT_EQ_INT(0, loader.count);

    obc_event_bus_shutdown();
}

/* -------------------------------------------------------------------------
 * Runner
 * ---------------------------------------------------------------------- */

int main(void)
{
    /* Path validation */
    RUN(path_validate_null_rejected);
    RUN(path_validate_empty_rejected);
    RUN(path_validate_traversal_prefix);
    RUN(path_validate_traversal_backslash);
    RUN(path_validate_traversal_mid);
    RUN(path_validate_traversal_end);
    RUN(path_validate_absolute_unix);
    RUN(path_validate_absolute_win_drive);
    RUN(path_validate_wrong_extension);
    RUN(path_validate_valid_simple);
    RUN(path_validate_valid_subdir);
    RUN(path_validate_dotdot_in_filename_ok);

    /* API table build */
    RUN(api_build_version);
    RUN(api_build_event_ptrs_non_null);
    RUN(api_build_config_ptrs_non_null);
    RUN(api_build_log_ptrs_non_null);
    RUN(api_build_peer_ptrs_non_null);
    RUN(api_build_ship_ptrs_non_null);
    RUN(api_build_score_ptrs_non_null);
    RUN(api_build_messaging_ptrs_non_null);
    RUN(api_build_game_ptrs_non_null);
    RUN(api_build_registry_ptrs_non_null);
    RUN(api_build_shield_ptrs_non_null);
    RUN(api_build_timer_ptrs_null);

    /* Loader lifecycle */
    RUN(loader_zero_modules_succeeds);
    RUN(loader_lua_only_skipped);
    RUN(loader_invalid_path_fails);
    RUN(loader_missing_dll_fails);
    RUN(loader_shutdown_empty_safe);
    RUN(loader_empty_dll_and_lua_fails);

    printf("%d/%d tests passed\n", test_pass, test_count);
    return test_fail > 0 ? 1 : 0;
}
