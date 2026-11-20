# CLAUDE.md

Guidance for Claude Code when working in this repository.

## The project

CrealityHiMonitor (unit code **HM-01**) is firmware that shows the live status of a print on a **Creality Hi** on the same network: percent done, time left, finish time (ETA), layer, temperatures. Two builds of the same code:

- **M5Stack AtomS3R** (`pio run -e atoms3r`): the 128 × 128 display is a mech-style HUD that turns smoothly with the cube.
- **M5Stack CoreS3** (`pio run -e cores3`, build flag `HM_BOARD_CORES3`): a fixed 320 × 240 dashboard with every detail the printer sends (`dash.cpp`).

Public repository: https://github.com/icefoxj/CrealityHiMonitor (branch `main`). The user asked for it to be public (2026-11-13).

## Language

**All project material is in English:** docs, code, comments, identifiers, file names, commit messages and the text on the screens. The user talks in Brazilian Portuguese; answer in Portuguese, but keep everything committed in English.

## User requirements (do not break without asking)

From the original request (2026-11-13):

1. **AtomS3R, real-time data** of the print running on the Creality Hi, over the local network.
2. **A mech-graphics image for the display, like the other projects:**
   - The house livery: Tab5 stand, OutageAlarm, FilamentDryer, CrealityHiDrawer.
   - The boot card is `docs/img/splash.png`; every screen shares the look.
3. **ETA and percent** are the essential data. Keep them the largest things on the printing screen.
4. **Real-time orientation with smooth rotation, following claude-monitor's pattern:**
   - accelerometer → low-pass filtered angle → redraw at ≥ 1° change, hold when flat;
   - see `e:\work\claude-monitor`, `components/monitor-core/src/tilt.cpp`.
5. **Public GitHub repo** with README.md and CLAUDE.md.
6. **An alert when the print ends**, with sound if the hardware can make one (2026-11-13).
   - The AtomS3R has no speaker, so the alert is always visual.
   - Sound plays on an Atomic Echo Base (auto-detected) or a GPIO buzzer.
7. **Standalone over Wi-Fi** (2026-11-13): HM-01 runs on any USB power supply with no PC attached.
   - It joins the Wi-Fi itself and reads the Hi's port 9999 directly. Nothing goes through a PC.
   - USB is only for power, flashing and the optional console. Never make the firmware wait for a USB host.
8. **A CoreS3 version** (2026-11-19): the same mech look as the other projects, with **as much information about the current print as possible** on the screen.
   - Percent and ETA stay the largest things on the printing dashboard.
   - It plays the alerts on its own speaker; the touch screen is the button.
   - A battery indicator in the title band (user, 2026-11-19).
   - With its IMU (user asked for all four, 2026-11-19): turn over when upside down; a portrait layout when stood on a side; wake on a move; a knock on the case silences an alert.

## Current phase: v0.2.0, two boards on hardware

- 36 host tests pass. The estimator test replays a real 2.5 h print from the user's Hi.
- The screens are rendered on the PC with the same code.
- **Flashed on the user's AtomS3R (2026-11-13).** That unit normally runs claude-monitor: ask before flashing it.
  - Seen working: boot, setup portal, console, IMU. The angle matched claude-monitor's in the same position.
  - **Live link seen working (2026-11-13):**
    - The portal worked from a phone, after the portal, search and link fixes now in v0.1.0.
    - HM-01 joined the user's Wi-Fi, the search found the Hi, and the data held steady mid-print.
  - **Alert flash seen working (2026-11-13)** with the console's `alert done`, mid-print.
  - Not yet checked:
    - an alert raised by a real job end;
    - the alert sounds (no Echo Base or buzzer fitted);
    - frame time while turning;
    - the colours.
- **v0.1.0 released (2026-11-13).**
- **CoreS3 build flashed on the user's CoreS3 (2026-11-19).** It was a test board; the user said it may be wiped.
  - Seen working: boot into the setup portal, PSRAM (8 MB), the speaker output (`beep`, `alert done` from the console), 115–160 ms per full frame.
  - **Live dashboard seen working (2026-11-19):** set up from a phone, it joined the Wi-Fi, found the Hi (at a new DHCP address) and showed the job; the user saw the data.
  - The battery reads (54 %, charging, 3853 mV): this CoreS3 has its battery fitted. The user asked for the battery indicator.
  - The user heard the alert chime on the CoreS3 speaker (2026-11-19).
  - Not yet checked: an alert from a real job end, confirmed on the console (the job ended before a watcher attached).
  - **IMU features seen working (user, 2026-11-20): "perfect"** for turning (portrait, upside down), wake on a move, knock to silence.
    - The normal pose was measured: gravity along +y, so `calibrate offset=90` gives quarter 0 / rotation 1. It is the compiled default and is also saved in that unit's NVS.
    - The direction of the turn (sign −1) follows from M5GFX's `Panel_Device::convertRawXY`: each +1 of rotation turns the image 90° clockwise. With the measured axes (z out of the screen, gravity along +y when upright), a clockwise turn of the device gives quarter 3 → rotation 0, which turns the image back. Confirmed on the panel.
