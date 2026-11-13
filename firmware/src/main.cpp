// HM-01: live print status of a Creality Hi on an M5Stack AtomS3R.
//
// The 128x128 panel shows a fixed bezel and a round HUD that turns with
// the cube (accelerometer, smoothed: the claude-monitor AtomS3R pattern).
// Data comes from the printer's Creality websocket (port 9999). See
// README.md.
//
// Board-independent logic (parsing, ETA, screens, drawing, tilt filter,
// console parser) is in lib/hm_core and tested on the host; this file wires
// it to the hardware: display, IMU, button, backlight, serial console.

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "alert.h"
#include "config.h"
#include "console.h"
#include "eta.h"
#include "gfx.h"
#include "job.h"
#include "net.h"
#include "screens.h"
#include "settings.h"
#include "sound.h"
#include "tilt.h"

namespace {

constexpr float kPi = 3.14159265f;

hm::Color*   g_fb   = nullptr;   // 128x128 RGB565, 32 KB
uint8_t*     g_mask = nullptr;   // 128x128 coverage, 16 KB
hm::Canvas*  g_canvas = nullptr;

hm::Tilt       g_tilt;
hm::TiltConfig g_tiltCfg;
bool           g_imu = false;
float          g_ax = 0.0f, g_ay = 0.0f, g_az = 0.0f;

hm::Estimator  g_estimator;
hm::Estimate   g_estimate;
hm::JobView    g_job;
hm::LineReader g_reader;
hm::Alerts     g_alerts;

hm::View g_drawn;
bool     g_haveDrawn = false;
uint32_t g_splashUntil = 0;
uint32_t g_frame = 0;

// Backlight: the button cycles bright -> dim -> off; READY for a long time dims
enum class Lamp : uint8_t { Bright, Dim, Off };
Lamp     g_lamp = Lamp::Bright;
uint8_t  g_brightness = 0xFF;    // applied value (0xFF = not yet)
uint32_t g_idleSince = 0;
bool     g_holdLatched = false;

uint8_t defaultRotation(){
    return static_cast<uint8_t>((M5.Display.getRotation() + kBootRotationOffset) & 3);
}

void applyCalibration(){
    const Settings& s = settings::get();
    M5.Display.setRotation(s.rotation);
    g_tiltCfg.angleSign   = s.sign;
    g_tiltCfg.angleOffset = s.offsetDeg * kPi / 180.0f;
    g_haveDrawn = false;
}

bool localTime(int64_t epoch, int* hour, int* minute){
    time_t t = static_cast<time_t>(epoch);
    struct tm tm;
    if (!localtime_r(&t, &tm)){
        return false;
    }
    *hour = tm.tm_hour;
    *minute = tm.tm_min;
    return true;
}

int64_t epochNow(){
    const time_t t = time(nullptr);
    return t > 1700000000 ? static_cast<int64_t>(t) : 0;   // 0 until SNTP has set the clock
}

void upper(char* dst, size_t n, const char* src){
    size_t i = 0;
    for (; src && src[i] && i < n - 1; i++){
        dst[i] = static_cast<char>(toupper(static_cast<unsigned char>(src[i])));
    }
    dst[i] = 0;
}

// ---------------------------------------------------------------- view

hm::View buildView(uint32_t nowMs){
    hm::View v;
    snprintf(v.version, sizeof(v.version), "V%s", FW_VERSION);
    v.blink = static_cast<uint8_t>(((nowMs / 500) & 1) + 1);   // 1 or 2: never 0 while live
    v.spin  = static_cast<uint8_t>((nowMs / 60) % hm::kRingSegments);

    if (nowMs < g_splashUntil){
        v.screen = hm::Screen::Splash;
        v.blink = 0;
        v.spin = 0;
        return v;
    }

    const net::Info& ni = net::info();
    switch (ni.link){
        case net::Link::Portal:
            v.screen = hm::Screen::Setup;
            upper(v.line1, sizeof(v.line1), ni.apName);
            upper(v.line2, sizeof(v.line2), ni.apPass);
            snprintf(v.line3, sizeof(v.line3), "192.168.4.1");
            break;
        case net::Link::Joining:
            v.screen = hm::Screen::Linking;
            snprintf(v.line1, sizeof(v.line1), "WIFI");
            upper(v.line2, sizeof(v.line2), WiFi.SSID().c_str());
            break;
        case net::Link::Searching:
            v.screen = hm::Screen::Linking;
            snprintf(v.line1, sizeof(v.line1), "SEARCH");
            snprintf(v.line2, sizeof(v.line2), ni.scanFailed ? "NOT FOUND" : "PRINTER");
            if (!ni.scanFailed){
                snprintf(v.line3, sizeof(v.line3), "%d/254", ni.scanAt);
            }
            break;
        case net::Link::Connecting:
            v.screen = hm::Screen::Linking;
            snprintf(v.line1, sizeof(v.line1), "PRINTER");
            upper(v.line2, sizeof(v.line2), ni.printer);
            snprintf(v.line3, sizeof(v.line3), "PORT %u", kCrealityPort);
            break;
        case net::Link::Offline:
            v.screen = hm::Screen::Offline;
            snprintf(v.line1, sizeof(v.line1), "PRINTER");
            upper(v.line2, sizeof(v.line2), ni.printer);
            snprintf(v.line3, sizeof(v.line3), "RETRYING");
            break;
        case net::Link::Online:
            g_job.fill(v, net::printer(), g_estimate, epochNow(), localTime);
            break;
    }
    if (!hm::animates(v)){
        v.blink = 0;
        v.spin = 0;
    } else if (v.screen != hm::Screen::Setup && v.screen != hm::Screen::Linking){
        v.spin = 0;
    }
    return v;
}

void render(const hm::View& v){
    hm::drawScreen(*g_canvas, v, g_tilt.angle);
    g_tilt.drawn = g_tilt.angle;
    M5.Display.pushImage(0, 0, hm::kScreenSize, hm::kScreenSize,
                         reinterpret_cast<const lgfx::rgb565_t*>(g_fb));
}

// ---------------------------------------------------------------- backlight

void updateBacklight(const hm::View& v, uint32_t nowS, bool alert){
    if (v.screen == hm::Screen::Idle){
        if (g_idleSince == 0) g_idleSince = nowS;
    } else {
        g_idleSince = 0;
    }
    uint8_t want;
    if (alert){
        // An alert lights the screen, even if it was dimmed or off
        want = settings::get().brightness < kBrightness ? kBrightness : settings::get().brightness;
    } else switch (g_lamp){
        case Lamp::Off: want = 0; break;
        case Lamp::Dim: want = kDimBrightness; break;
        default:
            want = settings::get().brightness;
            if (g_idleSince != 0 && nowS - g_idleSince >= kIdleDimS){
                want = kDimBrightness;
            }
            break;
    }
    if (want != g_brightness){
        g_brightness = want;
        M5.Display.setBrightness(want);
    }
}

// ---------------------------------------------------------------- console

void sendStatus(){
    const net::Info& ni = net::info();
    const hm::PrinterStatus& ps = net::printer();
    const Settings& s = settings::get();
    Serial.printf("STATUS fw=%s link=%s wifi=%s ip=%s printer=%s%s phase=%s state=%d file_pct=%d"
                  " left=%ld job=%ld layer=%d/%d pct=%d eta_left=%ld total=%ld%s"
                  " screen=%s angle=%.1f rot=%u sign=%d offset=%.1f ax=%.2f ay=%.2f az=%.2f"
                  " tz=%s sound=%s%s alert=%d heap=%u msgs=%lu\n",
                  FW_VERSION, net::linkName(ni.link), ni.ssid[0] ? ni.ssid : "-", ni.ip[0] ? ni.ip : "-",
                  ni.printer[0] ? ni.printer : "-", s.printerAuto ? "(found)" : "",
                  hm::phaseName(hm::phaseOf(ps)), ps.state, ps.progress,
                  static_cast<long>(ps.leftS), static_cast<long>(ps.jobS), ps.layer, ps.layers,
                  g_estimate.percent, static_cast<long>(g_estimate.remainingS),
                  static_cast<long>(g_estimator.total()), g_estimate.overrun ? "(overrun)" : "", hm::screenName(g_drawn.screen), g_tilt.angle * 180.0f / kPi,
                  s.rotation, s.sign < 0 ? -1 : 1, s.offsetDeg, g_ax, g_ay, g_az, s.tz,
                  sound::describe(), s.muted ? "(muted)" : "", static_cast<int>(g_alerts.kind()),
                  static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned long>(ni.messages));
}

void sendVersion(){
    Serial.printf("VERSION fw=%s board=atoms3r unit=%s idf=%s\n", FW_VERSION, kUnitCode, esp_get_idf_version());
}

void sendHelp(){
    Serial.print(
        "commands:\n"
        "  status | version | help\n"
        "  printer <ip|host>     set the printer address\n"
        "  printer auto          search the network for it\n"
        "  tz <POSIX TZ>         time zone, e.g. tz WET0WEST,M3.5.0/1,M10.5.0\n"
        "  calibrate rot=<0-3> sign=<1|-1> offset=<deg> | calibrate reset\n"
        "  bright <0-255>\n"
        "  sound on|off | volume <0-255> | buzzer <gpio>|off | beep\n"
        "  setup                 open the Wi-Fi setup portal\n"
        "  wifi reset            forget the Wi-Fi network and reboot\n"
        "  reboot\n");
}

void handleCommand(const hm::Command& c){
    switch (c.kind){
        case hm::Cmd::None: break;
        case hm::Cmd::Help: sendHelp(); break;
        case hm::Cmd::Status: sendStatus(); break;
        case hm::Cmd::Version: sendVersion(); break;
        case hm::Cmd::Printer:
            net::usePrinter(strcmp(c.arg, "auto") == 0 ? "" : c.arg);
            Serial.printf("OK printer=%s\n", settings::get().printer[0] ? settings::get().printer : "auto");
            break;
        case hm::Cmd::Tz:
            settings::setTz(c.arg);
            setenv("TZ", settings::get().tz, 1);
            tzset();
            Serial.printf("OK tz=%s\n", settings::get().tz);
            break;
        case hm::Cmd::Calibrate: {
            if (c.reset){
                settings::resetCalibration();
            } else {
                const Settings& s = settings::get();
                settings::setCalibration(c.hasRot ? c.rot : s.rotation,
                                         c.hasSign ? static_cast<float>(c.sign) : s.sign,
                                         c.hasOffset ? c.offsetDeg : s.offsetDeg);
            }
            applyCalibration();
            sendStatus();
            break;
        }
        case hm::Cmd::Bright:
            settings::setBrightness(static_cast<uint8_t>(c.value));
            g_lamp = Lamp::Bright;
            g_brightness = 0xFF;   // re-apply
            Serial.printf("OK bright=%d\n", c.value);
            break;
        case hm::Cmd::Sound:
            settings::setMuted(c.value == 0);
            if (c.value == 0) sound::stop();
            Serial.printf("OK sound=%s output=%s\n", c.value ? "on" : "off", sound::describe());
            break;
        case hm::Cmd::Volume:
            settings::setVolume(static_cast<uint8_t>(c.value));
            sound::reconfigure();
            Serial.printf("OK volume=%d\n", c.value);
            break;
        case hm::Cmd::Buzzer:
            if (c.value >= 0 && !sound::validBuzzerPin(c.value)){
                Serial.println("ERROR buzzer: use a free pin: 1 2 (Grove), 5 6 7 8 38 39 (bottom), or off");
                break;
            }
            settings::setBuzzerPin(static_cast<int8_t>(c.value));
            sound::reconfigure();
            Serial.printf("OK output=%s\n", sound::describe());
            break;
        case hm::Cmd::Beep:
            sound::play(hm::AlertKind::Done);   // a sound test: the screen does not change
            Serial.printf("OK beep output=%s%s\n", sound::describe(),
                          settings::get().muted ? " (muted)" : "");
            break;
        case hm::Cmd::Setup:
            net::startPortal();
            Serial.printf("OK portal AP=%s password=%s\n", net::info().apName, net::info().apPass);
            break;
        case hm::Cmd::WifiReset:
            Serial.println("OK forgetting Wi-Fi, rebooting");
            Serial.flush();
            net::wifiReset();
            break;
        case hm::Cmd::Reboot:
            Serial.println("OK rebooting");
            Serial.flush();
            delay(100);
            ESP.restart();
            break;
        case hm::Cmd::BadArgs:
            Serial.printf("ERROR usage: %s\n", c.arg);
            break;
        case hm::Cmd::Unknown:
            Serial.printf("ERROR unknown command \"%s\" (try help)\n", c.arg);
            break;
    }
}

void pollConsole(){
    while (Serial.available() > 0){
        if (g_reader.feed(static_cast<char>(Serial.read()))){
            handleCommand(hm::parseCommand(g_reader.line()));
        }
    }
}

// ---------------------------------------------------------------- button

void pollButton(const hm::View& v){
    if (M5.BtnA.pressedFor(kHoldSetupMs)){
        if (!g_holdLatched){
            g_holdLatched = true;
            net::startPortal();
        }
        return;
    }
    if (M5.BtnA.wasReleased() && g_holdLatched){
        g_holdLatched = false;
        return;
    }
    if (M5.BtnA.wasClicked()){
        if (v.alert){
            g_alerts.acknowledge();
            sound::stop();
            return;
        }
        // On an end screen the first press dismisses it, then the lamp cycles
        if (g_job.ended() && (v.screen == hm::Screen::Done || v.screen == hm::Screen::Stopped
                              || v.screen == hm::Screen::Error)){
            g_job.dismiss();
            g_lamp = Lamp::Bright;
        } else if (g_idleSince != 0 && g_lamp == Lamp::Bright && g_brightness == kDimBrightness){
            g_idleSince = 0;   // idle-dimmed: a press brightens it again
        } else {
            g_lamp = g_lamp == Lamp::Bright ? Lamp::Dim : (g_lamp == Lamp::Dim ? Lamp::Off : Lamp::Bright);
        }
    }
}

}  // namespace

