#ifndef OPENBC_CIPHER_H
#define OPENBC_CIPHER_H

#include "openbc/types.h"

/*
 * AlbyRules XOR stream cipher.
 *
 * All game traffic (NOT GameSpy) is encrypted with a 10-byte XOR key
 * derived from "AlbyRules!". The cipher is symmetric -- encrypt and
 * decrypt are the same operation. Applied in-place.
 *
 * Key: { 0x41, 0x6C, 0x62, 0x79, 0x52, 0x75, 0x6C, 0x65, 0x73, 0x21 }
 */
void alby_rules_cipher(u8 *data, size_t len);

#endif /* OPENBC_CIPHER_H */
