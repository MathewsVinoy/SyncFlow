// src/transfer/transfer_protocol.cpp

#include <syncflow/transfer/transfer.h>
#include <syncflow/common/utils.h>

namespace syncflow::transfer {

std::vector<uint8_t> TransferProtocol::encode_handshake(const DeviceInfo& local_info) {
    utils::BinaryWriter writer;
    
    writer.write_uint8(HANDSHAKE_REQ);
    writer.write_uint32(HANDSHAKE_MAGIC);
    writer.write_uint32(PROTOCOL_VERSION);
    writer.write_string(local_info.id);
    writer.write_string(local_info.name);
    writer.write_string(local_info.hostname);
    writer.write_uint8((uint8_t)local_info.platform);
    writer.write_string(local_info.version);
    
    return writer.get_buffer();
}

std::vector<uint8_t> TransferProtocol::encode_file_offer(const FileMetadata& file,
                                                         const std::string& dest_path) {
    utils::BinaryWriter writer;
    
    writer.write_uint8(FILE_OFFER);
    writer.write_string(file.path);
    writer.write_string(file.id);
    writer.write_uint64(file.size);
    writer.write_uint64((uint64_t)std::chrono::system_clock::to_time_t(file.modified_time));
    writer.write_uint32(file.crc32);
    writer.write_uint8(file.is_directory ? 1 : 0);
    writer.write_string(dest_path);
    
    return writer.get_buffer();
}

std::vector<uint8_t> TransferProtocol::encode_chunk_data(const ChunkInfo& chunk,
                                                         const std::vector<uint8_t>& data) {
    utils::BinaryWriter writer;
    
    writer.write_uint8(CHUNK_DATA);
    writer.write_uint32(chunk.id);
    writer.write_uint64(chunk.offset);
    writer.write_uint32((uint32_t)data.size());
    writer.write_uint32(chunk.crc32);
    writer.write_uint8(chunk.is_compressed ? 1 : 0);
    writer.write_bytes(data);
    
    return writer.get_buffer();
}

std::vector<uint8_t> TransferProtocol::encode_transfer_complete(const SessionID& session_id) {
    utils::BinaryWriter writer;
    
    writer.write_uint8(TRANSFER_COMPLETE);
    writer.write_string(session_id);
    
    return writer.get_buffer();
}

bool TransferProtocol::decode_handshake(const std::vector<uint8_t>& data, DeviceInfo& info) {
    utils::BinaryReader reader(data);
    
    uint8_t msg_type;
    uint32_t magic, version;
    
    if (!reader.read_uint8(msg_type) || msg_type != HANDSHAKE_REQ) {
        return false;
    }
    
    if (!reader.read_uint32(magic) || magic != HANDSHAKE_MAGIC) {
        return false;
    }
    
    if (!reader.read_uint32(version) || version != PROTOCOL_VERSION) {
        return false;
    }
    
    if (!reader.read_string(info.id) ||
        !reader.read_string(info.name) ||
        !reader.read_string(info.hostname)) {
        return false;
    }
    
    uint8_t platform_byte;
    if (!reader.read_uint8(platform_byte)) {
        return false;
    }
    info.platform = (PlatformType)platform_byte;
    
    if (!reader.read_string(info.version)) {
        return false;
    }
    
    return true;
}

bool TransferProtocol::decode_file_offer(const std::vector<uint8_t>& data,
                                         FileMetadata& file,
                                         std::string& dest_path) {
    utils::BinaryReader reader(data);
    
    uint8_t msg_type;
    if (!reader.read_uint8(msg_type) || msg_type != FILE_OFFER) {
        return false;
    }
    
    if (!reader.read_string(file.path) ||
        !reader.read_string(file.id)) {
        return false;
    }
    
    uint64_t size, mtime;
    if (!reader.read_uint64(size) || !reader.read_uint64(mtime)) {
        return false;
    }
    
    file.size = size;
    file.modified_time = std::chrono::system_clock::from_time_t(mtime);
    
    if (!reader.read_uint32(file.crc32)) {
        return false;
    }
    
    uint8_t is_dir;
    if (!reader.read_uint8(is_dir)) {
        return false;
    }
    file.is_directory = (is_dir != 0);
    
    if (!reader.read_string(dest_path)) {
        return false;
    }
    
    return true;
}

bool TransferProtocol::decode_chunk_data(const std::vector<uint8_t>& data,
                                         ChunkInfo& chunk,
                                         std::vector<uint8_t>& chunk_data) {
    utils::BinaryReader reader(data);
    
    uint8_t msg_type;
    uint32_t data_size;
    
    if (!reader.read_uint8(msg_type) || msg_type != CHUNK_DATA) {
        return false;
    }
    
    if (!reader.read_uint32(chunk.id) ||
        !reader.read_uint64(chunk.offset) ||
        !reader.read_uint32(data_size)) {
        return false;
    }
    
    chunk.size = data_size;
    
    if (!reader.read_uint32(chunk.crc32)) {
        return false;
    }
    
    uint8_t is_compressed;
    if (!reader.read_uint8(is_compressed)) {
        return false;
    }
    chunk.is_compressed = (is_compressed != 0);
    
    if (!reader.read_bytes(chunk_data, data_size)) {
        return false;
    }
    
    return true;
}

} // namespace syncflow::transfer