- **v0.2.0 released (2026-11-20):** the CoreS3 build, and the AtomS3R's printer code as a grey notice (LEFT stays).

## Commands

```bash
# Firmware (PlatformIO at ~/.platformio/penv/Scripts/pio.exe on the user's PC)
cd firmware
pio test -e native            # host tests of lib/hm_core (MinGW gcc 6.3 here: keep hm_core C++14)
pio run -e atoms3r            # build (~1 min); -e cores3 for the CoreS3
pio run -e atoms3r -t upload --upload-port COMx   # flash over USB-C: always name the port
pio device monitor            # console, 115200; type "help"

# Screens -> docs/img (g++ + Pillow; Pillow is in the FilamentRecycler venv)
/e/GitHub/FilamentRecycler/.venv/Scripts/python tools/previews.py
```

- After any change to `screens.*`, `dash.*`, `gfx.*` or `mechfont.*`, run `tools/previews.py` and commit the regenerated `docs/img/` with the code.
- Look at the result before committing: read `docs/img/screens.png`, `docs/img/hero.png`, `docs/img/cores3-screens.png`, `docs/img/cores3-portrait-screens.png` and `docs/img/cores3.png`. The Read tool may serve a cached image for a path it read before: copy the file to a new name to check it.

## Layout

```
firmware/src/            hardware glue (Arduino): main.cpp (display, IMU, button / touch, backlight, console,
                         alerts), net.cpp (WiFiManager portal, printer search task, websocket), sound.cpp
                         (CoreS3 speaker; AtomS3R: Echo Base probe before M5.begin, GPIO buzzer; melodies),
                         motion.cpp (CoreS3: the IMU task, 200 Hz on core 0, the only reader of M5.Imu),
                         settings.cpp (NVS "hm01"), config.h (board constants under HM_BOARD_CORES3)
firmware/lib/hm_core/    plain C++ + ArduinoJson, no Arduino headers, unit-tested on the host:
                         printer (parsing, state codes), eta (estimator), job (state -> screen, end latch,
                         job details, file-name split), alert (when to flash / which melody),
                         screens (bezel + HUD, View/Details), dash (CoreS3 dashboard, landscape + portrait),
                         orient (quarter turns, knock / move detection), gfx (AA vector raster), mechfont,
                         tilt, console (parser)
firmware/test/test_core/ Unity tests (fixtures are real captured messages)
tools/                   preview.cpp (renders every screen to PPM), previews.py (PNGs, GIF, hero mock-up)
docs/                    creality-websocket.md (protocol notes), img/ (generated)
```

## Printer facts (measured on the user's Hi, 2026-11-13)

- **Creality websocket, `ws://<ip>:9999/`:**
  - libhv server, no subprotocol.
  - Sends `{"connectionCount":N}`, then a full snapshot, then deltas.
  - Temperatures are strings.
- **State codes** come from Creality Print's own dashboard code:
  - 0 idle, 1 printing, 2 complete (only ~50 s, then 0), 3 failed, 4 aborted;
  - 5 paused, 6 pausing, 7 stopping, 8 restoring, 9 starting.
  - `layer == 0` with state 1 = heating/levelling.
- **`printProgress` is the file position, not time.** `printLeftTime` = slicer total − `printJobTime`: a countdown that ignores how fast the printer really goes.
- **The ETA comes from the countdown, measured over a whole 2.5 h print:**
  - **`printLeftTime` was right within 1.8 %.**
  - **Moonraker's M73 was useless:** 78 % when the job ended.
  - HM-01 therefore reads only port 9999. Do not bring M73 back without new measurements.
  - Read `eta.h` and `docs/creality-websocket.md` §3 before changing the estimator.
- **Keep-alive:** the printer's web UI sends `{"ModeCode":"heart_beat",...}` and gets `ok`; Creality Print reconnects after 20 s of silence. HM-01 does both.
- The user's printer is on the same LAN as the development PC. Probe ports 9999/7125 on the LAN neighbours to find it.
  - Reading its APIs is fine.
  - **Never send it commands** (no G-code, no `set` methods): a job may be running.

