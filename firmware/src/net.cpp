#include "net.h"

#include <WebSocketsClient.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <errno.h>
#include <esp_mac.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <lwip/sockets.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "settings.h"

namespace net {

namespace {

enum class PortalWhy : uint8_t { None, NoCredentials, JoinFailed, Asked };

Info              s_info;
hm::PrinterStatus s_printer;
PortalWhy         s_portalWhy = PortalWhy::None;
uint32_t          s_portalAt  = 0;
uint32_t          s_joinAt    = 0;
bool              s_wifiUp    = false;

WiFiManager           wm;
WiFiManagerParameter* pPrinter = nullptr;
WiFiManagerParameter* pTz      = nullptr;
volatile bool         s_paramsSaved = false;

// ---- websocket
WebSocketsClient ws;
bool     s_wsStarted   = false;
bool     s_wsUp        = false;
uint32_t s_lastRx      = 0;
uint32_t s_lastBeat    = 0;
uint32_t s_linkSince   = 0;   // start of the current attempt / outage
bool     s_everData    = false;

// ---- search task
TaskHandle_t  s_scanTask  = nullptr;
volatile bool s_scanDone  = false;
volatile int  s_scanAt    = 0;
char          s_scanFound[16] = "";
uint32_t      s_scanEndAt = 0;
uint8_t       s_net[3]    = {0, 0, 0};
uint8_t       s_self      = 0;


// Milliseconds since t, a millis() stamp. Reads the clock afresh on purpose:
// stamps set during the current pass (startWs(), startPortal(), the
// websocket callbacks inside ws.loop()) are newer than a time read at the
// top of loop(), and an unsigned "now - t" then wraps to about 49 days.
uint32_t since(uint32_t t){
    return millis() - t;
}

void copyStr(char* dst, size_t n, const char* src){
    snprintf(dst, n, "%s", src ? src : "");
}

// ---------------------------------------------------------------- websocket

void onWsEvent(WStype_t type, uint8_t* payload, size_t length){
    switch (type){
        case WStype_CONNECTED:
            s_wsUp = true;
            s_lastRx = millis();
            s_lastBeat = 0;
            s_info.reconnects++;
            break;
        case WStype_DISCONNECTED:
            if (s_wsUp){
                s_linkSince = millis();
            }
            s_wsUp = false;
            break;
        case WStype_TEXT: {
            s_lastRx = millis();
            s_info.messages++;
            if (length == 2 && payload[0] == 'o' && payload[1] == 'k'){
                break;   // the printer's answer to our heartbeat
            }
            bool beat = false;
            if (hm::applyCrealityMessage(s_printer, reinterpret_cast<const char*>(payload), length, &beat)){
                s_everData = true;
            }
            if (beat){
                ws.sendTXT("ok");
            }
            break;
        }
        default:
            break;
    }
}

void sendHeartbeat(){
    char iso[32] = "";
    time_t now = time(nullptr);
    if (now > 1700000000){
        struct tm tm;
        gmtime_r(&now, &tm);
        strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S.000Z", &tm);
    }
    char msg[80];
    snprintf(msg, sizeof(msg), "{\"ModeCode\":\"heart_beat\",\"msg\":\"%s\"}", iso);
    ws.sendTXT(msg);
}

void stopWs(){
    if (s_wsStarted){
        ws.disconnect();
        s_wsStarted = false;
    }
    s_wsUp = false;
}

void startWs(const char* host){
    stopWs();
    copyStr(s_info.printer, sizeof(s_info.printer), host);
    s_printer = hm::PrinterStatus{};
    s_everData = false;
    ws.begin(host, kCrealityPort, "/", "");   // no subprotocol, like Creality Print
    ws.onEvent(onWsEvent);
    ws.setReconnectInterval(kWsReconnectMs);
    s_wsStarted = true;
    s_linkSince = millis();
    s_info.link = Link::Connecting;
    Serial.printf("printer: connecting to %s:%u\n", host, kCrealityPort);
}

// ---------------------------------------------------------------- printer search

// A Creality printer answers the websocket upgrade on 9999 and then sends
// its JSON snapshot, which has "TotalLayer" / "hostname" / "printProgress"
bool looksLikeCreality(WiFiClient& c, const IPAddress& ip){
    c.printf("GET / HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
             "Sec-WebSocket-Key: SE0tMDEgc2VhcmNoIGtleQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
             ip.toString().c_str(), kCrealityPort);
    char win[48];
    size_t have = 0;
    bool upgraded = false;
    const uint32_t until = millis() + 4000;
    while (millis() < until && c.connected()){
        while (c.available()){
            const int ch = c.read();
            if (ch < 0) break;
            if (have == sizeof(win) - 1){
                memmove(win, win + 1, have - 1);
                have--;
            }
            win[have++] = static_cast<char>(ch);
            win[have] = 0;
            if (!upgraded && strstr(win, " 101 ")) upgraded = true;
            if (upgraded && (strstr(win, "\"TotalLayer\"") || strstr(win, "\"printProgress\"")
                             || strstr(win, "\"hostname\""))){
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

// Port 9999 answered on this host: is it a Creality printer? By now ARP has
// resolved it, so a fresh connection is quick.
bool checkHost(uint8_t host){
    const IPAddress ip(s_net[0], s_net[1], s_net[2], host);
    WiFiClient c;
    if (!c.connect(ip, kCrealityPort, 1000)){
        return false;
    }
    const bool yes = looksLikeCreality(c, ip);
    c.stop();
    return yes;
}

// Starts a non-blocking connect to host:9999. -1: no socket free (retry
// later), -2: the connect failed at once (skip the host).
int openProbe(uint8_t host){
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0){
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kCrealityPort);
    addr.sin_addr.s_addr = htonl((uint32_t(s_net[0]) << 24) | (uint32_t(s_net[1]) << 16)
                                 | (uint32_t(s_net[2]) << 8) | host);
    if (lwip_connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0 && errno != EINPROGRESS){
        close(fd);
        return -2;
    }
    return fd;
}

void scanTask(void*){
    struct Probe { int fd; uint8_t host; uint32_t at; };
    Probe probe[kScanParallel];
    for (Probe& p : probe){
        p.fd = -1;
    }
    int next = 1;
    bool found = false;
    while (!found){
        // Keep kScanParallel connects in flight
        bool noSocket = false;
        for (Probe& p : probe){
            while (p.fd < 0 && next <= 254 && !noSocket){
                const uint8_t host = static_cast<uint8_t>(next);
                const int fd = host == s_self ? -2 : openProbe(host);
                if (fd == -1){
                    noSocket = true;   // try this host again on the next pass
                    break;
                }
                next++;
                if (fd >= 0){
                    p.fd = fd;
                    p.host = host;
                    p.at = millis();
                    s_scanAt = host;
                }
            }
        }
        fd_set wr;
        FD_ZERO(&wr);
        int maxFd = -1;
        for (const Probe& p : probe){
            if (p.fd >= 0){
                FD_SET(p.fd, &wr);
                maxFd = p.fd > maxFd ? p.fd : maxFd;
            }
        }
        if (maxFd < 0){
            if (noSocket){
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            break;   // every host tried
        }
        struct timeval tv = {0, 50 * 1000};
        if (select(maxFd + 1, nullptr, &wr, nullptr, &tv) < 0){
            FD_ZERO(&wr);
        }
        // Writable = the connect finished, accepted (SO_ERROR 0) or refused
        for (Probe& p : probe){
            if (p.fd < 0){
                continue;
            }
            bool open = false;
            if (FD_ISSET(p.fd, &wr)){
                int err = -1;
                socklen_t len = sizeof(err);
                open = getsockopt(p.fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
            } else if (millis() - p.at < kScanConnectMs){
                continue;   // still waiting
            }
            close(p.fd);
            p.fd = -1;
            if (open && checkHost(p.host)){
                snprintf(s_scanFound, sizeof(s_scanFound), "%u.%u.%u.%u", s_net[0], s_net[1], s_net[2], p.host);
                found = true;
                break;
            }
        }
    }
    for (Probe& p : probe){
        if (p.fd >= 0){
            close(p.fd);
        }
    }
    s_scanDone = true;
    s_scanTask = nullptr;
    vTaskDelete(nullptr);
}

void startSearch(){
    stopWs();
    if (s_scanTask){
        return;
    }
    const IPAddress me = WiFi.localIP();
    s_net[0] = me[0];
    s_net[1] = me[1];
    s_net[2] = me[2];
    s_self = me[3];
    s_scanFound[0] = 0;
    s_scanDone = false;
    s_scanAt = 0;
    s_info.scanFailed = false;
    s_info.printer[0] = 0;
    s_info.link = Link::Searching;
    Serial.printf("printer: searching %u.%u.%u.0/24 for port %u\n", s_net[0], s_net[1], s_net[2], kCrealityPort);
    xTaskCreatePinnedToCore(scanTask, "hm-scan", 6144, nullptr, 1, &s_scanTask, 0);
}

// ---------------------------------------------------------------- Wi-Fi

void onSaveParams(){
    s_paramsSaved = true;
}

void onWifiUp(){
    s_wifiUp = true;
    copyStr(s_info.ssid, sizeof(s_info.ssid), WiFi.SSID().c_str());
    copyStr(s_info.ip, sizeof(s_info.ip), WiFi.localIP().toString().c_str());
    configTzTime(settings::get().tz, kNtpServer1, kNtpServer2);
    Serial.printf("wifi: %s, ip %s\n", s_info.ssid, s_info.ip);
    if (settings::get().printer[0]){
        startWs(settings::get().printer);
    } else {
        startSearch();
    }
}

void applySavedParams(){
    s_paramsSaved = false;
    const char* host = pPrinter->getValue();
    const char* tz = pTz->getValue();
    const bool hostChanged = strcmp(host, settings::get().printer) != 0;
    settings::setTz(tz);
    if (s_wifiUp){
        configTzTime(settings::get().tz, kNtpServer1, kNtpServer2);
    }
    if (hostChanged){
        usePrinter(host);
    }
}

void closePortal(){
    if (wm.getConfigPortalActive()){
        wm.stopConfigPortal();
    }
    s_portalWhy = PortalWhy::None;
}

}  // namespace

const char* linkName(Link l){
    switch (l){
        case Link::Portal:     return "portal";
        case Link::Joining:    return "joining";
        case Link::Searching:  return "searching";
        case Link::Connecting: return "connecting";
        case Link::Online:     return "online";
        case Link::Offline:    return "offline";
    }
    return "?";
}

void begin(){
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_info.apName, sizeof(s_info.apName), "%s-%02X%02X", kUnitCode, mac[4], mac[5]);
    snprintf(s_info.apPass, sizeof(s_info.apPass), "HM01%02X%02X", mac[4], mac[5]);

    WiFi.mode(WIFI_STA);
    // Mains powered: no modem sleep. Dozing makes the radio wake only every
    // few beacons, which slows the printer search and the websocket.
    WiFi.setSleep(false);
    char host[24];
    snprintf(host, sizeof(host), "hm-01-%02x%02x", mac[4], mac[5]);
    WiFi.setHostname(host);
    WiFi.setAutoReconnect(true);

    pPrinter = new WiFiManagerParameter("printer", "Printer IP or host name (leave empty to search the network)",
                                        settings::get().printer, 63);
    pTz = new WiFiManagerParameter("tz", "Time zone, POSIX TZ (e.g. WET0WEST,M3.5.0/1,M10.5.0)",
                                   settings::get().tz, 47);
    wm.addParameter(pPrinter);
    wm.addParameter(pTz);
    wm.setParamsPage(true);   // printer and time zone on their own "Setup" page
    wm.setSaveParamsCallback(onSaveParams);
    wm.setConfigPortalBlocking(false);
    wm.setTitle("HM-01 Creality Hi monitor");
    wm.setClass("invert");
    wm.setShowInfoUpdate(false);
    const char* menu[] = {"wifi", "param", "info", "exit"};
    wm.setMenu(menu, 4);

    if (wm.getWiFiIsSaved()){
        WiFi.begin();
        s_joinAt = millis();
        s_info.link = Link::Joining;
    } else {
        s_portalWhy = PortalWhy::NoCredentials;
        startPortal();
    }
}

void startPortal(){
    if (wm.getConfigPortalActive()){
        return;
    }
    if (s_portalWhy == PortalWhy::None){
        s_portalWhy = PortalWhy::Asked;
    }
    pPrinter->setValue(settings::get().printer, 63);
    pTz->setValue(settings::get().tz, 47);
    wm.startConfigPortal(s_info.apName, s_info.apPass);
    s_portalAt = millis();
    Serial.printf("wifi: setup portal open, AP %s password %s, http://192.168.4.1\n",
                  s_info.apName, s_info.apPass);
}

void wifiReset(){
    stopWs();
    wm.resetSettings();
    delay(200);
    ESP.restart();
}

void usePrinter(const char* host){
    settings::setPrinter(host, false);
    if (!s_wifiUp){
        return;
    }
    if (host && *host){
        startWs(host);
    } else {
        startSearch();
    }
}

void service(){
    if (wm.getConfigPortalActive()){
        wm.process();
    }
}

void loop(){
    const bool portal = wm.getConfigPortalActive();
    if (portal){
        wm.process();
        if (s_paramsSaved){
            applySavedParams();
        }
    }

    // Wi-Fi up / down
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !s_wifiUp){
        onWifiUp();
        // The portal opened only because the network was missing: close it
        if (portal && (s_portalWhy == PortalWhy::JoinFailed || s_portalWhy == PortalWhy::NoCredentials)){
            closePortal();
        }
    } else if (!connected && s_wifiUp){
        s_wifiUp = false;
        stopWs();
        s_joinAt = millis();
        Serial.println("wifi: lost, rejoining");
    }
    if (!connected && !wm.getConfigPortalActive() && since(s_joinAt) > kWifiJoinTimeoutMs){
        s_portalWhy = PortalWhy::JoinFailed;
        startPortal();
    }
    // Opened because the saved network did not answer (after a power cut the
    // router often boots slower than we do): nobody on the portal for 3
    // minutes, so try the network again; the portal reopens if it still fails
    if (wm.getConfigPortalActive() && !connected && s_portalWhy == PortalWhy::JoinFailed
        && since(s_portalAt) > 3UL * 60UL * 1000UL && WiFi.softAPgetStationNum() == 0){
        Serial.println("wifi: nobody on the portal, trying the saved network again");
        closePortal();
        WiFi.mode(WIFI_STA);
        WiFi.begin();
        s_joinAt = millis();
    }
    // Opened from the button or console while online: close after 10 minutes
    if (wm.getConfigPortalActive() && connected && s_portalWhy == PortalWhy::Asked
        && since(s_portalAt) > 10UL * 60UL * 1000UL){
        closePortal();
    }
    if (!wm.getConfigPortalActive()){
        s_portalWhy = PortalWhy::None;
    }

    // Printer link
    if (s_wifiUp){
        if (s_scanTask == nullptr && s_info.link == Link::Searching && s_scanDone){
            if (s_scanFound[0]){
                Serial.printf("printer: found at %s\n", s_scanFound);
                settings::setPrinter(s_scanFound, true);
                startWs(s_scanFound);
            } else {
                Serial.println("printer: not found, searching again in a minute");
                s_info.scanFailed = true;
                s_scanEndAt = millis();
                s_scanDone = false;
            }
        }
        if (s_info.link == Link::Searching && s_info.scanFailed && s_scanTask == nullptr
            && since(s_scanEndAt) > kScanRetryMs){
            startSearch();
        }
        if (s_wsStarted){
            ws.loop();
            if (s_wsUp){
                if (since(s_lastBeat) > kHeartbeatMs){
                    s_lastBeat = millis();
                    sendHeartbeat();
                }
                if (since(s_lastRx) > kSilenceReconnectMs){
                    Serial.println("printer: silent, reconnecting");
                    ws.disconnect();
                    s_wsUp = false;
                    s_linkSince = millis();
                }
            }
            if (s_wsUp && s_everData && since(s_lastRx) < kOfflineAfterMs){
                s_info.link = Link::Online;
            } else if (since(s_wsUp ? s_lastRx : s_linkSince) > kOfflineAfterMs){
                s_info.link = Link::Offline;
                // An address found by the search may have changed (DHCP)
                if (settings::get().printerAuto && since(s_linkSince) > 3UL * 60UL * 1000UL){
                    Serial.println("printer: gone for 3 minutes, searching again");
                    startSearch();
                }
            } else {
                s_info.link = Link::Connecting;
            }
        }
        s_info.scanAt = s_scanAt;
    } else {
        s_info.link = Link::Joining;
    }
    if (wm.getConfigPortalActive()){
        s_info.link = Link::Portal;
    }

}

const Info& info(){
    return s_info;
}

const hm::PrinterStatus& printer(){
    return s_printer;
}


}  // namespace net
