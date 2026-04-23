#include <gtest/gtest.h>
#include <syncflow/chunk_hasher.hpp>
#include <fstream>
#include <cstring>

namespace {

// Test fixture for chunk hasher tests
class ChunkHasherTest : public ::testing::Test {
 protected:
  std::string test_file_;

  void SetUp() override {
    test_file_ = "/tmp/test_chunk_file.bin";
    // Create a test file with known content
    std::ofstream f(test_file_, std::ios::binary);
    std::string data = "Hello, World! This is test data for chunk hashing.";
    f.write(data.c_str(), data.size());
    f.close();
  }

  void TearDown() override {
    std::remove(test_file_.c_str());
  }
};

TEST_F(ChunkHasherTest, HashFileReturnsHashes) {
  auto hashes = syncflow::ChunkHasher::hash_file(test_file_);
  EXPECT_GT(hashes.size(), 0);
  EXPECT_EQ(hashes[0].size(), 32);  // BLAKE3 = 256 bits = 32 bytes
}

TEST_F(ChunkHasherTest, HashBytesProducesDeterministicHash) {
  std::string data = "Test data for hashing";
  uint8_t* ptr = reinterpret_cast<uint8_t*>(data.data());
  
  auto hash1 = syncflow::ChunkHasher::hash_bytes(ptr, data.size());
  auto hash2 = syncflow::ChunkHasher::hash_bytes(ptr, data.size());
  
  EXPECT_EQ(hash1, hash2);
  EXPECT_EQ(hash1.size(), 32);
}

TEST_F(ChunkHasherTest, HashCompleteFileMatches) {
  auto full_hash = syncflow::ChunkHasher::hash_file_complete(test_file_);
  EXPECT_EQ(full_hash.size(), 32);
  EXPECT_GT(full_hash.size(), 0);
}

TEST_F(ChunkHasherTest, DifferentDataProduceDifferentHashes) {
  std::string data1 = "Data 1";
  std::string data2 = "Data 2";
  
  auto hash1 = syncflow::ChunkHasher::hash_bytes(
    reinterpret_cast<uint8_t*>(data1.data()), data1.size());
  auto hash2 = syncflow::ChunkHasher::hash_bytes(
    reinterpret_cast<uint8_t*>(data2.data()), data2.size());
  
  EXPECT_NE(hash1, hash2);
}

}  // anonymous namespace