## Design rules

- **Everything that must stay upright lives inside the HUD circle:**
  - Content within radius ~52; the ring is 53.5–59, the face disc 62.5.
  - The bezel's corner markings are fixed to the panel. Keep them outside radius ~64 and free of text, since they turn with the cube.
- **Draw with vectors, no bitmaps.** The HUD is redrawn at the current angle each frame. New shapes go through `Canvas` (half-planes + circles).
- **Shapes of one colour go in one `begin()/end()` layer.** Coverage adds up there, so joints have no seams.
- **The mech style:**
  - straight lines, 45° cuts;
  - the title band with the label knocked out and hazard stripes that fit;
  - brackets, speed dashes, reticles;
  - the stroke font in `mechfont.cpp`: glyphs copied from FilamentDryer `cad/mech.py`; `%` and `°` are new.
  - The font has no lower case: screen strings are upper case.
- **House colours:** graphite and grey with yellow #FFCC00 (`pal::yellow`; no orange, user 2026-11-18). Text on yellow is dark (`pal::face`).
- **Colours by state:** yellow printing (and the splash/ready screens), cyan paused, green done, red stopped/error, cyan network screens. Paused is cyan, not amber, so it cannot be mistaken for printing.
- **Render time:**
  - About 0.4 ms per frame on the PC with SSE. Expected ~10–20 ms on the ESP32-S3, plus ~6.5 ms to push 32 KB.
  - Keep it there. Check with a host benchmark if you add heavy shapes.
  - Each row is clipped to the shape's span; fully covered spans are a memset.
- **The CoreS3 dashboard (`dash.cpp`):**
  - Within a frame nothing turns: screen pixels, identity transform. The display turns by quarters (`setRotation`) with the device. The HUD-circle rule above is for the AtomS3R.
  - Two layouts, one `Geo` each (`kLand`, `kPort`); landscape and portrait previews are both in `docs/img`.
  - Landscape: title band + clock (0–26), gauge left and panel right (30–164), four tiles (168–212), two text lines (225, 237).
  - Portrait (240 × 320): gauge + ETA block (30–149), panel (152–226), tiles 2 × 2 (230–306), the job line (317). The printer's code moves from the header to the job line.
  - Percent (cap 36 landscape, 30 portrait) and ETA (cap 28) are the largest text; keep them so.
  - Smallest text: cap 6 (tile labels). The panel is ~0.13 mm per pixel. The font has no `,` or `?`: hints avoid them.
  - It is drawn in strips of 80 rows from internal RAM (`drawDash(cv, v, top, portrait)`): three in landscape, four in portrait, 115–180 ms per frame on the device (4 ms on the PC). Strips match the full frame within one colour level (a test checks both layouts).
  - Content changes are drawn at most every `kDashMinMs` (300 ms); a new screen and an alert's flash at once.
  - `Details` values are rounded to what is shown, so a redraw happens only when a shown digit changes. The AtomS3R clears `View::d`.

## Keep in sync

- **Version** (one for the project): `FW_VERSION` in `firmware/src/config.h`, `firmware/lib/hm_core/library.json`, the README status line, `CHANGELOG.md`, the splash version in `tools/preview.cpp`.
- **Console commands:** `console.h` (comment + parser), `sendHelp()` in `main.cpp`, the README table, tests.
- **Protocol knowledge:** `printer.cpp` ↔ `docs/creality-websocket.md` ↔ the README table of ports ↔ test fixtures.
- **Orientation defaults:**
  - `config.h` (`kBootRotationOffset`, `kTiltSign`, `kTiltOffsetDeg`, per board) and the README "Orientation" section.
  - AtomS3R: they match claude-monitor's AtomS3R build. CoreS3: offset 90 measured on the user's unit. Change them only after measuring on hardware.
- **Screens:** `screens.cpp` and `dash.cpp` ↔ `docs/img/*` (regenerate) ↔ the README tables (incl. the CoreS3 dashboard table).
- **Boards:** `platformio.ini` envs ↔ `firmware.yml` (build + merge loop) ↔ the `release.yml` downloads table ↔ the README Install section.
- Record user-visible changes in `CHANGELOG.md` under `[Unreleased]`.

## Releases

- Annotated tags `vMAJOR.MINOR.PATCH` on `main`. Stay on 0.x until it has run on hardware.
- **Before tagging:**
  - CI green;
  - `CHANGELOG.md` section written: move `[Unreleased]`, update the compare links;
  - version bumped everywhere above.
