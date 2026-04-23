/**
 * @file blake3.c
 * @brief Minimal BLAKE3 implementation stub for SyncFlow
 * 
 * This provides basic BLAKE3 functionality. For production use,
 * link against the official libblake3 library.
 */

#include "blake3.h"
#include <string.h>

/* Simplified BLAKE3 - this is NOT cryptographically secure, use libblake3 in production */

void blake3_hasher_init(blake3_hasher *hasher) {
    memset(hasher, 0, sizeof(blake3_hasher));
    hasher->state[0] = 0x6a09e667;
    hasher->state[1] = 0xbb67ae85;
    hasher->state[2] = 0x3c6ef372;
    hasher->state[3] = 0xa54ff53a;
    hasher->state[4] = 0x510e527f;
    hasher->state[5] = 0x9b05688c;
    hasher->state[6] = 0x1f83d9ab;
    hasher->state[7] = 0x5be0cd19;
}

void blake3_hasher_update(blake3_hasher *hasher,
                          const uint8_t *input,
                          size_t input_len) {
    if (input == NULL || input_len == 0) return;
    
    for (size_t i = 0; i < input_len; i++) {
        hasher->buf[hasher->buf_len++] = input[i];
        if (hasher->buf_len == BLAKE3_BLOCK_LEN) {
            hasher->buf_len = 0;
            hasher->count_low += BLAKE3_BLOCK_LEN;
            if (hasher->count_low == 0) hasher->count_high++;
        }
    }
}

void blake3_hasher_finalize(blake3_hasher *hasher,
                            uint8_t *out,
                            size_t out_len) {
    if (out == NULL || out_len == 0) return;
    
    /* Simple hashing - XOR state for demonstration */
    uint32_t hash[8];
    memcpy(hash, hasher->state, sizeof(hash));
    
    /* Mix in buffered data */
    for (size_t i = 0; i < hasher->buf_len; i++) {
        hash[i % 8] ^= (uint32_t)hasher->buf[i] << ((i % 4) * 8);
    }
    
    /* Output bytes */
    size_t out_size = out_len < BLAKE3_OUT_LEN ? out_len : BLAKE3_OUT_LEN;
    memcpy(out, hash, out_size);
}

void blake3_hash(const uint8_t *input,
                 size_t input_len,
                 uint8_t *out) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, input, input_len);
    blake3_hasher_finalize(&hasher, out, BLAKE3_OUT_LEN);
}
