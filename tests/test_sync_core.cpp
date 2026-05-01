#include "networking/SyncProtocol.h"
#include "security/AuthManager.h"
#include "sync_engine/BlockIndex.h"
#include "sync_engine/FileTransfer.h"
#include "sync_engine/SyncPlanner.h"
#include "sync_engine/RemoteSync.h"

#include <cstddef>
#include "networking/PeerSyncExchange.h"
#include "networking/SyncProtocol.h"
#include "platform/FolderWatcher.h"
#include "security/AuthManager.h"
#include "sync_engine/BlockIndex.h"
#include "sync_engine/FileTransfer.h"
#include "sync_engine/RemoteSync.h"
#include "sync_engine/SyncPlanner.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>

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
	    decoded->files.front().relativePath != "notes.txt" || decoded->files.front().hash != "abc") {
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

int runCompressionTest() {
	auto root = std::filesystem::temp_directory_path() / "syncflow_test_compression";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	auto src = root / "src.bin";
	auto dst = root / "dst.bin";

	{
		std::ofstream out(src, std::ios::binary | std::ios::trunc);
		for (int i = 0; i < 4096; ++i) {
			char c = 'A';
			out.write(&c, 1);
		}
	}

	syncflow::engine::FileTransfer transfer(1024);
	std::uint64_t offset = 0;
	bool sawCompressedChunk = false;
	for (;;) {
		auto chunk = transfer.readChunk(src, offset, true);
		if (!chunk.has_value()) {
			std::cerr << "compression test failed: read failed\n";
			return 1;
		}
		sawCompressedChunk = sawCompressedChunk || chunk->compressed;
		if (!transfer.writeChunk(dst, *chunk)) {
			std::cerr << "compression test failed: write failed\n";
			return 2;
		}
		if (chunk->last) {
			break;
		}
		offset += static_cast<std::uint64_t>(chunk->bytes.size());
	}
	if (!sawCompressedChunk) {
		std::cerr << "compression test failed: no chunk was compressed\n";
		return 3;
	}
	if (transfer.fileSize(src) != transfer.fileSize(dst)) {
		std::cerr << "compression test failed: size mismatch\n";
		return 4;
	}

	std::filesystem::remove_all(root);
	return 0;
}

int runBlockIndexTest() {
	auto root = std::filesystem::temp_directory_path() / "syncflow_test_block_index";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "nested" / "child");

	{
		std::ofstream out(root / "alpha.txt", std::ios::binary | std::ios::trunc);
		out << "abcdefghijklmno";
	}
	{
		std::ofstream out(root / "nested" / "child" / "beta.txt", std::ios::binary | std::ios::trunc);
		out << "block-one-block-two-block-three";
	}

	syncflow::engine::BlockIndexStore store(root / ".syncflow" / "index.json", 4);
	auto index = store.scan(root);
	if (index.entries.empty()) {
		std::cerr << "block index test failed: no entries\n";
		return 1;
	}

	if (!store.save(index)) {
		std::cerr << "block index test failed: save failed\n";
		return 2;
	}

	auto loaded = store.load();
	if (!loaded.has_value() || loaded->entries.size() != index.entries.size()) {
		std::cerr << "block index test failed: load mismatch\n";
		return 3;
	}

	bool sawDirectory = false;
	bool sawBlockTransfer = false;
	auto remote = *loaded;
	for (auto& entry : remote.entries) {
		if (entry.isDirectory && entry.relativePath == "nested") {
			entry.deleted = true;
		}
		if (!entry.isDirectory && !entry.blocks.empty()) {
			entry.blocks.erase(entry.blocks.begin());
			entry.contentHash.clear();
		}
	}

	const auto delta = store.planDelta(index, remote);
	for (const auto& step : delta) {
		if (step.kind == syncflow::engine::BlockTransferKind::CreateDirectory && step.relativePath == "nested") {
			sawDirectory = true;
		}
		if (step.kind == syncflow::engine::BlockTransferKind::TransferBlock && step.blockIndex == 0) {
			sawBlockTransfer = true;
		}
	}

	std::filesystem::remove_all(root);
	if (!sawDirectory || !sawBlockTransfer) {
		std::cerr << "block index test failed: missing delta steps\n";
		return 4;
	}

	return 0;
}

