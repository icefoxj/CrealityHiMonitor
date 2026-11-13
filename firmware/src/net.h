#pragma once

// Wi-Fi (with a setup portal), the printer search and the Creality
// websocket. net::loop() runs in the main loop; the search runs in its own
// task because it blocks.

#include <stdint.h>

#include "printer.h"

namespace net {

enum class Link : uint8_t {
    Portal,       // setup portal open (AP "HM-01-xxxx")
    Joining,      // joining the saved Wi-Fi network
    Searching,    // looking for the printer on the network
    Connecting,   // websocket to the printer not up yet
    Online,       // websocket up, data flowing
    Offline,      // printer not answering
};

const char* linkName(Link l);

struct Info {
    Link     link        = Link::Joining;
    char     ssid[33]    = "";
    char     ip[16]      = "";
    char     printer[64] = "";   // address in use (or "" while searching)
    char     apName[20]  = "";
    char     apPass[12]  = "";
    int      scanAt      = 0;    // search progress, host number 1..254
    bool     scanFailed  = false;
    uint32_t messages    = 0;    // websocket messages received
    uint32_t reconnects  = 0;
};

void begin();
void loop();

void startPortal();
void wifiReset();              // forget the network, reboot into the portal
void usePrinter(const char* host);   // "" = search the network

const Info& info();
const hm::PrinterStatus& printer();

}  // namespace net
