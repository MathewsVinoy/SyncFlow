#include "networking/SyncProtocol.h"

#include <sstream>
#include <string>

namespace syncflow::protocol {
namespace {

std::string typeToString(MessageType type) {
	switch (type) {
		case MessageType::Hello:
			return "HELLO";
		case MessageType::Heartbeat:
			return "HEARTBEAT";
		case MessageType::MetadataPush:
			return "META_PUSH";
		case MessageType::MetadataRequest:
			return "META_REQ";
		case MessageType::TransferRequest:
			return "TX_REQ";
		case MessageType::TransferChunk:
			return "TX_CHUNK";
		case MessageType::TransferComplete:
			return "TX_DONE";
		case MessageType::DeleteNotice:
			return "DELETE";
		case MessageType::Ack:
			return "ACK";
		case MessageType::Error:
		default:
			return "ERROR";
	}
}

MessageType stringToType(const std::string& value) {
	if (value == "HELLO") {
		return MessageType::Hello;
	}
	if (value == "HEARTBEAT") {
		return MessageType::Heartbeat;
	}
	if (value == "META_PUSH") {
		return MessageType::MetadataPush;
	}
	if (value == "META_REQ") {
		return MessageType::MetadataRequest;
	}
	if (value == "TX_REQ") {
		return MessageType::TransferRequest;
	}
	if (value == "TX_CHUNK") {
		return MessageType::TransferChunk;
	}
	if (value == "TX_DONE") {
		return MessageType::TransferComplete;
	}
	if (value == "DELETE") {
		return MessageType::DeleteNotice;
	}
	if (value == "ACK") {
		return MessageType::Ack;
	}
	return MessageType::Error;
}

}  // namespace

std::string encode(const SyncMessage& message) {
	std::ostringstream out;
	out << typeToString(message.type) << '|'
	    << message.requestId << '|'
	    << message.sourceDeviceId << '|'
	    << message.destinationDeviceId << '|'
	    << message.offset << '|'
	    << (message.finalChunk ? 1 : 0) << '|'
	    << message.payload;

	if (!message.files.empty()) {
		out << '|';
		for (std::size_t i = 0; i < message.files.size(); ++i) {
			const auto& file = message.files[i];
			out << file.relativePath << ','
			    << file.size << ','
			    << file.modifiedUnixSeconds << ','
			    << file.hash << ','
			    << file.lastEditorDeviceId;
			if (i + 1 != message.files.size()) {
				out << ';';
			}
		}
	}

	return out.str();
}

std::optional<SyncMessage> decode(const std::string& raw) {
	std::stringstream line(raw);
	std::string token;
	std::vector<std::string> tokens;
	while (std::getline(line, token, '|')) {
		tokens.push_back(token);
	}

	if (tokens.size() < 7) {
		return std::nullopt;
	}

	SyncMessage message;
	message.type = stringToType(tokens[0]);
	message.requestId = tokens[1];
	message.sourceDeviceId = tokens[2];
	message.destinationDeviceId = tokens[3];

	try {
		message.offset = static_cast<std::uint64_t>(std::stoull(tokens[4]));
		message.finalChunk = tokens[5] == "1";
	} catch (...) {
		return std::nullopt;
	}

	message.payload = tokens[6];
	if (tokens.size() < 8 || tokens[7].empty()) {
		return message;
	}

	std::stringstream filesStream(tokens[7]);
	std::string fileToken;
	while (std::getline(filesStream, fileToken, ';')) {
		if (fileToken.empty()) {
			continue;
		}
		std::stringstream fileFields(fileToken);
		std::vector<std::string> fields;
		while (std::getline(fileFields, token, ',')) {
			fields.push_back(token);
		}
		if (fields.size() != 5) {
			continue;
		}

		FileMetadata metadata;
		metadata.relativePath = fields[0];
		metadata.hash = fields[3];
		metadata.lastEditorDeviceId = fields[4];
		try {
			metadata.size = static_cast<std::uint64_t>(std::stoull(fields[1]));
			metadata.modifiedUnixSeconds = std::stoll(fields[2]);
		} catch (...) {
			continue;
		}
		message.files.push_back(std::move(metadata));
	}

	return message;
}

}  // namespace syncflow::protocol
