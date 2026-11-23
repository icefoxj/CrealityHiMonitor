// HM-01 on the M5Stack CardputerZero (Raspberry Pi CM0, Debian, 320 x 170).
//
// An app for the CardputerZero's launcher (APPLaunch): it owns the
// framebuffer while it runs and returns to the menu on ESC. The same
// portable core as the firmware (lib/hm_core: parser, ETA, job latch,
// alerts, the dashboard drawing) on a Linux platform layer.
//
//   hm01                      search the network for the printer (or use the saved one)
//   hm01 --printer 192.168.1.50   use this address (saved)
//   hm01 --search             forget the saved address and search
//   hm01 --demo               sample data, no network (to check the screen)
//   hm01 --fb /dev/fb1 --rotate 90   framebuffer device and turn
//
// Keys: ESC (or Q) back to the menu; Tab / arrows / , . ; / page; Enter
// or Space silence an alert, then dismiss an end screen; T test the alert.

#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "alert.h"
#include "dash.h"
#include "eta.h"
#include "gfx.h"
#include "job.h"
#include "link.hpp"
#include "platform.hpp"
#include "printer.h"
#include "screens.h"

#ifndef HM01_VERSION
#define HM01_VERSION "0.0.0"
#endif

namespace {

constexpr uint32_t kFrameMs   = 33;     // the loop's pace
constexpr uint32_t kMinDrawMs = 250;    // content changes drawn at most this often
constexpr uint32_t kSplashMs  = 1800;

volatile std::sig_atomic_t g_quit = 0;

void onSignal(int){
    g_quit = 1;
}

uint32_t millis(){
    static const auto t0 = std::chrono::steady_clock::now();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t0).count());
}

bool localTime(int64_t epoch, int* hour, int* minute){
    time_t t = static_cast<time_t>(epoch);
    struct tm tm;
    if (!localtime_r(&t, &tm)) return false;
    *hour = tm.tm_hour;
    *minute = tm.tm_min;
    return true;
}

int64_t epochNow(){
    const time_t t = time(nullptr);
    return t > 1700000000 ? static_cast<int64_t>(t) : 0;
}

void upper(char* dst, size_t n, const std::string& src){
    size_t i = 0;
    for (; i < src.size() && i < n - 1; i++) dst[i] = static_cast<char>(toupper(static_cast<unsigned char>(src[i])));
    dst[i] = 0;
}

// The real job of 2026-11-19 on the user's Hi, for --demo
const char* kDemo =
    "{\"TotalLayer\":50,\"bedTemp0\":\"60.47\",\"targetBedTemp0\":60,\"nozzleTemp\":\"220.15\","
    "\"targetNozzleTemp\":220,\"maxBedTemp\":100,\"maxNozzleTemp\":300,\"curFeedratePct\":100,"
    "\"curFlowratePct\":100,\"curPosition\":\"X:69.37 Y:69.79 Z:7.56\",\"modelFanPct\":100,"
    "\"auxiliaryFanPct\":0,\"caseFanPct\":0,\"deviceState\":1,\"err\":{\"errcode\":0},"
    "\"hostname\":\"Creality Hi-5EBC\",\"layer\":37,\"model\":\"F018\","
    "\"printFileName\":\"/mnt/UDISK/printer_data/gcodes/wo01_panel_8x8.step_PLA_4h40m54s.gcode\","
    "\"printJobTime\":14620,\"printLeftTime\":2234,\"printProgress\":85,\"realTimeFlow\":\"5.4\","
    "\"realTimeSpeed\":\"250\",\"state\":1,\"usedMaterialLength\":33265}";

void usage(){
    printf("HM-01 %s for the M5Stack CardputerZero: live status of a Creality Hi.\n"
           "  --printer <ip>   use this printer address (saved)\n"
           "  --search         forget the saved address and search the network\n"
           "  --demo           sample data, no network\n"
           "  --fb <device>    framebuffer (default: the first 320x170 one)\n"
           "  --rotate <deg>   0, 90 or 270 (default: from the panel's shape)\n"
           "  --mute | --unmute  alert sounds off / on (saved)\n"
           "Keys: ESC back to the menu, Tab/arrows page, Enter silence, T test the alert.\n"
           "Config: %s\n",
           HM01_VERSION, cz::configPath().c_str());
}

}  // namespace

