#pragma once

// HM-01 on a 320 x 240 panel (M5Stack CoreS3): the same data and the same
// mech livery as the AtomS3R HUD, laid out as a fixed dashboard with room
// for every detail the printer sends:
//
//   +-------------------------------------------------------------+
//   | ////PRINT//////        ERR 500  HI-5EBC  (wifi)  18:52      |  title band, clock
//   |  .--------.   +-----------------------------------------+   |
//   | /  40 seg  \  | ETA  19:31                   LEFT 0:37  |   |  the two largest:
//   | |   85 %   |  | TIME 4:03   TOTAL 4:40   START 13:48    |   |  percent and ETA
//   | \  L37/50  /  | LAYER 37/50  Z 7.56     FILE 85%        |   |
//   |  '--------'   | [layer bar................]             |   |
//   | [NOZZLE 220/220] [BED 60/60] [SPEED 500 100%] [FLOW 4.2]   |  telemetry tiles
//   | FAN 100%  AUX 0%  CASE 0%  FIL 33.3 M                      |
//   | WO01 PANEL 8X8  PLA  EST 4:41                       HM-01  |  the job's file
//   +-------------------------------------------------------------+
//
// Stood on its side, the CoreS3 shows a portrait layout (240 x 320) with
// the same data: the gauge with ETA, LEFT, TOTAL, START and FILE beside
// it, a panel with time, layer, Z, fans, filament and the slicer's
// estimate, tiles two by two, the job's line at the bottom.
//
// The firmware turns the display by quarters; within a frame nothing
// turns, so it is drawn in screen pixels with the identity transform.

#include "gfx.h"
#include "screens.h"

namespace hm {

constexpr int kDashW = 320;   // landscape; portrait is kDashH wide and kDashW tall
constexpr int kDashH = 240;
constexpr int kWideH = 170;   // the CardputerZero's 320 x 170 panel

enum class DashLayout : uint8_t {
    Landscape,   // CoreS3, 320 x 240
    Portrait,    // CoreS3 stood on a side, 240 x 320
    Wide,        // CardputerZero, 320 x 170: two pages (View::page), the
                 // gauge and the ETA on both
};

// Width and height of a layout's frame
int dashWidth(DashLayout l);
int dashHeight(DashLayout l);

// Draws the frame into a canvas as wide as the layout. The canvas holds
// rows top .. top + height - 1 of the frame: the whole frame, or a strip
// of it, so the firmware can draw it in a few strips from internal RAM.
// Shapes outside the strip cost almost nothing.
void drawDash(Canvas& cv, const View& v, int top, DashLayout layout);

// The CoreS3 calls: landscape, or portrait
inline void drawDash(Canvas& cv, const View& v, int top = 0, bool portrait = false){
    drawDash(cv, v, top, portrait ? DashLayout::Portrait : DashLayout::Landscape);
}

}  // namespace hm