void setup(){
    auto cfg = M5.config();
    sound::prepare(cfg);   // looks for an Atomic Echo Base
    M5.begin(cfg);
    Serial.begin(115200);

    settings::begin(defaultRotation());
    applyCalibration();
    setenv("TZ", settings::get().tz, 1);
    tzset();
    sound::begin();

    g_fb   = static_cast<hm::Color*>(heap_caps_malloc(hm::kScreenSize * hm::kScreenSize * sizeof(hm::Color),
                                                      MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    g_mask = static_cast<uint8_t*>(heap_caps_calloc(hm::kScreenSize * hm::kScreenSize, 1,
                                                    MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    if (!g_fb || !g_mask){
        Serial.println("FATAL: no memory for the frame buffer");
        delay(2000);
        ESP.restart();
    }
    g_canvas = new hm::Canvas(g_fb, g_mask, hm::kScreenSize, hm::kScreenSize);

    g_imu = M5.Imu.isEnabled();
    if (g_imu){
        // Prime the tilt so the first frame is already upright
        M5.Imu.update();
        M5.Imu.getAccel(&g_ax, &g_ay, &g_az);
        hm::updateTilt(g_tilt, g_tiltCfg, g_ax, g_ay, g_az);
    }

    g_splashUntil = millis() + kSplashMs;
    const hm::View splash = buildView(millis());
    render(splash);
    g_drawn = splash;
    g_haveDrawn = true;
    M5.Display.setBrightness(settings::get().brightness);
    g_brightness = settings::get().brightness;

    sendVersion();
    Serial.printf("sound: %s\n", sound::describe());
    Serial.printf("imu %s, rotation %u, sign %d, offset %.1f (%s)\n", g_imu ? "enabled" : "absent",
                  settings::get().rotation, settings::get().sign < 0 ? -1 : 1, settings::get().offsetDeg,
                  settings::get().calStored ? "from nvs" : "compiled");
    net::begin();
}

void loop(){
    static uint32_t next = millis();
    const uint32_t now = millis();
    if (static_cast<int32_t>(next - now) > 0){
        delay(next - now);   // the rest of the ~33 ms frame
    }
    next += kFrameMs;
    if (static_cast<int32_t>(millis() - next) > 200){
        next = millis();     // fell behind (portal page, long DNS...): do not race
    }
    g_frame++;

    M5.update();

    bool tiltMoved = false;
    if (g_imu){
        M5.Imu.update();
        M5.Imu.getAccel(&g_ax, &g_ay, &g_az);
        tiltMoved = hm::updateTilt(g_tilt, g_tiltCfg, g_ax, g_ay, g_az);
    }

    pollConsole();
    net::loop();

    const uint32_t ms = millis();
    const uint32_t nowS = ms / 1000;
    if (net::info().link == net::Link::Online){
        g_estimate = g_estimator.update(net::printer());
        g_job.update(net::printer(), g_estimate, nowS, epochNow());
    }

    hm::View v = buildView(ms);
    const hm::AlertKind cue = g_alerts.update(v, ms);
    if (cue != hm::AlertKind::None){
        sound::play(cue);
        Serial.printf("alert: %s\n", cue == hm::AlertKind::Done ? "job done" : "attention");
    }
    if (g_alerts.active(ms)){
        v.alert = true;
        v.blink = static_cast<uint8_t>(((ms / 250) & 1) + 1);   // flash at 2 Hz
    }
    sound::loop();
    pollButton(v);
    updateBacklight(v, nowS, v.alert);

    if (g_brightness == 0){
        g_haveDrawn = false;   // redraw when it comes back on
        return;
    }
    if (!g_haveDrawn || tiltMoved || v != g_drawn){
        render(v);
        g_drawn = v;
        g_haveDrawn = true;
    }
}
