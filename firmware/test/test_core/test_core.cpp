// Host tests of lib/hm_core:  pio test -e native

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "alert.h"
#include "console.h"
#include "dash.h"
#include "eta.h"
#include "gfx.h"
#include "job.h"
#include "mechfont.h"
#include "orient.h"
#include "printer.h"
#include "screens.h"
#include "tilt.h"

using namespace hm;

void setUp(void){}
void tearDown(void){}

// ---------------------------------------------------------------- fixtures

// First full message from a Creality Hi (F018) while printing, captured on
// 2026-11-13 (trimmed of fields we do not read)
static const char* kSnapshot =
    "{ \"TotalLayer\": 148, \"bedTemp0\": \"60.630000\", \"cfsConnect\": 1, \"connect\": 1, "
    "\"deviceState\": 1, \"err\": { \"errcode\": 0, \"key\": 0, \"value\": \"\" }, "
    "\"hostname\": \"Creality Hi-5EBC\", \"layer\": 14, \"model\": \"F018\", "
    "\"nozzleTemp\": \"220.380000\", "
    "\"printFileName\": \"\\/mnt\\/UDISK\\/printer_data\\/gcodes\\/NovoCubo2_base.stl_PLA_2h31m7s.gcode\", "
    "\"printJobTime\": 3918, \"printLeftTime\": 5148, \"printProgress\": 16, \"state\": 1, "
    "\"targetBedTemp0\": 60, \"targetNozzleTemp\": 220, \"features\": [ \"printSettings.filamentRemaining\" ] }";

// First full message from the Hi on another job, captured on 2026-11-19
// (layer 37 of 50), trimmed to the fields HM-01 reads, the details for the
// CoreS3 dashboard included. Note errcode 500 while printing normally.
static const char* kSnapshotFull =
    "{ \"TotalLayer\": 50, \"bedTemp0\": \"60.470000\", \"targetBedTemp0\": 60, "
    "\"nozzleTemp\": \"220.150000\", \"targetNozzleTemp\": 220, "
    "\"boxTemp\": 0, \"targetBoxTemp\": 0, \"maxBoxTemp\": 0, \"maxBedTemp\": 100, \"maxNozzleTemp\": 300, "
    "\"curFeedratePct\": 100, \"curFlowratePct\": 100, \"curPosition\": \"X:69.37 Y:69.79 Z:7.56\", "
    "\"modelFanPct\": 100, \"auxiliaryFanPct\": 0, \"caseFanPct\": 0, \"deviceState\": 1, "
    "\"err\": { \"errcode\": 500, \"key\": 0, \"value\": \"\" }, \"hostname\": \"Creality Hi-5EBC\", "
    "\"layer\": 37, \"model\": \"F018\", "
    "\"printFileName\": \"\\/mnt\\/UDISK\\/printer_data\\/gcodes\\/wo01_panel_8x8.step_PLA_4h40m54s.gcode\", "
    "\"printJobTime\": 14620, \"printLeftTime\": 2234, \"printProgress\": 85, \"printStartTime\": 1795096136, "
    "\"realTimeFlow\": \"4.180000\", \"realTimeSpeed\": \"500.000000\", \"state\": 1, "
    "\"usedMaterialLength\": 33265 }";

static PrinterStatus printing(int jobS, int leftS, int progress = 20, int layer = 30){
    PrinterStatus s;
    s.seen = true;
    s.state = 1;
    s.jobS = jobS;
    s.leftS = leftS;
    s.progress = progress;
    s.layer = layer;
    s.layers = 148;
    return s;
}

// ---------------------------------------------------------------- printer

void test_snapshot_fields(void){
    PrinterStatus s;
    bool beat = true;
    TEST_ASSERT_TRUE(applyCrealityMessage(s, kSnapshot, strlen(kSnapshot), &beat));
    TEST_ASSERT_FALSE(beat);
    TEST_ASSERT_TRUE(s.seen);
    TEST_ASSERT_EQUAL(1, s.state);
    TEST_ASSERT_EQUAL(1, s.deviceState);
    TEST_ASSERT_EQUAL(16, s.progress);
    TEST_ASSERT_EQUAL(5148, s.leftS);
    TEST_ASSERT_EQUAL(3918, s.jobS);
    TEST_ASSERT_EQUAL(14, s.layer);
    TEST_ASSERT_EQUAL(148, s.layers);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 220.38f, s.nozzle);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.63f, s.bed);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 220.0f, s.nozzleTarget);
    TEST_ASSERT_EQUAL(0, s.errorCode);
    TEST_ASSERT_EQUAL_STRING("NovoCubo2_base.stl_PLA_2h31m7s.gcode", s.file);
    TEST_ASSERT_EQUAL_STRING("Creality Hi-5EBC", s.hostname);
    TEST_ASSERT_EQUAL_STRING("F018", s.model);
}

void test_delta_keeps_other_fields(void){
    PrinterStatus s;
    applyCrealityMessage(s, kSnapshot, strlen(kSnapshot));
    const char* d = "{ \"printLeftTime\": 5147, \"realTimeSpeed\": \"500.000000\", \"layer\": 15 }";
    TEST_ASSERT_TRUE(applyCrealityMessage(s, d, strlen(d)));
    TEST_ASSERT_EQUAL(5147, s.leftS);
    TEST_ASSERT_EQUAL(15, s.layer);
    TEST_ASSERT_EQUAL(3918, s.jobS);
    TEST_ASSERT_EQUAL(16, s.progress);
}

void test_heartbeat_and_garbage(void){
    PrinterStatus s;
    bool beat = false;
    const char* hb = "{\"ModeCode\":\"heart_beat\",\"msg\":\"2026-11-13T16:07:03.000Z\"}";
    TEST_ASSERT_TRUE(applyCrealityMessage(s, hb, strlen(hb), &beat));
    TEST_ASSERT_TRUE(beat);
    TEST_ASSERT_FALSE(s.seen);
    TEST_ASSERT_FALSE(applyCrealityMessage(s, "ok", 2, &beat));
    TEST_ASSERT_FALSE(applyCrealityMessage(s, "{broken", 7, &beat));
}

void test_error_code(void){
    PrinterStatus s;
    const char* m = "{ \"state\": 5, \"err\": { \"errcode\": 2839, \"key\": 1, \"value\": \"\" } }";
    applyCrealityMessage(s, m, strlen(m));
    TEST_ASSERT_EQUAL(2839, s.errorCode);
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Paused), static_cast<int>(phaseOf(s)));
}

void test_phase_codes(void){
    PrinterStatus s;
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Unknown), static_cast<int>(phaseOf(s)));
    s.seen = true;
    s.layer = 10;
    const int codes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 42};
    const Phase want[] = {Phase::Idle, Phase::Printing, Phase::Complete, Phase::Failed, Phase::Aborted,
                          Phase::Paused, Phase::Paused, Phase::Stopping, Phase::Printing, Phase::Preparing,
                          Phase::Unknown};
    for (int i = 0; i < 11; i++){
        s.state = codes[i];
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(want[i]), static_cast<int>(phaseOf(s)), "state code");
    }
    s.state = 1;
    s.layer = 0;   // heating and levelling before the first layer
    TEST_ASSERT_EQUAL(static_cast<int>(Phase::Preparing), static_cast<int>(phaseOf(s)));
}