int main(int argc, char** argv){
    cz::Config cfg = cz::loadConfig();
    std::string fbPath;
    int rotate = -1;
    bool demo = false;
    bool saveCfg = false;
    int probeS = 0;   // --probe: the link only, printed, for this many seconds
    for (int i = 1; i < argc; i++){
        const std::string a = argv[i];
        if (a == "--printer" && i + 1 < argc){
            cfg.printer = argv[++i];
            cfg.printerAuto = false;
            saveCfg = true;
        } else if (a == "--search"){
            cfg.printer.clear();
            cfg.printerAuto = false;
            saveCfg = true;
        } else if (a == "--demo"){
            demo = true;
        } else if (a == "--fb" && i + 1 < argc){
            fbPath = argv[++i];
        } else if (a == "--rotate" && i + 1 < argc){
            rotate = atoi(argv[++i]);
        } else if (a == "--mute" || a == "--unmute"){
            cfg.muted = a == "--mute";
            saveCfg = true;
        } else if (a == "--version"){
            printf("HM-01 %s cardputerzero\n", HM01_VERSION);
            return 0;
        } else if (a == "--probe" && i + 1 < argc){
            probeS = atoi(argv[++i]);
        } else if (a == "--info"){
            // The screen and the keyboards found, without drawing
            cz::Framebuffer f;
            const bool ok = f.open(fbPath, rotate);
            printf("framebuffer: %s\n", ok ? f.describe().c_str() : "none");
            cz::Keyboard k;
            k.open();
            printf("keyboards: %d\n", k.devices());
            const cz::Battery b = cz::readBattery();
            printf("battery: %d%%%s  wifi bars: %d  ip: %s\n", b.percent, b.charging ? " charging" : "",
                   cz::wifiBars(), cz::ownIp().c_str());
            printf("config: %s\n", cz::configPath().c_str());
            return ok ? 0 : 1;
        } else {
            usage();
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }
    if (saveCfg) cz::saveConfig(cfg);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    // --probe: no screen; the link and the parsed numbers once a second
    if (probeS > 0){
        cz::PrinterLink link;
        link.start(cfg.printer, cfg.printerAuto, [](const std::string& found){
            printf("found printer at %s\n", found.c_str());
        });
        hm::PrinterStatus st;
        hm::Estimator est;
        for (int s = 0; s < probeS && !g_quit; s++){
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::vector<std::string> msgs;
            link.take(msgs);
            for (const std::string& m : msgs) hm::applyCrealityMessage(st, m.data(), m.size());
            const cz::LinkInfo li = link.info();
            const hm::Estimate e = est.update(st);
            printf("%3ds link=%s printer=%s scan=%d msgs=%u state=%d layer=%d/%d pct=%d left=%ld nozzle=%.0f "
                   "bed=%.0f speed=%.0f z=%.2f file=%s\n",
                   s + 1, cz::linkName(li.link), li.printer.empty() ? "-" : li.printer.c_str(), li.scanAt,
                   li.messages, st.state, st.layer, st.layers, e.percent, static_cast<long>(e.remainingS),
                   st.nozzle, st.bed, st.speed, st.z, st.file[0] ? st.file : "-");
            fflush(stdout);
        }
        link.stop();
        return 0;
    }

    cz::Framebuffer fb;
    if (!fb.open(fbPath, rotate)){
        fprintf(stderr, "hm01: no framebuffer (%s): run it on the CardputerZero\n",
                fbPath.empty() ? "/dev/fb*" : fbPath.c_str());
        return 1;
    }
    fprintf(stderr, "hm01 %s: %s\n", HM01_VERSION, fb.describe().c_str());
    cz::Keyboard keys;
    if (!keys.open()) fprintf(stderr, "hm01: no keyboard found (ESC will not work; Ctrl+C or the Home key)\n");

    const hm::DashLayout layout = hm::DashLayout::Wide;
    const int W = hm::dashWidth(layout), H = hm::dashHeight(layout);
    std::vector<hm::Color> px(static_cast<size_t>(W) * H);
    std::vector<uint8_t> mask(static_cast<size_t>(W) * H, 0);
    hm::Canvas cv(px.data(), mask.data(), W, H);

    cz::PrinterLink link;
    if (!demo){
        link.start(cfg.printer, cfg.printerAuto, [&cfg](const std::string& found){
            cfg.printer = found;
            cfg.printerAuto = true;
            cz::saveConfig(cfg);
        });
    }

    hm::PrinterStatus st;
    hm::Estimator estimator;
    hm::Estimate estimate;
    hm::JobView job;
    hm::Alerts alerts;
    if (demo){
        hm::applyCrealityMessage(st, kDemo, strlen(kDemo));
    }

    hm::View drawn;
    bool haveDrawn = false;
    uint32_t drawnAt = 0;
    uint8_t page = 0;
    cz::Battery battery;
    int bars = -1;
    std::string ip;
    uint32_t sysAt = 0;
    bool sysRead = false;
    const uint32_t splashUntil = millis() + kSplashMs;

    while (!g_quit){
        const uint32_t frameStart = millis();

        // Keys
        for (cz::Key k = keys.poll(); k != cz::Key::None; k = keys.poll()){
            switch (k){
                case cz::Key::Quit: g_quit = 1; break;
                case cz::Key::PageNext:
                case cz::Key::PagePrev: page ^= 1; break;
                case cz::Key::Click:
                    if (alerts.active(millis())) alerts.acknowledge();
                    else if (job.ended()) job.dismiss();
                    break;
                case cz::Key::Test:
                    alerts.test(hm::AlertKind::Done, millis());
                    if (!cfg.muted) cz::playMelody(hm::AlertKind::Done);
                    break;
                default: break;
            }
        }

        // The printer
        std::vector<std::string> msgs;
        link.take(msgs);
        for (const std::string& m : msgs) hm::applyCrealityMessage(st, m.data(), m.size());
        const cz::LinkInfo li = demo ? cz::LinkInfo{cz::Link::Online, "DEMO", false, 0, false, 1} : link.info();

        const uint32_t ms = millis();
        if (!sysRead || ms - sysAt >= 5000){
            sysRead = true;
            sysAt = ms;
            battery = cz::readBattery();
            bars = cz::wifiBars();
            ip = cz::ownIp();
        }
        if (li.link == cz::Link::Online){
            estimate = estimator.update(st);
            job.update(st, estimate, ms / 1000, epochNow());
        }

        // The view
        hm::View v;
        snprintf(v.version, sizeof(v.version), "V%s", HM01_VERSION);
        v.blink = static_cast<uint8_t>(((ms / 500) & 1) + 1);
        v.spin = static_cast<uint8_t>((ms / kMinDrawMs) % hm::kRingSegments);
        if (ms < splashUntil){
            v.screen = hm::Screen::Splash;
            v.blink = 0;
            v.spin = 0;
        } else {
            switch (li.link){
                case cz::Link::Searching:
                    v.screen = hm::Screen::Linking;
                    snprintf(v.line1, sizeof(v.line1), "SEARCH");
                    snprintf(v.line2, sizeof(v.line2), li.scanFailed ? "NOT FOUND" : "PRINTER");
                    if (!li.scanFailed) snprintf(v.line3, sizeof(v.line3), "%d/254", li.scanAt);
                    break;
                case cz::Link::Connecting:
                    v.screen = hm::Screen::Linking;
                    snprintf(v.line1, sizeof(v.line1), "PRINTER");
                    upper(v.line2, sizeof(v.line2), li.printer);
                    snprintf(v.line3, sizeof(v.line3), "PORT 9999");
                    break;
                case cz::Link::Offline:
                    v.screen = hm::Screen::Offline;
                    snprintf(v.line1, sizeof(v.line1), "PRINTER");
                    upper(v.line2, sizeof(v.line2), li.printer);
                    snprintf(v.line3, sizeof(v.line3), "RETRYING");
                    break;
                case cz::Link::Online:
                    job.fill(v, st, estimate, epochNow(), localTime);
                    break;
            }
            if (!hm::animates(v)){
                v.blink = 0;
                v.spin = 0;
            } else if (v.screen != hm::Screen::Linking){
                v.spin = 0;
            }
        }
        snprintf(v.d.ip, sizeof(v.d.ip), "%s", ip.c_str());
        v.d.wifiBars = static_cast<int8_t>(bars);
        v.d.battery = static_cast<int8_t>(battery.percent);
        v.d.charging = battery.charging;
        const bool live = v.screen == hm::Screen::Printing || v.screen == hm::Screen::Paused;
        v.page = live ? page : 0;

        // Alerts: the end of a job, an error or a pause on one
        const hm::AlertKind cue = alerts.update(v, ms);
        if (cue != hm::AlertKind::None){
            if (!cfg.muted) cz::playMelody(cue);
            fprintf(stderr, "hm01: alert: %s\n", cue == hm::AlertKind::Done ? "job done" : "attention");
        }
        if (alerts.active(ms)){
            v.alert = true;
            v.blink = static_cast<uint8_t>(((ms / 250) & 1) + 1);   // flash at 2 Hz
        }

        // Draw: a new screen, a page or an alert at once; other changes at
        // most every kMinDrawMs
        bool draw = !haveDrawn || v != drawn;
        if (draw && haveDrawn && v.screen == drawn.screen && v.page == drawn.page && v.alert == drawn.alert
            && !v.alert && ms - drawnAt < kMinDrawMs){
            draw = false;
        }
        if (draw){
            hm::drawDash(cv, v, 0, layout);
            fb.push(px.data(), W, H, 0);
            drawn = v;
            haveDrawn = true;
            drawnAt = ms;
        }

        const uint32_t spent = millis() - frameStart;
        if (spent < kFrameMs) std::this_thread::sleep_for(std::chrono::milliseconds(kFrameMs - spent));
    }

    link.stop();
    fprintf(stderr, "hm01: back to the menu\n");
    return 0;
}
