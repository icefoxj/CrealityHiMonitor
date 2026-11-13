# Creality Hi: what the printer tells a LAN client

HM-01 reads two interfaces of the stock Creality Hi firmware (Creality OS, Klipper-based, model code `F018`). Neither needs root or a password. This page records what was captured from a real Hi while printing (2026-11-13), and where the meaning of each code comes from.

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

## 2. Moonraker, `http://<printer>:7125/`

The Hi runs Moonraker (API 1.0.5) without authentication. HM-01 polls one query:

```
GET /printer/objects/query?display_status=progress&print_stats=state,print_duration
```

```json
{"result": {"status": {
  "display_status": {"progress": 0.39},
  "print_stats": {"state": "printing", "print_duration": 3899.26}}}}
```

- `display_status.progress` is the slicer's **M73** percentage. Creality Print writes M73 into the G-code, and Klipper keeps the last value while printing.
- M73 is the share of the slicer's estimated time that the current file position represents. It is the meaningful "percent done".

## 3. Why HM-01 computes its own ETA

Measured on one print (`NovoCubo2_base`, slicer estimate 2 h 31 min, a part with wide bottom layers), at the same moment:

| Source | Value |
|---|---|
| `printProgress` (file position) | 16 % |
| layers | 14 / 148 = 9 % |
| M73 (slicer, time-based) | 39 % |
| `printJobTime / (printJobTime + printLeftTime)` | 43 % |

- **File position lags the time.** Wide bottom layers hold few bytes per minute of printing.
- **`printLeftTime` is only a countdown.** It is `T - printJobTime`, where T is the slicer's total.
- **This printer ran slower than the slicer assumed.** Over 23 minutes M73 advanced 10 %, which the slicer had budgeted at about 15 minutes: a speed ratio of about 1.4. At that pace `printLeftTime` reaches 0:00 long before the end.

**HM-01's estimate:**

1. **Percent** = M73.
2. **Time left** = T × (1 − p) × r, where:
   - T × (1 − p) is the slicer's time left at the current position.
   - r is the measured speed ratio: real seconds per slicer second between M73 steps, over the last 20 min of printing.
3. Below 5 min of history, r = 1.
4. While paused, the estimate is held.
5. **Without Moonraker**, HM-01 shows the printer's own `printProgress` and `printLeftTime`.

The code is in [`firmware/lib/hm_core/src/eta.cpp`](../firmware/lib/hm_core/src/eta.cpp). The tests in `firmware/test/test_core` replay a slower-than-sliced print.
