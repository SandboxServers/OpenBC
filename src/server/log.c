#include "openbc/log.h"

#include <stdio.h>
#include <stdarg.h>

#include <windows.h>  /* For GetTickCount() */

static bc_log_level_t g_log_level = LOG_INFO;
static FILE          *g_log_file  = NULL;
static u32            g_start_time = 0;

static const char *level_names[] = {
    "QUIET", "ERROR", "WARN ", "INFO ", "DEBUG", "TRACE"
};

void bc_log_init(bc_log_level_t level, const char *log_file_path)
{
    g_log_level  = level;
    g_start_time = GetTickCount();

    if (log_file_path) {
        g_log_file = fopen(log_file_path, "a");
        if (!g_log_file) {
            fprintf(stderr, "Warning: could not open log file: %s\n",
                    log_file_path);
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

    u32 elapsed = GetTickCount() - g_start_time;
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
