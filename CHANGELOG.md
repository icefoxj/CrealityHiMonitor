# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/) (0.x until it has run on hardware for a while).

## [Unreleased]

## [0.1.0] - 2026-11-13

First release. Compiled and unit-tested. The parser and the ETA estimator were checked against data captured from a real Creality Hi during a print. **Not yet run on an AtomS3R.**

Known limitations:

- The orientation defaults are claude-monitor's, verified there on an AtomS3R. Use `calibrate` on the console if the dial is not upright.
- The setup portal and the printer's interfaces have no authentication: keep both on a trusted network.
- Only the Creality Hi was tested.

### Added

- **Live job status on an M5Stack AtomS3R:**
  - percent done (the slicer's M73 progress, from Moonraker);
  - time left and wall-clock finish time (ETA);
  - layer, nozzle and bed temperatures, error code.
- **Data from the stock printer firmware:**
  - the Creality websocket on port 9999: snapshot + deltas, heartbeat, reconnect after 20 s of silence;
  - Moonraker on port 7125, polled every 10 s while printing.
- **ETA estimator:** the slicer's time left at the current position × the speed ratio measured between M73 steps over the last 20 minutes. Falls back to the printer's own numbers without Moonraker.
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
- **Serial console:** `status`, `printer`, `tz`, `calibrate`, `bright`, `setup`, `wifi reset`, `version`, `reboot`.
- `tools/previews.py`: renders every screen with the firmware's drawing code into `docs/img`.
- CI: unit tests and firmware build on every push; release with the binaries on a version tag.

[Unreleased]: https://github.com/icefoxj/CrealityHiMonitor/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/icefoxj/CrealityHiMonitor/releases/tag/v0.1.0
