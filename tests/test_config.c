#include "test_util.h"
#include "openbc/config.h"

#include <string.h>
#include <math.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static const char *FULL_TOML =
    "[server]\n"
    "port        = 7777\n"
    "max_players = 4\n"
    "name        = \"TestServer\"\n"
    "log_level   = \"debug\"\n"
    "log_file    = \"/tmp/test.log\"\n"
    "\n"
    "[game]\n"
    "map              = \"Multiplayer.Episode.Mission2.Mission2\"\n"
    "system           = 3\n"
    "time_limit       = 15\n"
    "frag_limit       = 20\n"
    "collision_damage = false\n"
    "friendly_fire    = true\n"
    "difficulty       = 2\n"
    "respawn_time     = 5\n"
    "mode_file        = \"modes/deathmatch.toml\"\n"
    "\n"
    "[data]\n"
    "registry  = \"data/vanilla-1.1/\"\n"
    "manifest  = \"manifests/vanilla-1.1.json\"\n"
    "mod_packs = [\"mods/mymod/data/\"]\n"
    "\n"
    "[gamespy]\n"
    "enabled       = false\n"
    "lan_discovery = false\n"
    "master        = [\"master.example.com:28900\"]\n"
    "\n"
    "[master]\n"
    "heartbeat_interval = 30\n"
    "\n"
    "[[modules]]\n"
    "name = \"combat\"\n"
    "dll  = \"modules/combat.dll\"\n"
    "[modules.config]\n"
    "collision_damage   = true\n"
    "friendly_fire      = false\n"
    "collision_cooldown = 0.5\n"
    "\n"
    "[[modules]]\n"
    "name = \"scoring\"\n"
    "dll  = \"modules/scoring.dll\"\n"
    "[modules.config]\n"
    "kill_points   = 200\n"
    "death_penalty = -10\n"
    "label         = \"FFA Scoring\"\n";

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

TEST(test_defaults)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    ASSERT_EQ_INT(22101, (int)cfg.port);
    ASSERT_EQ_INT(6,     cfg.max_players);
    ASSERT(strcmp(cfg.name,      "OpenBC Server") == 0);
    ASSERT(strcmp(cfg.log_level, "info") == 0);
    ASSERT(cfg.log_file[0] == '\0');

    ASSERT(strcmp(cfg.map, "Multiplayer.Episode.Mission1.Mission1") == 0);
    ASSERT_EQ_INT(1,  cfg.system);
    ASSERT_EQ_INT(-1, cfg.time_limit);
    ASSERT_EQ_INT(-1, cfg.frag_limit);
    ASSERT(cfg.collision_damage == true);
    ASSERT(cfg.friendly_fire    == false);
    ASSERT_EQ_INT(1,  cfg.difficulty);
    ASSERT_EQ_INT(10, cfg.respawn_time);

    ASSERT(cfg.registry[0]       == '\0');
    ASSERT(cfg.manifest_path[0]  == '\0');
    ASSERT_EQ_INT(0, cfg.mod_pack_count);

    ASSERT(cfg.gamespy_enabled == true);
    ASSERT(cfg.lan_discovery   == true);
    ASSERT_EQ_INT(0, cfg.master_count);
    ASSERT_EQ_INT(60, cfg.heartbeat_interval);

    ASSERT_EQ_INT(0, cfg.module_count);
}

TEST(test_load_str_server_section)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    const char *toml =
        "[server]\n"
        "port        = 9000\n"
        "max_players = 8\n"
        "name        = \"MyServer\"\n"
        "log_level   = \"trace\"\n"
        "log_file    = \"myserver.log\"\n";

    ASSERT(obc_config_load_str(toml, &cfg) == true);

    ASSERT_EQ_INT(9000, (int)cfg.port);
    ASSERT_EQ_INT(8,    cfg.max_players);
    ASSERT(strcmp(cfg.name,      "MyServer")    == 0);
    ASSERT(strcmp(cfg.log_level, "trace")       == 0);
    ASSERT(strcmp(cfg.log_file,  "myserver.log") == 0);

    /* Game defaults untouched */
    ASSERT(strcmp(cfg.map, "Multiplayer.Episode.Mission1.Mission1") == 0);
    ASSERT_EQ_INT(-1, cfg.time_limit);
}