// ---------------------------------------------------------------- estimator

// A real job on the Creality Hi (2026-11-13), every 30 s from 1 h 7 min in:
// printJobTime, printLeftTime, printProgress, layer. Slicer estimate
// 9067 s; the printer reported "complete" at printJobTime 9230.
struct LogRow {
    int job, left, fileProgress, layer;
};
static const LogRow kRealJob[] = {
    {4018, 5049, 18, 17},
    {4048, 5019, 18, 17},
    {4078, 4988, 18, 18},
    {4108, 4958, 19, 19},
    {4138, 4928, 19, 19},
    {4169, 4898, 19, 20},
    {4199, 4868, 20, 21},
    {4229, 4837, 20, 21},
    {4260, 4807, 21, 22},
    {4290, 4777, 21, 23},
    {4320, 4746, 21, 23},
    {4350, 4716, 22, 24},
    {4381, 4686, 22, 25},
    {4411, 4656, 23, 26},
    {4441, 4625, 23, 26},
    {4471, 4595, 23, 27},
    {4502, 4565, 24, 27},
    {4532, 4534, 24, 28},
    {4562, 4504, 25, 29},
    {4592, 4474, 25, 29},
    {4623, 4444, 25, 30},
    {4653, 4414, 26, 31},
    {4683, 4383, 26, 31},
    {4714, 4353, 26, 32},
    {4744, 4323, 27, 33},
    {4774, 4293, 28, 33},
    {4804, 4262, 28, 34},
    {4834, 4232, 29, 34},
    {4865, 4202, 29, 35},
    {4895, 4172, 30, 36},
    {4925, 4141, 30, 37},
    {4955, 4111, 31, 38},
    {4985, 4081, 31, 38},
    {5016, 4051, 32, 39},
    {5046, 4021, 32, 40},
    {5076, 3991, 32, 41},
    {5106, 3961, 33, 41},
    {5137, 3930, 33, 42},
    {5167, 3899, 34, 43},
    {5197, 3869, 34, 44},
    {5228, 3839, 35, 45},
    {5258, 3809, 35, 45},
    {5288, 3779, 35, 46},
    {5318, 3748, 36, 47},
    {5348, 3718, 36, 48},
    {5379, 3688, 37, 48},
    {5409, 3658, 37, 49},
    {5439, 3628, 38, 50},
    {5469, 3597, 38, 50},
    {5499, 3567, 38, 51},
    {5530, 3537, 39, 52},
    {5560, 3506, 40, 53},
    {5591, 3476, 40, 53},
    {5621, 3446, 41, 54},
    {5651, 3415, 41, 55},
    {5681, 3385, 42, 55},
    {5711, 3355, 42, 56},
    {5742, 3325, 42, 57},
    {5772, 3295, 43, 58},
    {5802, 3264, 43, 59},
    {5832, 3234, 44, 59},
    {5863, 3204, 44, 60},
    {5893, 3174, 45, 61},
    {5923, 3143, 45, 62},
    {5954, 3113, 46, 62},
    {5984, 3083, 46, 63},
    {6014, 3052, 47, 64},
    {6045, 3022, 47, 65},
    {6075, 2992, 48, 65},
    {6105, 2961, 48, 66},
    {6135, 2931, 49, 67},
    {6165, 2901, 49, 68},
    {6196, 2871, 50, 68},
    {6226, 2840, 50, 69},
    {6256, 2810, 51, 70},
    {6287, 2780, 51, 71},
    {6317, 2750, 51, 71},
    {6347, 2719, 52, 72},
    {6377, 2690, 52, 73},
    {6408, 2659, 53, 74},
    {6438, 2628, 54, 74},
    {6468, 2598, 54, 75},
    {6499, 2568, 55, 76},
    {6529, 2538, 55, 76},
    {6559, 2508, 56, 77},
    {6589, 2478, 57, 78},
    {6620, 2447, 57, 79},
    {6650, 2417, 58, 79},
    {6680, 2386, 58, 80},
    {6710, 2356, 59, 81},
    {6741, 2326, 59, 81},
    {6771, 2296, 60, 82},
    {6801, 2265, 61, 83},
    {6831, 2235, 61, 84},
    {6862, 2205, 62, 84},
    {6892, 2175, 62, 85},
    {6922, 2144, 63, 86},
    {6952, 2114, 63, 87},
    {6983, 2084, 64, 87},
    {7013, 2054, 65, 88},
    {7043, 2023, 65, 89},
    {7073, 1993, 66, 90},
    {7103, 1963, 66, 91},
    {7134, 1933, 67, 92},
    {7164, 1902, 67, 92},
    {7194, 1872, 68, 93},
    {7225, 1842, 68, 94},
    {7255, 1812, 69, 95},
    {7285, 1782, 69, 96},
    {7315, 1751, 70, 97},
    {7346, 1721, 70, 97},
    {7376, 1691, 71, 98},
    {7406, 1661, 71, 99},
    {7436, 1630, 72, 100},
    {7467, 1600, 72, 101},
    {7497, 1570, 73, 101},
    {7527, 1539, 73, 102},
    {7557, 1509, 74, 103},
    {7588, 1479, 74, 104},
    {7618, 1449, 75, 104},
    {7648, 1418, 75, 105},
    {7678, 1388, 76, 106},
    {7708, 1358, 76, 107},
    {7739, 1328, 77, 108},
    {7769, 1298, 77, 108},
    {7799, 1267, 78, 109},
    {7830, 1237, 78, 110},
    {7860, 1207, 79, 111},
    {7890, 1177, 79, 111},
    {7920, 1146, 79, 112},
    {7951, 1116, 80, 113},
    {7981, 1086, 80, 114},
    {8011, 1055, 80, 115},
    {8041, 1025, 81, 116},
    {8072, 995, 81, 117},
    {8102, 964, 82, 118},
    {8132, 934, 82, 118},
    {8163, 904, 82, 119},
    {8193, 874, 83, 120},
    {8223, 843, 83, 121},
    {8253, 813, 83, 122},
    {8283, 783, 84, 123},
    {8314, 753, 84, 124},
    {8344, 722, 85, 125},
    {8374, 692, 85, 126},
    {8404, 662, 85, 126},
    {8435, 632, 86, 127},
    {8465, 602, 86, 128},
    {8495, 572, 86, 129},
    {8525, 541, 87, 130},
    {8556, 511, 87, 131},
    {8586, 481, 87, 132},
    {8616, 450, 88, 133},
    {8646, 420, 88, 133},
    {8677, 390, 89, 134},
    {8707, 360, 89, 135},
    {8737, 330, 90, 136},
    {8767, 299, 90, 137},
    {8797, 269, 91, 138},
    {8828, 239, 91, 139},
    {8858, 209, 91, 139},
    {8888, 178, 92, 140},
    {8918, 148, 93, 140},
    {8949, 118, 94, 140},
    {8979, 88, 95, 140},
    {9009, 58, 96, 140},
    {9039, 27, 98, 140},
    {9069, 96, 98, 141},
    {9100, 70, 99, 143},
    {9130, 53, 99, 144},
    {9160, 30, 99, 146},
    {9190, 1, 99, 148},
    {9216, 0, 100, 148},
};
static const int kRealJobEnd = 9230;

