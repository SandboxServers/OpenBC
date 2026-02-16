#ifndef OPENBC_TRANSPORT_H
#define OPENBC_TRANSPORT_H

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/buffer.h"
#include "openbc/opcodes.h"

/*
 * Transport layer -- handles UDP packet framing and reliable delivery.
 *
 * Packet format (after AlbyRules decrypt):
 *   [direction:1][msg_count:1][transport_msg...]
 *
 * Transport message types:
 *   0x01 ACK:      [0x01][seq:1][0x00][flags:1]             (4 bytes fixed)
 *   0x32 Reliable: [0x32][totalLen:1][flags:1][seqHi:1][seqLo:1][payload...]
 *   Other:         [type:1][totalLen:1][data...]
 *
 * Direction bytes:
 *   0x01 = from server
 *   0x02 = from client
 *   0xFF = initial handshake
 */

#define BC_DIR_SERVER   0x01
#define BC_DIR_CLIENT   0x02
#define BC_DIR_INIT     0xFF

#define BC_MAX_PACKET_SIZE  512

/* A parsed transport message from an incoming packet */
typedef struct {
    u8  type;           /* Transport message type (0x00, 0x01, 0x32, etc.) */
    u8  flags;          /* For reliable: reliability flags */
    u16 seq;            /* Sequence number (reliable) */
    u8 *payload;        /* Pointer to game payload (within packet buffer) */
    int payload_len;    /* Length of game payload */
} bc_transport_msg_t;

/* Incoming packet context */
typedef struct {
    u8  direction;      /* Direction byte */
    u8  msg_count;      /* Number of transport messages */
    bc_transport_msg_t msgs[32]; /* Parsed messages (32 should be plenty) */
} bc_packet_t;

/* Parse an incoming packet (already decrypted).
 * Returns true on success, fills out pkt structure. */
bool bc_transport_parse(const u8 *data, int len, bc_packet_t *pkt);

/* Build an outgoing packet with a single unreliable game message.
 * Writes to 'out' buffer. Returns total packet length, or -1 on error. */
int bc_transport_build_unreliable(u8 *out, int out_size,
                                  const u8 *payload, int payload_len);

/* Build an outgoing packet with a single reliable game message.
 * Writes to 'out' buffer. Returns total packet length, or -1 on error. */
int bc_transport_build_reliable(u8 *out, int out_size,
                                const u8 *payload, int payload_len,
                                u16 seq);

/* Build an ACK packet for a received reliable message.
 * Returns total packet length. */
int bc_transport_build_ack(u8 *out, int out_size, u16 seq, u8 flags);

#endif /* OPENBC_TRANSPORT_H */
