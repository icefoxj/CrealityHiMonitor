# CrealityHiMonitor — HM-01

A live print monitor for the **Creality Hi**, on an **M5Stack AtomS3R**. The 24 mm cube sits next to the printer and shows the job's **percent done**, the **time left** and the **finish time (ETA)**. The display is a mech-style HUD that **turns with the cube**: stand it on any side or tilt it, and the dial rotates smoothly to stay upright.

![HM-01 on an AtomS3R, upright and tilted](docs/img/hero.png)

> **Status: v0.1.0, first release.**
> - Firmware compiles; its logic passes 24 unit tests on the host.
> - The parser was checked against messages captured from a real Creality Hi during a print, and the ETA against that print's progress.
> - Every image here is rendered by the firmware's own drawing code.
> - **Not yet run on an AtomS3R.** The orientation defaults come from claude-monitor's AtomS3R build, where they were verified on hardware. If the dial turns the wrong way on yours, see [Orientation](#orientation).

## What it shows

| Printing | Paused | Done |
|---|---|---|
| ![Printing](docs/img/screen-printing.png) | ![Paused](docs/img/screen-paused.png) | ![Done](docs/img/screen-done.png) |

**Printing screen:**

- **48 %**: progress through the job. This is the slicer's own time-based progress (M73), not the position in the file.
- **LEFT 1:48**: hours:minutes still to print. The estimate corrects for how fast *your* printer is really going, see [How the ETA is computed](#how-the-eta-is-computed).
- **ETA 18:47**: the wall-clock time the job should end.
- **Ring**: 40 segments, one per 2.5 %. The next segment blinks while printing.
- **L36/148**: current layer / total layers.
- **Title band**: PRINT, HEAT (heating and levelling before the first layer), PAUSED, DONE, STOP or ERROR, in its own colour.

When the job ends, the DONE / STOP / ERROR screen stays for 30 minutes with the total print time and the time it ended. The printer itself only says "complete" for about a minute.

| Idle | Boot | Setup |
|---|---|---|
| ![Ready](docs/img/screen-idle.png) | ![Splash](docs/img/screen-splash.png) | ![Setup](docs/img/screen-setup.png) |

- **READY** (no job): a clock, nozzle (N) and bed (B) temperatures, and the printer's name.
- **Boot**: the HM-01 title card.
- **Setup**: the Wi-Fi setup portal is open, with the network name and password to use.
- **Link / Offline**: Wi-Fi, the search for the printer, the connection to it.

All screens are on [one sheet](docs/img/screens.png). The boot image at panel size (128 × 128) is [`docs/img/splash.png`](docs/img/splash.png).

## Smooth auto-rotation

![The HUD following a quarter turn](docs/img/rotation.gif)

HM-01 follows claude-monitor's AtomS3R pattern:

- **Gravity, not the gyro.** The accelerometer gives the direction of gravity.
- **Smoothed.** A low-pass filter (~130 ms at 30 fps) turns the HUD towards it.
- **Redrawn only when needed.** A new frame is drawn once the angle has moved a degree.
- **Held when flat.** Lying flat on the desk, the cube keeps the last angle.

**Why the HUD stays sharp at any angle:**

- Everything that matters lives inside the round dial. Only the bezel's corner markings are fixed to the panel.
- The dial is drawn as vectors: anti-aliased polygons, arcs and the house mech stroke font.
- Turning it means turning the vertices and drawing again, not rotating a bitmap. Text stays sharp at 37° as well as at 90°.

## Mech look

Same house style as the printed parts of the other projects (Tab5 stand, OutageAlarm, FilamentDryer, CrealityHiDrawer):

- straight lines and 45° cuts;
- a title band with the label knocked out and hazard stripes;
- corner brackets, speed dashes, reticles;
- the chamfered stroke font, here drawn on screen instead of inlaid in plastic.

Orange livery on graphite, with a colour per state: orange printing, amber paused, green done, red stopped or failed, cyan for network screens.

## Hardware

| Part | Notes |
|---|---|
| **M5Stack AtomS3R** (ESP32-S3, 0.85" 128 × 128 IPS, BMI270 IMU, button under the screen) | the whole device |
| USB-C cable and any 5 V USB supply | power |

The printer and the AtomS3R must be on the same network. HM-01 talks to the printer's stock interfaces: no root, no printer settings to change.

| Port | Interface | Used for |
|---|---|---|
| 9999 | Creality websocket (what Creality Print uses) | state, times, layers, temperatures, errors |
| 7125 | Moonraker (Klipper API) | the slicer's M73 progress, for the percent and the ETA |

If Moonraker does not answer, HM-01 falls back to the printer's own percent and time left. Protocol notes: [docs/creality-websocket.md](docs/creality-websocket.md).

## Install

### From a release

Download `CrealityHiMonitor-atoms3r-merged.bin` from the [latest release](https://github.com/icefoxj/CrealityHiMonitor/releases) and write it at `0x0`:

```bash
pip install esptool
esptool.py --chip esp32s3 write_flash 0x0 CrealityHiMonitor-atoms3r-merged.bin
```

If the port is not found, hold the AtomS3R's reset button until the green LED lights, to enter download mode. Then run the command again.

### From source

```bash
cd firmware
pio run -e atoms3r -t upload     # PlatformIO
pio device monitor               # serial console, 115200
```

## Setup

1. **Power it.** On first boot the screen shows **SETUP**, a network name `HM-01-xxxx` and its password.
2. **Join that network** with a phone. The setup page opens by itself; if it does not, browse to `http://192.168.4.1`.
3. **Choose your Wi-Fi** and save. HM-01 joins it and closes the portal.
4. **Wait for the printer.** HM-01 searches its subnet (the /24) for a Creality printer on port 9999 and remembers the address. If the printer later moves to another address, it searches again.

On the portal's **Setup** page:

- **Printer address**: set it there to skip the search (blank = search).
- **Time zone**: a POSIX TZ string for the clock and the ETA. The default is Lisbon/London, `WET0WEST,M3.5.0/1,M10.5.0`. Examples: `CET-1CEST,M3.5.0,M10.5.0/3` (central Europe), `<-03>3` (São Paulo), `EST5EDT,M3.2.0,M11.1.0` (New York).

**Button** (the screen itself):

| Press | Effect |
|---|---|
| Click | Brightness: bright → dim → off → bright. On a DONE / STOP / ERROR screen, the first click dismisses it |
| Hold 3 s | Open the setup portal again (Wi-Fi, printer, time zone) |

The READY screen dims by itself after 15 minutes.

## Serial console

Any serial terminal at 115200 baud on the USB port (`pio device monitor`):

| Command | Effect |
|---|---|
| `status` | one line with the link, the printer's raw numbers, the estimate, the tilt angle and the IMU reading |
| `printer <ip>` / `printer auto` | set the printer address / search again |
| `tz <POSIX TZ>` | time zone |
| `calibrate rot=<0-3> sign=<1\|-1> offset=<deg>` / `calibrate reset` | orientation, saved |
| `bright <0-255>` | backlight level, saved |
| `setup` | open the setup portal |
| `wifi reset` | forget the Wi-Fi network and reboot into setup |
| `version`, `reboot`, `help` | |

## Orientation

The defaults are the ones claude-monitor verified on an AtomS3R, with the same M5Unified IMU mapping:

- display rotation = the board default + 1;
- tilt sign −1, offset 0°.

If the dial is not upright on yours, calibrate it from the console:

1. **Fix the base orientation.** Stand the cube with the USB port down and run `calibrate rot=N` (N = 0, 1, 2 or 3). Pick the N that shows the HUD upright.
2. **Fix the direction.** Turn the cube a quarter. If the HUD turns away from upright instead of staying upright, run `calibrate sign=1`.
3. **Fix a constant tilt.** If the HUD is upright but tilted by a fixed angle, set `calibrate offset=<deg>`.

`status` reports `angle=`, the angle in use, and the raw `ax/ay/az`. `calibrate reset` returns to the defaults.

## How the ETA is computed

The printer offers two numbers, and on a real print both were misleading:

- **`printProgress`** is the position in the G-code file. Wide bottom layers hold few bytes per minute. On the measured print it said 16 % at 43 % of the time.
- **`printLeftTime`** is the slicer's total estimate minus the time printed: a plain countdown. The Hi printed about 1.4 × slower than Creality Print had estimated, so this would reach 0:00 with a third of the print still to go.

**HM-01's estimate:**

1. **Percent** = the slicer's M73 progress, read from Moonraker.
2. **Time left** = the slicer's time left at this point, multiplied by the **speed ratio** measured on your printer. The ratio is real print seconds per slicer second between M73 steps, over the last 20 minutes.
3. **ETA** = now + time left.
4. **While paused**, the estimate is held.

Details and the captured numbers: [docs/creality-websocket.md](docs/creality-websocket.md).

## Development

```bash
cd firmware
pio test -e native          # 24 unit tests on the host (needs gcc)
pio run -e atoms3r          # firmware

# Screens -> docs/img (needs g++ and Pillow). Same drawing code as the device.
python tools/previews.py
```

```
firmware/
  src/                 hardware glue: main.cpp (display, IMU, button, console), net.cpp (Wi-Fi portal,
                       printer search, websocket, Moonraker poll), settings.cpp (NVS), config.h
  lib/hm_core/src/     plain C++, tested on the host:
    printer.*          Creality websocket + Moonraker parsing, state codes
    eta.*              percent and time-left estimate
    job.*              printer state -> screen, end-of-job latch
    screens.*          the bezel and the HUD screens
    gfx.*              anti-aliased vector drawing into an RGB565 buffer
    mechfont.*         the mech stroke font
    tilt.*             accelerometer -> smoothed HUD angle (from claude-monitor)
    console.*          serial command parser
  test/test_core/      Unity tests
tools/                 preview.cpp (host renderer), previews.py (docs images)
docs/                  protocol notes, images
```

CI (`.github/workflows/firmware.yml`) runs the tests and builds the firmware on every push. A `vX.Y.Z` tag publishes a release with the binaries (`release.yml`).

## Known limitations

- The setup portal and the printer interfaces have no authentication: keep HM-01 and the printer on a trusted network.
- The printer search covers the /24 around the AtomS3R's own address. On other networks, set the address.
- Only the Creality Hi has been tested. Other Creality printers with the same websocket (K1, K1C, K1 Max, K2) should work. Moonraker on port 7125 is usually closed on them without root, and HM-01 then uses the printer's own numbers.

## License

[MIT](LICENSE). The tilt filter and the orientation defaults come from claude-monitor, by the same author.
