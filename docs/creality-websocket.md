# Creality Hi: what the printer tells a LAN client

HM-01 reads the Creality websocket of the stock Creality Hi firmware (Creality OS, Klipper-based, model code `F018`). It needs no root or password. This page records what was captured from a real Hi while printing (2026-11-13), and where the meaning of each code comes from.

## 1. Creality websocket, `ws://<printer>:9999/`

This is the interface Creality Print uses in LAN mode. The server is `libhv/1.3.3`.

- Connect with no subprotocol.
- The first message is `{ "connectionCount": N }`. It is followed by one **full JSON snapshot** of the printer.
- After that come **deltas**: only the fields that changed, several messages a second while printing (temperatures, position, times).
- A message is a flat JSON object. Temperatures arrive as strings (`"220.380000"`), counters as numbers.

### Fields HM-01 reads

| Field | Example | Meaning |
|---|---|---|
| `state` | `1` | job state, see the table below |
| `deviceState` | `1` | 0 idle, non-zero busy (7 = homing) |
| `printProgress` | `16` | **position in the G-code file**, %. Not the share of the time (see below) |
| `printJobTime` | `3918` | seconds printed so far |
| `printLeftTime` | `5148` | the slicer's total estimate minus `printJobTime`: a plain countdown |
| `layer`, `TotalLayer` | `14`, `148` | current layer; 0 while heating and levelling |
| `nozzleTemp`, `targetNozzleTemp` | `"220.380000"`, `220` | °C |
| `bedTemp0`, `targetBedTemp0` | `"60.630000"`, `60` | °C |
| `err.errcode` | `0` | error code, 0 = none (2839 = filament run-out in Creality Print's table) |
| `printFileName` | `/mnt/UDISK/printer_data/gcodes/<name>.gcode` | the job's file |
| `hostname`, `model` | `Creality Hi-5EBC`, `F018` | identification |

### `state` codes

From the label table in Creality Print 6.3's device dashboard (the JS that talks to this websocket for every LAN printer, F018 included). Creality's C++ (`IsFailureState(state) == 3`) and the Home Assistant integration [ha_creality_ws](https://github.com/3dg1luk43/ha_creality_ws) agree.

| Code | Creality's label | HM-01 screen |
|---|---|---|
| 0 | Idle | READY (or the latched end of the last job) |
| 1 | Printing | PRINT (HEAT while `layer` is 0) |
| 2 | Printing Complete | DONE. The printer goes back to 0 after about 50 s, so HM-01 latches it for 30 min |
| 3 | Printing Failed | ERROR |
| 4 | Print Abort | STOP |
| 5 | Printing Paused | PAUSED |
| 6 | Printing Pausing | PAUSED |
| 7 | Printing Stopping | STOP (about 30 s, then 4) |
| 8 | Printing Restoring | PRINT |
| 9 | Printing (starting a job) | HEAT |

### Keep-alive

- The printer's own web UI sends `{"ModeCode":"heart_beat","msg":"<ISO time>"}`. The printer answers with the bare text `ok`.
- When the printer sends a `heart_beat` itself, the client answers `ok`.
- Creality Print sends nothing and reconnects after 20 s without a message.
- HM-01 does both: a heartbeat every 10 s, and a reconnect after 20 s of silence.

## 2. Moonraker, `http://<printer>:7125/` (not used)

The Hi also runs Moonraker (API 1.0.5) without authentication. Its `display_status.progress` is the M73 percentage that Creality Print writes into the G-code. It looked like the right "percent done", but over a whole print it was not (next section). HM-01 does not use Moonraker, so it also works on Creality printers where port 7125 is closed.

## 3. Which numbers give a good ETA

Logged every 30 s over one whole print (`NovoCubo2_base`, a part with wide bottom layers, Creality Print estimate 2 h 31 min 7 s = 9067 s). The printer reported "complete" at a print time of **9230 s**.

| Print time | `printProgress` (file) | layers | M73 (Moonraker) | time / estimate | true share |
|---|---|---|---|---|---|
| 4018 s | 18 % | 17/148 | 41 % | 44 % | 44 % |
| 6196 s | 50 % | 68/148 | 58 % | 68 % | 67 % |
| 8133 s | 82 % | 118/148 | 72 % | 90 % | 88 % |
| 9100 s | 99 % | 143/148 | 78 % | (countdown at 0) | 99 % |

- **`printLeftTime` is a good time left.** The countdown said the job would end at 9067 s; it ended at 9230 s (1.8 % late).
- **Print time ÷ (print time + `printLeftTime`) is a good percent.** It stayed within ~1.5 points of the real share of the time.
- **`printProgress` lags the time.** Wide bottom layers hold few bytes per minute. It catches up at the end, where it is accurate.
- **M73 was wrong.** It ran at a near-constant rate that would have put the end at about 14 000 s, and read 78 % when the job ended. An estimator built on it predicted about 11 500 s: 25 % too late.

**HM-01's estimate**, in [`firmware/lib/hm_core/src/eta.cpp`](../firmware/lib/hm_core/src/eta.cpp):

- Percent = print time ÷ estimated total, capped at 99 % until the printer says it is done.
- Time left = `printLeftTime`.
- Once the countdown reaches 0 and the job goes on, the time left is extrapolated from `printProgress`.
- While paused, the estimate is held.

`firmware/test/test_core` replays this print (173 samples) and checks the time left within 3 % of the job and the percent within 3 points.
