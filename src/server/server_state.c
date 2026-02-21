#include "openbc/server_state.h"

/* --- Session statistics --- */

bc_session_stats_t g_stats;

/* --- Server state --- */

volatile bool g_running = true;
#ifdef _WIN32
HANDLE g_shutdown_done;  /* Signaled after main thread completes cleanup */
#endif

bc_socket_t    g_socket;       /* Game port (default 22101) */
bc_socket_t    g_query_socket; /* LAN query port (6500) */
bool           g_query_socket_open = false;
bc_peer_mgr_t  g_peers;
bc_server_info_t g_info;

/* Ship data registry (Phase E: server-authoritative damage) */
bc_game_registry_t g_registry;
bool               g_registry_loaded = false;
bc_torpedo_mgr_t   g_torpedoes;

/* System lookup table: index 1-9 maps to SpeciesToSystem key + display name.
 * Keys come from Multiplayer/SpeciesToSystem.py (clean room doc Section 4.2).
 * Display names come from the BC multiplayer system selector (observable).
 * Multi1 = Asteroids confirmed.  Other mappings are alphabetical best-guess
 * and may need correction from live testing. */
const bc_system_entry_t g_system_table[SYSTEM_TABLE_SIZE] = {
    [0] = { NULL,       NULL },            /* unused (1-based indexing) */
    [1] = { "Multi1",   "Asteroids" },     /* confirmed */
    [2] = { "Multi2",   "Cloudy" },
    [3] = { "Multi3",   "Planetorama" },
    [4] = { "Multi4",   "Showers" },
    [5] = { "Multi5",   "Space" },
    [6] = { "Multi6",   "StarSystem" },
    [7] = { "Multi7",   "Sunny" },
    [8] = { "Albirea",  "Albirea" },       /* campaign map */
    [9] = { "Poseidon", "Poseidon" },      /* campaign map */
};

/* Game settings */
bool        g_collision_dmg = true;
bool        g_friendly_fire = false;
const char *g_map_name = "Multiplayer.Episode.Mission1.Mission1";
int         g_system_index = 1;   /* Star system 1-9 (SpeciesToSystem) */
int         g_max_players = BC_MAX_PLAYERS;  /* Total slots incl. dedi */
int         g_time_limit = -1;    /* Minutes, -1 = no limit */
int         g_frag_limit = -1;    /* Kills, -1 = no limit */
f32         g_game_time = 0.0f;

/* Win condition */
bool g_game_ended = false;

/* Manifest / checksum validation */
bc_manifest_t g_manifest;
bool          g_manifest_loaded = false;
bool          g_no_checksum = false;  /* auto-set when no manifest */

/* Master servers */
bc_master_list_t g_masters;