- Pushing the tag runs `release.yml`:
  - It reruns the firmware workflow.
  - It publishes the release with `CrealityHiMonitor-<board>-merged.bin` (flash at 0x0) and `-app.bin` (0x10000), for `atoms3r` and `cores3`. The job fails if no `.bin` is found.
  - The notes are the tag's CHANGELOG section; the job fails if that section is missing.
- Commit and push only when the user asks; push straight to `main`.

## Gotchas

- **M5GFX byte order:** `pushImage(..., const uint16_t*)` treats the data as *byte-swapped* RGB565. Our buffer is native RGB565, so `main.cpp` passes it as `const lgfx::rgb565_t*`. Keep that cast.
- **`int32_t` vs `int`:** they are the same type on ESP32 Arduino 2.x and on MinGW. Do not overload on both (see the `readInt` template in `printer.cpp`).
- **WiFiManager runs non-blocking** (`process()` in `net::loop()`). Printer and time zone are on the portal's separate "Setup" page.
- **The portal's web server takes one step per call and queues only 4 connections.** `net::service()` runs it all through the wait between frames. Once per frame was too slow: phones got their connections refused.
- **Elapsed time in `net::loop()`: always `since(t)`, never a `now` read at the top.** `startWs()`, `startPortal()` and the websocket callbacks set stamps newer than that `now`, and unsigned `now - t` wraps to ~49 days.
- **Printer search timing:**
  - First contact with a Wi-Fi host waits for ARP, and the access point holds broadcasts until the next DTIM beacon. That can take several hundred ms; 150 ms missed the Hi every time.
  - Keep the per-host timeout ≥ 1 s, with parallel probes. Stay ≤ 6 in flight: lwIP's ARP table has 10 entries.
- **Blocking work runs in a task on core 0:** the printer search. Everything else runs in the Arduino loop on core 1. The websocket callbacks run inside `ws.loop()`, in the main loop.
- **Opening the USB serial port can reset the board.** pyserial toggles DTR/RTS, which resets an ESP32-S3 on its USB Serial/JTAG port. Open it with `dtr = rts = False` set before `open()`; `pio device monitor` is fine.
- **Sound:** the Echo Base must be configured before `M5.begin()` (`sound::prepare`). The Atomic SPK base is not supported: with it, M5Unified switches the IMU off.
- **Reading back the whole 8 MB flash corrupts over USB.** Read it in 64 KB chunks with retries.
- **Serial ports on the user's PC:** the claude-monitor daemon holds the Tab5's port (COM7) all the time. Always pass `--upload-port`; the CoreS3 was COM4, the AtomS3R COM3.
- **Writing to a board whose firmware does not read its USB serial blocks pyserial forever.** Set `write_timeout` or listen first.
- **CoreS3:**
  - Do not probe the Echo Base pins (G38/G39) there; `sound::prepare` skips it under `HM_BOARD_CORES3`.
  - The touch hold uses `M5.Touch.setHoldThresh(kHoldSetupMs)` and `wasHold()`; there is no BtnA.
  - Download mode: hold the side reset button ~3 s until the green LED.
- **The Hi sends `err.errcode` 500 during normal prints** (seen 2026-11-19); Creality's table calls 500 an AI detection prompt. The user asked why the screen showed "error 500". Both boards show codes in grey (`CODE 500`) while printing and in red (`ERR`) only on ERROR / PAUSED (user, 2026-11-20). On the AtomS3R the notice sits under the title band and LEFT stays.
- **`printStartTime` must be read as an integer** (`readEpoch`): through a float it loses ~2 minutes.
- **Never mark "now" as `now | 1` to keep 0 for "unset".** When `now` is even, the stamp is 1 ms in the future, and `now - stamp` wraps to ~49 days. A unit test caught it in the move detector (2026-11-19). Use a separate bool.
- **CoreS3 IMU:**
  - Only the IMU task reads `M5.Imu`. The internal I2C bus (touch, power chip, IMU) is locked per transaction in M5GFX's i2c layer, so the task on core 0 and the main loop can share it.
  - M5Unified does not remap the CoreS3's accel and gyro axes. Its z points out of the screen.
  - A finger tapping the screen shakes the device: moves within `kTouchQuietMs` of a touch are ignored, judged `kMoveJudgeMs` after they happen.
- **The Bash tool mangles quotes and `\n`** in heredocs and `sed`. Write patch scripts to a file and run them.
