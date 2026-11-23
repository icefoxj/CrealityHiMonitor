#pragma once

// The printer link on the CardputerZero: the same behaviour as the
// firmware's net.cpp, on Linux sockets. A worker thread searches the /24
// for a Creality printer on port 9999 (when no address is set), keeps the
// websocket open, answers and sends heartbeats, and reconnects after 20 s
// of silence. The main loop takes the messages from it.

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cz {

enum class Link : uint8_t { Searching, Connecting, Online, Offline };

const char* linkName(Link l);

struct LinkInfo {
    Link        link = Link::Searching;
    std::string printer;          // address in use ("" while searching)
    bool        printerAuto = false;
    int         scanAt = 0;       // host being tried, 1..254
    bool        scanFailed = false;
    uint32_t    messages = 0;
};

class PrinterLink {
public:
    // printer: an address, or "" to search. autoFound: it came from a
    // search (search again if it stays offline for 3 minutes). onFound
    // saves an address the search found.
    void start(const std::string& printer, bool autoFound,
               std::function<void(const std::string&)> onFound);
    void stop();
    ~PrinterLink(){ stop(); }

    // The messages that arrived since the last call
    void take(std::vector<std::string>& out);
    LinkInfo info() const;

private:
    void run();
    std::string search();

    std::thread       th_;
    std::atomic<bool> stop_{false};
    mutable std::mutex mu_;
    LinkInfo          info_;
    std::vector<std::string> queue_;
    std::function<void(const std::string&)> onFound_;
};

}  // namespace cz
