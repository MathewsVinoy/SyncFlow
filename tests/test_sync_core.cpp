#include "networking/SyncProtocol.h"
#include "security/AuthManager.h"
#include "sync_engine/FileTransfer.h"
#include "sync_engine/SyncPlanner.h"
#include "sync_engine/RemoteSync.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

int runProtocolRoundtripTest() {
	syncflow::protocol::SyncMessage message;
	message.type = syncflow::protocol::MessageType::MetadataPush;
	message.requestId = "r1";
	message.sourceDeviceId = "devA";
	message.destinationDeviceId = "devB";
	message.offset = 128;
	message.finalChunk = false;
	message.payload = "meta";
	message.files.push_back({"notes.txt", 42, 1700000000, "abc", "devA"});

	const auto encoded = syncflow::protocol::encode(message);
	auto decoded = syncflow::protocol::decode(encoded);
	if (!decoded.has_value()) {
		return 1;
	}
	if (decoded->requestId != message.requestId || decoded->files.size() != 1 ||
	    decoded->files.front().relativePath != "notes.txt") {
		return 2;
	}
	return 0;
}

int runAuthTest() {
	syncflow::security::AuthManager auth("test-secret", 60);
	const auto token = auth.issue("dev-a", 1000, 7);
	if (!auth.verify(token, 1010)) {
		return 1;
	}
	if (auth.verify(token, 2000)) {
		return 2;
	}

	const auto wire = syncflow::security::AuthManager::toWire(token);
	auto parsed = syncflow::security::AuthManager::fromWire(wire);
	if (!parsed.has_value()) {
		return 3;
	}
	if (!auth.verify(*parsed, 1010)) {
		return 4;
	}
	return 0;
}

int runPlannerTest() {
	syncflow::engine::SyncPlanner planner("dev-a");
	std::unordered_map<std::string, syncflow::engine::FileMetadata> local;
	std::unordered_map<std::string, syncflow::engine::FileMetadata> remote;

	local.emplace("a.txt", syncflow::engine::FileMetadata{"a.txt", 10, 100, "h1", "dev-a", false});
	remote.emplace("a.txt", syncflow::engine::FileMetadata{"a.txt", 10, 80, "h0", "dev-b", false});
	remote.emplace("b.txt", syncflow::engine::FileMetadata{"b.txt", 5, 90, "h2", "dev-b", false});

	const auto actions = planner.plan(local, remote);
	bool sawUpload = false;
	bool sawDownload = false;
	for (const auto& action : actions) {
		if (action.relativePath == "a.txt" && action.type == syncflow::engine::ActionType::Upload) {
			sawUpload = true;
		}
		if (action.relativePath == "b.txt" && action.type == syncflow::engine::ActionType::Download) {
			sawDownload = true;
		}
	}

	return (sawUpload && sawDownload) ? 0 : 1;
}

int runTransferTest() {
	auto root = std::filesystem::temp_directory_path() / "syncflow_test_transfer";
	std::filesystem::create_directories(root);
	auto src = root / "src.bin";
	auto dst = root / "dst.bin";

	{
		std::ofstream out(src, std::ios::binary | std::ios::trunc);
		for (int i = 0; i < 10000; ++i) {
			char c = static_cast<char>(i % 251);
			out.write(&c, 1);
		}
	}

	syncflow::engine::FileTransfer transfer(2048);
	std::uint64_t offset = 0;
	for (;;) {
		auto chunk = transfer.readChunk(src, offset);
		if (!chunk.has_value()) {
			return 1;
		}
		if (!transfer.writeChunk(dst, *chunk)) {
			return 2;
		}
		if (chunk->last) {
			break;
		}
		offset += static_cast<std::uint64_t>(chunk->bytes.size());
	}

	if (transfer.fileSize(src) != transfer.fileSize(dst)) {
		return 3;
	}

	std::filesystem::remove_all(root);
	return 0;
}

}  // namespace

int main() {
	if (runProtocolRoundtripTest() != 0) {
		std::cerr << "protocol test failed\n";
		return 1;
	}
	if (runAuthTest() != 0) {
		std::cerr << "auth test failed\n";
		return 1;
	}
	if (runPlannerTest() != 0) {
		std::cerr << "planner test failed\n";
		return 1;
	}
	if (runTransferTest() != 0) {
		std::cerr << "transfer test failed\n";
		return 1;
	}
	if (runRemoteSyncTest() != 0) {
		std::cerr << "remote sync test failed\n";
		return 1;
	}
	std::cout << "all tests passed\n";
	return 0;
}

int runRemoteSyncTest() {
	// Create test directories
	std::filesystem::path tmpDir = "tmp_test_remote_sync";
	std::filesystem::remove_all(tmpDir);
	std::filesystem::create_directories(tmpDir / "device1");
	std::filesystem::create_directories(tmpDir / "device2");
	
	// Create test files on device1
	std::ofstream f1(tmpDir / "device1" / "file1.txt");
	f1 << "content1";
	f1.close();
	
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	
	// Create file2 on device2 (newer)
	std::ofstream f2(tmpDir / "device2" / "file2.txt");
	f2 << "content2";
	f2.close();
	
	// Get metadata from both devices
	syncflow::engine::RemoteSync sync;
	auto meta1 = sync.getLocalFileMetadata(tmpDir / "device1");
	auto meta2 = sync.getLocalFileMetadata(tmpDir / "device2");
	
	if (meta1.empty() || meta2.empty()) {
		std::cout << "RemoteSync test failed: no metadata collected\n";
		return 1;
	}
	
	// Compare and plan sync
	auto plan = sync.compareMeta(meta1, meta2, tmpDir / "device1");
	
	// We expect: file1.txt from device1 to upload, file2.txt from device2 to download
	bool hasDownloadFile2 = false;
	for (const auto& p : plan) {
		if (p.remotePath == "file2.txt" && 
		    p.action == syncflow::engine::SyncAction::DownloadFile) {
			hasDownloadFile2 = true;
			break;
		}
	}
	
	if (!hasDownloadFile2) {
		std::cout << "RemoteSync test failed: expected DOWNLOAD plan for file2.txt\n";
		return 1;
	}
	
	// Test encoding/decoding
	std::string encoded = sync.encodeMetadataList(meta1);
	auto decoded = sync.decodeMetadataList(encoded);
	
	if (decoded.size() != meta1.size()) {
		std::cout << "RemoteSync test failed: encode/decode mismatch\n";
		return 1;
	}
	
	std::filesystem::remove_all(tmpDir);
	std::cout << "RemoteSync test passed\n";
	return 0;
}