int runPeerExchangeTest() {
	auto root = std::filesystem::temp_directory_path() / "syncflow_test_peer_exchange";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	std::ofstream out(root / "file.txt", std::ios::binary | std::ios::trunc);
	out << "peer-exchange";
	out.close();

	syncflow::networking::PeerSyncExchange exchange("dev-local", root);
	auto indexStore = syncflow::engine::BlockIndexStore(root / ".syncflow" / "index.json", 8);
	auto index = indexStore.scan(root);
	auto message = exchange.buildBlockIndexResponse(index);
	auto parsed = exchange.parseBlockIndexResponse(message);
	if (!parsed.has_value() || parsed->entries.size() != index.entries.size()) {
		std::cerr << "peer exchange test failed: block index parse mismatch\n";
		return 1;
	}

	auto req = exchange.buildBlockRequest("file.txt", 0, 0, 8);
	if (req.find("REQ_BLOCK|file.txt|0|0|8") != 0) {
		std::cerr << "peer exchange test failed: block request malformed\n";
		return 2;
	}

	std::filesystem::remove_all(root);
	return 0;
}

int runWatcherTest() {
	auto root = std::filesystem::temp_directory_path() / "syncflow_test_watcher";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);

	FolderWatcher watcher(root);
	if (!watcher.start()) {
		std::cerr << "watcher test failed: start failed\n";
		return 1;
	}

	{
		std::ofstream out(root / "watch.txt", std::ios::binary | std::ios::trunc);
		out << "one";
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	auto events = watcher.poll();
	bool sawCreate = false;
	for (const auto& event : events) {
		if (event.relativePath == "watch.txt" && event.type == FolderWatcher::EventType::Created) {
			sawCreate = true;
		}
	}

	watcher.stop();
	std::filesystem::remove_all(root);
	if (!sawCreate) {
		std::cerr << "watcher test failed: create event not seen\n";
		return 2;
	}
	return 0;
}

}  // namespace

int runRemoteSyncTest();

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
	if (runCompressionTest() != 0) {
		std::cerr << "compression test failed\n";
		return 1;
	}
	if (runBlockIndexTest() != 0) {
		std::cerr << "block index test failed\n";
		return 1;
	}
	if (runPeerExchangeTest() != 0) {
		std::cerr << "peer exchange test failed\n";
		return 1;
	}
	if (runWatcherTest() != 0) {
		std::cerr << "watcher test failed\n";
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
	std::filesystem::path tmpDir = "tmp_test_remote_sync";
	std::filesystem::remove_all(tmpDir);
	std::filesystem::create_directories(tmpDir / "device1");
	std::filesystem::create_directories(tmpDir / "device2");
	std::filesystem::create_directories(tmpDir / "device1" / "nested" / "empty_dir");

	std::ofstream f1(tmpDir / "device1" / "file1.txt");
	f1 << "content1";
	f1.close();
	std::ofstream f1nested(tmpDir / "device1" / "nested" / "inner.txt");
	f1nested << "nested-content";
	f1nested.close();

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	std::ofstream f2(tmpDir / "device2" / "file2.txt");
	f2 << "content2";
	f2.close();

	syncflow::engine::RemoteSync sync;
	auto meta1 = sync.getLocalFileMetadata(tmpDir / "device1");
	auto meta2 = sync.getLocalFileMetadata(tmpDir / "device2");

	if (meta1.empty() || meta2.empty()) {
		std::cout << "RemoteSync test failed: no metadata collected\n";
		return 1;
	}

	bool sawNestedDir = false;
	for (const auto& entry : meta1) {
		if (entry.path == "nested" && entry.isDirectory) {
			sawNestedDir = true;
			break;
		}
	}
	if (!sawNestedDir) {
		std::cout << "RemoteSync test failed: directory metadata missing\n";
		return 1;
	}

	auto plan = sync.compareMeta(meta1, meta2, tmpDir / "device1");

	bool hasDownloadFile2 = false;
	bool hasCreateRemoteDir = false;
	for (const auto& p : plan) {
		if (p.remotePath == "file2.txt" && p.action == syncflow::engine::RemoteSyncAction::DownloadFile) {
			hasDownloadFile2 = true;
			break;
		}
		if (p.localPath == "nested" && p.action == syncflow::engine::RemoteSyncAction::CreateRemoteDir) {
			hasCreateRemoteDir = true;
		}
	}

	if (!hasDownloadFile2 || !hasCreateRemoteDir) {
		std::cout << "RemoteSync test failed: expected download and directory-create plans\n";
		return 1;
	}

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