TEST(test_load_str_game_section)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    const char *toml =
        "[game]\n"
        "map              = \"Multiplayer.Episode.Mission2.Mission2\"\n"
        "system           = 4\n"
        "time_limit       = 10\n"
        "frag_limit       = 15\n"
        "collision_damage = false\n"
        "friendly_fire    = true\n"
        "difficulty       = 0\n"
        "respawn_time     = 3\n"
        "mode_file        = \"modes/tdm.toml\"\n";

    ASSERT(obc_config_load_str(toml, &cfg) == true);

    ASSERT(strcmp(cfg.map, "Multiplayer.Episode.Mission2.Mission2") == 0);
    ASSERT_EQ_INT(4,  cfg.system);
    ASSERT_EQ_INT(10, cfg.time_limit);
    ASSERT_EQ_INT(15, cfg.frag_limit);
    ASSERT(cfg.collision_damage == false);
    ASSERT(cfg.friendly_fire    == true);
    ASSERT_EQ_INT(0, cfg.difficulty);
    ASSERT_EQ_INT(3, cfg.respawn_time);
    ASSERT(strcmp(cfg.mode_file, "modes/tdm.toml") == 0);

    /* Server defaults untouched */
    ASSERT_EQ_INT(22101, (int)cfg.port);
}

TEST(test_load_str_int_range_validation)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    const char *toml =
        "[server]\n"
        "port = 70000\n"
        "\n"
        "[game]\n"
        "system = 0\n"
        "difficulty = 99\n"
        "respawn_time = -5\n"
        "\n"
        "[master]\n"
        "heartbeat_interval = 0\n";

    ASSERT(obc_config_load_str(toml, &cfg) == true);

    /* Invalid values must be rejected and defaults preserved. */
    ASSERT_EQ_INT(22101, (int)cfg.port);
    ASSERT_EQ_INT(1, cfg.system);        /* system=0 rejected (range 1..9) */
    ASSERT_EQ_INT(1, cfg.difficulty);
    ASSERT_EQ_INT(10, cfg.respawn_time);
    ASSERT_EQ_INT(60, cfg.heartbeat_interval);
}

TEST(test_load_str_int_range_valid_boundaries)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    const char *toml =
        "[server]\n"
        "port = 65535\n"
        "\n"
        "[game]\n"
        "system = 9\n"
        "difficulty = 0\n"
        "respawn_time = 3600\n"
        "\n"
        "[master]\n"
        "heartbeat_interval = 10\n";

    ASSERT(obc_config_load_str(toml, &cfg) == true);
    ASSERT_EQ_INT(65535, (int)cfg.port);
    ASSERT_EQ_INT(9, cfg.system);
    ASSERT_EQ_INT(0, cfg.difficulty);
    ASSERT_EQ_INT(3600, cfg.respawn_time);
    ASSERT_EQ_INT(10, cfg.heartbeat_interval);
}

TEST(test_load_str_data_section)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    const char *toml =
        "[data]\n"
        "registry  = \"data/vanilla-1.1/\"\n"
        "manifest  = \"manifests/vanilla-1.1.json\"\n"
        "mod_packs = [\"mods/pack1/\", \"mods/pack2/\"]\n";

    ASSERT(obc_config_load_str(toml, &cfg) == true);

    ASSERT(strcmp(cfg.registry,      "data/vanilla-1.1/") == 0);
    ASSERT(strcmp(cfg.manifest_path, "manifests/vanilla-1.1.json") == 0);
    ASSERT_EQ_INT(2, cfg.mod_pack_count);
    ASSERT(strcmp(cfg.mod_packs[0], "mods/pack1/") == 0);
    ASSERT(strcmp(cfg.mod_packs[1], "mods/pack2/") == 0);
}

