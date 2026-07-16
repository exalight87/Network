#include <test_curl/kernel/Http2.hpp>
#include <cstring>
#include <stdexcept>
#include <iostream>
#ifdef _WIN32
#include <Winsock2.h>
#else
#include <arpa/inet.h>
#endif
// Helper function to convert big-endian
static uint32_t readUint32BE(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static uint16_t readUint16BE(const uint8_t* data) {
    return (data[0] << 8) | data[1];
}

static void writeUint32BE(uint8_t* data, uint32_t value) {
    data[0] = (value >> 24) & 0xFF;
    data[1] = (value >> 16) & 0xFF;
    data[2] = (value >> 8) & 0xFF;
    data[3] = value & 0xFF;
}

static void writeUint16BE(uint8_t* data, uint16_t value) {
    data[0] = (value >> 8) & 0xFF;
    data[1] = value & 0xFF;
}

// Http2FrameHeader
std::vector<uint8_t> Http2FrameHeader::serialize() const {
    std::vector<uint8_t> data(9);
    writeUint32BE(data.data(), length);
    data[3] = type;
    data[4] = flags;
    writeUint32BE(data.data() + 5, streamId);
    return data;
}

Result<Http2FrameHeader, DefaultErrorType> Http2FrameHeader::parse(const uint8_t* data, size_t size) {
    if (size < 9) {
        return Error<DefaultErrorType>(DefaultErrorType::InvalidArgument, "Frame header too short: " + std::to_string(size));
    }
    
    Http2FrameHeader header;
    header.length = readUint32BE(data) & 0xFFFFFF;  // 24-bit length
    header.type = data[3];
    header.flags = data[4];
    header.streamId = readUint32BE(data + 5) & 0x7FFFFFFF;  // 31-bit stream ID
    
    return header;
}

// Http2Connection constructor
Http2Connection::Http2Connection() {
    // Initialize with default settings
    updateSetting(static_cast<uint16_t>(Http2SettingId::HEADER_TABLE_SIZE), 4096);
    updateSetting(static_cast<uint16_t>(Http2SettingId::ENABLE_PUSH), 0);
    updateSetting(static_cast<uint16_t>(Http2SettingId::MAX_CONCURRENT_STREAMS), 100);
    updateSetting(static_cast<uint16_t>(Http2SettingId::INITIAL_WINDOW_SIZE), 65535);
    updateSetting(static_cast<uint16_t>(Http2SettingId::MAX_FRAME_SIZE), 16384);
}

bool Http2Connection::isConnectionPreface(const uint8_t* data, size_t size) {
    if (size < HTTP2_PREFACE_SIZE) return false;
    return memcmp(data, HTTP2_CONNECTION_PREFACE, HTTP2_PREFACE_SIZE) == 0;
}

Result<std::vector<uint8_t>, DefaultErrorType> Http2Connection::processData(const uint8_t* data, size_t size) {
    std::vector<uint8_t> output;
    size_t offset = 0;
    
    // Check for connection preface
    if (isConnectionPreface(data, size)) {
        offset = HTTP2_PREFACE_SIZE;
        std::cout << "HTTP/2: Connection preface received\n";
        
        // Send server settings
        auto settingsResult = buildSettingsFrame();
        if (!settingsResult) {
            return Error<DefaultErrorType>(settingsResult.GetError().type, settingsResult.GetError().message);
        }
        output.insert(output.end(), settingsResult.Data().begin(), settingsResult.Data().end());
    }
    
    // Parse frames
    while (offset + 9 <= size) {
        auto headerResult = Http2FrameHeader::parse(data + offset, size - offset);
        if (!headerResult) {
            return Error<DefaultErrorType>(headerResult.GetError().type, headerResult.GetError().message);
        }
        
        Http2FrameHeader header = headerResult.Data();
        size_t frameSize = 9 + header.length;
        
        if (offset + frameSize > size) {
            break;  // Incomplete frame
        }
        
        auto frameResult = parseFrame(header, data + offset + 9);
        if (!frameResult) {
            return Error<DefaultErrorType>(frameResult.GetError().type, frameResult.GetError().message);
        }
        
        auto frameData = frameResult.Data();
        if (!frameData.empty()) {
            output.insert(output.end(), frameData.begin(), frameData.end());
        }
        
        offset += frameSize;
    }
    
    return output;
}

Result<std::vector<uint8_t>, DefaultErrorType> Http2Connection::parseFrame(const Http2FrameHeader& header, const uint8_t* payload) {
    std::vector<uint8_t> response;
    
    switch (static_cast<Http2FrameType>(header.type)) {
        case Http2FrameType::SETTINGS:
            if (header.flags == HTTP2_FLAG_ACK) {
                std::cout << "HTTP/2: SETTINGS ACK received\n";
            } else {
                // Parse settings
                for (size_t i = 0; i < header.length; i += 6) {
                    uint16_t id = readUint16BE(payload + i);
                    uint32_t value = readUint32BE(payload + i + 2);
                    updateSetting(id, value);
                }
                // Send SETTINGS ACK
                auto ackFrame = buildSettingsFrame(true);
                if (ackFrame) {
                    response = ackFrame.Data();
                }
            }
            break;
            
        case Http2FrameType::HEADERS:
            {
                uint32_t streamId = header.streamId;
                if (!getStream(streamId)) {
                    createStream(streamId);
                }
                auto stream = getStream(streamId);
                if (stream) {
                    stream->state = Http2StreamState::OPEN;
                }
                std::cout << "HTTP/2: HEADERS frame for stream " << streamId << "\n";
            }
            break;
            
        case Http2FrameType::DATA:
            {
                uint32_t streamId = header.streamId;
                auto stream = getStream(streamId);
                if (stream) {
                    stream->receivedData.insert(stream->receivedData.end(), payload, payload + header.length);
                    if (header.flags & HTTP2_FLAG_END_STREAM) {
                        std::cout << "HTTP/2: DATA complete for stream " << streamId << "\n";
                    }
                }
            }
            break;
            
        case Http2FrameType::PING:
            if (header.flags == HTTP2_FLAG_ACK) {
                std::cout << "HTTP/2: PING ACK received\n";
            } else {
                // Echo PING back as ACK
                std::vector<uint8_t> pingFrame(9 + header.length);
                Http2FrameHeader pingHeader{header.length, static_cast<uint8_t>(Http2FrameType::PING), HTTP2_FLAG_ACK, header.streamId};
                auto headerData = pingHeader.serialize();
                memcpy(pingFrame.data(), headerData.data(), 9);
                if (header.length > 0) {
                    memcpy(pingFrame.data() + 9, payload, header.length);
                }
                response = pingFrame;
            }
            break;
            
        case Http2FrameType::WINDOW_UPDATE:
            // Ignore for now
            break;
            
        default:
            std::cout << "HTTP/2: Unknown frame type: " << static_cast<int>(header.type) << "\n";
    }
    
    return response;
}

void Http2Connection::updateSetting(uint16_t id, uint32_t value) {
    switch (static_cast<Http2SettingId>(id)) {
        case Http2SettingId::HEADER_TABLE_SIZE:
            break;
        case Http2SettingId::ENABLE_PUSH:
            break;
        case Http2SettingId::MAX_CONCURRENT_STREAMS:
            m_maxConcurrentStreams = value;
            break;
        case Http2SettingId::INITIAL_WINDOW_SIZE:
            m_initialWindowSize = value;
            break;
        case Http2SettingId::MAX_FRAME_SIZE:
            m_maxFrameSize = value;
            break;
        case Http2SettingId::MAX_HEADER_LIST_SIZE:
            break;
    }
}

std::shared_ptr<Http2Stream> Http2Connection::getStream(uint32_t streamId) {
    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        return it->second;
    }
    return nullptr;
}

