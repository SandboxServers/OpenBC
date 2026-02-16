#ifndef OPENBC_CLIENT_TRANSPORT_H
#define OPENBC_CLIENT_TRANSPORT_H

#include "openbc/types.h"

/*
 * Client-side transport packet builders.
 *
 * The server-side transport builders (transport.h) hardcode direction=0x01.
 * Clients use direction=0xFF (init) or 0x02+slot (game traffic).
 * These builders produce client-perspective packets for test harnesses.
 *
 * All functions return packet length on success, or -1 on error.
 */

/* Build a Connect packet.
 * Wire: [dir=0xFF][count=1][type=0x03][totalLen=8][flags=0x01][pad][pad][pad][ip:4]
 * The client sends this to initiate a connection. */
int bc_client_build_connect(u8 *out, int out_size, u32 local_ip);

/* Build a Keepalive with embedded player name (UTF-16LE).
 * Wire: [dir=0x02+slot][count=1][type=0x00][totalLen][flags=0x80]
 *   [pad:2][slot?:1][ip:4][name_utf16le...]
 * Sent during handshake so the server learns the player name. */
int bc_client_build_keepalive_name(u8 *out, int out_size, u8 slot,
                                    u32 local_ip, const char *name);

/* Build a reliable game message with client direction byte.
 * Wire: [dir=0x02+slot][count=1][0x32][totalLen][flags=0x80][seqHi][0x00][payload]
 * Uses the same envelope format as server-side reliable. */
int bc_client_build_reliable(u8 *out, int out_size,
                              u8 slot, const u8 *payload, int payload_len, u16 seq);

/* Build an unreliable game message with client direction byte.
 * Wire: [dir=0x02+slot][count=1][type=0x00][totalLen][payload] */
int bc_client_build_unreliable(u8 *out, int out_size,
                                u8 slot, const u8 *payload, int payload_len);

/* Build an ACK with client direction byte.
 * Wire: [dir=0x02+slot][count=1][0x01][counter][0x00][flags] */
int bc_client_build_ack(u8 *out, int out_size, u8 slot, u16 seq, u8 flags);

/* Build a dummy checksum response for round 0-3.
 * Wire: [0x21][round][ref_hash:u32][dir_hash:u32][file_count=0:u16]
 * Returns a valid-format response with zero files (passes --no-checksum). */
int bc_client_build_dummy_checksum_resp(u8 *buf, int buf_size, u8 round);

/* Build a dummy checksum response for the final round (0xFF).
 * Wire: [0x21][0xFF][dir_hash=0:u32][file_count=0:u32]
 * Returns a valid-format final response (passes --no-checksum). */
int bc_client_build_dummy_checksum_final(u8 *buf, int buf_size);

#endif /* OPENBC_CLIENT_TRANSPORT_H */
