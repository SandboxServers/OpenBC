#include "openbc/log.h"
#include "openbc/opcodes.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <time.h>
#endif

static bc_log_level_t g_log_level = LOG_INFO;
static FILE          *g_log_file  = NULL;
static u32            g_start_time = 0;

u32 bc_ms_now(void)
{
#ifdef _WIN32
    return GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u32)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

static const char *level_names[] = {
    "QUIET", "ERROR", "WARN ", "INFO ", "DEBUG", "TRACE"
};

void bc_log_init(bc_log_level_t level, const char *log_file_path)
{
    g_log_level  = level;
    g_start_time = bc_ms_now();

    if (log_file_path) {
        g_log_file = fopen(log_file_path, "w");
        if (!g_log_file) {
            fprintf(stderr, "Warning: could not open log file: %s (%s)\n",
                    log_file_path, strerror(errno));
        }
    }
}

void bc_log_shutdown(void)
{
    if (g_log_file) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void bc_log(bc_log_level_t level, const char *tag, const char *fmt, ...)
{
    if (level > g_log_level || level == LOG_QUIET) return;

    u32 elapsed = bc_ms_now() - g_start_time;
    u32 ms  = elapsed % 1000;
    u32 sec = (elapsed / 1000) % 60;
    u32 min = (elapsed / 60000) % 60;
    u32 hr  = elapsed / 3600000;

    char prefix[64];
    snprintf(prefix, sizeof(prefix), "[%02u:%02u:%02u.%03u] [%s] [%s] ",
             hr, min, sec, ms, level_names[level], tag);

    va_list args;

    /* Write to stdout */
    va_start(args, fmt);
    fputs(prefix, stdout);
    vfprintf(stdout, fmt, args);
    fputc('\n', stdout);
    va_end(args);

    /* Write to log file if open */
    if (g_log_file) {
        va_start(args, fmt);
        fputs(prefix, g_log_file);
        vfprintf(g_log_file, fmt, args);
        fputc('\n', g_log_file);
        fflush(g_log_file);
        va_end(args);
    }
}

void bc_log_packet_trace(const bc_packet_t *pkt, int slot, const char *label)
{
    if (g_log_level < LOG_TRACE) return;

    LOG_TRACE("pkt", "%s slot=%d dir=0x%02X msgs=%d",
              label, slot, pkt->direction, pkt->msg_count);

    for (int i = 0; i < pkt->msg_count; i++) {
        const bc_transport_msg_t *msg = &pkt->msgs[i];
        const char *tname = bc_transport_type_name(msg->type);

        if (msg->type == BC_TRANSPORT_ACK) {
            LOG_TRACE("pkt", "  [%d] %s seq=%d flags=0x%02X",
                      i, tname ? tname : "?", (int)msg->seq, msg->flags);
            continue;
        }

        if (msg->type == BC_TRANSPORT_RELIABLE) {
            /* Build hex dump of first 32 payload bytes */
            char hex[128];
            int hpos = 0;
            int show = msg->payload_len < 32 ? msg->payload_len : 32;
            for (int j = 0; j < show; j++)
                hpos += snprintf(hex + hpos, (size_t)(sizeof(hex) - hpos),
                                 "%02X ", msg->payload[j]);
            if (msg->payload_len > 32)
                hpos += snprintf(hex + hpos, (size_t)(sizeof(hex) - hpos), "...");
            if (hpos > 0 && hex[hpos - 1] == ' ') hex[--hpos] = '\0';

            /* Resolve game opcode name from first payload byte */
            const char *oname = NULL;
            u8 opcode = 0;
            if (msg->payload_len > 0) {
                opcode = msg->payload[0];
                oname = bc_opcode_name(opcode);
            }

            /* flags==0x00: unreliable game data (no seq, no ack).
             * Any other flags value: reliable, has a valid seq number. */
            if (msg->flags == 0x00) {
                if (oname) {
                    LOG_TRACE("pkt", "  [%d] Unreliable flags=0x00 "
                              "opcode=0x%02X(%s) len=%d [%s]",
                              i, opcode, oname, msg->payload_len, hex);
                } else {
                    LOG_TRACE("pkt", "  [%d] Unreliable flags=0x00 len=%d [%s]",
                              i, msg->payload_len, hex);
                }
            } else {
                const char *frag = (msg->flags & BC_RELIABLE_FLAG_FRAGMENT)
                                    ? "[FRAG]" : "";
                if (oname) {
                    LOG_TRACE("pkt", "  [%d] Reliable seq=0x%04X flags=0x%02X%s "
                              "opcode=0x%02X(%s) len=%d [%s]",
                              i, msg->seq, msg->flags, frag,
                              opcode, oname, msg->payload_len, hex);
                } else {
                    LOG_TRACE("pkt", "  [%d] Reliable seq=0x%04X flags=0x%02X%s "
                              "len=%d [%s]",
                              i, msg->seq, msg->flags, frag,
                              msg->payload_len, hex);
                }
            }
            continue;
        }

        /* Keepalive, Connect, Disconnect, etc. */
        char tname_buf[32];
        const char *tname_display = tname;
        if (!tname_display) {
            snprintf(tname_buf, sizeof(tname_buf), "Unknown(0x%02X)", msg->type);
            tname_display = tname_buf;
        }

        char hex[128];
        int hpos = 0;
        int show = msg->payload_len < 32 ? msg->payload_len : 32;
        for (int j = 0; j < show; j++)
            hpos += snprintf(hex + hpos, (size_t)(sizeof(hex) - hpos),
                             "%02X ", msg->payload[j]);
        if (msg->payload_len > 32)
            hpos += snprintf(hex + hpos, (size_t)(sizeof(hex) - hpos), "...");
        if (hpos > 0 && hex[hpos - 1] == ' ') hex[--hpos] = '\0';

        if (msg->payload_len > 0) {
            LOG_TRACE("pkt", "  [%d] %s len=%d [%s]",
                      i, tname_display, msg->payload_len, hex);
        } else {
            LOG_TRACE("pkt", "  [%d] %s len=%d",
                      i, tname_display, msg->payload_len);
        }
    }
}
