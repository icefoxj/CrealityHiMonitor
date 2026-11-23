#pragma once

// HM-01 build-time settings. Things a user changes at run time (printer
// address, time zone, orientation calibration, brightness) live in NVS, see
// settings.h; these are the defaults and the fixed tuning.

#include <stdint.h>

#define FW_VERSION "0.3.0"

constexpr const char* kUnitCode = "HM-01";

// ---- board: the build environment defines HM_BOARD_CORES3 for the
// M5Stack CoreS3; otherwise this is the AtomS3R build
#if defined(HM_BOARD_CORES3)
constexpr const char* kBoardName = "cores3";
// The 320 x 240 dashboard is drawn in strips of this many rows, from
// internal RAM (320 x 80 x 3 bytes = 77 KB with the coverage mask)
constexpr int      kStripRows = 80;
// The printer sends several messages a second and a full redraw takes
// tens of ms: content changes are drawn at most this often (a new screen,
// and an alert's flash, at once)
constexpr uint32_t kDashMinMs = 300;
// The battery gauge (AXP2101, over I2C) is read this often
constexpr uint32_t kBatteryPollMs = 5000;
// A move is judged this long after it, against touches: a finger on the
// screen shakes the device, and that must not undo the tap's dimming
constexpr uint32_t kMoveJudgeMs  = 300;
constexpr uint32_t kTouchQuietMs = 1500;   // moves this soon after a touch are the finger's
#else
constexpr const char* kBoardName = "atoms3r";
#endif

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
// default rotation, and the tilt mapping; "calibrate" on the console
// overrides them.
// - AtomS3R: the values of claude-monitor's AtomS3R build (verified there
//   on hardware in all four standing positions).
// - CoreS3: rotation 1 is upright in the normal pose, where the BMI270
//   reads gravity along +y (measured on the user's CoreS3, 2026-11-19).
//   Offset 90 maps that to quarter 0; the dashboard then turns by
//   quarters with the device.
#if defined(HM_BOARD_CORES3)
constexpr uint8_t kBootRotationOffset = 0;
constexpr float   kTiltSign           = -1.0f;
constexpr float   kTiltOffsetDeg      = 90.0f;
#else
constexpr uint8_t kBootRotationOffset = 1;
constexpr float   kTiltSign           = -1.0f;
constexpr float   kTiltOffsetDeg      = 0.0f;
#endif

// Button (AtomS3R) or touch screen (CoreS3): hold this long to open the
// setup portal
constexpr uint32_t kHoldSetupMs = 3000;
