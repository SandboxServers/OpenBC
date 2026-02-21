#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>

#include "openbc/server_state.h"
#include "openbc/server_send.h"
#include "openbc/server_handshake.h"
#include "openbc/server_dispatch.h"
#include "openbc/server_stats.h"
#include "openbc/transport.h"
#include "openbc/cipher.h"
#include "openbc/gamespy.h"
#include "openbc/manifest.h"
#include "openbc/reliable.h"
#include "openbc/master.h"
#include "openbc/ship_state.h"
#include "openbc/ship_power.h"
#include "openbc/combat.h"
#include "openbc/torpedo_tracker.h"
#include "openbc/game_builders.h"
#include "openbc/log.h"

#ifdef _WIN32
#  include <windows.h>  /* For Sleep(), GetTickCount() */
#else
#  include <unistd.h>   /* For usleep() */
#  include <time.h>     /* For time(), localtime() */
#  include <dirent.h>   /* For opendir(), readdir() */
#  include <sys/stat.h> /* For stat(), S_ISDIR() */
#endif

/* --- Signal handler --- */

#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        /* Console stays open -- main thread will run cleanup normally */
        g_running = false;
        return TRUE;
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        /* Console is closing -- Windows will force-kill us after handler returns.
         * Signal main thread to stop, then wait for it to finish cleanup so
         * sockets are closed and the process exits cleanly. */
        g_running = false;
        WaitForSingleObject(g_shutdown_done, 5000);
        return TRUE;
    default:
        return FALSE;
    }
}
#else
static void posix_signal_handler(int sig)
{
    (void)sig;
    g_running = false;
}
#endif

/* --- Main --- */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "Options:\n"
        "  -p <port>          Listen port (default: 22101)\n"
        "  -n <name>          Server name (default: \"OpenBC Server\")\n"
        "  -m <mode>          Game mode (default: \"Multiplayer.Episode.Mission1.Mission1\")\n"
        "  --system <n>       Star system index 1-9 (default: 1)\n"
        "  --max <n>          Max players (default: 6)\n"
        "  --time-limit <n>   Time limit in minutes (default: none)\n"
        "  --frag-limit <n>   Frag/kill limit (default: none)\n"
        "  --collision        Enable collision damage (default)\n"
        "  --no-collision     Disable collision damage\n"
        "  --friendly-fire    Enable friendly fire\n"
        "  --no-friendly-fire Disable friendly fire (default)\n"
        "  --data <path>      Ship data registry: JSON file or versioned directory\n"
        "                     (e.g. data/vanilla-1.1.json or data/vanilla-1.1/)\n"
        "  --manifest <path>  Hash manifest JSON (e.g. manifests/vanilla-1.1.json)\n"
        "  --master <h:p>     Master server address (repeatable; replaces defaults)\n"
        "  --no-master        Disable all master server heartbeating\n"
        "  --log-level <lvl>  Log verbosity: quiet|error|warn|info|debug|trace (default: info)\n"
        "  --log-file <path>  Write log to this file (default: openbc-YYYYMMDD-HHMMSS.log)\n"
        "  --no-log-file      Disable disk logging entirely\n"
        "  -q                 Shorthand for --log-level quiet\n"
        "  -v                 Shorthand for --log-level debug\n"
        "  -vv                Shorthand for --log-level trace\n"
        "  -h, --help         Show this help\n",
        prog);
}

static bc_log_level_t parse_log_level(const char *str)
{
    if (strcmp(str, "quiet") == 0) return LOG_QUIET;
    if (strcmp(str, "error") == 0) return LOG_ERROR;
    if (strcmp(str, "warn")  == 0) return LOG_WARN;
    if (strcmp(str, "info")  == 0) return LOG_INFO;
    if (strcmp(str, "debug") == 0) return LOG_DEBUG;
    if (strcmp(str, "trace") == 0) return LOG_TRACE;
    fprintf(stderr, "Unknown log level: %s (using info)\n", str);
    return LOG_INFO;
}

