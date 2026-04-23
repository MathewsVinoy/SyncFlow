#pragma once

/**
 * @file blake3.h
 * @brief Minimal BLAKE3 C interface for SyncFlow
 * 
 * This is a stub that wraps system blake3 library if available,
 * or provides minimal functionality for testing.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * BLAKE3 output size in bytes
 */
#define BLAKE3_OUT_LEN 32

/**
 * BLAKE3 block size
 */
#define BLAKE3_BLOCK_LEN 64

/**
 * BLAKE3 chunk size
 */
#define BLAKE3_CHUNK_LEN 1024

/**
 * BLAKE3 hasher structure
 */
typedef struct {
    uint32_t state[8];
    uint64_t count_high;
    uint64_t count_low;
    uint8_t buf[BLAKE3_BLOCK_LEN];
    size_t buf_len;
    size_t bytes_compressed;
    uint8_t input_cv_stack[13 * 32];
    uint8_t input_cv_stack_len;
} blake3_hasher;

/**
 * Initialize a BLAKE3 hasher
 */
void blake3_hasher_init(blake3_hasher *hasher);

/**
 * Update hasher with input data
 * 
 * @param hasher Hasher state
 * @param input Input data
 * @param input_len Length of input
 */
void blake3_hasher_update(blake3_hasher *hasher,
                          const uint8_t *input,
                          size_t input_len);

/**
 * Finalize hasher and extract digest
 * 
 * @param hasher Hasher state
 * @param out Output buffer (must be at least out_len bytes)
 * @param out_len Desired output length
 */
void blake3_hasher_finalize(blake3_hasher *hasher,
                            uint8_t *out,
                            size_t out_len);

/**
 * All-in-one BLAKE3 hash function
 * 
 * @param input Input data
 * @param input_len Length of input
 * @param out Output buffer (receives BLAKE3_OUT_LEN bytes)
 */
void blake3_hash(const uint8_t *input,
                 size_t input_len,
                 uint8_t *out);

#ifdef __cplusplus
}
#endif
