#include "openbc/net.h"
#include "openbc/log.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
#endif

bool bc_net_init(void)
{
#ifdef _WIN32
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err != 0) {
        LOG_ERROR("net", "WSAStartup failed: %d", err);
        return false;
    }
#endif
    return true;
}

void bc_net_shutdown(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

bool bc_socket_open(bc_socket_t *sock, u16 port)
{
    /* Bind to all interfaces */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("net", "socket() failed: %d", WSAGetLastError());
        return false;
    }

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
#else
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == -1) {
        LOG_ERROR("net", "socket() failed: %d", errno);
        return false;
    }

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        LOG_ERROR("net", "bind() failed on port %u: %d", port, errno);
        close(s);
        return false;
    }

    /* Set non-blocking */
    int flags = fcntl(s, F_GETFL, 0);
    if (fcntl(s, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_ERROR("net", "fcntl(O_NONBLOCK) failed: %d", errno);
        close(s);
        return false;
    }

    sock->fd = s;
#endif
    return true;
}

void bc_socket_close(bc_socket_t *sock)
{
#ifdef _WIN32
    if (sock->fd != (int)INVALID_SOCKET) {
        closesocket((SOCKET)sock->fd);
        sock->fd = (int)INVALID_SOCKET;
    }
#else
    if (sock->fd != -1) {
        close(sock->fd);
        sock->fd = -1;
    }
#endif
}

int bc_socket_send(bc_socket_t *sock, const bc_addr_t *to,
                   const u8 *data, int len)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = to->port;
    addr.sin_addr.s_addr = to->ip;

#ifdef _WIN32
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
#else
    int sent = (int)sendto(sock->fd, (const void *)data, (size_t)len, 0,
                           (struct sockaddr *)&addr, sizeof(addr));
    if (sent == -1) {
        if (errno == EWOULDBLOCK) return 0;
        LOG_ERROR("net", "sendto() failed: err=%d, fd=%d, to=%u.%u.%u.%u:%u, len=%d",
                  errno, sock->fd,
                  to->ip & 0xFF, (to->ip >> 8) & 0xFF,
                  (to->ip >> 16) & 0xFF, (to->ip >> 24) & 0xFF,
                  ntohs(to->port), len);
        return -1;
    }
#endif
    return sent;
}

int bc_socket_recv(bc_socket_t *sock, bc_addr_t *from,
                   u8 *buf, int buf_size)
{
    struct sockaddr_in addr;

#ifdef _WIN32
    int addr_len = sizeof(addr);
    int received = recvfrom((SOCKET)sock->fd, (char *)buf, buf_size, 0,
                            (struct sockaddr *)&addr, &addr_len);
    if (received == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAECONNRESET) return 0;
        return -1;
    }
#else
    socklen_t addr_len = sizeof(addr);
    int received = (int)recvfrom(sock->fd, (void *)buf, (size_t)buf_size, 0,
                                 (struct sockaddr *)&addr, &addr_len);
    if (received == -1) {
        if (errno == EWOULDBLOCK || errno == ECONNRESET) return 0;
        return -1;
    }
#endif

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