int main(int argc, char **argv)
{
    /* Defaults */
    u16 port = BC_DEFAULT_PORT;
    const char *name = "OpenBC Server";
    const char *map = "Multiplayer.Episode.Mission1.Mission1";
    int max_players = BC_MAX_PLAYERS;
    const char *manifest_path = NULL;
    const char *data_path = NULL;
    const char *user_masters[BC_MAX_MASTERS];
    int user_master_count = 0;
    bool no_master = false;
    bc_log_level_t log_level = LOG_INFO;
    const char *log_file_path = NULL;
    bool no_log_file = false;

    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = (u16)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            map = argv[++i];
        } else if (strcmp(argv[i], "--max") == 0 && i + 1 < argc) {
            max_players = atoi(argv[++i]);
            if (max_players < 1) max_players = 1;
            if (max_players > BC_MAX_PLAYERS) max_players = BC_MAX_PLAYERS;
        } else if (strcmp(argv[i], "--system") == 0 && i + 1 < argc) {
            g_system_index = atoi(argv[++i]);
            if (g_system_index < 1) g_system_index = 1;
            if (g_system_index > 9) g_system_index = 9;
        } else if (strcmp(argv[i], "--time-limit") == 0 && i + 1 < argc) {
            g_time_limit = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frag-limit") == 0 && i + 1 < argc) {
            g_frag_limit = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data_path = argv[++i];
        } else if (strcmp(argv[i], "--manifest") == 0 && i + 1 < argc) {
            manifest_path = argv[++i];
        } else if (strcmp(argv[i], "--collision") == 0) {
            g_collision_dmg = true;
        } else if (strcmp(argv[i], "--no-collision") == 0) {
            g_collision_dmg = false;
        } else if (strcmp(argv[i], "--friendly-fire") == 0) {
            g_friendly_fire = true;
        } else if (strcmp(argv[i], "--no-friendly-fire") == 0) {
            g_friendly_fire = false;
        } else if (strcmp(argv[i], "--master") == 0 && i + 1 < argc) {
            if (user_master_count < BC_MAX_MASTERS)
                user_masters[user_master_count++] = argv[++i];
        } else if (strcmp(argv[i], "--no-master") == 0) {
            no_master = true;
        } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
            log_level = parse_log_level(argv[++i]);
        } else if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            log_file_path = argv[++i];
        } else if (strcmp(argv[i], "--no-log-file") == 0) {
            no_log_file = true;
        } else if (strcmp(argv[i], "-q") == 0) {
            log_level = LOG_QUIET;
        } else if (strcmp(argv[i], "-vv") == 0) {
            log_level = LOG_TRACE;
        } else if (strcmp(argv[i], "-v") == 0) {
            log_level = LOG_DEBUG;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
    }

    /* Apply parsed settings to globals */
    g_map_name = map;
    g_max_players = max_players;

    /* Generate default log file name if none specified and not disabled.
     * Format: openbc-YYYYMMDD-HHMMSS.log (one file per session). */
    if (!log_file_path && !no_log_file) {
        static char default_log[64];
#ifdef _WIN32
        SYSTEMTIME st;
        GetLocalTime(&st);
        snprintf(default_log, sizeof(default_log),
                 "openbc-%04d%02d%02d-%02d%02d%02d.log",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
#else
        time_t now_t = time(NULL);
        struct tm *tm_info = localtime(&now_t);
        snprintf(default_log, sizeof(default_log),
                 "openbc-%04d%02d%02d-%02d%02d%02d.log",
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
                 tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
#endif
        log_file_path = default_log;
    }

    /* Initialize logging (before anything that uses LOG_*) */
    bc_log_init(log_level, log_file_path);

    /* Initialize session stats */
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.start_time = bc_ms_now();

    /* Load manifest.
     * If --manifest was given, use that path.  Otherwise, scan manifests/
     * for .json files -- if exactly one exists, auto-load it. */
    if (!manifest_path) {
        static char auto_path[512];
        int json_count = 0;
#ifdef _WIN32
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA("manifests\\*.json", &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    if (json_count == 0)
                        snprintf(auto_path, sizeof(auto_path),
                                 "manifests/%s", fd.cFileName);
                    json_count++;
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
#else
        DIR *mdir = opendir("manifests");
        if (mdir) {
            struct dirent *ment;
            while ((ment = readdir(mdir)) != NULL) {
                const char *n = ment->d_name;
                size_t nlen = strlen(n);
                if (nlen > 5 && strcmp(n + nlen - 5, ".json") == 0) {
                    if (json_count == 0)
                        snprintf(auto_path, sizeof(auto_path),
                                 "manifests/%s", n);
                    json_count++;
                }
            }
            closedir(mdir);
        }
#endif
        if (json_count == 1) {
            manifest_path = auto_path;
            LOG_INFO("init", "Auto-detected manifest: %s", manifest_path);
        }
    }

    if (manifest_path) {
        if (bc_manifest_load(&g_manifest, manifest_path)) {
            g_manifest_loaded = true;
            bc_manifest_print_summary(&g_manifest);
        } else {
            LOG_ERROR("init", "Failed to load manifest: %s", manifest_path);
            bc_log_shutdown();
            return 1;
        }
    }

    if (!g_manifest_loaded && !g_no_checksum) {
        LOG_WARN("init", "No manifest loaded, running in permissive mode");
        LOG_WARN("init", "  Use --manifest <path> to enable checksum validation");
        g_no_checksum = true;
    }

    /* Load ship data registry for server-authoritative damage.
     * Accepts both a versioned directory (contains manifest.json) and a
     * legacy monolith JSON file.  If --data was not given, scan data/ for
     * a directory with manifest.json first, then fall back to a lone .json. */
    memset(&g_registry, 0, sizeof(g_registry));
    bc_torpedo_mgr_init(&g_torpedoes);

    bool data_is_dir = false;

    if (!data_path) {
        /* Use separate buffers so dir and json don't clobber each other. */
        static char auto_dir[512];
        static char auto_json[512];
        int dir_count = 0;
        int json_count = 0;
#ifdef _WIN32
        /* Check for versioned directories first */
        WIN32_FIND_DATAA dfd;
        HANDLE dFind = FindFirstFileA("data\\*", &dfd);
        if (dFind != INVALID_HANDLE_VALUE) {
            do {
                if ((dfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    strcmp(dfd.cFileName, ".") != 0 &&
                    strcmp(dfd.cFileName, "..") != 0) {
                    char mpath[512];
                    snprintf(mpath, sizeof(mpath),
                             "data/%s/manifest.json", dfd.cFileName);
                    DWORD ma = GetFileAttributesA(mpath);
                    if (ma != INVALID_FILE_ATTRIBUTES &&
                        !(ma & FILE_ATTRIBUTE_DIRECTORY)) {
                        if (dir_count == 0)
                            snprintf(auto_dir, sizeof(auto_dir),
                                     "data/%s", dfd.cFileName);
                        dir_count++;
                    }
                } else if (!(dfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    const char *fn = dfd.cFileName;
                    size_t fnlen = strlen(fn);
                    if (fnlen > 5 && strcmp(fn + fnlen - 5, ".json") == 0) {
                        if (json_count == 0)
                            snprintf(auto_json, sizeof(auto_json),
                                     "data/%s", fn);
                        json_count++;
                    }
                }
            } while (FindNextFileA(dFind, &dfd));
            FindClose(dFind);
        }
#else
        DIR *ddir = opendir("data");
        if (ddir) {
            struct dirent *dent;
            while ((dent = readdir(ddir)) != NULL) {
                const char *n = dent->d_name;
                if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;

                char full[512];
                snprintf(full, sizeof(full), "data/%s", n);
                struct stat st;
                if (stat(full, &st) != 0) continue;

                if (S_ISDIR(st.st_mode)) {
                    char mpath[640];
                    snprintf(mpath, sizeof(mpath), "%s/manifest.json", full);
                    struct stat mst;
                    if (stat(mpath, &mst) == 0 && S_ISREG(mst.st_mode)) {
                        if (dir_count == 0)
                            snprintf(auto_dir, sizeof(auto_dir), "%s", full);
                        dir_count++;
                    }
                } else if (S_ISREG(st.st_mode)) {
                    size_t nlen = strlen(n);
                    if (nlen > 5 && strcmp(n + nlen - 5, ".json") == 0) {
                        if (json_count == 0)
                            snprintf(auto_json, sizeof(auto_json), "%s", full);
                        json_count++;
                    }
                }
            }
            closedir(ddir);
        }
#endif
        if (dir_count == 1) {
            data_path = auto_dir;
            data_is_dir = true;
            LOG_INFO("init", "Auto-detected data registry: %s/", data_path);
        } else if (dir_count == 0 && json_count == 1) {
            data_path = auto_json;
            data_is_dir = false;
            LOG_INFO("init", "Auto-detected data registry: %s", data_path);
        }
    } else {
#ifdef _WIN32
        {
            DWORD _attr = GetFileAttributesA(data_path);
            data_is_dir = (_attr != INVALID_FILE_ATTRIBUTES) &&
                          (_attr & FILE_ATTRIBUTE_DIRECTORY);
        }
#else
        {
            struct stat _dstat;
            data_is_dir = stat(data_path, &_dstat) == 0 && S_ISDIR(_dstat.st_mode);
        }
#endif
    }

    if (data_path) {
        bool ok = data_is_dir
            ? bc_registry_load_dir(&g_registry, data_path)
            : bc_registry_load(&g_registry, data_path);
        if (ok) {
            g_registry_loaded = true;
            LOG_INFO("init", "Ship registry loaded: %d ships, %d projectiles from %s",
                     g_registry.ship_count, g_registry.projectile_count, data_path);
        } else {
            LOG_WARN("init", "Failed to load ship registry: %s", data_path);
            LOG_WARN("init", "  Running in relay-only mode (no damage authority)");
        }
    }

    /* Initialize */
    if (!bc_net_init()) {
        LOG_ERROR("init", "Failed to initialize networking");
        bc_log_shutdown();
        return 1;
    }

    if (!bc_socket_open(&g_socket, port)) {
        LOG_ERROR("init", "Failed to bind port %u", port);
        bc_net_shutdown();
        bc_log_shutdown();
        return 1;
    }
    /* Open LAN query socket on port 6500 (GameSpy standard).
     * BC clients broadcast queries here for LAN server discovery.
     * Non-fatal if port is in use (e.g., another server instance). */
    if (port != BC_GAMESPY_QUERY_PORT) {
        if (bc_socket_open(&g_query_socket, BC_GAMESPY_QUERY_PORT)) {
            g_query_socket_open = true;
            LOG_INFO("init", "LAN query socket open on port %u",
                     BC_GAMESPY_QUERY_PORT);
        } else {
            LOG_WARN("init", "Could not bind LAN query port %u "
                     "(LAN browser discovery may not work)",
                     BC_GAMESPY_QUERY_PORT);
        }
    }

    bc_peers_init(&g_peers);

    /* Reserve slot 0 for the dedicated server itself.
     * The stock BC dedi creates a "Dedicated Server" pseudo-player at slot 0
     * that doesn't count as a joined player.  This ensures joining players
     * start at slot 1 (wire_slot=2, direction=0x02), matching stock behavior. */
    {
        bc_peer_t *dedi = &g_peers.peers[0];
        dedi->state = PEER_LOBBY;  /* Always "connected" */
        snprintf(dedi->name, sizeof(dedi->name), "Dedicated Server");
        g_peers.count++;
    }

    /* Server info for GameSpy responses.
     * Fields must match stock BC QR1 callbacks (basic + info + rules).
     * missionscript = game mode (e.g. "DM"), mapname = system key (e.g. "Multi1"),
     * system = display name (e.g. "Asteroids"), maxplayers excludes dedi slot. */
    memset(&g_info, 0, sizeof(g_info));
    snprintf(g_info.hostname, sizeof(g_info.hostname), "%s", name);
    snprintf(g_info.missionscript, sizeof(g_info.missionscript), "%s", map);
    /* mapname = game mode display (e.g. "DM"), system = system key (e.g. "Multi1").
     * Verified from stock trace + live client: Type column shows mapname,
     * Game Info panel shows system. */
    snprintf(g_info.mapname, sizeof(g_info.mapname), "DM");
    if (g_system_index >= 1 && g_system_index < SYSTEM_TABLE_SIZE &&
        g_system_table[g_system_index].key) {
        snprintf(g_info.system, sizeof(g_info.system), "%s",
                 g_system_table[g_system_index].key);
    } else {
        snprintf(g_info.system, sizeof(g_info.system), "Multi1");
    }
    snprintf(g_info.gamemode, sizeof(g_info.gamemode), "openplaying");
    g_info.numplayers = 0;
    g_info.maxplayers = max_players > 1 ? max_players - 1 : 1; /* exclude dedi slot */
    g_info.timelimit = g_time_limit > 0 ? g_time_limit : -1;
    g_info.fraglimit = g_frag_limit > 0 ? g_frag_limit : -1;
    /* Player list: slot 0 = "Dedicated Server" (always present, not shown in lobby/scoreboard) */
    snprintf(g_info.player_names[0], sizeof(g_info.player_names[0]),
             "Dedicated Server");
    g_info.player_count = 1;

    /* Master server registration */
    memset(&g_masters, 0, sizeof(g_masters));
    if (!no_master) {
        if (user_master_count > 0) {
            for (int i = 0; i < user_master_count; i++)
                bc_master_add(&g_masters, user_masters[i], port);
        } else {
            bc_master_init_defaults(&g_masters, port);
        }
        if (g_masters.count > 0)
            bc_master_probe(&g_masters, &g_socket, &g_info);
    }

#ifdef _WIN32
    /* Create shutdown synchronization event (manual reset, initially unsignaled) */
    g_shutdown_done = CreateEvent(NULL, TRUE, FALSE, NULL);

    /* Register CTRL+C handler */
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    /* Register POSIX signal handlers */
    signal(SIGINT,  posix_signal_handler);
    signal(SIGTERM, posix_signal_handler);
#endif

    /* Startup banner (raw printf, not a log message) */
    printf("OpenBC Server v0.1.0\n");
    printf("Listening on port %u (%d max players)\n", port, g_info.maxplayers);
    printf("Server name: %s | System: %s (%s)\n", name,
           g_info.system, g_info.mapname);
    printf("Collision damage: %s | Friendly fire: %s\n",
           g_collision_dmg ? "on" : "off",
           g_friendly_fire ? "on" : "off");
    if (g_manifest_loaded) {
        printf("Checksum validation: on (manifest loaded)\n");
    } else {
        printf("Checksum validation: off (no manifest, permissive mode)\n");
    }
    if (g_registry_loaded) {
        printf("Damage authority: server (%d ships, %d projectiles)\n",
               g_registry.ship_count, g_registry.projectile_count);
    } else {
        printf("Damage authority: client (relay-only, no registry)\n");
    }
    if (log_file_path)
        printf("Log file: %s\n", log_file_path);
    if (g_masters.count > 0) {
        int verified = 0;
        for (int i = 0; i < g_masters.count; i++)
            if (g_masters.entries[i].verified) verified++;
        printf("Master servers: %d/%d registered\n",
               verified, g_masters.count);
        for (int i = 0; i < g_masters.count; i++) {
            if (g_masters.entries[i].verified)
                printf("  + %s\n", g_masters.entries[i].hostname);
        }
    }
    printf("Press Ctrl+C to stop.\n\n");

    /* Diagnostic: check for ghost peers created during startup/probe.
     * Only slot 0 (dedi) should be non-empty at this point. */
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        if (g_peers.peers[i].state != PEER_EMPTY) {
            LOG_WARN("init", "Ghost peer at slot %d: state=%d, addr=%08X:%u, "
                     "last_recv=%u",
                     i, g_peers.peers[i].state,
                     g_peers.peers[i].addr.ip, g_peers.peers[i].addr.port,
                     g_peers.peers[i].last_recv_time);
            bc_peers_remove(&g_peers, i);
        }
    }

    /* Main loop -- 33ms tick (~30 Hz).
     * Stock BC dedi runs an unbounded busy loop at thousands of FPS.
     * 30 Hz is more than sufficient: network sends StateUpdates at ~10 Hz
     * and most game timers fire at 1-second intervals. */
    u8 recv_buf[2048];
    u32 last_tick = bc_ms_now();
    u32 tick_counter = 0;

    while (g_running) {
        /* Receive all pending packets on game port */
        bc_addr_t from;
        int received;
        while ((received = bc_socket_recv(&g_socket, &from,
                                           recv_buf, sizeof(recv_buf))) > 0) {
            if (bc_gamespy_is_query(recv_buf, received)) {
                bc_handle_gamespy(&g_socket, &from, recv_buf, received);
            } else {
                bc_handle_packet(&from, recv_buf, received);
            }
        }

        /* Receive all pending packets on LAN query port (6500) */
        if (g_query_socket_open) {
            while ((received = bc_socket_recv(&g_query_socket, &from,
                                               recv_buf, sizeof(recv_buf))) > 0) {
                if (bc_gamespy_is_query(recv_buf, received)) {
                    bc_handle_gamespy(&g_query_socket, &from, recv_buf, received);
                }
                /* Non-GameSpy packets on port 6500 are ignored */
            }
        }

        /* Tick at ~33ms intervals (~30 Hz) */
        u32 now = bc_ms_now();
        if (now - last_tick >= 33) {
            /* Advance game clock */
            g_game_time += (f32)(now - last_tick) / 1000.0f;
            tick_counter++;

            /* Every 30 ticks (~1 second): retransmit, timeout, master heartbeat */
            if (tick_counter % 30 == 0) {
                /* Retransmit unACKed reliable messages (skip slot 0 = dedi) */
                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *peer = &g_peers.peers[i];
                    if (peer->state == PEER_EMPTY) continue;

                    /* Check for dead peers (max retries exceeded) */
                    if (bc_reliable_check_timeout(&peer->reliable_out)) {
                        char addr_str[32];
                        bc_addr_to_string(&peer->addr, addr_str, sizeof(addr_str));
                        LOG_INFO("net", "Peer %s (slot %d) timed out (no ACK)",
                                 addr_str, i);
                        bc_handle_peer_disconnect(i);
                        continue;
                    }

                    /* Retransmit overdue messages (direct send, not via outbox) */
                    int idx;
                    while ((idx = bc_reliable_check_retransmit(
                                &peer->reliable_out, now)) >= 0) {
                        g_stats.reliable_retransmits++;
                        bc_reliable_entry_t *e = &peer->reliable_out.entries[idx];
                        u8 pkt[BC_MAX_PACKET_SIZE];
                        int len = bc_transport_build_reliable(
                            pkt, sizeof(pkt), e->payload, e->payload_len, e->seq);
                        if (len > 0) {
                            bc_packet_t trace;
                            if (bc_transport_parse(pkt, len, &trace))
                                bc_log_packet_trace(&trace, i, "RTXM");
                            alby_cipher_encrypt(pkt, (size_t)len);
                            bc_socket_send(&g_socket, &peer->addr, pkt, len);
                        }
                    }
                }

                /* Timeout stale peers (skip slot 0 = dedi) */
                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    if (g_peers.peers[i].state == PEER_EMPTY) continue;
                    if (now - g_peers.peers[i].last_recv_time > 30000) {
                        g_stats.timeouts++;
                        LOG_INFO("net", "Peer slot %d timed out (no packets)", i);
                        bc_handle_peer_disconnect(i);
                    }
                }

                /* Master server heartbeat */
                bc_master_tick(&g_masters, &g_socket, now);
            }

            /* Delta time for this tick (used by simulation + respawn) */
            f32 dt = (f32)(now - last_tick) / 1000.0f;

            /* === Simulation tick (every 100ms when registry loaded) === */
            if (g_registry_loaded) {

                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *p = &g_peers.peers[i];
                    if (!p->has_ship || !p->ship.alive) continue;

                    const bc_ship_class_t *cls =
                        bc_registry_get_ship(&g_registry, p->class_index);
                    if (!cls) continue;

                    /* Reactor: generate power, compute per-subsystem efficiency */
                    bc_ship_power_tick(&p->ship, cls, dt);

                    /* Server-side position estimate for range checks + torpedo targeting */
                    f32 eng_eff = bc_powered_efficiency(&p->ship, cls, "impulse");
                    bc_ship_move_tick(&p->ship, eng_eff, dt);

                    /* Shield recharge (shield gen is Base format, eff = 1.0) */
                    bc_combat_shield_tick(&p->ship, cls, 1.0f, dt);

                    /* Phaser charge + torpedo cooldown (use weapon efficiency) */
                    f32 wep_eff = bc_powered_efficiency(&p->ship, cls, "phaser");
                    f32 pulse_eff = bc_powered_efficiency(&p->ship, cls, "pulse_weapon");
                    f32 min_wep = (pulse_eff < wep_eff) ? pulse_eff : wep_eff;
                    bc_combat_charge_tick(&p->ship, cls, min_wep, dt);
                    bc_combat_torpedo_tick(&p->ship, cls, dt);

                    /* Cloak state machine */
                    bc_cloak_tick(&p->ship, dt);

                    /* Repair */
                    bc_repair_tick(&p->ship, cls, dt);
                    bc_repair_auto_queue(&p->ship, cls);

                    /* Tractor beam physics: drag target if engaged */
                    if (p->ship.tractor_target_id >= 0) {
                        int tgt = find_peer_by_object(p->ship.tractor_target_id);
                        if (tgt >= 0 && g_peers.peers[tgt].has_ship &&
                            g_peers.peers[tgt].ship.alive) {
                            bc_combat_tractor_tick(&p->ship,
                                                    &g_peers.peers[tgt].ship,
                                                    cls, dt);
                        } else {
                            bc_combat_tractor_disengage(&p->ship);
                        }
                    }
                }

                /* Torpedo tracker tick */
                if (g_torpedoes.count > 0) {
                    bc_torpedo_tick(&g_torpedoes, dt, 5.0f,
                                    bc_torpedo_target_pos,
                                    bc_torpedo_hit_callback, NULL);
                }
            }

            /* Health broadcast: every 3 ticks (~100ms = 10 Hz), send 0x20 StateUpdate.
             * Stock dedi sends at ~10 Hz.  Uses hierarchical round-robin with
             * 10-byte budget per tick.  Owner gets is_own_ship=true (no power_pct
             * bytes in Powered entries), remote observers get is_own_ship=false. */
            if (g_registry_loaded && (tick_counter % 3 == 0)) {
                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *p = &g_peers.peers[i];
                    if (!p->has_ship || !p->ship.alive) continue;

                    const bc_ship_class_t *cls =
                        bc_registry_get_ship(&g_registry, p->class_index);
                    if (!cls) continue;

                    /* Build owner version (no power data in Powered entries) */
                    u8 hbuf_own[128];
                    u8 next_idx;
                    int hlen_own = bc_ship_build_health_update(
                        &p->ship, cls, g_game_time,
                        p->subsys_rr_idx, &next_idx, true,
                        hbuf_own, sizeof(hbuf_own));

                    /* Build remote version (with power data) using same
                     * start_idx so both cover the same entries. */
                    u8 hbuf_rmt[128];
                    u8 rmt_next;
                    int hlen_rmt = bc_ship_build_health_update(
                        &p->ship, cls, g_game_time,
                        p->subsys_rr_idx, &rmt_next, false,
                        hbuf_rmt, sizeof(hbuf_rmt));

                    /* Advance cursor using the owner version (smaller budget,
                     * may cover fewer entries -- that's fine, the remote
                     * version just sends more data this tick). */
                    if (hlen_own > 0)
                        p->subsys_rr_idx = next_idx;

                    /* Send appropriate version to each client */
                    for (int j = 1; j < BC_MAX_PLAYERS; j++) {
                        if (g_peers.peers[j].state < PEER_LOBBY) continue;
                        if (j == i && hlen_own > 0) {
                            bc_queue_unreliable(j, hbuf_own, hlen_own);
                        } else if (j != i && hlen_rmt > 0) {
                            bc_queue_unreliable(j, hbuf_rmt, hlen_rmt);
                        }
                    }
                }
            }

            /* Win condition: time limit */
            if (g_registry_loaded && !g_game_ended && g_time_limit > 0) {
                f32 limit_sec = (f32)g_time_limit * 60.0f;
                if (g_game_time >= limit_sec) {
                    u8 eg[8];
                    int eglen = bc_build_end_game(eg, sizeof(eg),
                                                   BC_END_REASON_TIME_UP);
                    if (eglen > 0) bc_send_to_all(eg, eglen, true);
                    g_game_ended = true;
                    LOG_INFO("game", "Time limit reached (%.0f sec)", g_game_time);
                }
            }

            /* Respawn: countdown dead players, re-create ships */
            if (g_registry_loaded && !g_game_ended) {
                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *rp = &g_peers.peers[i];
                    if (rp->state < PEER_IN_GAME || rp->has_ship) continue;
                    if (rp->respawn_timer <= 0.0f) continue;

                    rp->respawn_timer -= dt;
                    if (rp->respawn_timer > 0.0f) continue;
                    rp->respawn_timer = 0.0f;

                    const bc_ship_class_t *rcls =
                        bc_registry_get_ship(&g_registry, rp->respawn_class);
                    if (!rcls) continue;

                    int gs = i > 0 ? i - 1 : 0;
                    bc_ship_init(&rp->ship, rcls, rp->respawn_class,
                                 bc_make_ship_id(gs), (u8)i, rp->ship.team_id);
                    rp->ship.pos.x = (f32)(rand() % 4001) - 2000.0f;
                    rp->ship.pos.y = (f32)(rand() % 1001) - 500.0f;
                    rp->ship.pos.z = (f32)(rand() % 4001) - 2000.0f;
                    rp->class_index = rp->respawn_class;
                    rp->has_ship = true;
                    rp->subsys_rr_idx = 0;
                    bc_ship_assign_subsystem_ids(&rp->ship, rcls,
                                                  &g_script_obj_counter);

                    u8 cpkt[1024];
                    int clen = bc_ship_build_create_packet(&rp->ship, rcls,
                                                            cpkt, sizeof(cpkt));
                    if (clen > 0) {
                        if (clen <= (int)sizeof(rp->spawn_payload)) {
                            memcpy(rp->spawn_payload, cpkt, (size_t)clen);
                            rp->spawn_len = clen;
                        }
                        bc_send_to_all(cpkt, clen, true);
                        LOG_INFO("game", "slot=%d respawned as %s", i, rcls->name);
                    }
                }
            }

            /* Every 30 ticks (~1 second): send keepalive to all active peers.
             * Stock dedi echoes the client's identity data (22 bytes) back
             * instead of sending a minimal [0x00][0x02] keepalive. */
            if (tick_counter % 30 == 0) {
                for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                    bc_peer_t *peer = &g_peers.peers[i];
                    if (peer->state < PEER_LOBBY) continue;
                    if (peer->keepalive_len > 0) {
                        bc_outbox_add_keepalive_data(&peer->outbox,
                                                      peer->keepalive_data,
                                                      peer->keepalive_len);
                    } else {
                        bc_outbox_add_keepalive(&peer->outbox);
                    }
                }
            }

            /* Flush all peer outboxes (skip slot 0 = dedi) */
            for (int i = 1; i < BC_MAX_PLAYERS; i++) {
                if (g_peers.peers[i].state == PEER_EMPTY) continue;
                bc_flush_peer(i);
            }

            last_tick = now;
        }

        /* Don't burn CPU -- sleep 1ms between polls */
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }

    LOG_INFO("shutdown", "Shutting down...");

    /* Log session summary before tearing down */
    bc_log_session_summary();

    /* Flush all pending outbox data before sending shutdown (skip slot 0 = dedi) */
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        if (g_peers.peers[i].state == PEER_EMPTY) continue;
        bc_flush_peer(i);
    }

    /* Send ConnectAck shutdown notification to all connected peers.
     * Real BC server sends ConnectAck (0x05) to each peer on shutdown,
     * NOT BootPlayer or DeletePlayer. (skip slot 0 = dedi) */
    for (int i = 1; i < BC_MAX_PLAYERS; i++) {
        bc_peer_t *peer = &g_peers.peers[i];
        if (peer->state == PEER_EMPTY) continue;

        u8 pkt[16];
        int len = bc_transport_build_shutdown_notify(
            pkt, sizeof(pkt), (u8)(i + 1), peer->addr.ip);
        if (len > 0) {
            bc_packet_t trace;
            if (bc_transport_parse(pkt, len, &trace))
                bc_log_packet_trace(&trace, i, "SEND");
            alby_cipher_encrypt(pkt, (size_t)len);
            bc_socket_send(&g_socket, &peer->addr, pkt, len);
            LOG_INFO("shutdown", "Sent shutdown to slot %d", i);
        }

        /* Clear peer state */
        peer->state = PEER_EMPTY;
    }
    g_peers.count = 0;

    /* Unregister from master servers (sends exit heartbeat) */
    bc_master_shutdown(&g_masters, &g_socket);

    /* Close all sockets */
    if (g_query_socket_open) {
        bc_socket_close(&g_query_socket);
        g_query_socket_open = false;
    }
    bc_socket_close(&g_socket);

    /* Release Winsock */
    bc_net_shutdown();

#ifdef _WIN32
    /* Unregister console handler */
    SetConsoleCtrlHandler(console_handler, FALSE);
#endif

    LOG_INFO("shutdown", "Server stopped.");
    bc_log_shutdown();

#ifdef _WIN32
    /* Signal console handler that cleanup is done (unblocks CTRL_CLOSE_EVENT) */
    SetEvent(g_shutdown_done);
    CloseHandle(g_shutdown_done);
#endif
    return 0;
}