TEST(test_load_str_gamespy_section)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    const char *toml =
        "[gamespy]\n"
        "enabled       = false\n"
        "lan_discovery = false\n"
        "master        = [\"master.example.com:28900\", \"backup.example.com:28900\"]\n"
        "\n"
        "[master]\n"
        "heartbeat_interval = 120\n";

    ASSERT(obc_config_load_str(toml, &cfg) == true);

    ASSERT(cfg.gamespy_enabled == false);
    ASSERT(cfg.lan_discovery   == false);
    ASSERT_EQ_INT(2, cfg.master_count);
    ASSERT(strcmp(cfg.masters[0], "master.example.com:28900") == 0);
    ASSERT(strcmp(cfg.masters[1], "backup.example.com:28900") == 0);
    ASSERT_EQ_INT(120, cfg.heartbeat_interval);
}

TEST(test_load_str_modules)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    const char *toml =
        "[[modules]]\n"
        "name = \"combat\"\n"
        "dll  = \"modules/combat.dll\"\n"
        "[modules.config]\n"
        "collision_damage   = true\n"
        "collision_cooldown = 0.5\n"
        "\n"
        "[[modules]]\n"
        "name = \"helper\"\n"
        "lua  = \"mods/helper.lua\"\n";

    ASSERT(obc_config_load_str(toml, &cfg) == true);

    ASSERT_EQ_INT(2, cfg.module_count);

    ASSERT(strcmp(cfg.modules[0].name, "combat") == 0);
    ASSERT(strcmp(cfg.modules[0].dll,  "modules/combat.dll") == 0);
    ASSERT(cfg.modules[0].lua[0] == '\0');
    ASSERT_EQ_INT(2, cfg.modules[0].kv_count);

    ASSERT(strcmp(cfg.modules[1].name, "helper") == 0);
    ASSERT(strcmp(cfg.modules[1].lua,  "mods/helper.lua") == 0);
    ASSERT(cfg.modules[1].dll[0] == '\0');
    ASSERT_EQ_INT(0, cfg.modules[1].kv_count);
}

TEST(test_load_str_absent_fields_unchanged)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    /* Only override server.port; everything else should stay default */
    ASSERT(obc_config_load_str("[server]\nport = 1234\n", &cfg) == true);

    ASSERT_EQ_INT(1234, (int)cfg.port);
    ASSERT_EQ_INT(6, cfg.max_players);      /* default unchanged */
    ASSERT_EQ_INT(-1, cfg.time_limit);      /* default unchanged */
    ASSERT(cfg.collision_damage == true);   /* default unchanged */
    ASSERT_EQ_INT(0, cfg.module_count);     /* default unchanged */
}

TEST(test_load_nonexistent_returns_false)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    /* Non-existent file must return false without crashing or modifying cfg */
    bool ok = obc_config_load("/this/path/does/not/exist.toml", &cfg);
    ASSERT(ok == false);

    /* Defaults untouched */
    ASSERT_EQ_INT(22101, (int)cfg.port);
    ASSERT_EQ_INT(6,     cfg.max_players);
}

TEST(test_load_str_invalid_toml_returns_false)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    bool ok = obc_config_load_str("[[[ this is not valid TOML !!!", &cfg);
    ASSERT(ok == false);

    /* Defaults untouched */
    ASSERT_EQ_INT(22101, (int)cfg.port);
}

TEST(test_mod_string_found)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    ASSERT(obc_config_load_str(FULL_TOML, &cfg) == true);

    const char *v = obc_config_mod_string(&cfg, "scoring", "label",
                                           "default-label");
    ASSERT(strcmp(v, "FFA Scoring") == 0);
}

TEST(test_mod_string_missing_key_returns_default)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    ASSERT(obc_config_load_str(FULL_TOML, &cfg) == true);

    const char *v = obc_config_mod_string(&cfg, "scoring", "nonexistent_key",
                                           "fallback");
    ASSERT(strcmp(v, "fallback") == 0);
}

TEST(test_mod_string_missing_module_returns_default)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    const char *v = obc_config_mod_string(&cfg, "no_such_module", "key",
                                           "fallback");
    ASSERT(strcmp(v, "fallback") == 0);
}

