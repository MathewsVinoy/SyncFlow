#include "syncflow/chunk_hasher.hpp"
#include <blake3.h>
#include <fstream>
#include <vector>
#include <thread>
#include <algorithm>
#include <cstring>

namespace syncflow {

std::vector<FileHash> ChunkHasher::hash_file(const std::string& file_path,
                                              uint64_t chunk_size) {
  std::vector<FileHash> hashes;
  std::ifstream file(file_path, std::ios::binary);
  
  if (!file.is_open()) {
    return hashes;  // Return empty on error
  }

  std::vector<uint8_t> buffer(chunk_size);
  
  while (file.good()) {
    file.read(reinterpret_cast<char*>(buffer.data()), chunk_size);
    auto bytes_read = file.gcount();
    
    if (bytes_read > 0) {
      FileHash hash = hash_bytes(buffer.data(), bytes_read);
      hashes.push_back(hash);
    }
  }
  
  return hashes;
}

FileHash ChunkHasher::hash_bytes(const uint8_t* data, uint64_t size) {
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, data, size);
  
  uint8_t hash_bytes[BLAKE3_HASH_SIZE];
  blake3_hasher_finalize(&hasher, hash_bytes, BLAKE3_HASH_SIZE);
  
  return FileHash(hash_bytes, hash_bytes + BLAKE3_HASH_SIZE);
}

FileHash ChunkHasher::hash_file_complete(const std::string& file_path) {
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  
  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    return FileHash();  // Return empty on error
  }

  std::vector<uint8_t> buffer(DEFAULT_CHUNK_SIZE);
  while (file.good()) {
    file.read(reinterpret_cast<char*>(buffer.data()), DEFAULT_CHUNK_SIZE);
    auto bytes_read = file.gcount();
    if (bytes_read > 0) {
      blake3_hasher_update(&hasher, buffer.data(), bytes_read);
    }
  }

  uint8_t hash_bytes[BLAKE3_HASH_SIZE];
  blake3_hasher_finalize(&hasher, hash_bytes, BLAKE3_HASH_SIZE);
  
  return FileHash(hash_bytes, hash_bytes + BLAKE3_HASH_SIZE);
}

std::vector<FileHash> ChunkHasher::hash_file_parallel(
    const std::string& file_path,
    uint32_t num_threads,
    uint64_t chunk_size) {
  // Read file into memory first
  std::ifstream file(file_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return {};
  }

  uint64_t file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> file_data(file_size);
  file.read(reinterpret_cast<char*>(file_data.data()), file_size);

  // Compute number of chunks
  uint64_t num_chunks = (file_size + chunk_size - 1) / chunk_size;
  
  // Preallocate result
  std::vector<FileHash> hashes(num_chunks);

  // Hash chunks in parallel using std::transform
  auto hash_chunk = [&](uint64_t chunk_idx) {
    uint64_t offset = chunk_idx * chunk_size;
    uint64_t size = std::min(chunk_size, file_size - offset);
    hashes[chunk_idx] = hash_bytes(file_data.data() + offset, size);
  };

  // Simple parallelization: divide work among threads
  std::vector<std::thread> threads;
  uint64_t chunks_per_thread = (num_chunks + num_threads - 1) / num_threads;

  for (uint32_t t = 0; t < num_threads && t * chunks_per_thread < num_chunks; ++t) {
    threads.emplace_back([=, &hashes, &hash_chunk]() {
      uint64_t start = t * chunks_per_thread;
      uint64_t end = std::min(start + chunks_per_thread, num_chunks);
      for (uint64_t i = start; i < end; ++i) {
        uint64_t offset = i * chunk_size;
        uint64_t size = std::min(chunk_size, file_size - offset);
        hashes[i] = hash_bytes(file_data.data() + offset, size);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  return hashes;
}

}  // namespace syncflow
