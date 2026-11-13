# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/) (0.x until it has run on hardware for a while).

## [Unreleased]

First run against the printer on hardware: from the setup portal on a phone to a steady live link.

### Changed

- **The finish time (ETA) gets the big line** on the printing and paused screens. The time left moves to the small line under it.
- **Wi-Fi modem sleep is off.** HM-01 runs on mains power, and dozing slowed both the printer search and the websocket.

### Fixed

- **The setup portal refused connections from phones.** Its web server ran once per ~33 ms frame, and a phone opens several connections at once. It now runs all through the wait between frames.
- **The printer search missed the printer:**
  - The search allowed 150 ms per host. First contact with a host waits for ARP, which on Wi-Fi can take several hundred ms.
  - It now waits 1.5 s per host, with 6 hosts tried at once.
- **The printer link dropped right after connecting:**
  - Elapsed times measured against a timestamp from the same pass of the network loop wrapped around to ~49 days.
  - So a printer just found was declared gone and searched for again, and every message counted as 20 s of silence.
  - Also, a failed Wi-Fi join closed the setup portal at once.

## [0.1.0] - 2026-11-13

First release. Unit-tested; the parser and the ETA are checked against a real print on a Creality Hi. On an AtomS3R it boots, opens the setup portal and reads the IMU. The live printer link and the alerts are not yet checked on hardware.

Known limitations:

- The orientation defaults are claude-monitor's. They gave the same tilt angle on the AtomS3R tested. Use `calibrate` on the console if the dial is not upright.
- The setup portal and the printer's interfaces have no authentication: keep both on a trusted network.
- Only the Creality Hi was tested.

### Added

- **Live job status on an M5Stack AtomS3R:**
  - percent done: the share of Creality's time estimate already printed;
  - time left and wall-clock finish time (ETA);
  - layer, nozzle and bed temperatures, error code.
- **Data from the stock printer firmware:** the Creality websocket on port 9999 (snapshot + deltas, heartbeat, reconnect after 20 s of silence). No root, no Moonraker.
- **ETA from a measured print:**
  - The printer's countdown is the time left: within 1.8 % on a 2.5 h print.
  - Past the estimate, the tail comes from the file position.
  - The file position and Moonraker's M73 were measured too, and rejected.
- **Alerts:**
  - Job done: the screen flashes at full brightness for 90 s and a chime plays three times.
  - Error, or a pause with an error code (filament run-out): an alarm every 30 s for up to 5 min.
  - A click silences them.
  - Sound on an Atomic Echo Base (detected at boot) or a piezo buzzer on a GPIO (`buzzer <pin>`).
- **Screens:**
  - boot card;
  - setup portal, link (Wi-Fi, printer search, connecting), offline;
  - READY (clock, temperatures);
  - PRINT / HEAT, PAUSED;
  - DONE / STOP / ERROR, latched for 30 minutes with the total time and the end time.
- **Mech HUD:** a fixed bezel with corner markings, and a round dial with a 40-segment progress ring and a title band, drawn in the house mech stroke font.
- **Smooth auto-rotation:** accelerometer, low-pass filtered, as in claude-monitor's AtomS3R build. The dial is redrawn as vectors at any angle.
- **Setup:**
  - Wi-Fi portal (`HM-01-xxxx`, password shown on screen) with printer address and time zone;
  - automatic search for the printer on the /24, repeated if it moves;
  - button: click for brightness, hold 3 s for the portal.
- **Serial console:** `status`, `printer`, `tz`, `calibrate`, `bright`, `sound`, `volume`, `buzzer`, `beep`, `setup`, `wifi reset`, `version`, `reboot`.
- `tools/previews.py`: renders every screen with the firmware's drawing code into `docs/img`.
- CI: unit tests and firmware build on every push; release with the binaries on a version tag.

[Unreleased]: https://github.com/icefoxj/CrealityHiMonitor/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/icefoxj/CrealityHiMonitor/releases/tag/v0.1.0