TEST(test_mod_int)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    ASSERT(obc_config_load_str(FULL_TOML, &cfg) == true);

    ASSERT_EQ_INT(200, obc_config_mod_int(&cfg, "scoring", "kill_points", 0));
    ASSERT_EQ_INT(-10, obc_config_mod_int(&cfg, "scoring", "death_penalty", 0));
    /* Missing key returns default */
    ASSERT_EQ_INT(99,  obc_config_mod_int(&cfg, "scoring", "no_such", 99));
    /* Missing module returns default */
    ASSERT_EQ_INT(42,  obc_config_mod_int(&cfg, "no_mod", "key", 42));
}

TEST(test_mod_float)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    ASSERT(obc_config_load_str(FULL_TOML, &cfg) == true);

    double v = obc_config_mod_float(&cfg, "combat", "collision_cooldown", 9.9);
    /* 0.5 stored as "0.5" or "5.00000000000000000e-01"; strtod must parse it */
    ASSERT(v > 0.49 && v < 0.51);

    /* Missing key */
    double def = obc_config_mod_float(&cfg, "combat", "no_such", 3.14);
    ASSERT(def > 3.13 && def < 3.15);
}

TEST(test_mod_bool)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    ASSERT(obc_config_load_str(FULL_TOML, &cfg) == true);

    ASSERT(obc_config_mod_bool(&cfg, "combat", "collision_damage", false) == true);
    ASSERT(obc_config_mod_bool(&cfg, "combat", "friendly_fire",    true)  == false);
    /* Missing key returns default */
    ASSERT(obc_config_mod_bool(&cfg, "combat", "no_such", true)  == true);
    ASSERT(obc_config_mod_bool(&cfg, "combat", "no_such", false) == false);
    /* Missing module returns default */
    ASSERT(obc_config_mod_bool(&cfg, "no_mod", "key", true) == true);
}

TEST(test_mod_parse_validation)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);

    const char *toml =
        "[[modules]]\n"
        "name = \"parse\"\n"
        "[modules.config]\n"
        "int_ok = 7\n"
        "int_bad = \"abc\"\n"
        "int_trailing = \"7abc\"\n"
        "int_overflow = \"999999999999999999999\"\n"
        "float_ok = 0.5\n"
        "float_bad = \"abc\"\n"
        "float_trailing = \"0.5abc\"\n"
        "float_inf = \"inf\"\n"
        "float_nan = \"nan\"\n"
        "bool_true = true\n"
        "bool_false = false\n"
        "bool_one = 1\n"
        "bool_zero = \"0\"\n"
        "bool_bad = \"banana\"\n";

    ASSERT(obc_config_load_str(toml, &cfg) == true);

    ASSERT_EQ_INT(7, obc_config_mod_int(&cfg, "parse", "int_ok", 99));
    ASSERT_EQ_INT(99, obc_config_mod_int(&cfg, "parse", "int_bad", 99));
    ASSERT_EQ_INT(88, obc_config_mod_int(&cfg, "parse", "int_trailing", 88));
    ASSERT_EQ_INT(77, obc_config_mod_int(&cfg, "parse", "int_overflow", 77));

    ASSERT(obc_config_mod_float(&cfg, "parse", "float_ok", 9.9) > 0.49);
    ASSERT(obc_config_mod_float(&cfg, "parse", "float_ok", 9.9) < 0.51);
    ASSERT(obc_config_mod_float(&cfg, "parse", "float_bad", 3.14) > 3.13);
    ASSERT(obc_config_mod_float(&cfg, "parse", "float_bad", 3.14) < 3.15);
    ASSERT(obc_config_mod_float(&cfg, "parse", "float_trailing", 2.25) > 2.24);
    ASSERT(obc_config_mod_float(&cfg, "parse", "float_trailing", 2.25) < 2.26);
    ASSERT(obc_config_mod_float(&cfg, "parse", "float_inf", 1.5) > 1.49);
    ASSERT(obc_config_mod_float(&cfg, "parse", "float_inf", 1.5) < 1.51);
    ASSERT(obc_config_mod_float(&cfg, "parse", "float_nan", 2.5) > 2.49);
    ASSERT(obc_config_mod_float(&cfg, "parse", "float_nan", 2.5) < 2.51);

    ASSERT(obc_config_mod_bool(&cfg, "parse", "bool_true", false) == true);
    ASSERT(obc_config_mod_bool(&cfg, "parse", "bool_false", true) == false);
    ASSERT(obc_config_mod_bool(&cfg, "parse", "bool_one", false) == true);
    ASSERT(obc_config_mod_bool(&cfg, "parse", "bool_zero", true) == false);
    ASSERT(obc_config_mod_bool(&cfg, "parse", "bool_bad", true) == true);
    ASSERT(obc_config_mod_bool(&cfg, "parse", "bool_bad", false) == false);
}

