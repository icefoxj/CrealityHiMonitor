#pragma once

// A minimal websocket client (RFC 6455) over POSIX sockets: enough for the
// Creality websocket on port 9999 (text frames, ping / pong, close). The
// printer's frames are unmasked; ours are masked, as a client's must be.

#include <cstdint>
#include <string>
#include <vector>

namespace cz {

class WsClient {
public:
    WsClient() = default;
    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;
    ~WsClient();

    // Connects and upgrades within timeoutMs. False on any failure.
    bool open(const std::string& host, uint16_t port, int timeoutMs);

    // Waits up to waitMs for data, then appends the complete text messages
    // that arrived. False when the connection closed or failed.
    bool poll(std::vector<std::string>& out, int waitMs);

    bool sendText(const std::string& text);
    void close();
    bool isOpen() const { return fd_ >= 0; }

private:
    bool sendFrame(uint8_t opcode, const std::string& payload);
    bool sendAll(const char* data, size_t len, int timeoutMs);

    int         fd_ = -1;
    std::string in_;     // bytes received, not yet parsed into frames
    std::string frag_;   // a fragmented message being put together
};

}  // namespace cz
