#include "openbc/cipher.h"
#include <string.h>

/*
 * AlbyRules stream cipher -- reimplemented from protocol specification.
 *
 * Per-byte cipher with PRNG-derived keystream and plaintext feedback.
 * The key "AlbyRules!" is split into 5 key words, which drive a
 * cross-multiplication PRNG. Each byte's plaintext feeds back into
 * the key, making the cipher position-dependent.
 *
 * Byte 0 of each UDP packet (direction flag: 0x01/0x02/0xFF) is NOT
 * encrypted -- the transport layer skips it before calling the cipher.
 */

static const char ALBY_KEY[10] = { 'A','l','b','y','R','u','l','e','s','!' };

typedef struct {
    int           temp_a;
    int           temp_c;
    int           temp_d;
    int           state_a;
    int           running_sum;
    int           key_word[5];
    unsigned int  prng_output;
    int           round_counter;
    unsigned int  accumulator;
    unsigned char key_string[10];
    int           byte_state;
} cipher_state_t;

static void cipher_reset(cipher_state_t *s)
{
    memset(s, 0, sizeof(*s));
    memcpy(s->key_string, ALBY_KEY, 10);
}

/* LCG-variant PRNG step.
 * Multiplier 0x4E35, addend 0x15A, cross-multiplication. */
static void cipher_prng_step(cipher_state_t *s)
{
    int rnd     = s->round_counter;
    int kw      = s->key_word[rnd];
    int mix     = s->running_sum + rnd;
    int cross1  = mix * 0x4E35;
    int cross2  = kw * 0x15A;
    int new_rsum = s->state_a + cross1 + cross2;
    int new_kw   = kw * 0x4E35 + 1;

    s->running_sum  = new_rsum;
    s->state_a      = cross2;
    s->key_word[rnd] = new_kw;
    s->prng_output  = (unsigned int)new_rsum ^ (unsigned int)new_kw;
    s->round_counter = rnd + 1;
}

/* Key schedule -- see transport-cipher.md.
 * Derives 5 key words from key_string, runs 5 PRNG rounds,
 * accumulates XOR of all PRNG outputs. */
static void cipher_key_schedule(cipher_state_t *s)
{
    unsigned char *k = s->key_string;

    s->key_word[0] = (int)((unsigned int)k[0] * 256 + k[1]);
    cipher_prng_step(s);
    s->accumulator = s->prng_output;

    s->key_word[1] = (int)(((unsigned int)k[2] * 256 + k[3])
                           ^ (unsigned int)s->key_word[0]);
    cipher_prng_step(s);
    s->accumulator ^= s->prng_output;

    s->key_word[2] = (int)(((unsigned int)k[4] * 256 + k[5])
                           ^ (unsigned int)s->key_word[1]);
    cipher_prng_step(s);
    s->accumulator ^= s->prng_output;

    s->key_word[3] = (int)(((unsigned int)k[6] * 256 + k[7])
                           ^ (unsigned int)s->key_word[2]);
    cipher_prng_step(s);
    s->accumulator ^= s->prng_output;

    s->key_word[4] = (int)(((unsigned int)k[8] * 256 + k[9])
                           ^ (unsigned int)s->key_word[3]);
    cipher_prng_step(s);
    s->round_counter = 0;
    s->accumulator ^= s->prng_output;
}

/* Encrypt payload bytes in-place.
 * Per-byte: key_schedule → XOR with accumulator → feed plaintext into key. */
static void encrypt_payload(u8 *data, int len)
{
    cipher_state_t s;
    cipher_reset(&s);

    for (int i = 0; i < len; i++) {
        s.byte_state = (int)(signed char)data[i];  /* MOVSX: sign-extend plaintext */
        cipher_key_schedule(&s);

        /* Save plaintext for feedback */
        unsigned char pt = data[i];

        /* XOR with accumulator low byte and high byte → ciphertext */
        s.byte_state = (int)((unsigned int)s.byte_state
                             ^ (s.accumulator & 0xFF)
                             ^ (s.accumulator >> 8));
        data[i] = (unsigned char)s.byte_state;

        /* Feed PLAINTEXT into key (not ciphertext) */
        for (int j = 0; j < 10; j++)
            s.key_string[j] ^= pt;
    }
}

/* Decrypt payload bytes in-place.
 * Per-byte: key_schedule → XOR with accumulator → feed plaintext into key. */
static void decrypt_payload(u8 *data, int len)
{
    cipher_state_t s;
    cipher_reset(&s);

    for (int i = 0; i < len; i++) {
        s.byte_state = (int)(signed char)data[i];  /* MOVSX: sign-extend ciphertext */
        cipher_key_schedule(&s);

        /* XOR with accumulator low byte and high byte → plaintext */
        s.byte_state = (int)((unsigned int)s.byte_state
                             ^ (s.accumulator & 0xFF)
                             ^ (s.accumulator >> 8));
        data[i] = (unsigned char)s.byte_state;

        /* Feed PLAINTEXT into key (byte_state is now plaintext) */
        for (int j = 0; j < 10; j++)
            s.key_string[j] ^= (unsigned char)s.byte_state;
    }
}

void alby_cipher_encrypt(u8 *data, size_t len)
{
    if (len <= 1) return;
    /* Byte 0 (direction flag) is NOT encrypted */
    encrypt_payload(data + 1, (int)(len - 1));
}

void alby_cipher_decrypt(u8 *data, size_t len)
{
    if (len <= 1) return;
    /* Byte 0 (direction flag) is NOT encrypted */
    decrypt_payload(data + 1, (int)(len - 1));
}
