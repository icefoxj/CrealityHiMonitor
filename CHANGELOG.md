# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/) (0.x until it has run on hardware for a while).

## [Unreleased]

### Added

- **M5Stack CoreS3 build** (`pio run -e cores3`, release image `CrealityHiMonitor-cores3-merged.bin`): a 320 × 240 dashboard in the house mech livery, with everything the printer reports about the job:
  - percent (40-segment gauge) and ETA, the largest things on screen; time left, print time, total, start time;
  - layer, Z height, file position, a layer bar;
  - nozzle and bed temperatures with their targets, speed and flow in real time with their overrides, the three fans, filament used, the chamber on printers that have a sensor;
  - the job name, material and slicer estimate, read from Creality Print's file name;
  - end screens with the end time, total time, filament and, after a failure, the error code.
- On the CoreS3, the touch screen is the button (tap, hold 3 s for setup) and the alerts play on its own speaker.
- CoreS3: a **battery** indicator in the title band (%, a bolt while charging, red at 15 % or less), from the power chip every 5 s; `status` adds `bat`, `chg` and `batmv`.
- CoreS3: a printer code shows in grey as `CODE 500` during a normal print, and in red as `ERR` only where the job stopped (error, pause). The Hi reports 500, an AI detection prompt, through whole normal prints.
- CoreS3 motion, from its accelerometer (BMI270), read 200 times a second by its own task:
  - **the dashboard turns with the device:** upside down it turns over; stood on a side it switches to a **portrait layout** (240 × 320) with the same data. It turns once the device has rested half a second in the new position, and holds while it lies flat. `autorotate on|off` (saved);
  - **moving it wakes it:** picking it up or turning it lights a dimmed or dark screen; the shake of a finger on the screen does not count;
  - **a knock on the case silences an alert.**
  - `status` adds `quarter`, `orient`, `taps` and `moves`.
- The parser reads more of the printer's websocket: start time, real-time speed and flow, overrides, fans, Z, filament used, chamber and temperature limits. See `docs/creality-websocket.md`.
- `status` reports the last frame's draw time (`render_ms`); `version` names the board.

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
