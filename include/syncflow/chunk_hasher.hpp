#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <array>
#include "types.hpp"

namespace syncflow {

// ============================================================================
// BLAKE3 Chunk Hasher
// ============================================================================

/**
 * @brief Computes BLAKE3 hashes for file chunks (fixed + rolling hash).
 *
 * Strategy:
 * 1. Split files into 16KB fixed chunks
 * 2. Compute BLAKE3 hash for each chunk
 * 3. For delta sync: only re-hash if file size/mtime changed
 */
class ChunkHasher {
 public:
  static constexpr uint64_t DEFAULT_CHUNK_SIZE = 16384;  // 16KB
  static constexpr uint64_t BLAKE3_HASH_SIZE = 32;       // 256 bits

  /**
   * @brief Hash a file and return all chunk hashes
   * @param file_path Path to file to hash
   * @param chunk_size Size of chunks (default 16KB)
   * @return Vector of chunk hashes, or empty on error
   */
  static std::vector<FileHash> hash_file(const std::string& file_path,
                                         uint64_t chunk_size = DEFAULT_CHUNK_SIZE);

  /**
   * @brief Compute BLAKE3 hash of raw bytes
   * @param data Bytes to hash
   * @param size Size of data
   * @return 32-byte BLAKE3 hash
   */
  static FileHash hash_bytes(const uint8_t* data, uint64_t size);

  /**
   * @brief Compute hash of entire file (for verification)
   * @param file_path Path to file
   * @return 32-byte BLAKE3 hash of complete file
   */
  static FileHash hash_file_complete(const std::string& file_path);

  /**
   * @brief Parallel hash computation using thread pool
   * @param file_path File to hash
   * @param num_threads Number of threads to use
   * @param chunk_size Chunk size
   * @return Vector of hashes
   */
  static std::vector<FileHash> hash_file_parallel(
      const std::string& file_path,
      uint32_t num_threads,
      uint64_t chunk_size = DEFAULT_CHUNK_SIZE);

 private:
  ChunkHasher() = delete;
};

}  // namespace syncflow