void test_estimate_real_job(void){
    Estimator e;
    int checked = 0;
    for (const LogRow& r : kRealJob){
        PrinterStatus s = printing(r.job, r.left, r.fileProgress, r.layer);
        const Estimate est = e.update(s);
        const int trueLeft = kRealJobEnd - r.job;
        const int truePct = 100 * r.job / kRealJobEnd;
        // Time left within 3 % of the whole job, percent within 3 points
        TEST_ASSERT_INT_WITHIN_MESSAGE(kRealJobEnd * 3 / 100, trueLeft, est.remainingS, "time left");
        TEST_ASSERT_INT_WITHIN_MESSAGE(3, truePct, est.percent, "percent");
        checked++;
    }
    TEST_ASSERT_TRUE(checked > 60);
}

void test_estimate_overrun_uses_file_tail(void){
    Estimator e;
    e.update(printing(9000, 67, 97));
    Estimate r = e.update(printing(9100, 0, 98));
    TEST_ASSERT_TRUE(r.overrun);
    TEST_ASSERT_EQUAL(99, r.percent);
    TEST_ASSERT_INT_WITHIN(5, 9100 * 2 / 98, r.remainingS);
    r = e.update(printing(9200, 0, 100));
    TEST_ASSERT_EQUAL(0, r.remainingS);
}

void test_estimate_heating_uses_printer_numbers(void){
    Estimator e;
    PrinterStatus s = printing(-1, -1, 0, 0);
    Estimate r = e.update(s);
    TEST_ASSERT_EQUAL(0, r.percent);
    TEST_ASSERT_EQUAL(-1, r.remainingS);
    r = e.update(printing(30, 9030, 0, 0));
    TEST_ASSERT_EQUAL(0, r.percent);
    TEST_ASSERT_EQUAL(9030, r.remainingS);
}

void test_estimate_holds_while_paused(void){
    Estimator e;
    const Estimate before = e.update(printing(3000, 6000, 20));
    PrinterStatus paused = printing(3500, 5500, 20);
    paused.state = 5;
    const Estimate r = e.update(paused);
    TEST_ASSERT_EQUAL(before.remainingS, r.remainingS);
    TEST_ASSERT_EQUAL(before.percent, r.percent);
    const Estimate resumed = e.update(printing(3510, 5490, 21));
    TEST_ASSERT_EQUAL(5490, resumed.remainingS);
}

void test_estimate_new_job_and_idle(void){
    Estimator e;
    e.update(printing(8000, 1000, 90));
    Estimate r = e.update(printing(10, 4990, 0, 1));   // the next job
    TEST_ASSERT_EQUAL(0, r.percent);
    TEST_ASSERT_EQUAL(5000, e.total());
    PrinterStatus idle = printing(0, 0, 0);
    idle.state = 0;
    e.update(idle);
    TEST_ASSERT_EQUAL(-1, e.total());
}

// ---------------------------------------------------------------- job view

static bool fakeLocal(int64_t epoch, int* h, int* m){
    const int64_t s = epoch % 86400;
    *h = static_cast<int>(s / 3600);
    *m = static_cast<int>((s / 60) % 60);
    return true;
}

void test_job_done_latch(void){
    JobView j;
    Estimate est;
    est.percent = 99;
    est.remainingS = 60;
    PrinterStatus s = printing(9000, 60, 99, 147);
    strcpy(s.hostname, "Creality Hi-5EBC");
    const int64_t t0 = 1794600000;   // 20:00:00 UTC
    j.update(s, est, 1000, t0);
    View v;
    j.fill(v, s, est, t0, fakeLocal);
    TEST_ASSERT_EQUAL(static_cast<int>(Screen::Printing), static_cast<int>(v.screen));
    TEST_ASSERT_EQUAL_STRING("20:01", v.eta);

    s.state = 2;
    s.jobS = 9060;
    j.update(s, est, 1060, t0 + 60);
    s.state = 0;   // the printer drops back to idle after ~50 s
    j.update(s, est, 1120, t0 + 120);
    View d;
    j.fill(d, s, est, t0 + 120, fakeLocal);
    TEST_ASSERT_EQUAL(static_cast<int>(Screen::Done), static_cast<int>(d.screen));
    TEST_ASSERT_EQUAL(100, d.percent);
    TEST_ASSERT_EQUAL(9060, d.elapsedS);        // the printer's final print time
    TEST_ASSERT_EQUAL_STRING("20:01", d.eta);   // when it finished

    j.update(s, est, 1060 + JobView::kEndHoldS, t0 + 2000);
    View idle;
    j.fill(idle, s, est, t0 + 2000, fakeLocal);
    TEST_ASSERT_EQUAL(static_cast<int>(Screen::Idle), static_cast<int>(idle.screen));
    TEST_ASSERT_EQUAL_STRING("HI-5EBC", idle.line1);
}

void test_job_missed_end_state(void){
    // Back to idle without seeing state 2: done if it was at 99-100 %, else stopped
    JobView j;
    Estimate est;
    est.percent = 45;
    PrinterStatus s = printing(3000, 4000, 30);
    j.update(s, est, 10, 0);
    s.state = 0;
    j.update(s, est, 20, 0);
    View v;
    j.fill(v, s, est, 0, fakeLocal);
    TEST_ASSERT_EQUAL(static_cast<int>(Screen::Stopped), static_cast<int>(v.screen));
    TEST_ASSERT_EQUAL(45, v.percent);
    TEST_ASSERT_EQUAL_STRING("", v.eta);   // clock not set
}

void test_job_abort_and_new_job(void){
    JobView j;
    Estimate est;
    est.percent = 37;
    PrinterStatus s = printing(3720, 5000, 25);
    j.update(s, est, 10, 0);
    s.state = 7;   // stopping
    j.update(s, est, 11, 0);
    s.state = 4;   // aborted
    j.update(s, est, 40, 0);
    View v;
    j.fill(v, s, est, 0, fakeLocal);
    TEST_ASSERT_EQUAL(static_cast<int>(Screen::Stopped), static_cast<int>(v.screen));
    TEST_ASSERT_EQUAL(37, v.percent);
    TEST_ASSERT_EQUAL(3720, v.elapsedS);

    est.percent = 0;
    s = printing(10, 9000, 0, 0);   // next job, heating
    j.update(s, est, 100, 0);
    View n;
    j.fill(n, s, est, 0, fakeLocal);
    TEST_ASSERT_EQUAL(static_cast<int>(Screen::Printing), static_cast<int>(n.screen));
    TEST_ASSERT_TRUE(n.heating);
}

