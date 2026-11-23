#include "ws.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

namespace cz {

namespace {

int64_t nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string base64(const unsigned char* data, size_t len){
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < len; i += 3){
        const uint32_t n = (uint32_t(data[i]) << 16) | (i + 1 < len ? uint32_t(data[i + 1]) << 8 : 0)
                         | (i + 2 < len ? data[i + 2] : 0);
        out += k[(n >> 18) & 63];
        out += k[(n >> 12) & 63];
        out += i + 1 < len ? k[(n >> 6) & 63] : '=';
        out += i + 2 < len ? k[n & 63] : '=';
    }
    return out;
}

std::mt19937& rng(){
    static std::mt19937 r(std::random_device{}());
    return r;
}

}  // namespace

WsClient::~WsClient(){
    close();
}

void WsClient::close(){
    if (fd_ >= 0){
        ::close(fd_);
        fd_ = -1;
    }
    in_.clear();
    frag_.clear();
}

bool WsClient::sendAll(const char* data, size_t len, int timeoutMs){
    const int64_t end = nowMs() + timeoutMs;
    while (len > 0){
        const ssize_t n = ::send(fd_, data, len, MSG_NOSIGNAL);
        if (n > 0){
            data += n;
            len -= static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR){
            return false;
        }
        const int64_t left = end - nowMs();
        if (left <= 0){
            return false;
        }
        pollfd p{fd_, POLLOUT, 0};
        ::poll(&p, 1, static_cast<int>(left));
    }
    return true;
}

bool WsClient::open(const std::string& host, uint16_t port, int timeoutMs){
    close();
    const int64_t end = nowMs() + timeoutMs;

    // A numeric address directly: the binary is static, and glibc's name
    // lookup (getaddrinfo with a host name) needs its shared NSS modules
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    const std::string portStr = std::to_string(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1){
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res){
            return false;
        }
        addr = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
        freeaddrinfo(res);
    }
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0){
        return false;
    }
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    const int rc = ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS){
        close();
        return false;
    }
    pollfd p{fd_, POLLOUT, 0};
    if (::poll(&p, 1, static_cast<int>(end - nowMs())) <= 0){
        close();
        return false;
    }
    int err = 0;
    socklen_t elen = sizeof(err);
    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &elen) != 0 || err != 0){
        close();
        return false;
    }

    unsigned char nonce[16];
    for (unsigned char& b : nonce) b = static_cast<unsigned char>(rng()() & 0xFF);
    const std::string req = "GET / HTTP/1.1\r\nHost: " + host + ":" + portStr +
                            "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " +
                            base64(nonce, sizeof(nonce)) + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if (!sendAll(req.data(), req.size(), static_cast<int>(end - nowMs()))){
        close();
        return false;
    }

    // The HTTP answer: "101 Switching Protocols", then frames may follow
    std::string head;
    char buf[2048];
    while (head.find("\r\n\r\n") == std::string::npos){
        const int64_t left = end - nowMs();
        if (left <= 0 || head.size() > 8192){
            close();
            return false;
        }
        pollfd q{fd_, POLLIN, 0};
        if (::poll(&q, 1, static_cast<int>(left)) <= 0){
            continue;
        }
        const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n <= 0){
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            close();
            return false;
        }
        head.append(buf, static_cast<size_t>(n));
    }
    const size_t split = head.find("\r\n\r\n");
    const std::string status = head.substr(0, head.find("\r\n"));
    if (status.find(" 101") == std::string::npos){
        close();
        return false;
    }
    in_ = head.substr(split + 4);
    return true;
}

bool WsClient::sendFrame(uint8_t opcode, const std::string& payload){
    if (fd_ < 0){
        return false;
    }
    std::string f;
    f += static_cast<char>(0x80 | opcode);
    const size_t n = payload.size();
    if (n < 126){
        f += static_cast<char>(0x80 | n);
    } else if (n < 65536){
        f += static_cast<char>(0x80 | 126);
        f += static_cast<char>((n >> 8) & 0xFF);
        f += static_cast<char>(n & 0xFF);
    } else {
        f += static_cast<char>(0x80 | 127);
        for (int i = 7; i >= 0; i--) f += static_cast<char>((uint64_t(n) >> (8 * i)) & 0xFF);
    }
    unsigned char mask[4];
    for (unsigned char& b : mask) b = static_cast<unsigned char>(rng()() & 0xFF);
    f.append(reinterpret_cast<char*>(mask), 4);
    for (size_t i = 0; i < n; i++) f += static_cast<char>(payload[i] ^ mask[i % 4]);
    return sendAll(f.data(), f.size(), 1000);
}

bool WsClient::sendText(const std::string& text){
    return sendFrame(0x1, text);
}

bool WsClient::poll(std::vector<std::string>& out, int waitMs){
    if (fd_ < 0){
        return false;
    }
    // Wait only when nothing is buffered (a partial frame still needs more)
    pollfd p{fd_, POLLIN, 0};
    if (::poll(&p, 1, in_.empty() ? waitMs : 0) > 0){
        char buf[8192];
        for (;;){
            const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
            if (n > 0){
                in_.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0){
                close();   // the printer hung up
                return false;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            close();
            return false;
        }
    }

    // Frames
    for (;;){
        if (in_.size() < 2) break;
        const unsigned char b0 = static_cast<unsigned char>(in_[0]);
        const unsigned char b1 = static_cast<unsigned char>(in_[1]);
        const bool fin = b0 & 0x80;
        const int op = b0 & 0x0F;
        const bool masked = b1 & 0x80;
        uint64_t len = b1 & 0x7F;
        size_t pos = 2;
        if (len == 126){
            if (in_.size() < 4) break;
            len = (uint64_t(static_cast<unsigned char>(in_[2])) << 8) | static_cast<unsigned char>(in_[3]);
            pos = 4;
        } else if (len == 127){
            if (in_.size() < 10) break;
            len = 0;
            for (int i = 0; i < 8; i++) len = (len << 8) | static_cast<unsigned char>(in_[2 + i]);
            pos = 10;
        }
        if (len > (1u << 22)){
            close();   // 4 MB: not the printer
            return false;
        }
        unsigned char mask[4] = {0, 0, 0, 0};
        if (masked){
            if (in_.size() < pos + 4) break;
            memcpy(mask, in_.data() + pos, 4);
            pos += 4;
        }
        if (in_.size() < pos + len) break;
        std::string payload = in_.substr(pos, static_cast<size_t>(len));
        in_.erase(0, pos + static_cast<size_t>(len));
        if (masked){
            for (size_t i = 0; i < payload.size(); i++) payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
        }
        switch (op){
            case 0x0:
            case 0x1:
            case 0x2:
                frag_ += payload;
                if (fin){
                    if (op != 0x2 || !frag_.empty()) out.push_back(frag_);
                    frag_.clear();
                }
                break;
            case 0x8:
                close();
                return false;
            case 0x9:
                sendFrame(0xA, payload);
                break;
            default:
                break;   // pong, reserved
        }
    }
    return true;
}

}  // namespace cz
