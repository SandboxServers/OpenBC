#include "openbc/cipher.h"

static const u8 ALBY_KEY[10] = {
    0x41, 0x6C, 0x62, 0x79, 0x52,  /* "AlbyR" */
    0x75, 0x6C, 0x65, 0x73, 0x21   /* "ules!" */
};

void alby_rules_cipher(u8 *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        data[i] ^= ALBY_KEY[i % 10];
    }
}