void test_job_failed_shows_error(void){
    JobView j;
    Estimate est;
    PrinterStatus s = printing(100, 100);
    j.update(s, est, 1, 0);
    s.state = 3;
    s.errorCode = 1234;
    j.update(s, est, 2, 0);
    View v;
    j.fill(v, s, est, 0, fakeLocal);
    TEST_ASSERT_EQUAL(static_cast<int>(Screen::Error), static_cast<int>(v.screen));
    TEST_ASSERT_EQUAL(1234, v.errorCode);
    j.dismiss();
    View w;
    j.fill(w, s, est, 0, fakeLocal);
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(Screen::Error), static_cast<int>(w.screen));
}

void test_formatting(void){
    char b[12];
    formatDuration(6480, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("1:48", b);
    formatDuration(29, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("0:00", b);
    formatDuration(31, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("0:01", b);
    formatDuration(-1, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("-:--", b);
    formatDuration(500000, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("99:59", b);
    formatClock(7, 5, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("07:05", b);
}

// ---------------------------------------------------------------- console

void test_console_commands(void){
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::None), static_cast<int>(parseCommand("   ").kind));
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Status), static_cast<int>(parseCommand("STATUS").kind));
    Command c = parseCommand("printer 192.168.1.216");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Printer), static_cast<int>(c.kind));
    TEST_ASSERT_EQUAL_STRING("192.168.1.216", c.arg);
    c = parseCommand("printer Auto");
    TEST_ASSERT_EQUAL_STRING("auto", c.arg);
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::BadArgs), static_cast<int>(parseCommand("printer").kind));
    c = parseCommand("tz GMT0BST,M3.5.0/1,M10.5.0 ");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Tz), static_cast<int>(c.kind));
    TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", c.arg);
    c = parseCommand("calibrate rot=2 sign=-1 offset=12.5");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Calibrate), static_cast<int>(c.kind));
    TEST_ASSERT_TRUE(c.hasRot && c.hasSign && c.hasOffset);
    TEST_ASSERT_EQUAL(2, c.rot);
    TEST_ASSERT_EQUAL(-1, c.sign);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 12.5f, c.offsetDeg);
    c = parseCommand("calibrate reset");
    TEST_ASSERT_TRUE(c.reset);
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::BadArgs), static_cast<int>(parseCommand("calibrate rot=4").kind));
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::BadArgs), static_cast<int>(parseCommand("calibrate reset rot=1").kind));
    c = parseCommand("bright 40");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Bright), static_cast<int>(c.kind));
    TEST_ASSERT_EQUAL(40, c.value);
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::BadArgs), static_cast<int>(parseCommand("bright 300").kind));
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::WifiReset), static_cast<int>(parseCommand("wifi reset").kind));
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Unknown), static_cast<int>(parseCommand("fly away").kind));
    c = parseCommand("sound off");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Sound), static_cast<int>(c.kind));
    TEST_ASSERT_EQUAL(0, c.value);
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::BadArgs), static_cast<int>(parseCommand("sound maybe").kind));
    c = parseCommand("volume 200");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Volume), static_cast<int>(c.kind));
    TEST_ASSERT_EQUAL(200, c.value);
    c = parseCommand("buzzer 2");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Buzzer), static_cast<int>(c.kind));
    TEST_ASSERT_EQUAL(2, c.value);
    c = parseCommand("buzzer off");
    TEST_ASSERT_EQUAL(-1, c.value);
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Beep), static_cast<int>(parseCommand("beep").kind));
    c = parseCommand("alert Done");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Alert), static_cast<int>(c.kind));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::Done), c.value);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::Attention), parseCommand("alert attention").value);
    c = parseCommand("alert off");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::Alert), static_cast<int>(c.kind));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::None), c.value);
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::BadArgs), static_cast<int>(parseCommand("alert").kind));
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::BadArgs), static_cast<int>(parseCommand("alert loud").kind));
    c = parseCommand("autorotate off");
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::AutoRotate), static_cast<int>(c.kind));
    TEST_ASSERT_EQUAL(0, c.value);
    TEST_ASSERT_EQUAL(1, parseCommand("AUTOROTATE ON").value);
    TEST_ASSERT_EQUAL(static_cast<int>(Cmd::BadArgs), static_cast<int>(parseCommand("autorotate").kind));
}

void test_line_reader(void){
    LineReader r;
    const char* in = "status\r\nversion\n\nhelp";
    int lines = 0;
    const char* got[3] = {nullptr, nullptr, nullptr};
    char store[3][16];
    for (const char* p = in; *p; p++){
        if (r.feed(*p)){
            strcpy(store[lines], r.line());
            got[lines] = store[lines];
            lines++;
        }
    }
    TEST_ASSERT_EQUAL(2, lines);
    TEST_ASSERT_EQUAL_STRING("status", got[0]);
    TEST_ASSERT_EQUAL_STRING("version", got[1]);
    TEST_ASSERT_TRUE(r.feed('\n'));
    TEST_ASSERT_EQUAL_STRING("help", r.line());
}

// ---------------------------------------------------------------- tilt

void test_tilt_snap_smooth_hold(void){
    TiltConfig cfg;
    Tilt t;
    // Standing: gravity along -y in the sensor frame
    TEST_ASSERT_TRUE(updateTilt(t, cfg, 0.0f, -1.0f, 0.0f) || t.valid);
    const float first = t.angle;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, wrapAngle(-atan2f(-1.0f, 0.0f)), first);
    t.drawn = t.angle;
    // Turned a quarter: eases towards it
    updateTilt(t, cfg, 1.0f, 0.0f, 0.0f);
    const float target = wrapAngle(-atan2f(0.0f, 1.0f));
    const float step1 = fabsf(wrapAngle(t.angle - first));
    TEST_ASSERT_TRUE(step1 > 0.1f && step1 < fabsf(wrapAngle(target - first)));
    for (int i = 0; i < 60; i++) updateTilt(t, cfg, 1.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, target, t.angle);
    // Lying flat: holds
    const float held = t.angle;
    for (int i = 0; i < 10; i++) updateTilt(t, cfg, 0.0f, 1.0f, 0.95f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, held, t.angle);
    // Redraw only after the redraw step
    t.drawn = t.angle;
    TEST_ASSERT_FALSE(updateTilt(t, cfg, 0.0f, 0.0f, 1.0f));
}

// ---------------------------------------------------------------- drawing

static Color s_px[kScreenSize * kScreenSize];
static Color s_px2[kScreenSize * kScreenSize];
static uint8_t s_mask[kScreenSize * kScreenSize];

