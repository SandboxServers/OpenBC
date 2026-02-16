#include "openbc/master.h"
#include "openbc/log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <winsock2.h>
#include <ws2tcpip.h>

static bool resolve_address(const char *host_port, bc_addr_t *out)
{
    /* Parse "host:port" */
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", host_port);

    char *colon = strrchr(buf, ':');
    if (!colon) {
        LOG_ERROR("master", "Invalid address (no port): %s", host_port);
        return false;
    }
    *colon = '\0';
    const char *host = buf;
    u16 port = (u16)atoi(colon + 1);
    if (port == 0) {
        LOG_ERROR("master", "Invalid port in: %s", host_port);
        return false;
    }

    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(host, colon + 1, &hints, &result) != 0) {
        LOG_ERROR("master", "DNS resolution failed for: %s", host);
        return false;
    }

    struct sockaddr_in *sin = (struct sockaddr_in *)result->ai_addr;
    out->ip = sin->sin_addr.s_addr;
    out->port = sin->sin_port;
    freeaddrinfo(result);

    (void)port;
    return true;
}

bool bc_master_init(bc_master_t *ms, const char *host_port, u16 game_port)
{
    memset(ms, 0, sizeof(*ms));
    ms->game_port = game_port;

    if (!resolve_address(host_port, &ms->addr)) {
        ms->enabled = false;
        ms->resolved = false;
        return false;
    }

    ms->enabled = true;
    ms->resolved = true;
    ms->last_beat = 0;

    char addr_str[32];
    bc_addr_to_string(&ms->addr, addr_str, sizeof(addr_str));
    LOG_INFO("master", "Registered with master server at %s", addr_str);
    return true;
}

static void send_heartbeat(bc_master_t *ms, bc_socket_t *sock, bool final)
{
    if (!ms->enabled || !ms->resolved) return;

    char buf[128];
    int len;
    if (final) {
        len = snprintf(buf, sizeof(buf),
                       "\\heartbeat\\%u\\gamename\\bcommander\\final\\",
                       ms->game_port);
    } else {
        len = snprintf(buf, sizeof(buf),
                       "\\heartbeat\\%u\\gamename\\bcommander\\",
                       ms->game_port);
    }

    bc_socket_send(sock, &ms->addr, (const u8 *)buf, len);
}

void bc_master_tick(bc_master_t *ms, bc_socket_t *sock, u32 now_ms)
{
    if (!ms->enabled) return;

    if (now_ms - ms->last_beat >= BC_MASTER_HEARTBEAT_INTERVAL) {
        send_heartbeat(ms, sock, false);
        ms->last_beat = now_ms;
    }
}

void bc_master_shutdown(bc_master_t *ms, bc_socket_t *sock)
{
    if (!ms->enabled) return;
    send_heartbeat(ms, sock, true);
    ms->enabled = false;
}
