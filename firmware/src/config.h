#pragma once

// HM-01 build-time settings. Things a user changes at run time (printer
// address, time zone, orientation calibration, brightness) live in NVS, see
// settings.h; these are the defaults and the fixed tuning.

#include <stdint.h>

#define FW_VERSION "0.1.0"

constexpr const char* kUnitCode = "HM-01";

// ---- printer
constexpr uint16_t kCrealityPort  = 9999;   // Creality LAN websocket

// Creality Print reconnects after 20 s without a message; so do we
constexpr uint32_t kSilenceReconnectMs = 20000;
// The printer's web UI sends {"ModeCode":"heart_beat"} every few seconds
constexpr uint32_t kHeartbeatMs = 10000;
constexpr uint32_t kWsReconnectMs = 3000;
// No data for this long (or never connected for this long): OFFLINE screen
constexpr uint32_t kOfflineAfterMs = 12000;


// Network search for the printer (when no address is set): a TCP connect to
// port 9999 on every host of our own /24. The first packet to a host waits
// for ARP, and on Wi-Fi the access point holds broadcasts for a dozing
// client until its next DTIM beacon, so first contact can take several
// hundred ms (150 ms missed the user's Hi every time). Hence a long timeout,
// with several hosts in flight at once; lwIP's ARP table holds only 10.
constexpr uint32_t kScanConnectMs = 1500;
constexpr int      kScanParallel  = 6;
constexpr uint32_t kScanRetryMs   = 60000;

// ---- Wi-Fi
constexpr uint32_t kWifiJoinTimeoutMs = 30000;   // then the setup portal opens
constexpr uint32_t kPortalTimeoutS    = 0;       // 0 = the portal stays open until configured
// Default time zone: Lisbon / London (POSIX TZ). Change it in the setup
// portal or with the console command "tz".
constexpr const char* kDefaultTz = "WET0WEST,M3.5.0/1,M10.5.0";
constexpr const char* kNtpServer1 = "pool.ntp.org";
constexpr const char* kNtpServer2 = "time.cloudflare.com";

// ---- display
constexpr uint8_t  kBrightness     = 128;
constexpr uint8_t  kDimBrightness  = 24;
constexpr uint32_t kIdleDimS       = 15 * 60;   // READY screen this long: dim
constexpr uint32_t kSplashMs       = 2500;
constexpr uint32_t kFrameMs        = 33;        // ~30 fps, the tilt filter's pace

// Boot orientation, in 90-degree clockwise steps added to the display's
// default rotation, and the tilt mapping. Same values as claude-monitor's
// AtomS3R build (verified there on hardware in all four standing
// positions); "calibrate" on the console overrides them.
constexpr uint8_t kBootRotationOffset = 1;
constexpr float   kTiltSign           = -1.0f;
constexpr float   kTiltOffsetDeg      = 0.0f;

// Button: hold this long to open the setup portal
constexpr uint32_t kHoldSetupMs = 3000;