void test_font(void){
    const TextStyle st = {7.0f, 1.0f, 1.0f};
    // 4-column glyph: 4 * (7-1)/6 + 1 = 5 wide; "11" = 2*(2+1) + 1 gap
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, textWidth("A", st));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 7.0f, textWidth("11", st));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, textWidth("", st));
    TEST_ASSERT_TRUE(hasGlyph('%'));
    TEST_ASSERT_TRUE(hasGlyph('\xB0'));
    TEST_ASSERT_FALSE(hasGlyph('a'));
}

void test_disc_coverage(void){
    Canvas cv(s_px, s_mask, kScreenSize, kScreenSize);
    cv.clear(0);
    cv.setXform(Xform::rotate(64.0f, 64.0f, 0.3f));
    cv.begin(rgb(255, 255, 255));
    cv.disc({0, 0}, 10.0f);
    cv.end();
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, s_px[64 * kScreenSize + 64]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, s_px[64 * kScreenSize + 80]);
    for (int i = 0; i < kScreenSize * kScreenSize; i++){
        TEST_ASSERT_EQUAL_UINT8(0, s_mask[i]);   // end() leaves the mask clean
    }
}

void test_stroke_has_no_seams(void){
    // An L stroke: the pixel on the mitre line must be fully covered
    Canvas cv(s_px, s_mask, kScreenSize, kScreenSize);
    cv.clear(0);
    cv.begin(rgb(255, 255, 255));
    const Vec l[3] = {{20.5f, 20.5f}, {20.5f, 60.5f}, {60.5f, 60.5f}};
    cv.stroke(l, 3, 4.0f);
    cv.end();
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, s_px[60 * kScreenSize + 20]);   // the corner
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, s_px[59 * kScreenSize + 21]);   // on the mitre
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, s_px[40 * kScreenSize + 20]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, s_px[40 * kScreenSize + 30]);
}

// The HUD turned a quarter turn equals the upright HUD with its pixels
// turned (inside the disc; the bezel does not turn)
void test_hud_rotation_matches_pixel_rotation(void){
    Canvas cv(s_px, s_mask, kScreenSize, kScreenSize);
    View v;
    v.screen = Screen::Printing;
    v.percent = 48;
    v.remainingS = 6480;
    strcpy(v.eta, "18:47");
    v.layer = 36;
    v.layers = 148;
    v.blink = 1;
    drawScreen(cv, v, 0.0f);
    memcpy(s_px2, s_px, sizeof(s_px));
    drawScreen(cv, v, 0.5f * 3.14159265f);
    int checked = 0, off = 0;
    for (int y = 0; y < kScreenSize; y++){
        for (int x = 0; x < kScreenSize; x++){
            const float dx = x + 0.5f - 64.0f, dy = y + 0.5f - 64.0f;
            if (dx * dx + dy * dy > 58.0f * 58.0f) continue;
            // Clockwise quarter turn: upright (x, y) lands on (127 - y, x)
            const Color a = s_px2[y * kScreenSize + x];
            const Color b = s_px[x * kScreenSize + (kScreenSize - 1 - y)];
            checked++;
            const int dr = abs((a >> 11) - (b >> 11));
            const int dg = abs(((a >> 5) & 63) - ((b >> 5) & 63));
            const int db = abs((a & 31) - (b & 31));
            if (dr > 1 || dg > 2 || db > 1) off++;
        }
    }
    TEST_ASSERT_TRUE(checked > 10000);
    TEST_ASSERT_TRUE_MESSAGE(off * 1000 < checked, "rotated HUD differs from the turned pixels");
}

void test_view_equality(void){
    View a, b;
    TEST_ASSERT_TRUE(a == b);
    b.percent = 3;
    TEST_ASSERT_TRUE(a != b);
    View c;
    strcpy(c.eta, "10:00");
    TEST_ASSERT_TRUE(a != c);
}


// ---------------------------------------------------------------- alerts

static View screenView(Screen s, int err = 0){
    View v;
    v.screen = s;
    v.errorCode = err;
    return v;
}

void test_alert_done_after_print_with_repeats(void){
    Alerts a;
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::None), static_cast<int>(a.update(screenView(Screen::Printing), 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::Done), static_cast<int>(a.update(screenView(Screen::Done), 1000)));
    TEST_ASSERT_TRUE(a.active(2000));
    int chimes = 1;
    for (uint32_t t = 1000; t < 200000; t += 33){
        if (a.update(screenView(Screen::Done), t) == AlertKind::Done) chimes++;
    }
    TEST_ASSERT_EQUAL(Alerts::kDoneRounds, chimes);
    TEST_ASSERT_FALSE(a.active(1000 + Alerts::kDoneFlashMs + 1));
}

void test_alert_none_for_stop_or_boot_into_done(void){
    Alerts a;
    a.update(screenView(Screen::Printing), 0);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::None), static_cast<int>(a.update(screenView(Screen::Stopped), 10)));
    Alerts b;
    b.update(screenView(Screen::Linking), 0);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::None), static_cast<int>(b.update(screenView(Screen::Done), 10)));
    TEST_ASSERT_FALSE(b.active(20));
}

void test_alert_attention_and_acknowledge(void){
    Alerts a;
    a.update(screenView(Screen::Printing), 0);
    a.update(screenView(Screen::Paused), 100);   // the user paused: no alert
    TEST_ASSERT_FALSE(a.active(200));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::Attention),
                      static_cast<int>(a.update(screenView(Screen::Paused, 2839), 300)));   // run-out
    TEST_ASSERT_TRUE(a.active(400));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::Attention),
                      static_cast<int>(a.update(screenView(Screen::Paused, 2839), 300 + Alerts::kRepeatMs)));
    a.acknowledge();
    TEST_ASSERT_FALSE(a.active(300 + Alerts::kRepeatMs + 1));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::None),
                      static_cast<int>(a.update(screenView(Screen::Paused, 2839), 300 + 2 * Alerts::kRepeatMs)));
    // Resumed, then the job fails
    a.update(screenView(Screen::Printing), 100000);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::Attention), static_cast<int>(a.update(screenView(Screen::Error), 100100)));
    a.update(screenView(Screen::Idle), 100200);   // dismissed on the printer
    TEST_ASSERT_FALSE(a.active(100300));
}

// The console's "alert" test runs mid-print, on a screen that would end a real alert
void test_alert_console_test(void){
    Alerts a;
    a.update(screenView(Screen::Printing), 0);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::Done), static_cast<int>(a.test(AlertKind::Done, 1000)));
    int chimes = 1;
    for (uint32_t t = 1000; t < 1000 + Alerts::kDoneFlashMs - 33; t += 33){
        if (a.update(screenView(Screen::Printing), t) == AlertKind::Done) chimes++;
        TEST_ASSERT_TRUE(a.active(t));
    }
    TEST_ASSERT_EQUAL(Alerts::kDoneRounds, chimes);
    a.update(screenView(Screen::Printing), 1000 + Alerts::kDoneFlashMs + 1);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::None), static_cast<int>(a.kind()));

    // Attention, stopped by "alert off" (= the button)
    a.test(AlertKind::Attention, 200000);
    a.update(screenView(Screen::Printing), 200033);
    TEST_ASSERT_TRUE(a.active(200033));
    a.acknowledge();
    TEST_ASSERT_FALSE(a.active(200066));

    // A real end during a test takes over and ends with the DONE screen as usual
    a.test(AlertKind::Attention, 300000);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertKind::Done), static_cast<int>(a.update(screenView(Screen::Done), 300033)));
    a.update(screenView(Screen::Idle), 300066);
    TEST_ASSERT_FALSE(a.active(300099));
}

