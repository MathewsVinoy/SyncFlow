#include "networking/SyncProtocol.h"
#include "security/AuthManager.h"
#include "sync_engine/FileTransfer.h"
#include "sync_engine/SyncPlanner.h"

#include <cstddef>
#include <cstdint>
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
	std::cout << "all tests passed\n";
	return 0;
}
