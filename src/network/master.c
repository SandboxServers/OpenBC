#include "openbc/master.h"
#include "openbc/log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

/* Default master servers: 333networks affiliates + OpenSpy */
static const char *bc_default_masters[] = {
    /* 333networks and affiliates */
    "master.333networks.com:27900",
    "master.errorist.eu:27900",
    "master.gonespy.com:27900",
    "master.newbiesplayground.net:27900",
    "master-au.unrealarchive.org:27900",
    "master.noccer.de:27900",
    "master.eatsleeput.com:27900",
    "master.frag-net.com:27900",
    "master.exsurge.net:27900",
    /* OpenSpy (independent) */
    "master.openspy.net:27900",
    NULL
};

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
        LOG_WARN("master", "DNS resolution failed for: %s", host_port);
        return false;
    }

    struct sockaddr_in *sin = (struct sockaddr_in *)result->ai_addr;
    out->ip = sin->sin_addr.s_addr;
    out->port = sin->sin_port;
    freeaddrinfo(result);

    (void)port;
    return true;
}

static void send_heartbeat_entry(bc_master_entry_t *entry, u16 game_port,
                                  bc_socket_t *sock, bool final)
{
    if (!entry->enabled) return;

    char buf[128];
    int len;
    if (final) {
        len = snprintf(buf, sizeof(buf),
                       "\\heartbeat\\%u\\gamename\\bcommander\\final\\",
                       game_port);
    } else {
        len = snprintf(buf, sizeof(buf),
                       "\\heartbeat\\%u\\gamename\\bcommander\\",
                       game_port);
    }

    bc_socket_send(sock, &entry->addr, (const u8 *)buf, len);
}

bool bc_master_add(bc_master_list_t *ml, const char *host_port, u16 game_port)
{
    if (ml->count >= BC_MAX_MASTERS) {
        LOG_WARN("master", "Maximum master servers reached (%d), ignoring: %s",
                 BC_MAX_MASTERS, host_port);
        return false;
    }

    ml->game_port = game_port;

    bc_master_entry_t *entry = &ml->entries[ml->count];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->hostname, sizeof(entry->hostname), "%s", host_port);

    LOG_INFO("master", "Resolving %s...", host_port);

    if (!resolve_address(host_port, &entry->addr)) {
        /* DNS failed -- don't add to active list */
        return false;
    }

    entry->enabled = true;
    entry->verified = false;
    entry->last_beat = 0;
    ml->count++;

    char addr_str[32];
    bc_addr_to_string(&entry->addr, addr_str, sizeof(addr_str));
    LOG_DEBUG("master", "Resolved %s -> %s", host_port, addr_str);
    return true;
}

int bc_master_init_defaults(bc_master_list_t *ml, u16 game_port)
{
    memset(ml, 0, sizeof(*ml));
    ml->game_port = game_port;

    for (int i = 0; bc_default_masters[i] != NULL; i++) {
        bc_master_add(ml, bc_default_masters[i], game_port);
    }

    return ml->count;
}

void bc_master_probe(bc_master_list_t *ml, bc_socket_t *sock)
{
    if (ml->count == 0) return;

    LOG_INFO("master", "Probing %d master server%s...",
             ml->count, ml->count == 1 ? "" : "s");

    /* Send initial heartbeat to all enabled entries */
    for (int i = 0; i < ml->count; i++) {
        send_heartbeat_entry(&ml->entries[i], ml->game_port, sock, false);
        ml->entries[i].last_beat = GetTickCount();
    }

    /* Poll for responses up to the probe timeout */
    u32 start = GetTickCount();
    int verified = 0;

    while (GetTickCount() - start < BC_MASTER_PROBE_TIMEOUT_MS) {
        u8 recv_buf[2048];
        bc_addr_t from;
        int received;

        while ((received = bc_socket_recv(sock, &from,
                                           recv_buf, sizeof(recv_buf))) > 0) {
            /* Check if this response is from a known master */
            for (int i = 0; i < ml->count; i++) {
                if (!ml->entries[i].enabled) continue;
                if (ml->entries[i].verified) continue;
                if (bc_addr_equal(&ml->entries[i].addr, &from)) {
                    ml->entries[i].verified = true;
                    verified++;
                    LOG_INFO("master", "Master %s: registered",
                             ml->entries[i].hostname);
                    break;
                }
            }
        }

        /* All verified -- no need to keep waiting */
        if (verified >= ml->count) break;

        Sleep(10);
    }

    /* Log warnings for unverified masters */
    for (int i = 0; i < ml->count; i++) {
        if (ml->entries[i].enabled && !ml->entries[i].verified) {
            LOG_WARN("master", "Master %s: no response (will continue heartbeating)",
                     ml->entries[i].hostname);
        }
    }

    LOG_INFO("master", "Master servers: %d/%d verified", verified, ml->count);
}

bool bc_master_is_from_master(const bc_master_list_t *ml, const bc_addr_t *from)
{
    for (int i = 0; i < ml->count; i++) {
        if (!ml->entries[i].enabled) continue;
        if (bc_addr_equal(&ml->entries[i].addr, from))
            return true;
    }
    return false;
}

void bc_master_tick(bc_master_list_t *ml, bc_socket_t *sock, u32 now_ms)
{
    for (int i = 0; i < ml->count; i++) {
        bc_master_entry_t *entry = &ml->entries[i];
        if (!entry->enabled) continue;

        if (now_ms - entry->last_beat >= BC_MASTER_HEARTBEAT_INTERVAL) {
            send_heartbeat_entry(entry, ml->game_port, sock, false);
            entry->last_beat = now_ms;
        }
    }
}

void bc_master_shutdown(bc_master_list_t *ml, bc_socket_t *sock)
{
    for (int i = 0; i < ml->count; i++) {
        bc_master_entry_t *entry = &ml->entries[i];
        if (!entry->enabled) continue;
        send_heartbeat_entry(entry, ml->game_port, sock, true);
        entry->enabled = false;
    }
}