void test_melody_player(void){
    MelodyPlayer p;
    p.start(melodyFor(AlertKind::Done), 0);
    Note n;
    int notes = 0;
    uint32_t t = 0;
    for (; t < 5000 && p.playing(); t += 10){
        if (p.next(t, &n)){
            notes++;
            TEST_ASSERT_TRUE(n.hz > 0 && n.ms > 0);
        }
    }
    TEST_ASSERT_EQUAL(8, notes);   // two runs of four notes, rests skipped
    TEST_ASSERT_FALSE(p.playing());
    TEST_ASSERT_TRUE(t < 3000);
    TEST_ASSERT_NULL(melodyFor(AlertKind::None));
}

void test_alert_view_flashes(void){
    View v;
    v.screen = Screen::Done;
    TEST_ASSERT_FALSE(animates(v));
    v.alert = true;
    TEST_ASSERT_TRUE(animates(v));
    View w = v;
    w.alert = false;
    TEST_ASSERT_TRUE(v != w);
}

// ---------------------------------------------------------------- details (CoreS3)

void test_snapshot_details(void){
    PrinterStatus s;
    TEST_ASSERT_TRUE(applyCrealityMessage(s, kSnapshotFull, strlen(kSnapshotFull)));
    TEST_ASSERT_TRUE(s.startEpoch == 1795096136LL);   // to the second: not through a float
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 500.0f, s.speed);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.18f, s.flow);
    TEST_ASSERT_EQUAL(100, s.feedPct);
    TEST_ASSERT_EQUAL(100, s.flowPct);
    TEST_ASSERT_EQUAL(100, s.fanPart);
    TEST_ASSERT_EQUAL(0, s.fanAux);
    TEST_ASSERT_EQUAL(0, s.fanCase);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.56f, s.z);
    TEST_ASSERT_EQUAL(33265, s.usedMm);
    TEST_ASSERT_EQUAL(0, s.boxMax);
    TEST_ASSERT_EQUAL(300, s.nozzleMax);
    TEST_ASSERT_EQUAL(100, s.bedMax);
    TEST_ASSERT_EQUAL(500, s.errorCode);
    TEST_ASSERT_EQUAL_STRING("wo01_panel_8x8.step_PLA_4h40m54s.gcode", s.file);

    // A delta moves only what it carries
    const char* delta = "{\"curPosition\":\"X:68.41 Y:99.01 Z:7.55\",\"usedMaterialLength\":33267,"
                        "\"realTimeSpeed\":\"150.000000\"}";
    TEST_ASSERT_TRUE(applyCrealityMessage(s, delta, strlen(delta)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.55f, s.z);
    TEST_ASSERT_EQUAL(33267, s.usedMm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, s.speed);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.18f, s.flow);
}

void test_job_file_names(void){
    char name[32], mat[10];
    int32_t t = 0;
    splitJobFile("wo01_panel_8x8.step_PLA_4h40m54s.gcode", name, sizeof(name), mat, sizeof(mat), &t);
    TEST_ASSERT_EQUAL_STRING("WO01 PANEL 8X8", name);
    TEST_ASSERT_EQUAL_STRING("PLA", mat);
    TEST_ASSERT_EQUAL(16854, t);
    splitJobFile("NovoCubo2_base.stl_PLA_2h31m7s.gcode", name, sizeof(name), mat, sizeof(mat), &t);
    TEST_ASSERT_EQUAL_STRING("NOVOCUBO2 BASE", name);
    TEST_ASSERT_EQUAL(9067, t);
    // Accents dropped (UTF-8 "Peça"), a four-letter material
    splitJobFile("Pe\xC3\xA7" "a_suporte_PETG_45m12s.gcode", name, sizeof(name), mat, sizeof(mat), &t);
    TEST_ASSERT_EQUAL_STRING("PECA SUPORTE", name);
    TEST_ASSERT_EQUAL_STRING("PETG", mat);
    TEST_ASSERT_EQUAL(2712, t);
    splitJobFile("cube.3mf_PLA-CF_1d2h3m4s.gcode", name, sizeof(name), mat, sizeof(mat), &t);
    TEST_ASSERT_EQUAL_STRING("CUBE", name);
    TEST_ASSERT_EQUAL_STRING("PLA-CF", mat);
    TEST_ASSERT_EQUAL(93784, t);
    // Not Creality Print's pattern: the whole name, nothing else
    splitJobFile("my_part_v2.gcode", name, sizeof(name), mat, sizeof(mat), &t);
    TEST_ASSERT_EQUAL_STRING("MY PART V2", name);
    TEST_ASSERT_EQUAL_STRING("", mat);
    TEST_ASSERT_EQUAL(-1, t);
    splitJobFile("", name, sizeof(name), mat, sizeof(mat), &t);
    TEST_ASSERT_EQUAL_STRING("", name);

    TEST_ASSERT_EQUAL(2700, parseSliceTime("45m", 3));
    TEST_ASSERT_EQUAL(-1, parseSliceTime("12", 2));
    TEST_ASSERT_EQUAL(-1, parseSliceTime("h", 1));
    TEST_ASSERT_EQUAL(-1, parseSliceTime("4h40", 4));
}

