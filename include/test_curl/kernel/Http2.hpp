#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <map>
#include <memory>
#include <test_curl/kernel/Result.hpp>

// HTTP/2 Connection Preface
constexpr const char* HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
constexpr uint32_t HTTP2_PREFACE_SIZE = 24;

// HTTP/2 Frame Types
enum class Http2FrameType : uint8_t {
    DATA = 0x00,
    HEADERS = 0x01,
    PRIORITY = 0x02,
    RST_STREAM = 0x03,
    SETTINGS = 0x04,
    PUSH_PROMISE = 0x05,
    PING = 0x06,
    GOAWAY = 0x07,
    WINDOW_UPDATE = 0x08,
    CONTINUATION = 0x09
};

// HTTP/2 Flags
constexpr uint8_t HTTP2_FLAG_END_STREAM = 0x01;
constexpr uint8_t HTTP2_FLAG_END_HEADERS = 0x04;
constexpr uint8_t HTTP2_FLAG_ACK = 0x01;
constexpr uint8_t HTTP2_FLAG_PRIORITY = 0x20;

// HTTP/2 Frame Header (9 bytes)
struct Http2FrameHeader {
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t streamId;
    
    static Result<Http2FrameHeader, DefaultErrorType> parse(const uint8_t* data, size_t size);
    std::vector<uint8_t> serialize() const;
};

// HTTP/2 Settings
enum class Http2SettingId : uint16_t {
    HEADER_TABLE_SIZE = 0x01,
    ENABLE_PUSH = 0x02,
    MAX_CONCURRENT_STREAMS = 0x03,
    INITIAL_WINDOW_SIZE = 0x04,
    MAX_FRAME_SIZE = 0x05,
    MAX_HEADER_LIST_SIZE = 0x06
};

struct Http2Setting {
    uint16_t id;
    uint32_t value;
};

// HTTP/2 Stream State
enum class Http2StreamState {
    IDLE,
    OPEN,
    RESERVED_LOCAL,
    RESERVED_REMOTE,
    HALF_CLOSED_LOCAL,
    HALF_CLOSED_REMOTE,
    CLOSED
};

// HTTP/2 Stream
class Http2Stream {
public:
    Http2Stream(uint32_t id) : streamId(id), state(Http2StreamState::IDLE) {}
    
    uint32_t streamId;
    Http2StreamState state;
    std::vector<uint8_t> receivedData;
    std::map<std::string, std::string> headers;
    
    void close() {
        if (state == Http2StreamState::HALF_CLOSED_REMOTE) {
            state = Http2StreamState::CLOSED;
        } else if (state == Http2StreamState::OPEN) {
            state = Http2StreamState::HALF_CLOSED_LOCAL;
        }
    }
};

// HTTP/2 Connection
class Http2Connection {
public:
    Http2Connection();
    
    // Parse incoming data
    Result<std::vector<uint8_t>, DefaultErrorType> processData(const uint8_t* data, size_t size);
    
    // Build frames for sending
    Result<std::vector<uint8_t>, DefaultErrorType> buildSettingsFrame(bool ack = false);
    Result<std::vector<uint8_t>, DefaultErrorType> buildHeadersFrame(uint32_t streamId, const std::map<std::string, std::string>& headers, bool endStream = false);
    Result<std::vector<uint8_t>, DefaultErrorType> buildDataFrame(uint32_t streamId, const std::vector<uint8_t>& data, bool endStream = false);
    
    // Stream management
    std::shared_ptr<Http2Stream> getStream(uint32_t streamId);
    void createStream(uint32_t streamId);
    
    // Settings
    void updateSetting(uint16_t id, uint32_t value);
    
    // Check if connection preface is present
    static bool isConnectionPreface(const uint8_t* data, size_t size);
    
private:
    std::map<uint32_t, std::shared_ptr<Http2Stream>> m_streams;
    uint32_t m_nextStreamId = 1;  // Server-initiated streams start at 2, client at 1
    uint32_t m_maxFrameSize = 16384;
    uint32_t m_initialWindowSize = 65535;
    uint32_t m_maxConcurrentStreams = 100;
    
    Result<std::vector<uint8_t>, DefaultErrorType> parseFrame(const Http2FrameHeader& header, const uint8_t* payload);
};

// HTTP/2 Response Builder
class Http2Response {
public:
    Http2Response() : statusCode(200) {}
    
    void setStatusCode(int code) { statusCode = code; }
    void addHeader(const std::string& name, const std::string& value) { headers[name] = value; }
    void setBody(const std::string& body) { m_body = body; }
    
    // Serialize to HTTP/2 frames
    std::vector<uint8_t> serialize(uint32_t streamId);
    
private:
    int statusCode;
    std::map<std::string, std::string> headers;
    std::string m_body;
};
