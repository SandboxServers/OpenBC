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
 *   0x01 ACK:      [0x01][counter:1][0x00][flags:1]          (4 bytes fixed)
 *   0x32 Reliable: [0x32][totalLen:1][flags:1][counter:1][0x00][payload...]
 *   Other:         [type:1][totalLen:1][data...]
 *
 * Reliable sequence numbering:
 *   Counter starts at 0 and increments by 1 for each reliable message sent.
 *   On the wire: seqHi = counter, seqLo = 0 (wire value increments by 256).
 *   ACK references the counter byte (seqHi).
 *
 * Direction bytes:
 *   0x01 = from server
 *   0x02 = from client (0x02 + peer_index for multiple clients)
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

/* --- Fragment reassembly --- */

/* Fragment reassembly buffer for large reliable messages.
 * BC fragments messages that exceed ~500 bytes (e.g. checksum round 2
 * with 102 files). Fragments arrive as consecutive reliable messages
 * with the FRAGMENT flag (0x02) set. */
#define BC_FRAGMENT_BUF_SIZE  4096

typedef struct {
    u8   buf[BC_FRAGMENT_BUF_SIZE];  /* Reassembly buffer */
    int  buf_len;                     /* Current data length in buffer */
    u8   frags_expected;              /* Total fragments (from first fragment) */
    u8   frags_received;              /* Fragments received so far */
    bool active;                      /* Currently reassembling */
} bc_fragment_buf_t;

/* Reset a fragment reassembly buffer. */
void bc_fragment_reset(bc_fragment_buf_t *frag);

/* Process a fragment from a reliable message with the FRAGMENT flag set.
 * Appends payload data to the reassembly buffer.
 *
 * For the first fragment: reads total_frags from payload[0], stores payload[1..].
 * For subsequent fragments: reads frag_idx from payload[0], stores payload[1..].
 *
 * Returns:
 *   true  if all fragments received (complete message in frag->buf, frag->buf_len)
 *   false if still waiting for more fragments (or on error) */
bool bc_fragment_receive(bc_fragment_buf_t *frag,
                         const u8 *payload, int payload_len);

#endif /* OPENBC_TRANSPORT_H */
