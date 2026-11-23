#include "link.hpp"

#include "ws.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cz {

namespace {

constexpr uint16_t kPort             = 9999;    // Creality LAN websocket
constexpr int64_t  kSilenceMs        = 20000;   // Creality Print reconnects after 20 s; so do we
constexpr int64_t  kHeartbeatMs      = 10000;
constexpr int64_t  kOfflineMs        = 12000;   // no data this long: OFFLINE
constexpr int64_t  kRetryMs          = 3000;
constexpr int64_t  kSearchAgainMs    = 3 * 60 * 1000;   // a found printer gone this long: search
constexpr int64_t  kSearchRetryMs    = 15000;
// Per host: first contact waits for ARP, slower on a weak Wi-Fi link (the
// CardputerZero once missed the Hi at 1.5 s, at -77 dBm)
constexpr int      kProbeMs          = 2500;
constexpr int      kProbesInFlight   = 16;

int64_t nowMs(){
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Our own IPv4 address (host order); 0 when there is none. Wi-Fi first.
uint32_t ownAddress(){
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return 0;
    uint32_t best = 0;
    for (ifaddrs* i = list; i; i = i->ifa_next){
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        const uint32_t a = ntohl(reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr.s_addr);
        if (strncmp(i->ifa_name, "wlan", 4) == 0){
            best = a;
            break;
        }
        if (!best) best = a;
    }
    freeifaddrs(list);
    return best;
}

std::string dotted(uint32_t a){
    char s[20];
    snprintf(s, sizeof(s), "%u.%u.%u.%u", (a >> 24) & 255, (a >> 16) & 255, (a >> 8) & 255, a & 255);
    return s;
}

// Port 9999 answered: is it a Creality printer? It upgrades to a websocket
// and its first messages carry the printer's fields.
bool looksLikeCreality(const std::string& ip){
    WsClient c;
    if (!c.open(ip, kPort, 2500)){
        return false;
    }
    const int64_t end = nowMs() + 4000;
    std::vector<std::string> msgs;
    while (nowMs() < end){
        if (!c.poll(msgs, 100)) return false;
        for (const std::string& m : msgs){
            if (m.find("\"TotalLayer\"") != std::string::npos || m.find("\"printProgress\"") != std::string::npos
                || m.find("\"hostname\"") != std::string::npos){
                return true;
            }
        }
        msgs.clear();
    }
    return false;
}

std::string heartbeat(){
    char iso[32] = "";
    const time_t now = time(nullptr);
    if (now > 1700000000){
        struct tm tm;
        gmtime_r(&now, &tm);
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S.000Z", &tm);
    }
    return std::string("{\"ModeCode\":\"heart_beat\",\"msg\":\"") + iso + "\"}";
}

}  // namespace

const char* linkName(Link l){
    switch (l){
        case Link::Searching:  return "searching";
        case Link::Connecting: return "connecting";
        case Link::Online:     return "online";
        case Link::Offline:    return "offline";
    }
    return "?";
}

void PrinterLink::start(const std::string& printer, bool autoFound,
                        std::function<void(const std::string&)> onFound){
    stop();
    {
        std::lock_guard<std::mutex> g(mu_);
        info_ = LinkInfo();
        info_.printer = printer;
        info_.printerAuto = autoFound;
        info_.link = printer.empty() ? Link::Searching : Link::Connecting;
    }
    onFound_ = std::move(onFound);
    stop_ = false;
    th_ = std::thread(&PrinterLink::run, this);
}

void PrinterLink::stop(){
    stop_ = true;
    if (th_.joinable()) th_.join();
}

void PrinterLink::take(std::vector<std::string>& out){
    std::lock_guard<std::mutex> g(mu_);
    for (std::string& m : queue_) out.push_back(std::move(m));
    queue_.clear();
}

LinkInfo PrinterLink::info() const {
    std::lock_guard<std::mutex> g(mu_);
    return info_;
}

// Non-blocking connects to port 9999 on every host of our /24, a few at a
// time; the first that looks like a Creality printer wins
std::string PrinterLink::search(){
    const uint32_t self = ownAddress();
    if (!self) return "";
    const uint32_t net = self & 0xFFFFFF00u;
    struct Probe { int fd; uint32_t host; int64_t at; };
    std::vector<Probe> probes;
    int next = 1;
    std::string found;
    while (!stop_ && found.empty() && (next <= 254 || !probes.empty())){
        while (static_cast<int>(probes.size()) < kProbesInFlight && next <= 254){
            const uint32_t host = net | static_cast<uint32_t>(next++);
            if (host == self) continue;
            {
                std::lock_guard<std::mutex> g(mu_);
                info_.scanAt = static_cast<int>(host & 255);
            }
            const int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) break;
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(kPort);
            a.sin_addr.s_addr = htonl(host);
            if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0 && errno != EINPROGRESS){
                close(fd);
                continue;
            }
            probes.push_back({fd, host, nowMs()});
        }
        std::vector<pollfd> pf;
        for (const Probe& p : probes) pf.push_back({p.fd, POLLOUT, 0});
        ::poll(pf.data(), pf.size(), 50);
        for (size_t i = 0; i < probes.size();){
            bool done = false, open = false;
            if (pf[i].revents & (POLLOUT | POLLERR | POLLHUP)){
                int err = -1;
                socklen_t len = sizeof(err);
                open = getsockopt(probes[i].fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
                done = true;
            } else if (nowMs() - probes[i].at >= kProbeMs){
                done = true;
            }
            if (!done){
                i++;
                continue;
            }
            close(probes[i].fd);
            const uint32_t host = probes[i].host;
            probes.erase(probes.begin() + static_cast<long>(i));
            pf.erase(pf.begin() + static_cast<long>(i));
            if (open && found.empty() && looksLikeCreality(dotted(host))){
                found = dotted(host);
            }
        }
    }
    for (const Probe& p : probes) close(p.fd);
    return found;
}

