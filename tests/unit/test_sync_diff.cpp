#include <gtest/gtest.h>
#include <syncflow/sync_diff_algorithm.hpp>

namespace {

class SyncDiffTest : public ::testing::Test {
 protected:
  syncflow::FileMetadata local_meta_, remote_meta_;
  syncflow::VersionVector local_vv_, remote_vv_;

  void SetUp() override {
    local_meta_.file_id = "file1";
    local_meta_.path = "/sync/document.txt";
    local_meta_.size = 1024;
    
    remote_meta_.file_id = "file1";
    remote_meta_.path = "/sync/document.txt";
    remote_meta_.size = 1024;
  }
};

TEST_F(SyncDiffTest, BothNotExistIsSynced) {
  auto result = syncflow::SyncDiffAlgorithm::compute_diff(
      std::nullopt, std::nullopt, local_vv_, remote_vv_);
  EXPECT_EQ(result, syncflow::SyncDiffAlgorithm::DiffResult::SYNCED);
}

TEST_F(SyncDiffTest, LocalOnlyIsNotFound) {
  auto result = syncflow::SyncDiffAlgorithm::compute_diff(
      local_meta_, std::nullopt, local_vv_, remote_vv_);
  EXPECT_EQ(result, syncflow::SyncDiffAlgorithm::DiffResult::NOT_FOUND);
}

TEST_F(SyncDiffTest, RemoteOnlyIsDeleted) {
  auto result = syncflow::SyncDiffAlgorithm::compute_diff(
      std::nullopt, remote_meta_, local_vv_, remote_vv_);
  EXPECT_EQ(result, syncflow::SyncDiffAlgorithm::DiffResult::DELETED);
}

TEST_F(SyncDiffTest, SameHashIsSynced) {
  local_meta_.file_hash = {1, 2, 3, 4};
  remote_meta_.file_hash = {1, 2, 3, 4};
  
  auto result = syncflow::SyncDiffAlgorithm::compute_diff(
      local_meta_, remote_meta_, local_vv_, remote_vv_);
  EXPECT_EQ(result, syncflow::SyncDiffAlgorithm::DiffResult::SYNCED);
}

TEST_F(SyncDiffTest, RemoteNewerIsNeedDownload) {
  local_meta_.file_hash = {1, 2, 3};
  remote_meta_.file_hash = {4, 5, 6};
  
  local_vv_.update("A", 1);
  remote_vv_.update("A", 2);
  
  auto result = syncflow::SyncDiffAlgorithm::compute_diff(
      local_meta_, remote_meta_, local_vv_, remote_vv_);
  EXPECT_EQ(result, syncflow::SyncDiffAlgorithm::DiffResult::NEED_DOWNLOAD);
}

TEST_F(SyncDiffTest, LocalNewerIsNeedUpload) {
  local_meta_.file_hash = {1, 2, 3};
  remote_meta_.file_hash = {4, 5, 6};
  
  local_vv_.update("A", 2);
  remote_vv_.update("A", 1);
  
  auto result = syncflow::SyncDiffAlgorithm::compute_diff(
      local_meta_, remote_meta_, local_vv_, remote_vv_);
  EXPECT_EQ(result, syncflow::SyncDiffAlgorithm::DiffResult::NEED_UPLOAD);
}

TEST_F(SyncDiffTest, ConcurrentChangesIsConflict) {
  local_meta_.file_hash = {1, 2, 3};
  remote_meta_.file_hash = {4, 5, 6};
  
  local_vv_.update("A", 2);
  remote_vv_.update("B", 2);
  
  auto result = syncflow::SyncDiffAlgorithm::compute_diff(
      local_meta_, remote_meta_, local_vv_, remote_vv_);
  EXPECT_EQ(result, syncflow::SyncDiffAlgorithm::DiffResult::CONFLICT);
}

TEST_F(SyncDiffTest, ChunkDeltaDetectsMismatches) {
  std::vector<syncflow::FileHash> local_chunks = {
    {1, 2, 3}, {4, 5, 6}, {7, 8, 9}
  };
  std::vector<syncflow::FileHash> remote_chunks = {
    {1, 2, 3}, {0, 0, 0}, {7, 8, 9}
  };
  
  auto delta = syncflow::SyncDiffAlgorithm::compute_chunk_delta(
      "/test/file", local_chunks, remote_chunks);
  
  EXPECT_EQ(delta.size(), 1);
  EXPECT_EQ(delta[0], 1);
}

TEST_F(SyncDiffTest, EstimateTransferSizeCalculatesCorrectly) {
  std::vector<uint64_t> delta_chunks = {0, 2, 5};
  uint64_t chunk_size = 16384;
  
  auto size = syncflow::SyncDiffAlgorithm::estimate_transfer_size(
      delta_chunks, chunk_size);
  
  EXPECT_EQ(size, 3 * chunk_size);
}

}  // anonymous namespace
