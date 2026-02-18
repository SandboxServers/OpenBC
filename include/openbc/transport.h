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
 *   0x32 Game:     [0x32][flags_len:u16 LE][seq:2 if reliable][payload...]
 *                  flags_len: bit15=reliable, bit13=fragment, bits12-0=total_len
 *   Other:         [type:1][totalLen:1][flags:1][data...]
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

/* Build a ConnectAck packet for a newly connected client.
 * Format from trace: [dir=0x02][count=1][0x05][0x0A][0xC0][0x02][0x00][slot][ip:4]
 * slot is 1-based. Returns packet length (12), or -1 on error. */
int bc_transport_build_connect_ack(u8 *out, int out_size, u8 slot, u32 ip_be);

/* Build a shutdown notification packet (ConnectAck format with status=disconnect).
 * Sent to each peer on graceful server shutdown.
 * Format: [dir=0x01][count=1][0x05][0x0A][0xC0][0x00][0x00][slot][ip_be:4]
 * Returns packet length, or -1 on error. */
int bc_transport_build_shutdown_notify(u8 *out, int out_size, u8 slot, u32 ip_be);

/* --- Outbox: multi-message packet accumulator --- */

/* Outbox accumulates multiple transport messages into a single UDP packet.
 * The real BC server packs 2-80 messages per packet (57.5% carry 2+).
 * Call add_* to queue messages, then flush to send them all in one packet. */
typedef struct {
    u8  buf[BC_MAX_PACKET_SIZE];
    int pos;        /* Write cursor (starts at 2, past direction+count) */
    int msg_count;  /* Messages accumulated */
} bc_outbox_t;

/* Reset outbox to empty state. */
void bc_outbox_init(bc_outbox_t *outbox);

/* Queue an unreliable game message. Returns true on success, false if no room. */
bool bc_outbox_add_unreliable(bc_outbox_t *outbox, const u8 *payload, int len);

/* Queue a reliable game message. Returns true on success, false if no room. */
bool bc_outbox_add_reliable(bc_outbox_t *outbox, const u8 *payload, int len, u16 seq);

/* Queue an ACK for a received reliable message. Returns true on success. */
bool bc_outbox_add_ack(bc_outbox_t *outbox, u16 seq, u8 flags);

/* Queue a keepalive message (type 0x00, 2 bytes). Returns true on success. */
bool bc_outbox_add_keepalive(bc_outbox_t *outbox);

/* Flush accumulated messages to a buffer (for testing without sockets).
 * Sets buf[0]=BC_DIR_SERVER, buf[1]=msg_count, copies to out.
 * Returns packet length, or 0 if outbox is empty. Resets outbox. */
int bc_outbox_flush_to_buf(bc_outbox_t *outbox, u8 *out, int out_size);

/* Flush accumulated messages: builds packet, encrypts, sends via socket.
 * No-op if outbox is empty. Resets outbox after sending. */
void bc_outbox_flush(bc_outbox_t *outbox, bc_socket_t *sock, const bc_addr_t *to);

/* Returns true if outbox has pending messages. */
bool bc_outbox_pending(const bc_outbox_t *outbox);

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
