#ifndef OPENBC_NET_H
#define OPENBC_NET_H

#include "openbc/types.h"

/*
 * UDP socket abstraction for BC protocol.
 *
 * Single non-blocking UDP socket shared between GameSpy queries and
 * game protocol. First-byte demultiplexing: '\' prefix = GameSpy
 * (plaintext), otherwise = TGNetwork packet (AlbyRules encrypted).
 *
 * Uses Winsock2 on Windows (cross-compiled via mingw).
 */

/* Network address (IPv4) */
typedef struct {
    u32 ip;     /* Network byte order */
    u16 port;   /* Network byte order */
} bc_addr_t;

/* UDP socket */
typedef struct {
    int fd;     /* SOCKET handle (cast from SOCKET type) */
} bc_socket_t;

/* Initialize networking subsystem (calls WSAStartup on Windows) */
bool bc_net_init(void);

/* Shutdown networking subsystem (calls WSACleanup on Windows) */
void bc_net_shutdown(void);

/* Create and bind a non-blocking UDP socket */
bool bc_socket_open(bc_socket_t *sock, u16 port);

/* Close a socket */
void bc_socket_close(bc_socket_t *sock);

/* Send data to an address. Returns bytes sent, or -1 on error. */
int bc_socket_send(bc_socket_t *sock, const bc_addr_t *to,
                   const u8 *data, int len);

/* Receive data. Returns bytes read, 0 if nothing available, -1 on error.
 * Fills 'from' with sender address. */
int bc_socket_recv(bc_socket_t *sock, bc_addr_t *from,
                   u8 *buf, int buf_size);

/* Address utilities */
bool bc_addr_equal(const bc_addr_t *a, const bc_addr_t *b);
void bc_addr_to_string(const bc_addr_t *addr, char *buf, int buf_size);

#endif /* OPENBC_NET_H */
