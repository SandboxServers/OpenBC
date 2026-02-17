#ifndef OPENBC_CIPHER_H
#define OPENBC_CIPHER_H

#include "openbc/types.h"

/*
 * AlbyRules stream cipher (TGWinsockNetwork encryption).
 *
 * All game traffic (NOT GameSpy) is encrypted with a custom stream cipher
 * using the hardcoded key "AlbyRules!".
 *
 * Critical properties:
 *   - Byte 0 of each UDP packet (direction flag) is NOT encrypted.
 *     Encryption applies to bytes 1 through len-1 only.
 *   - Per-packet reset: cipher state resets at the start of every packet.
 *   - Static key: no session randomness, no key exchange.
 *   - Plaintext feedback: each decrypted byte modifies the key state,
 *     making the cipher position-dependent (NOT a simple XOR).
 *
 * Reimplemented from transport-cipher.md specification.
 */

/* Encrypt a packet in-place for sending.
 * Byte 0 is left unchanged; bytes 1+ are encrypted. */
void alby_cipher_encrypt(u8 *data, size_t len);

/* Decrypt a received packet in-place.
 * Byte 0 is left unchanged; bytes 1+ are decrypted. */
void alby_cipher_decrypt(u8 *data, size_t len);

#endif /* OPENBC_CIPHER_H */
