#ifndef OPENBC_LOG_H
#define OPENBC_LOG_H

#include "openbc/types.h"
#include "openbc/transport.h"

/*
 * Server logging system -- leveled output with timestamps and optional file.
 *
 * Output format: [HH:MM:SS.mmm] [LEVEL] [tag] message\n
 * Timestamps are elapsed time since bc_log_init().
 */

typedef enum {
    LOG_QUIET = 0,   /* Suppress all log output */
    LOG_ERROR = 1,   /* Fatal/critical errors only */
    LOG_WARN  = 2,   /* Warnings (checksum fail, direction mismatch) */
    LOG_INFO  = 3,   /* Normal operation -- DEFAULT */
    LOG_DEBUG = 4,   /* Protocol details (handshake rounds, fragment reassembly) */
    LOG_TRACE = 5,   /* Per-packet data dumps */
} bc_log_level_t;

/* Initialize logging. Call once at startup before any LOG_* calls.
 * level: minimum severity to output.
 * log_file_path: if non-NULL, also write to this file (new file per session). */
void bc_log_init(bc_log_level_t level, const char *log_file_path);

/* Flush and close log file (if open). */
void bc_log_shutdown(void);

/* Core log function. Filtered by level threshold set in bc_log_init(). */
void bc_log(bc_log_level_t level, const char *tag, const char *fmt, ...);

/* Convenience macros */
#define LOG_ERROR(tag, ...) bc_log(LOG_ERROR, tag, __VA_ARGS__)
#define LOG_WARN(tag, ...)  bc_log(LOG_WARN,  tag, __VA_ARGS__)
#define LOG_INFO(tag, ...)  bc_log(LOG_INFO,  tag, __VA_ARGS__)
#define LOG_DEBUG(tag, ...) bc_log(LOG_DEBUG, tag, __VA_ARGS__)
#define LOG_TRACE(tag, ...) bc_log(LOG_TRACE, tag, __VA_ARGS__)

/* Full packet trace decode (only runs at LOG_TRACE level).
 * label: "RECV" or "SEND". slot: peer slot (-1 if unknown). */
void bc_log_packet_trace(const bc_packet_t *pkt, int slot, const char *label);

/* Monotonic millisecond clock (used for timeouts and retransmit timers).
 * Wraps GetTickCount() on Windows, clock_gettime(CLOCK_MONOTONIC) on POSIX. */
u32 bc_ms_now(void);

#endif /* OPENBC_LOG_H */