void test_job_details(void){
    PrinterStatus s;
    applyCrealityMessage(s, kSnapshotFull, strlen(kSnapshotFull));
    Estimator e;
    const Estimate est = e.update(s);
    JobView j;
    const int64_t now = 1795114080;   // 18:48 UTC
    j.update(s, est, 1000, now);
    View v;
    j.fill(v, s, est, now, fakeLocal);
    TEST_ASSERT_EQUAL(static_cast<int>(Screen::Printing), static_cast<int>(v.screen));
    const Details& d = v.d;
    TEST_ASSERT_EQUAL_STRING("WO01 PANEL 8X8", d.job);
    TEST_ASSERT_EQUAL_STRING("PLA", d.material);
    TEST_ASSERT_EQUAL(16854, d.sliceS);
    TEST_ASSERT_EQUAL(v.elapsedS + v.remainingS, d.totalS);
    TEST_ASSERT_EQUAL_STRING("13:48", d.start);   // printStartTime 13:48:56 UTC
    TEST_ASSERT_EQUAL(500, d.speed);
    TEST_ASSERT_EQUAL(42, d.flow10);
    TEST_ASSERT_EQUAL(756, d.z100);
    TEST_ASSERT_EQUAL(333, d.usedDm);             // 33.3 m
    TEST_ASSERT_EQUAL(85, d.filePct);
    TEST_ASSERT_EQUAL(220, d.nozzleTarget);
    TEST_ASSERT_EQUAL(60, d.bedTarget);
    TEST_ASSERT_EQUAL(300, d.nozzleMax);
    TEST_ASSERT_EQUAL(-1000, d.chamber);          // the Hi has no chamber sensor
    TEST_ASSERT_EQUAL_STRING("HI-5EBC", d.printer);
    TEST_ASSERT_EQUAL_STRING("F018", d.model);

    // The job ends; once idle the printer may clear the file and the count
    s.state = 2;
    j.update(s, est, 1100, now + 100);
    s.state = 0;
    s.file[0] = 0;
    s.usedMm = 0;
    j.update(s, est, 1160, now + 160);
    View end;
    j.fill(end, s, est, now + 160, fakeLocal);
    TEST_ASSERT_EQUAL(static_cast<int>(Screen::Done), static_cast<int>(end.screen));
    TEST_ASSERT_EQUAL_STRING("WO01 PANEL 8X8", end.d.job);
    TEST_ASSERT_EQUAL(333, end.d.usedDm);
    TEST_ASSERT_EQUAL_STRING("13:48", end.d.start);
    TEST_ASSERT_EQUAL(-1, end.d.speed);

    // A printer with a chamber sensor
    s.boxMax = 60;
    s.box = 35.4f;
    s.boxTarget = 40.0f;
    View c;
    j.fill(c, s, est, now + 160, fakeLocal);
    TEST_ASSERT_EQUAL(35, c.d.chamber);
    TEST_ASSERT_EQUAL(40, c.d.chamberTarget);
}

void test_view_details_equality(void){
    View a, b;
    TEST_ASSERT_TRUE(a == b);
    b.d.speed = 250;
    TEST_ASSERT_TRUE(a != b);
    b = a;
    strcpy(b.d.job, "CUBE");
    TEST_ASSERT_TRUE(a != b);
}

static Color   s_dash[kDashW * kDashH];
static Color   s_strip[kDashW * 80];
static uint8_t s_dmask[kDashW * kDashH];

static View dashView(Screen sc){
    View v;
    v.screen = sc;
    v.percent = 86;
    v.remainingS = 2234;
    v.elapsedS = 14620;
    strcpy(v.eta, "19:25");
    strcpy(v.clock, "18:48");
    v.layer = 37;
    v.layers = 50;
    v.nozzle = 220;
    v.bed = 60;
    v.blink = 1;
    v.spin = 12;
    strcpy(v.line1, "PRINTER");
    strcpy(v.line2, "192.168.1.50");
    strcpy(v.d.job, "WO01 PANEL 8X8");
    strcpy(v.d.material, "PLA");
    v.d.speed = 250;
    v.d.usedDm = 333;
    v.d.wifiBars = 3;
    return v;
}

// Largest difference of two RGB565 colours, in 5-bit levels
static int colourDiff(Color a, Color b){
    const int dr = abs(((a >> 11) & 31) - ((b >> 11) & 31));
    const int dg = abs(((a >> 5) & 63) - ((b >> 5) & 63)) / 2;
    const int db = abs((a & 31) - (b & 31));
    return dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
}