void Http2Connection::createStream(uint32_t streamId) {
    if (m_streams.find(streamId) == m_streams.end()) {
        m_streams[streamId] = std::make_shared<Http2Stream>(streamId);
    }
}

Result<std::vector<uint8_t>, DefaultErrorType> Http2Connection::buildSettingsFrame(bool ack) {
    std::vector<uint8_t> frame;
    
    if (ack) {
        Http2FrameHeader header{0, static_cast<uint8_t>(Http2FrameType::SETTINGS), HTTP2_FLAG_ACK, 0};
        auto headerData = header.serialize();
        frame.insert(frame.end(), headerData.begin(), headerData.end());
    } else {
        // Send server settings
        std::vector<uint8_t> payload;
        uint8_t settingData[6];
        
        // HEADER_TABLE_SIZE
        writeUint16BE(settingData, static_cast<uint16_t>(Http2SettingId::HEADER_TABLE_SIZE));
        writeUint32BE(settingData + 2, 4096);
        payload.insert(payload.end(), settingData, settingData + 6);
        
        // ENABLE_PUSH (0 = disabled)
        writeUint16BE(settingData, static_cast<uint16_t>(Http2SettingId::ENABLE_PUSH));
        writeUint32BE(settingData + 2, 0);
        payload.insert(payload.end(), settingData, settingData + 6);
        
        // MAX_CONCURRENT_STREAMS
        writeUint16BE(settingData, static_cast<uint16_t>(Http2SettingId::MAX_CONCURRENT_STREAMS));
        writeUint32BE(settingData + 2, m_maxConcurrentStreams);
        payload.insert(payload.end(), settingData, settingData + 6);
        
        Http2FrameHeader header{static_cast<uint32_t>(payload.size()), static_cast<uint8_t>(Http2FrameType::SETTINGS), 0, 0};
        auto headerData = header.serialize();
        frame.insert(frame.end(), headerData.begin(), headerData.end());
        frame.insert(frame.end(), payload.begin(), payload.end());
    }
    
    return frame;
}

