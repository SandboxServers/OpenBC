#ifndef OPENBC_SERVER_STATE_H
#define OPENBC_SERVER_STATE_H

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/peer.h"
#include "openbc/manifest.h"
#include "openbc/master.h"
#include "openbc/ship_data.h"
#include "openbc/torpedo_tracker.h"
#include "openbc/gamespy.h"

#ifdef _WIN32
#  include <windows.h>
#endif

/* --- Session statistics types --- */

typedef struct {
    char name[32];
    u32  connect_time;      /* GetTickCount() when connected */
    u32  disconnect_time;   /* GetTickCount() when left (0 = still connected) */
} player_record_t;

typedef struct {
    u32  start_time;
    u32  total_connections;
    u32  peak_players;
    u32  boots_full;
    u32  boots_checksum;
    u32  disconnects;
    u32  timeouts;
    u32  gamespy_queries;
    u32  reliable_retransmits;
    u32  opcodes_recv[256];
    u32  opcodes_rejected[256];   /* unhandled or wrong-state opcodes */
    player_record_t players[32];
    int  player_count;
} bc_session_stats_t;

/* System lookup table entry */
typedef struct {
    const char *key;    /* System key: "Multi1", "Multi2", etc. */
    const char *name;   /* Display name: "Asteroids", etc. */
} bc_system_entry_t;

#define SYSTEM_TABLE_SIZE 10

/* --- Server globals --- */

extern bc_session_stats_t  g_stats;
extern volatile bool       g_running;
#ifdef _WIN32
extern HANDLE              g_shutdown_done;
#endif

extern bc_socket_t         g_socket;
extern bc_socket_t         g_query_socket;
extern bool                g_query_socket_open;
extern bc_peer_mgr_t       g_peers;
extern bc_server_info_t    g_info;

extern bc_game_registry_t  g_registry;
extern bool                g_registry_loaded;
extern bc_torpedo_mgr_t    g_torpedoes;

extern const bc_system_entry_t g_system_table[SYSTEM_TABLE_SIZE];

extern bool        g_collision_dmg;
extern bool        g_friendly_fire;
extern const char *g_map_name;
extern int         g_system_index;
extern int         g_max_players;
extern int         g_time_limit;
extern int         g_frag_limit;
extern f32         g_game_time;
extern f32         g_round_end_time;
extern bool        g_use_score_limit;
extern bool        g_team_mode;
extern bool        g_accept_new_players;

extern bool             g_game_ended;

#define BC_TEAM_NONE 0xFF

typedef struct {
    f32 shield_damage;
    f32 hull_damage;
} bc_damage_ledger_entry_t;

extern i32 g_player_scores[BC_MAX_PLAYERS];
extern i32 g_player_kills[BC_MAX_PLAYERS];
extern i32 g_player_deaths[BC_MAX_PLAYERS];
extern u8  g_player_teams[BC_MAX_PLAYERS];
extern i32 g_team_scores[2];
extern i32 g_team_kills[2];
extern bc_damage_ledger_entry_t
    g_damage_ledger[BC_MAX_PLAYERS][BC_MAX_PLAYERS];

extern bc_manifest_t    g_manifest;
extern bool             g_manifest_loaded;
extern bool             g_no_checksum;

extern bc_master_list_t g_masters;

#endif /* OPENBC_SERVER_STATE_H */
