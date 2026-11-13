// Host tests of lib/hm_core:  pio test -e native

#include <math.h>
#include <string.h>
#include <unity.h>

#include "console.h"
#include "eta.h"
#include "gfx.h"
#include "job.h"
#include "mechfont.h"
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

// Moonraker /printer/objects/query reply from the same printer
static const char* kMoonraker =
    "{\"result\": {\"eventtime\": 1485043.08, \"status\": {\"display_status\": {\"progress\": 0.39}, "
    "\"print_stats\": {\"state\": \"printing\", \"print_duration\": 3899.26}}}}";

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

static MoonrakerStatus m73(float p, const char* state = "printing"){
    MoonrakerStatus m;
    m.valid = true;
    m.m73 = p;
    strcpy(m.state, state);
    return m;
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

void test_moonraker_parse(void){
    MoonrakerStatus m;
    TEST_ASSERT_TRUE(parseMoonraker(m, kMoonraker, strlen(kMoonraker)));
    TEST_ASSERT_TRUE(m.valid);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.39f, m.m73);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3899.26f, m.printS);
    TEST_ASSERT_EQUAL_STRING("printing", m.state);
    TEST_ASSERT_FALSE(parseMoonraker(m, "{\"error\":1}", 11));
}

// ---------------------------------------------------------------- estimator

void test_estimate_fallback_without_moonraker(void){
    Estimator e;
    Estimate r = e.update(printing(3918, 5148, 16), nullptr);
    TEST_ASSERT_EQUAL(16, r.percent);
    TEST_ASSERT_EQUAL(5148, r.remainingS);
    TEST_ASSERT_FALSE(r.fromM73);
}

void test_estimate_m73_before_history_is_slicer(void){
    Estimator e;
    const MoonrakerStatus m = m73(0.39f);
    Estimate r = e.update(printing(3918, 5148), &m);
    TEST_ASSERT_TRUE(r.fromM73);
    TEST_ASSERT_EQUAL(39, r.percent);
    // T = 9066 s; no measured ratio yet: T * (1 - 0.39)
    TEST_ASSERT_INT_WITHIN(2, 5530, r.remainingS);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, r.ratio);
}

void test_estimate_learns_slower_printer(void){
    // Slicer total 10000 s; the printer takes 140 s per 1 % instead of 100
    Estimator e;
    const int T = 10000;
    Estimate r;
    for (int t = 0; t <= 2800; t += 10){
        const float p = 0.10f + 0.01f * (t / 140);
        const MoonrakerStatus m = m73(p);
        r = e.update(printing(1000 + t, T - (1000 + t)), &m);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 1.4f, r.ratio);
    // p = 0.30 reached at t = 2800: 70 % left at 140 s per % = 9800 s
    TEST_ASSERT_INT_WITHIN(300, 9800, r.remainingS);
    TEST_ASSERT_EQUAL(30, r.percent);
}

void test_estimate_holds_while_paused(void){
    Estimator e;
    Estimate before;
    for (int t = 0; t <= 1500; t += 10){
        const MoonrakerStatus m = m73(0.2f + 0.01f * (t / 100));
        before = e.update(printing(2000 + t, 8000 - t), &m);
    }
    PrinterStatus paused = printing(3600, 6000);
    paused.state = 5;
    const MoonrakerStatus m = m73(0.35f, "paused");
    Estimate r = e.update(paused, &m);
    TEST_ASSERT_EQUAL(before.remainingS, r.remainingS);
    TEST_ASSERT_EQUAL(before.percent, r.percent);
}

void test_estimate_resets_on_new_job(void){
    Estimator e;
    for (int t = 0; t <= 1500; t += 10){
        const MoonrakerStatus m = m73(0.5f + 0.01f * (t / 200));
        e.update(printing(5000 + t, 5000 - t), &m);
    }
    const MoonrakerStatus m = m73(0.01f);
    Estimate r = e.update(printing(60, 3000), &m);
    TEST_ASSERT_EQUAL(1, r.percent);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, r.ratio);
    TEST_ASSERT_INT_WITHIN(40, 3030, r.remainingS);
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

int main(int, char**){
    UNITY_BEGIN();
    RUN_TEST(test_snapshot_fields);
    RUN_TEST(test_delta_keeps_other_fields);
    RUN_TEST(test_heartbeat_and_garbage);
    RUN_TEST(test_error_code);
    RUN_TEST(test_phase_codes);
    RUN_TEST(test_moonraker_parse);
    RUN_TEST(test_estimate_fallback_without_moonraker);
    RUN_TEST(test_estimate_m73_before_history_is_slicer);
    RUN_TEST(test_estimate_learns_slower_printer);
    RUN_TEST(test_estimate_holds_while_paused);
    RUN_TEST(test_estimate_resets_on_new_job);
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
    return UNITY_END();
}
