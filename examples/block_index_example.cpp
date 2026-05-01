#include "core/Logger.h"
#include "sync_engine/BlockIndex.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
	const auto root = argc > 1 ? std::filesystem::path(argv[1]) : (std::filesystem::temp_directory_path() / "syncflow_block_example");
	const auto indexFile = root / ".syncflow" / "index.json";
	std::filesystem::create_directories(root);
	std::filesystem::create_directories(root / "docs");

	if (!std::filesystem::exists(root / "docs" / "hello.txt")) {
		std::ofstream out(root / "docs" / "hello.txt", std::ios::binary | std::ios::trunc);
		out << "hello block sync\n";
	}
	if (!std::filesystem::exists(root / "root.txt")) {
		std::ofstream out(root / "root.txt", std::ios::binary | std::ios::trunc);
		out << "first line\nsecond line\n";
	}

	syncflow::engine::BlockIndexStore store(indexFile, 8);
	const auto local = store.scan(root);
	store.save(local);

	const auto loaded = store.load();
	if (!loaded.has_value()) {
		std::cerr << "failed to reload block index\n";
		return 1;
	}

	std::cout << "Root: " << loaded->rootPath << '\n';
	std::cout << "Entries: " << loaded->entries.size() << '\n';
	for (const auto& entry : loaded->entries) {
		std::cout << (entry.isDirectory ? "DIR  " : "FILE ") << entry.relativePath;
		if (!entry.isDirectory) {
			std::cout << " size=" << entry.size << " blocks=" << entry.blocks.size();
		}
		std::cout << '\n';
	}

	if (!loaded->entries.empty()) {
		// Simulate a remote peer missing the first block of the first file.
		auto remote = *loaded;
		for (auto& entry : remote.entries) {
			if (!entry.isDirectory) {
				if (!entry.blocks.empty()) {
					entry.blocks.erase(entry.blocks.begin());
				}
				entry.contentHash.clear();
				break;
			}
		}

		const auto delta = store.planDelta(*loaded, remote);
		std::cout << "Delta steps: " << delta.size() << '\n';
		for (const auto& step : delta) {
			std::cout << "  - ";
			switch (step.kind) {
				case syncflow::engine::BlockTransferKind::CreateDirectory:
					std::cout << "create-dir ";
					break;
				case syncflow::engine::BlockTransferKind::DeleteEntry:
					std::cout << "delete ";
					break;
				case syncflow::engine::BlockTransferKind::TransferBlock:
					std::cout << "block ";
					break;
			}
			std::cout << step.relativePath << " index=" << step.blockIndex << " offset=" << step.offset
			          << " size=" << step.size << '\n';
		}
	}

	Logger::shutdown();
	return 0;
}
