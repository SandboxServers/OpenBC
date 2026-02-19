#include "openbc/net.h"
#include "openbc/log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

bool bc_net_init(void)
{
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err != 0) {
        LOG_ERROR("net", "WSAStartup failed: %d", err);
        return false;
    }
    return true;
}

void bc_net_shutdown(void)
{
    WSACleanup();
}

bool bc_socket_open(bc_socket_t *sock, u16 port)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("net", "socket() failed: %d", WSAGetLastError());
        return false;
    }

    /* Bind to all interfaces */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("net", "bind() failed on port %u: %d", port, WSAGetLastError());
        closesocket(s);
        return false;
    }

    /* Set non-blocking */
    u_long mode = 1;
    if (ioctlsocket(s, FIONBIO, &mode) == SOCKET_ERROR) {
        LOG_ERROR("net", "ioctlsocket() failed: %d", WSAGetLastError());
        closesocket(s);
        return false;
    }

    sock->fd = (int)s;
    return true;
}

void bc_socket_close(bc_socket_t *sock)
{
    if (sock->fd != (int)INVALID_SOCKET) {
        closesocket((SOCKET)sock->fd);
        sock->fd = (int)INVALID_SOCKET;
    }
}

int bc_socket_send(bc_socket_t *sock, const bc_addr_t *to,
                   const u8 *data, int len)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = to->port;
    addr.sin_addr.s_addr = to->ip;

    int sent = sendto((SOCKET)sock->fd, (const char *)data, len, 0,
                      (struct sockaddr *)&addr, sizeof(addr));
    if (sent == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return 0;
        LOG_ERROR("net", "sendto() failed: err=%d, fd=%d, to=%u.%u.%u.%u:%u, len=%d",
                  err, sock->fd,
                  to->ip & 0xFF, (to->ip >> 8) & 0xFF,
                  (to->ip >> 16) & 0xFF, (to->ip >> 24) & 0xFF,
                  ntohs(to->port), len);
        return -1;
    }
    return sent;
}

int bc_socket_recv(bc_socket_t *sock, bc_addr_t *from,
                   u8 *buf, int buf_size)
{
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);

    int received = recvfrom((SOCKET)sock->fd, (char *)buf, buf_size, 0,
                            (struct sockaddr *)&addr, &addr_len);
    if (received == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAECONNRESET) return 0;
        return -1;
    }

    if (from) {
        from->ip = addr.sin_addr.s_addr;
        from->port = addr.sin_port;
    }
    return received;
}

bool bc_addr_equal(const bc_addr_t *a, const bc_addr_t *b)
{
    return a->ip == b->ip && a->port == b->port;
}

void bc_addr_to_string(const bc_addr_t *addr, char *buf, int buf_size)
{
    u32 ip = addr->ip;
    u16 port = ntohs(addr->port);
    snprintf(buf, (size_t)buf_size, "%u.%u.%u.%u:%u",
             ip & 0xFF, (ip >> 8) & 0xFF,
             (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, port);
}
