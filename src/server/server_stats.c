#include "openbc/server_state.h"
#include "openbc/server_stats.h"
#include "openbc/opcodes.h"
#include "openbc/log.h"

#include <stdio.h>

#include <windows.h>

/* Format a millisecond duration as "Xh Ym Zs" into buf. */
static void format_duration(u32 ms, char *buf, int bufsize)
{
    u32 secs = ms / 1000;
    u32 h = secs / 3600;
    u32 m = (secs % 3600) / 60;
    u32 s = secs % 60;
    if (h > 0)
        snprintf(buf, (size_t)bufsize, "%uh %um %us", h, m, s);
    else if (m > 0)
        snprintf(buf, (size_t)bufsize, "%um %us", m, s);
    else
        snprintf(buf, (size_t)bufsize, "%us", s);
}

/* Format a ms offset from session start as "H:MM:SS" into buf. */
static void format_time_offset(u32 offset_ms, char *buf, int bufsize)
{
    u32 secs = offset_ms / 1000;
    u32 h = secs / 3600;
    u32 m = (secs % 3600) / 60;
    u32 s = secs % 60;
    snprintf(buf, (size_t)bufsize, "%u:%02u:%02u", h, m, s);
}

void bc_log_session_summary(void)
{
    u32 now = GetTickCount();
    u32 elapsed = now - g_stats.start_time;
    char dur[32];
    format_duration(elapsed, dur, sizeof(dur));

    LOG_INFO("summary", "=== Session Summary ===");
    LOG_INFO("summary", "  Duration: %s", dur);
    LOG_INFO("summary", "  Connections: %u total, %u peak concurrent",
             g_stats.total_connections, g_stats.peak_players);
    LOG_INFO("summary", "  Disconnects: %u (%u timeout)",
             g_stats.disconnects, g_stats.timeouts);
    LOG_INFO("summary", "  Boots: %u (server full), %u (checksum fail)",
             g_stats.boots_full, g_stats.boots_checksum);

    /* Player history */
    if (g_stats.player_count > 0) {
        LOG_INFO("summary", "");
        LOG_INFO("summary", "  Players:");
        for (int i = 0; i < g_stats.player_count; i++) {
            player_record_t *p = &g_stats.players[i];
            char t_join[16], t_leave[16];
            format_time_offset(p->connect_time - g_stats.start_time,
                               t_join, sizeof(t_join));
            if (p->disconnect_time != 0)
                format_time_offset(p->disconnect_time - g_stats.start_time,
                                   t_leave, sizeof(t_leave));
            else
                snprintf(t_leave, sizeof(t_leave), "(active)");
            LOG_INFO("summary", "    %-20s %s - %s", p->name, t_join, t_leave);
        }
    }

    /* Opcode table -- collect non-zero entries and sort by count desc */
    typedef struct { int opcode; u32 count; } opcode_entry_t;
    opcode_entry_t entries[256];
    int entry_count = 0;
    for (int i = 0; i < 256; i++) {
        if (g_stats.opcodes_recv[i] > 0) {
            entries[entry_count].opcode = i;
            entries[entry_count].count = g_stats.opcodes_recv[i];
            entry_count++;
        }
    }
    /* Insertion sort descending by count */
    for (int i = 1; i < entry_count; i++) {
        opcode_entry_t tmp = entries[i];
        int j = i - 1;
        while (j >= 0 && entries[j].count < tmp.count) {
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = tmp;
    }
    if (entry_count > 0) {
        LOG_INFO("summary", "");
        LOG_INFO("summary", "  Opcodes received (client -> server):");
        for (int i = 0; i < entry_count; i++) {
            const char *oname = bc_opcode_name(entries[i].opcode);
            if (oname)
                LOG_INFO("summary", "    %-20s %u", oname, entries[i].count);
            else
                LOG_INFO("summary", "    0x%02X                 %u",
                         entries[i].opcode, entries[i].count);
        }
    }

    /* Rejected opcodes (unhandled or wrong-state) */
    {
        opcode_entry_t rej[256];
        int rej_count = 0;
        for (int i = 0; i < 256; i++) {
            if (g_stats.opcodes_rejected[i] > 0) {
                rej[rej_count].opcode = i;
                rej[rej_count].count = g_stats.opcodes_rejected[i];
                rej_count++;
            }
        }
        for (int i = 1; i < rej_count; i++) {
            opcode_entry_t tmp = rej[i];
            int j = i - 1;
            while (j >= 0 && rej[j].count < tmp.count) {
                rej[j + 1] = rej[j];
                j--;
            }
            rej[j + 1] = tmp;
        }
        if (rej_count > 0) {
            LOG_INFO("summary", "");
            LOG_INFO("summary", "  Opcodes rejected (unhandled/wrong-state):");
            for (int i = 0; i < rej_count; i++) {
                const char *rname = bc_opcode_name(rej[i].opcode);
                if (rname)
                    LOG_INFO("summary", "    %-20s %u", rname, rej[i].count);
                else
                    LOG_INFO("summary", "    0x%02X                 %u",
                             rej[i].opcode, rej[i].count);
            }
        }
    }

    /* Network stats */
    if (g_stats.gamespy_queries > 0 || g_stats.reliable_retransmits > 0) {
        LOG_INFO("summary", "");
        LOG_INFO("summary", "  Network:");
        if (g_stats.gamespy_queries > 0)
            LOG_INFO("summary", "    GameSpy queries: %u",
                     g_stats.gamespy_queries);
        if (g_stats.reliable_retransmits > 0)
            LOG_INFO("summary", "    Reliable retransmits: %u",
                     g_stats.reliable_retransmits);
    }

    /* Master server status */
    if (g_masters.count > 0) {
        int verified = 0;
        for (int i = 0; i < g_masters.count; i++)
            if (g_masters.entries[i].verified) verified++;
        LOG_INFO("summary", "");
        LOG_INFO("summary", "  Master servers: %d/%d registered",
                 verified, g_masters.count);
        for (int i = 0; i < g_masters.count; i++) {
            bc_master_entry_t *e = &g_masters.entries[i];
            if (!e->enabled) continue;
            if (e->verified)
                LOG_INFO("summary", "    + %s (%u status checks)",
                         e->hostname, e->status_checks);
            else
                LOG_INFO("summary", "    - %s (no response)",
                         e->hostname);
        }
    }

    LOG_INFO("summary", "========================");
}