TEST(test_config_free_noop)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    obc_config_free(&cfg);  /* Must not crash */
}

TEST(test_full_parse)
{
    obc_server_cfg_t cfg;
    obc_config_defaults(&cfg);
    ASSERT(obc_config_load_str(FULL_TOML, &cfg) == true);

    /* [server] */
    ASSERT_EQ_INT(7777, (int)cfg.port);
    ASSERT_EQ_INT(4,    cfg.max_players);
    ASSERT(strcmp(cfg.name,      "TestServer")     == 0);
    ASSERT(strcmp(cfg.log_level, "debug")          == 0);
    ASSERT(strcmp(cfg.log_file,  "/tmp/test.log")  == 0);

    /* [game] */
    ASSERT(strcmp(cfg.map, "Multiplayer.Episode.Mission2.Mission2") == 0);
    ASSERT_EQ_INT(3,  cfg.system);
    ASSERT_EQ_INT(15, cfg.time_limit);
    ASSERT_EQ_INT(20, cfg.frag_limit);
    ASSERT(cfg.collision_damage == false);
    ASSERT(cfg.friendly_fire    == true);
    ASSERT_EQ_INT(2, cfg.difficulty);
    ASSERT_EQ_INT(5, cfg.respawn_time);
    ASSERT(strcmp(cfg.mode_file, "modes/deathmatch.toml") == 0);

    /* [data] */
    ASSERT(strcmp(cfg.registry,      "data/vanilla-1.1/")          == 0);
    ASSERT(strcmp(cfg.manifest_path, "manifests/vanilla-1.1.json") == 0);
    ASSERT_EQ_INT(1, cfg.mod_pack_count);
    ASSERT(strcmp(cfg.mod_packs[0], "mods/mymod/data/") == 0);

    /* [gamespy] */
    ASSERT(cfg.gamespy_enabled == false);
    ASSERT(cfg.lan_discovery   == false);
    ASSERT_EQ_INT(1, cfg.master_count);
    ASSERT(strcmp(cfg.masters[0], "master.example.com:28900") == 0);

    /* [master] */
    ASSERT_EQ_INT(30, cfg.heartbeat_interval);

    /* [[modules]] */
    ASSERT_EQ_INT(2, cfg.module_count);
    ASSERT(strcmp(cfg.modules[0].name, "combat")  == 0);
    ASSERT(strcmp(cfg.modules[1].name, "scoring") == 0);
    ASSERT_EQ_INT(3, cfg.modules[0].kv_count);  /* collision_damage, friendly_fire, collision_cooldown */
    ASSERT_EQ_INT(3, cfg.modules[1].kv_count);  /* kill_points, death_penalty, label */
}

/* -------------------------------------------------------------------------
 * Runner
 * ---------------------------------------------------------------------- */

int main(void)
{
    RUN(test_defaults);
    RUN(test_load_str_server_section);
    RUN(test_load_str_game_section);
    RUN(test_load_str_int_range_validation);
    RUN(test_load_str_int_range_valid_boundaries);
    RUN(test_load_str_data_section);
    RUN(test_load_str_gamespy_section);
    RUN(test_load_str_modules);
    RUN(test_load_str_absent_fields_unchanged);
    RUN(test_load_nonexistent_returns_false);
    RUN(test_load_str_invalid_toml_returns_false);
    RUN(test_mod_string_found);
    RUN(test_mod_string_missing_key_returns_default);
    RUN(test_mod_string_missing_module_returns_default);
    RUN(test_mod_int);
    RUN(test_mod_float);
    RUN(test_mod_bool);
    RUN(test_mod_parse_validation);
    RUN(test_config_free_noop);
    RUN(test_full_parse);

    return 0;
}