void PrinterLink::run(){
    std::string addr;
    bool autoFound;
    {
        std::lock_guard<std::mutex> g(mu_);
        addr = info_.printer;
        autoFound = info_.printerAuto;
    }
    int64_t lastData = nowMs();   // the last message, or when we started trying
    auto setLink = [&](Link l){
        std::lock_guard<std::mutex> g(mu_);
        info_.link = l;
    };
    auto sleepMs = [&](int64_t ms){
        const int64_t end = nowMs() + ms;
        while (!stop_ && nowMs() < end) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    };

    while (!stop_){
        if (addr.empty()){
            {
                std::lock_guard<std::mutex> g(mu_);
                info_.link = Link::Searching;
                info_.scanFailed = false;
                info_.printer.clear();
            }
            addr = search();
            if (stop_) break;
            if (addr.empty()){
                {
                    std::lock_guard<std::mutex> g(mu_);
                    info_.scanFailed = true;
                }
                sleepMs(kSearchRetryMs);
                continue;
            }
            autoFound = true;
            {
                std::lock_guard<std::mutex> g(mu_);
                info_.printer = addr;
                info_.printerAuto = true;
                info_.link = Link::Connecting;
            }
            if (onFound_) onFound_(addr);
            lastData = nowMs();
        }

        WsClient ws;
        if (!ws.open(addr, kPort, 3000)){
            if (nowMs() - lastData > kOfflineMs) setLink(Link::Offline);
            if (autoFound && nowMs() - lastData > kSearchAgainMs){
                addr.clear();   // the address may have changed (DHCP)
                continue;
            }
            sleepMs(kRetryMs);
            continue;
        }

        int64_t lastRx = nowMs();
        int64_t lastBeat = 0;
        bool any = false;
        while (!stop_){
            std::vector<std::string> msgs;
            if (!ws.poll(msgs, 100)) break;
            const int64_t t = nowMs();
            if (!msgs.empty()){
                lastRx = lastData = t;
                any = true;
                for (const std::string& m : msgs){
                    // The printer's own heartbeat wants a bare "ok"
                    if (m.find("\"heart_beat\"") != std::string::npos) ws.sendText("ok");
                }
                std::lock_guard<std::mutex> g(mu_);
                info_.messages += static_cast<uint32_t>(msgs.size());
                info_.link = Link::Online;
                for (std::string& m : msgs) queue_.push_back(std::move(m));
            }
            if (t - lastBeat >= kHeartbeatMs){
                ws.sendText(heartbeat());
                lastBeat = t;
            }
            if (t - lastRx > kSilenceMs) break;        // silent: reconnect
            if (t - lastData > kOfflineMs) setLink(Link::Offline);
            else if (!any) setLink(Link::Connecting);
        }
        ws.close();
        if (!stop_) sleepMs(500);
    }
}

}  // namespace cz
