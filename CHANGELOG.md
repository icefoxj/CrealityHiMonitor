# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/) (0.x until it has run on hardware for a while).

## [Unreleased]

### Changed

- **House colour: yellow instead of orange.** The bezel, the title bands and the printing, splash and ready screens are yellow (#FFCC00) on graphite, with dark text on the yellow bands. The paused screen is now cyan (it was amber, too close to the new yellow): yellow printing, cyan paused, green done, red stopped or failed, cyan network screens.

## [0.1.0] - 2026-11-13

First release, run on hardware next to a Creality Hi:

- Set up from a phone, HM-01 joins the Wi-Fi, finds the printer by itself and holds a steady live link mid-print. It needs only USB power, no PC.
- The parser and the ETA are checked against a real 2.5 h print on the Hi, replayed in 29 unit tests.
- The alert flash was seen on the AtomS3R with the console's `alert done`.

Known limitations:

- **Not yet seen on hardware:** an alert raised by a real job end, and the alert sounds (Echo Base, buzzer).
- The orientation defaults are claude-monitor's. They gave the same tilt angle on the AtomS3R tested. Use `calibrate` on the console if the dial is not upright.
- The setup portal and the printer's interfaces have no authentication: keep both on a trusted network.
- Only the Creality Hi was tested. Other Creality printers with the same websocket (K1, K1C, K1 Max, K2) should work.
- Wi-Fi: 2.4 GHz only (the ESP32-S3 has no 5 GHz radio).

### Added

- **Live job status on an M5Stack AtomS3R:**
  - percent done: the share of Creality's time estimate already printed;
  - wall-clock finish time (ETA) on the big line, time left under it;
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
  - `alert done|attention|off` on the console runs one at once, to check it without waiting for a job to end.
- **Screens:**
  - boot card;
  - setup portal, link (Wi-Fi, printer search, connecting), offline;
  - READY (clock, temperatures);
  - PRINT / HEAT, PAUSED;
  - DONE / STOP / ERROR, latched for 30 minutes with the total time and the end time.
- **Mech HUD:** a fixed bezel with corner markings, and a round dial with a 40-segment progress ring and a title band, drawn in the house mech stroke font.
- **Smooth auto-rotation:** accelerometer, low-pass filtered, as in claude-monitor's AtomS3R build. The dial is redrawn as vectors at any angle.
- **Setup:**
  - Wi-Fi portal (`HM-01-xxxx`, password shown on screen) with printer address and time zone, served all through the wait between frames so a phone's burst of connections gets through;
  - automatic search for the printer on the /24, 6 hosts at a time with 1.5 s each (first contact on Wi-Fi waits for ARP), repeated if it moves;
  - Wi-Fi modem sleep off: HM-01 runs on mains power, and dozing slowed the search and the link;
  - button: click for brightness, hold 3 s for the portal.
- **Serial console:** `status`, `printer`, `tz`, `calibrate`, `bright`, `sound`, `volume`, `buzzer`, `beep`, `alert`, `setup`, `wifi reset`, `version`, `reboot`.
- `tools/previews.py`: renders every screen with the firmware's drawing code into `docs/img`.
- CI: unit tests and firmware build on every push; release with the binaries on a version tag.

[Unreleased]: https://github.com/icefoxj/CrealityHiMonitor/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/icefoxj/CrealityHiMonitor/releases/tag/v0.1.0