// Every screen draws; the strips the firmware draws in give the same frame
// (anti-aliased edges may round one level apart); the title band takes the
// state's colour
void test_dash_screens_and_strips(void){
    Canvas full(s_dash, s_dmask, kDashW, kDashH);
    Canvas strip(s_strip, s_dmask, kDashW, 80);
    const Screen all[] = {Screen::Splash, Screen::Setup, Screen::Linking, Screen::Offline, Screen::Idle,
                          Screen::Printing, Screen::Paused, Screen::Done, Screen::Stopped, Screen::Error};
    for (Screen sc : all){
        const View v = dashView(sc);
        drawDash(full, v);
        int worst = 0;
        for (int top = 0; top < kDashH; top += 80){
            drawDash(strip, v, top);
            for (int i = 0; i < kDashW * 80; i++){
                const int d = colourDiff(s_strip[i], s_dash[top * kDashW + i]);
                if (d > worst) worst = d;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(worst <= 1, screenName(sc));
    }
    // Portrait: 240 wide, 320 tall, four strips
    Canvas pfull(s_dash, s_dmask, kDashH, kDashW);
    Canvas pstrip(s_strip, s_dmask, kDashH, 80);
    for (Screen sc : all){
        const View v = dashView(sc);
        drawDash(pfull, v, 0, true);
        int worst = 0;
        for (int top = 0; top < kDashW; top += 80){
            drawDash(pstrip, v, top, true);
            for (int i = 0; i < kDashH * 80; i++){
                const int d = colourDiff(s_strip[i], s_dash[top * kDashH + i]);
                if (d > worst) worst = d;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(worst <= 1, screenName(sc));
    }
    // The CardputerZero's 320 x 170 layout, both pages: it draws, and the
    // gauge's centre shows the job's percent (a lit ring segment at the top)
    Canvas wide(s_dash, s_dmask, kDashW, kWideH);
    TEST_ASSERT_EQUAL(kWideH, dashHeight(DashLayout::Wide));
    for (Screen sc : all){
        for (uint8_t page = 0; page < 2; page++){
            View v = dashView(sc);
            v.page = page;
            drawDash(wide, v, 0, DashLayout::Wide);
        }
    }
    {
        View v = dashView(Screen::Printing);
        drawDash(wide, v, 0, DashLayout::Wide);
        TEST_ASSERT_EQUAL_HEX16(pal::yellow, s_dash[12 * kDashW + 4]);   // the band
        View p2 = v;
        p2.page = 1;
        static Color first[kDashW * kWideH];
        memcpy(first, s_dash, sizeof(first));
        drawDash(wide, p2, 0, DashLayout::Wide);
        TEST_ASSERT_TRUE(memcmp(first, s_dash, sizeof(first)) != 0);     // page 2 differs
    }

    const struct { Screen s; Color c; } bands[] = {
        {Screen::Printing, pal::yellow}, {Screen::Paused, pal::cyan}, {Screen::Done, pal::green},
        {Screen::Error, pal::red}, {Screen::Setup, pal::cyan},
    };
    for (const auto& b : bands){
        drawDash(full, dashView(b.s));
        TEST_ASSERT_EQUAL_HEX16(b.c, s_dash[12 * kDashW + 4]);   // the band, left of its label
    }
}

// ---------------------------------------------------------------- orientation and motion (CoreS3)

// Gravity for the panel turned by `deg` clockwise, with sign -1, offset 0:
// angle = -atan2(ay, ax)  ->  ay = -sin(angle), ax = cos(angle)
static void gravityAt(float deg, float* ax, float* ay){
    const float a = deg * 3.14159265f / 180.0f;
    *ax = cosf(a);
    *ay = -sinf(a);
}

void test_orient_quarters_hysteresis_and_flat(void){
    OrientConfig cfg;
    Orient o;
    float ax, ay;
    gravityAt(5.0f, &ax, &ay);
    TEST_ASSERT_TRUE(o.snap(ax, ay, 0.1f, cfg));
    TEST_ASSERT_EQUAL(0, o.quarter());

    // 50 degrees: past the 45-degree line, but inside the margin: stays
    gravityAt(50.0f, &ax, &ay);
    for (uint32_t t = 0; t < 2000; t += 20) TEST_ASSERT_FALSE(o.update(ax, ay, 0.1f, t, cfg));
    TEST_ASSERT_EQUAL(0, o.quarter());

    // 85 degrees: a new quarter, after the settle time
    gravityAt(85.0f, &ax, &ay);
    TEST_ASSERT_FALSE(o.update(ax, ay, 0.1f, 3000, cfg));
    TEST_ASSERT_FALSE(o.update(ax, ay, 0.1f, 3000 + cfg.settleMs - 20, cfg));
    TEST_ASSERT_TRUE(o.update(ax, ay, 0.1f, 3000 + cfg.settleMs, cfg));
    TEST_ASSERT_EQUAL(1, o.quarter());

    // A brief swing to the other side does not count
    gravityAt(-90.0f, &ax, &ay);
    TEST_ASSERT_FALSE(o.update(ax, ay, 0.1f, 5000, cfg));
    gravityAt(90.0f, &ax, &ay);
    TEST_ASSERT_FALSE(o.update(ax, ay, 0.1f, 5200, cfg));
    gravityAt(-90.0f, &ax, &ay);
    TEST_ASSERT_FALSE(o.update(ax, ay, 0.1f, 5400, cfg));
    TEST_ASSERT_FALSE(o.update(ax, ay, 0.1f, 5400 + cfg.settleMs - 1, cfg));
    TEST_ASSERT_TRUE(o.update(ax, ay, 0.1f, 5400 + cfg.settleMs, cfg));
    TEST_ASSERT_EQUAL(3, o.quarter());

    // Lying flat: held, whatever the in-plane noise says
    TEST_ASSERT_FALSE(o.update(0.3f, 0.2f, 0.95f, 9000, cfg));
    TEST_ASSERT_FALSE(o.update(0.3f, 0.2f, 0.95f, 12000, cfg));
    TEST_ASSERT_EQUAL(3, o.quarter());
    TEST_ASSERT_FALSE(o.snap(0.0f, 0.1f, 1.0f, cfg));
    TEST_ASSERT_EQUAL(3, o.quarter());

    // Upside down
    gravityAt(178.0f, &ax, &ay);
    TEST_ASSERT_TRUE(o.snap(ax, ay, 0.0f, cfg));
    TEST_ASSERT_EQUAL(2, o.quarter());
}

void test_motion_knock_move_and_quiet(void){
    MotionConfig cfg;
    MotionDetector m;
    uint32_t t = 0;
    // At rest, standing up (gravity along -y), with a little printer vibration
    for (int i = 0; i < 400; i++, t += 5){
        const float j = (i % 2) ? 0.03f : -0.03f;
        m.update(j, -1.0f + j, 0.0f, 1.0f, -1.0f, 0.5f, t, cfg);
    }
    TEST_ASSERT_EQUAL(0u, m.taps());
    TEST_ASSERT_EQUAL(0u, m.moves());
    float gx, gy, gz;
    m.gravity(&gx, &gy, &gz);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -1.0f, gy);

    // A knock: a 1.5 g spike for 10 ms, quiet again at once
    m.update(0.0f, -1.0f, 1.5f, 5.0f, 0.0f, 0.0f, t, cfg); t += 5;
    m.update(0.0f, -1.0f, -0.8f, 5.0f, 0.0f, 0.0f, t, cfg); t += 5;
    for (int i = 0; i < 20; i++, t += 5) m.update(0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, t, cfg);
    TEST_ASSERT_EQUAL(1u, m.taps());
    TEST_ASSERT_EQUAL(0u, m.moves());   // too short to be a move
    m.gravity(&gx, &gy, &gz);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, gz);   // the knock did not move the gravity estimate

    // Picked up and turned: the gyro says so; a long shake is not a knock
    for (int i = 0; i < 60; i++, t += 5){
        const float s = (i % 4 < 2) ? 1.2f : -1.2f;
        m.update(0.4f * s, -1.0f, 0.6f * s, 120.0f, 40.0f, 0.0f, t, cfg);
    }
    TEST_ASSERT_EQUAL(1u, m.taps());
    TEST_ASSERT_TRUE(m.moves() >= 1u);
    TEST_ASSERT_TRUE(m.moves() <= 1u + 300u / cfg.moveGapMs);   // rate-limited
}

int main(int, char**){
    UNITY_BEGIN();
    RUN_TEST(test_snapshot_fields);
    RUN_TEST(test_delta_keeps_other_fields);
    RUN_TEST(test_heartbeat_and_garbage);
    RUN_TEST(test_error_code);
    RUN_TEST(test_phase_codes);
    RUN_TEST(test_estimate_real_job);
    RUN_TEST(test_estimate_overrun_uses_file_tail);
    RUN_TEST(test_estimate_heating_uses_printer_numbers);
    RUN_TEST(test_estimate_holds_while_paused);
    RUN_TEST(test_estimate_new_job_and_idle);
    RUN_TEST(test_job_done_latch);
    RUN_TEST(test_job_missed_end_state);
    RUN_TEST(test_job_abort_and_new_job);
    RUN_TEST(test_job_failed_shows_error);
    RUN_TEST(test_formatting);
    RUN_TEST(test_console_commands);
    RUN_TEST(test_line_reader);
    RUN_TEST(test_tilt_snap_smooth_hold);
    RUN_TEST(test_font);
    RUN_TEST(test_disc_coverage);
    RUN_TEST(test_stroke_has_no_seams);
    RUN_TEST(test_hud_rotation_matches_pixel_rotation);
    RUN_TEST(test_view_equality);
    RUN_TEST(test_alert_done_after_print_with_repeats);
    RUN_TEST(test_alert_none_for_stop_or_boot_into_done);
    RUN_TEST(test_alert_attention_and_acknowledge);
    RUN_TEST(test_alert_console_test);
    RUN_TEST(test_melody_player);
    RUN_TEST(test_alert_view_flashes);
    RUN_TEST(test_snapshot_details);
    RUN_TEST(test_job_file_names);
    RUN_TEST(test_job_details);
    RUN_TEST(test_view_details_equality);
    RUN_TEST(test_dash_screens_and_strips);
    RUN_TEST(test_orient_quarters_hysteresis_and_flat);
    RUN_TEST(test_motion_knock_move_and_quiet);
    return UNITY_END();
}