Result<std::vector<uint8_t>, DefaultErrorType> Http2Connection::buildHeadersFrame(uint32_t streamId, const std::map<std::string, std::string>& headers, bool endStream) {
    std::vector<uint8_t> frame;
    
    // Build pseudo-headers first
    std::vector<uint8_t> payload;
    
    // Simplified header encoding (just for testing - real HPACK is complex)
    for (const auto& [name, value] : headers) {
        // Add name
        payload.push_back(static_cast<uint8_t>(name.length()));
        payload.insert(payload.end(), name.begin(), name.end());
        // Add value
        payload.push_back(static_cast<uint8_t>(value.length()));
        payload.insert(payload.end(), value.begin(), value.end());
    }
    
    uint8_t flags = HTTP2_FLAG_END_HEADERS;
    if (endStream) flags |= HTTP2_FLAG_END_STREAM;
    
    Http2FrameHeader header{static_cast<uint32_t>(payload.size()), static_cast<uint8_t>(Http2FrameType::HEADERS), flags, streamId};
    auto headerData = header.serialize();
    frame.insert(frame.end(), headerData.begin(), headerData.end());
    frame.insert(frame.end(), payload.begin(), payload.end());
    
    return frame;
}

Result<std::vector<uint8_t>, DefaultErrorType> Http2Connection::buildDataFrame(uint32_t streamId, const std::vector<uint8_t>& data, bool endStream) {
    std::vector<uint8_t> frame;
    
    uint8_t flags = endStream ? HTTP2_FLAG_END_STREAM : 0;
    
    Http2FrameHeader header{static_cast<uint32_t>(data.size()), static_cast<uint8_t>(Http2FrameType::DATA), flags, streamId};
    auto headerData = header.serialize();
    frame.insert(frame.end(), headerData.begin(), headerData.end());
    frame.insert(frame.end(), data.begin(), data.end());
    
    return frame;
}

// Http2Response
std::vector<uint8_t> Http2Response::serialize(uint32_t streamId) {
    Http2Connection conn;
    
    // Build headers
    std::map<std::string, std::string> headerMap;
    headerMap[":status"] = std::to_string(statusCode);
    
    for (const auto& [name, value] : headers) {
        headerMap[name] = value;
    }
    
    // Add content-length
    headerMap["content-length"] = std::to_string(m_body.length());
    
    auto headersFrame = conn.buildHeadersFrame(streamId, headerMap, m_body.empty());
    if (!headersFrame) {
        return {};
    }
    
    std::vector<uint8_t> result = headersFrame.Data();
    
    if (!m_body.empty()) {
        auto dataFrame = conn.buildDataFrame(streamId, std::vector<uint8_t>(m_body.begin(), m_body.end()), true);
        if (dataFrame) {
            auto data = dataFrame.Data();
            result.insert(result.end(), data.begin(), data.end());
        }
    }
    
    return result;
}
