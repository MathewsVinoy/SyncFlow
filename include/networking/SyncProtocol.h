#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace syncflow::protocol {

enum class MessageType {
	Hello,
	Heartbeat,
	MetadataPush,
	MetadataRequest,
	TransferRequest,
	TransferChunk,
	TransferComplete,
	DeleteNotice,
	Ack,
	Error
};

struct FileMetadata {
	std::string relativePath;
	std::uint64_t size = 0;
	std::int64_t modifiedUnixSeconds = 0;
	std::string hash;
	std::string lastEditorDeviceId;
};

struct SyncMessage {
	MessageType type = MessageType::Error;
	std::string requestId;
	std::string sourceDeviceId;
	std::string destinationDeviceId;
	std::string payload;
	std::uint64_t offset = 0;
	bool finalChunk = false;
	std::vector<FileMetadata> files;
};

std::string encode(const SyncMessage& message);
std::optional<SyncMessage> decode(const std::string& raw);

}  // namespace syncflow::protocol
