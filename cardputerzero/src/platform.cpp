#include "platform.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace cz {

// ---------------------------------------------------------------- framebuffer

bool Framebuffer::open(const std::string& path, int rotate){
    close();
    std::vector<std::string> candidates;
    if (!path.empty()){
        candidates.push_back(path);
    } else {
        for (int i = 0; i < 4; i++) candidates.push_back("/dev/fb" + std::to_string(i));
    }
    std::string chosen;
    fb_var_screeninfo var{};
    for (const std::string& c : candidates){
        const int fd = ::open(c.c_str(), O_RDWR);
        if (fd < 0) continue;
        fb_var_screeninfo v{};
        if (ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0){
            const bool panel = (v.xres == 320 && v.yres == 170) || (v.xres == 170 && v.yres == 320);
            if (panel || !path.empty() || chosen.empty()){
                if (!chosen.empty()) ::close(fd_);
                chosen = c;
                fd_ = fd;
                var = v;
                if (panel || !path.empty()) break;
                continue;
            }
        }
        ::close(fd);
    }
    if (fd_ < 0){
        return false;
    }
    fb_fix_screeninfo fix{};
    if (ioctl(fd_, FBIOGET_FSCREENINFO, &fix) != 0){
        close();
        return false;
    }
    path_ = chosen;
    xres_ = static_cast<int>(var.xres);
    yres_ = static_cast<int>(var.yres);
    bpp_ = static_cast<int>(var.bits_per_pixel);
    stride_ = static_cast<int>(fix.line_length);
    rshift_ = static_cast<int>(var.red.offset);
    gshift_ = static_cast<int>(var.green.offset);
    bshift_ = static_cast<int>(var.blue.offset);
    bgr_ = bpp_ == 16 && var.red.offset == 0;
    rotate_ = rotate >= 0 ? rotate : (xres_ < yres_ ? 90 : 0);
    len_ = fix.smem_len ? fix.smem_len : static_cast<size_t>(stride_) * yres_;
    void* m = mmap(nullptr, len_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (m == MAP_FAILED){
        close();
        return false;
    }
    mem_ = static_cast<uint8_t*>(m);
    return true;   // not cleared: the first frame covers it all
}

void Framebuffer::close(){
    if (mem_){
        munmap(mem_, len_);
        mem_ = nullptr;
    }
    if (fd_ >= 0){
        ::close(fd_);
        fd_ = -1;
    }
}

std::string Framebuffer::describe() const {
    char s[128];
    snprintf(s, sizeof(s), "%s %dx%d %d bpp stride %d, red/green/blue at bit %d/%d/%d%s, rotate %d", path_.c_str(),
             xres_, yres_, bpp_, stride_, rshift_, gshift_, bshift_, bgr_ ? " (bgr)" : "", rotate_);
    return s;
}

void Framebuffer::push(const uint16_t* px, int w, int h, int top){
    if (!mem_) return;
    for (int y = 0; y < h; y++){
        const int ly = top + y;   // logical row
        for (int x = 0; x < w; x++){
            // Logical (x, ly) -> device (dx, dy)
            int dx = x, dy = ly;
            if (rotate_ == 90){        // panel portrait, turned clockwise
                dx = xres_ - 1 - ly;
                dy = x;
            } else if (rotate_ == 270){
                dx = ly;
                dy = yres_ - 1 - x;
            }
            if (dx < 0 || dy < 0 || dx >= xres_ || dy >= yres_) continue;
            const uint16_t c = px[y * w + x];
            uint8_t* p = mem_ + static_cast<size_t>(dy) * stride_;
            if (bpp_ == 16){
                uint16_t v = c;
                if (bgr_) v = static_cast<uint16_t>(((c & 0x1F) << 11) | (c & 0x07E0) | (c >> 11));
                reinterpret_cast<uint16_t*>(p)[dx] = v;
            } else if (bpp_ == 32 || bpp_ == 24){
                const uint32_t r = ((c >> 11) & 31) * 255 / 31;
                const uint32_t g = ((c >> 5) & 63) * 255 / 63;
                const uint32_t b = (c & 31) * 255 / 31;
                if (bpp_ == 32){
                    reinterpret_cast<uint32_t*>(p)[dx] = (r << rshift_) | (g << gshift_) | (b << bshift_);
                } else {
                    uint8_t* q = p + dx * 3;
                    q[rshift_ / 8] = static_cast<uint8_t>(r);
                    q[gshift_ / 8] = static_cast<uint8_t>(g);
                    q[bshift_ / 8] = static_cast<uint8_t>(b);
                }
            }
        }
    }
}

// ---------------------------------------------------------------- keyboard

namespace {
bool testBit(const unsigned long* bits, int bit){
    return (bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1;
}
}  // namespace

bool Keyboard::open(){
    close();
    for (int i = 0; i < 32; i++){
        const std::string p = "/dev/input/event" + std::to_string(i);
        const int fd = ::open(p.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long keys[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {0};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0 && testBit(keys, KEY_ESC)
            && testBit(keys, KEY_A)){
            fds_.push_back(fd);
        } else {
            ::close(fd);
        }
    }
    return !fds_.empty();
}

void Keyboard::close(){
    for (int fd : fds_) ::close(fd);
    fds_.clear();
}

Key Keyboard::poll(){
    for (int fd : fds_){
        input_event e;
        while (read(fd, &e, sizeof(e)) == static_cast<ssize_t>(sizeof(e))){
            if (e.type != EV_KEY || e.value != 1) continue;   // presses only
            switch (e.code){
                case KEY_ESC:
                case KEY_Q:
                    return Key::Quit;
                // Pages: Tab, the arrows, and the Cardputer's arrow keys (, . ; /)
                case KEY_TAB:
                case KEY_RIGHT:
                case KEY_DOWN:
                case KEY_DOT:
                case KEY_SLASH:
                    return Key::PageNext;
                case KEY_LEFT:
                case KEY_UP:
                case KEY_COMMA:
                case KEY_SEMICOLON:
                    return Key::PagePrev;
                case KEY_ENTER:
                case KEY_SPACE:
                case KEY_KPENTER:
                    return Key::Click;
                case KEY_T:
                    return Key::Test;
                default:
                    break;
            }
        }
    }
    return Key::None;
}

// ---------------------------------------------------------------- system

namespace {
std::string readFile(const std::string& path){
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char buf[256];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}
}  // namespace

Battery readBattery(){
    Battery b;
    DIR* d = opendir("/sys/class/power_supply");
    if (!d) return b;
    while (dirent* e = readdir(d)){
        if (e->d_name[0] == '.') continue;
        const std::string base = std::string("/sys/class/power_supply/") + e->d_name;
        if (readFile(base + "/type") != "Battery") continue;
        const std::string cap = readFile(base + "/capacity");
        if (cap.empty()) continue;
        b.percent = atoi(cap.c_str());
        if (b.percent > 100) b.percent = 100;
        if (b.percent < 0) b.percent = 0;
        const std::string st = readFile(base + "/status");
        b.charging = st == "Charging" || st == "Full";
        break;
    }
    closedir(d);
    return b;
}

int wifiBars(){
    FILE* f = fopen("/proc/net/wireless", "r");
    if (!f) return -1;
    char line[256];
    int bars = -1;
    while (fgets(line, sizeof(line), f)){
        // " wlan0: 0000   54.  -56.  -256  ..."
        char name[32];
        float link, level;
        if (sscanf(line, " %31[^:]: %*x %f %f", name, &link, &level) == 3){
            const int dbm = static_cast<int>(level);
            bars = dbm > -55 ? 4 : dbm > -65 ? 3 : dbm > -75 ? 2 : dbm > -85 ? 1 : 0;
            break;
        }
    }
    fclose(f);
    return bars;
}

std::string ownIp(){
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return "";
    std::string ip;
    for (ifaddrs* i = list; i; i = i->ifa_next){
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        if (!(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
        char s[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(i->ifa_addr)->sin_addr, s, sizeof(s));
        ip = s;
        if (strncmp(i->ifa_name, "wlan", 4) == 0) break;
    }
    freeifaddrs(list);
    return ip;
}

// ---------------------------------------------------------------- sound

void playMelody(hm::AlertKind k){
    const hm::Note* m = hm::melodyFor(k);
    if (!m) return;
    // 16-bit mono PCM at 22050 Hz, square-ish waves softened a little
    constexpr int kRate = 22050;
    std::vector<int16_t> pcm;
    for (const hm::Note* n = m; !(n->hz == 0 && n->ms == 0); n++){
        const int count = kRate * n->ms / 1000;
        for (int i = 0; i < count; i++){
            int16_t s = 0;
            if (n->hz){
                const double phase = static_cast<double>(i) * n->hz / kRate;
                const double frac = phase - static_cast<long>(phase);
                const double fade = i < 200 ? i / 200.0 : (count - i < 400 ? (count - i) / 400.0 : 1.0);
                s = static_cast<int16_t>((frac < 0.5 ? 1 : -1) * 6000 * fade);
            }
            pcm.push_back(s);
        }
    }
    // The speaker, not HDMI: ALSA's default is the HDMI card on the
    // CardputerZero, its speaker is the ES8389 card. Take the first card
    // whose id has no "hdmi" in it.
    std::string device;
    if (FILE* c = fopen("/proc/asound/cards", "r")){
        char line[256];
        while (fgets(line, sizeof(line), c)){
            char id[64];
            if (sscanf(line, " %*d [%63[^] ]", id) != 1) continue;
            std::string lower(id);
            for (char& ch : lower) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            if (lower.find("hdmi") == std::string::npos){
                device = std::string("-D plughw:CARD=") + id + ",DEV=0 ";
                break;
            }
        }
        fclose(c);
    }
    const std::string cmd = "aplay -q " + device + "-t raw -f S16_LE -r 22050 -c 1 - 2>/dev/null";
    std::thread([pcm, cmd]() {
        FILE* p = popen(cmd.c_str(), "w");
        if (!p) return;
        fwrite(pcm.data(), sizeof(int16_t), pcm.size(), p);
        pclose(p);
    }).detach();
}

// ---------------------------------------------------------------- config

std::string configPath(){
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    std::string dir;
    if (xdg && *xdg) dir = std::string(xdg) + "/hm01";
    else if (home && *home) dir = std::string(home) + "/.config/hm01";
    else dir = "/var/tmp/hm01";
    return dir + "/hm01.conf";
}

Config loadConfig(){
    Config c;
    FILE* f = fopen(configPath().c_str(), "r");
    if (!f) return c;
    char line[256];
    while (fgets(line, sizeof(line), f)){
        char key[64], value[160] = "";
        if (sscanf(line, " %63[^= ] = %159[^\n]", key, value) < 1) continue;
        std::string v(value);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.pop_back();
        if (!strcmp(key, "printer")) c.printer = v;
        else if (!strcmp(key, "auto")) c.printerAuto = v == "1";
        else if (!strcmp(key, "muted")) c.muted = v == "1";
    }
    fclose(f);
    return c;
}

void saveConfig(const Config& c){
    const std::string path = configPath();
    const std::string dir = path.substr(0, path.rfind('/'));
    // mkdir -p
    for (size_t i = 1; i <= dir.size(); i++){
        if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
    }
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "# HM-01 on the CardputerZero\nprinter=%s\nauto=%d\nmuted=%d\n", c.printer.c_str(),
            c.printerAuto ? 1 : 0, c.muted ? 1 : 0);
    fclose(f);
}

}  // namespace cz
