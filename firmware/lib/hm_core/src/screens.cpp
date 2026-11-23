#include "screens.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mechfont.h"

namespace hm {

namespace {

constexpr float kPi  = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;

// HUD geometry, local units = pixels, origin at the panel centre
constexpr float kFaceR    = 62.5f;   // the HUD disc
constexpr float kRimR     = 61.0f;   // rim ring kRimR..kFaceR
constexpr float kRingR0   = 53.5f;   // progress ring
constexpr float kRingR1   = 59.0f;
constexpr float kSegPitch = 360.0f / kRingSegments;   // degrees per segment
constexpr float kSegGap   = 2.0f;                     // degrees between segments

// Title band: a trapezoid with 45-degree ends under the top of the ring
constexpr float kBandTop  = -46.0f;
constexpr float kBandBot  = -36.0f;
constexpr float kBandTopW = 22.0f;   // half width at the top edge

const TextStyle kBandText  = {7.0f, 1.5f, -1.0f};
const TextStyle kBig       = {28.0f, 4.6f, 3.0f};
const TextStyle kBigUnit   = {13.0f, 1.8f, -1.0f};
const TextStyle kValue     = {13.0f, 2.4f, 2.0f};
const TextStyle kValueSm   = {8.0f, 1.7f, 1.5f};
const TextStyle kLabel     = {6.0f, 1.3f, 1.1f};
const TextStyle kTiny      = {5.0f, 1.1f, -1.0f};
const TextStyle kLine      = {9.0f, 1.8f, 1.6f};
const TextStyle kLineSm    = {7.0f, 1.5f, 1.3f};
const TextStyle kClock     = {24.0f, 4.0f, 3.0f};

Vec V(float x, float y){ return {x, y}; }

// ---------------------------------------------------------------- bezel

// One corner of the bezel, drawn as the top-left corner in a frame centred
// on the panel. variantA: hazard stripes; otherwise a block and dashes.
void bezelCorner(Canvas& cv, bool variantA){
    cv.begin(pal::yellow);
    const Vec bracket[4] = {V(-61.5f, -38.0f), V(-61.5f, -53.0f), V(-53.0f, -61.5f), V(-38.0f, -61.5f)};
    cv.stroke(bracket, 4, 2.0f);
    cv.end();

    if (variantA){
        // Three hazard stripes parallel to the chamfer, shortening towards the HUD
        cv.begin(pal::yellow);
        const float cs[3]  = {-106.0f, -100.0f, -94.0f};
        const float len[3] = {9.0f, 7.0f, 5.0f};
        for (int i = 0; i < 3; i++){
            const float c  = cs[i] * 0.5f;            // centre on the diagonal (c, c)
            const float hl = len[i] * 0.7071f;        // half length along (1,-1)
            const float hw = 1.25f * 0.7071f;         // half width along (1,1)
            const Vec p[4] = {
                V(c - hl - hw, c + hl - hw), V(c + hl - hw, c - hl - hw),
                V(c + hl + hw, c - hl + hw), V(c - hl + hw, c + hl + hw),
            };
            cv.poly(p, 4);
        }
        cv.end();
    } else {
        // A block in the corner and speed dashes along both arms
        cv.begin(pal::yellow);
        cv.rect(-55.5f, -55.5f, 4.0f, 4.0f);
        cv.end();
        cv.begin(pal::mid);
        for (int i = 0; i < 3; i++){
            const float t = -49.0f + 4.0f * i;
            cv.line(V(-57.0f, t), V(-57.0f, t + 2.0f), 1.4f);
            cv.line(V(t, -57.0f), V(t + 2.0f, -57.0f), 1.4f);
        }
        cv.end();
    }
}

void drawBezel(Canvas& cv){
    const float c = kScreenSize * 0.5f;
    for (int k = 0; k < 4; k++){
        cv.setXform(Xform::rotate(c, c, k * 0.5f * kPi));
        bezelCorner(cv, (k & 1) == 0);
    }
}

// ---------------------------------------------------------------- HUD parts

void face(Canvas& cv){
    cv.begin(pal::face);
    cv.disc(V(0, 0), kFaceR);
    cv.end();
    cv.begin(pal::dim);
    cv.ring(V(0, 0), kRimR, kFaceR);
    cv.end();
}

void segment(Canvas& cv, int i){
    const float a0 = (i * kSegPitch + 0.5f * kSegGap) * kDeg;
    const float a1 = ((i + 1) * kSegPitch - 0.5f * kSegGap) * kDeg;
    cv.sector(V(0, 0), kRingR0, kRingR1, a0, a1);
}

// Progress ring: lit segments in colour, the next one blinking, rest dim
void progressRing(Canvas& cv, int percent, Color c, bool blinkNext, uint8_t blink){
    int lit = percent < 0 ? 0 : percent * kRingSegments / 100;
    if (lit > kRingSegments) lit = kRingSegments;
    cv.begin(pal::dim);
    for (int i = lit; i < kRingSegments; i++){
        segment(cv, i);
    }
    cv.end();
    if (lit > 0){
        cv.begin(c);
        for (int i = 0; i < lit; i++){
            segment(cv, i);
        }
        cv.end();
    }
    if (blinkNext && lit < kRingSegments && (blink & 1)){
        cv.begin(c, 150);
        segment(cv, lit);
        cv.end();
    }
}

// All segments dim, with a comet of `len` segments ending at `head`
void spinnerRing(Canvas& cv, int head, int len, Color c){
    cv.begin(pal::dim);
    for (int i = 0; i < kRingSegments; i++){
        const int behind = (head - i + kRingSegments) % kRingSegments;
        if (behind >= len){
            segment(cv, i);
        }
    }
    cv.end();
    for (int k = 0; k < len; k++){
        const int i = (head - k + kRingSegments) % kRingSegments;
        cv.begin(c, static_cast<uint8_t>(255 - k * (200 / len)));
        segment(cv, i);
        cv.end();
    }
}

void fullRing(Canvas& cv, Color c){
    cv.begin(c);
    for (int i = 0; i < kRingSegments; i++){
        segment(cv, i);
    }
    cv.end();
}

// Ring with every segment dim and four markers at the quarters
void quarterRing(Canvas& cv, Color c){
    cv.begin(pal::dim);
    for (int i = 0; i < kRingSegments; i++){
        if (i % (kRingSegments / 4) != 0){
            segment(cv, i);
        }
    }
    cv.end();
    cv.begin(c);
    for (int q = 0; q < 4; q++){
        segment(cv, q * kRingSegments / 4);
    }
    cv.end();
}

// Title band with the label knocked out, plus as many hazard stripes in
// each end as fit beside the label
void band(Canvas& cv, const char* label, Color c, bool show = true){
    if (!show){
        return;
    }
    const float h   = kBandBot - kBandTop;
    const float bot = kBandTopW + h;
    cv.begin(c);
    const Vec p[4] = {V(-kBandTopW, kBandTop), V(kBandTopW, kBandTop), V(bot, kBandBot), V(-bot, kBandBot)};
    cv.poly(p, 4);
    cv.end();

    const float tw = textWidth(label, kBandText);
    const float textTop = kBandTop + 0.5f * (h - kBandText.cap);
    const float textBase = textTop + kBandText.cap;
    cv.begin(pal::face);
    drawText(cv, label, 0.0f, textBase, kBandText, Align::Center);

    // Stripes parallel to the band's ends, 2 px wide, 3.5 px pitch,
    // starting 2.5 px in from the end and stopping 2.5 px short of the text
    const float halfAtTextTop = kBandTopW + (textTop - kBandTop);
    const float room = halfAtTextTop - 0.5f * tw - 2.5f;   // horizontal room at the text's top
    for (int i = 0; i < 3; i++){
        const float in0 = 2.5f + 3.5f * i;                   // offset from the end, at the top edge
        const float in1 = in0 + 2.0f;
        if (in1 + (textTop - kBandTop) > room + (textTop - kBandTop)){
            break;
        }
        if (in1 > room){
            break;
        }
        for (int side = -1; side <= 1; side += 2){
            // End line at the top: x = side*kBandTopW; moving down, it goes outwards by h
            const float xt0 = side * (kBandTopW - in0);
            const float xt1 = side * (kBandTopW - in1);
            const Vec s[4] = {
                V(xt0, kBandTop), V(xt1, kBandTop),
                V(xt1 + side * h, kBandBot), V(xt0 + side * h, kBandBot),
            };
            cv.poly(s, 4);
        }
    }
    cv.end();
}

// A label + value pair centred on x = 0, sharing a baseline
void pair(Canvas& cv, const char* label, Color lc, const char* value, Color vc,
          float baseline, const TextStyle& vs, float gap = 4.0f){
    const float lw = textWidth(label, kLabel);
    const float vw = textWidth(value, vs);
    const float x0 = -0.5f * (lw + gap + vw);
    cv.begin(lc);
    drawText(cv, label, x0, baseline, kLabel);
    cv.end();
    cv.begin(vc);
    drawText(cv, value, x0 + lw + gap, baseline, vs);
    cv.end();
}

// Big percentage with a small "%" at the top right of the digits
void bigPercent(Canvas& cv, int percent, float baseline){
    char num[8];
    if (percent < 0){
        snprintf(num, sizeof(num), "--");
    } else {
        snprintf(num, sizeof(num), "%d", percent > 100 ? 100 : percent);
    }
    const float gap = 2.5f;
    const float nw  = textWidth(num, kBig);
    const float uw  = textWidth("%", kBigUnit);
    const float x0  = -0.5f * (nw + gap + uw);
    cv.begin(pal::text);
    drawText(cv, num, x0, baseline, kBig);
    cv.end();
    cv.begin(pal::yellow);
    drawText(cv, "%", x0 + nw + gap, baseline - kBig.cap + kBigUnit.cap, kBigUnit);
    cv.end();
}

// Thin rule with three speed dashes on the left
void rule(Canvas& cv, float y, Color accent){
    cv.begin(pal::dim);
    cv.line(V(-26.0f, y), V(38.0f, y), 1.0f);
    cv.end();
    cv.begin(accent);
    for (int i = 0; i < 3; i++){
        const float x = -39.0f + 4.0f * i;
        const Vec p[4] = {V(x, y + 1.0f), V(x + 2.0f, y - 1.0f), V(x + 3.5f, y - 1.0f), V(x + 1.5f, y + 1.0f)};
        cv.poly(p, 4);
    }
    cv.end();
}

void layerLine(Canvas& cv, const View& v){
    if (v.layer < 0 || v.layers <= 0){
        return;
    }
    char s[16];
    snprintf(s, sizeof(s), "L%d/%d", v.layer, v.layers);
    cv.begin(pal::mid);
    drawText(cv, s, 0.0f, 49.0f, kTiny, Align::Center);
    cv.end();
}

// Reticle: ring, four ticks, centre diamond; `turn` rotates the ticks
void reticle(Canvas& cv, Vec c, float r, Color ringC, Color tickC, float turn){
    cv.begin(ringC);
    cv.ring(c, r - 1.8f, r);
    cv.end();
    cv.begin(tickC);
    for (int i = 0; i < 4; i++){
        const float a = turn + i * 0.5f * kPi;
        const Vec d = {sinf(a), -cosf(a)};
        cv.line(V(c.x + d.x * (r - 6.0f), c.y + d.y * (r - 6.0f)),
                V(c.x + d.x * (r + 5.0f), c.y + d.y * (r + 5.0f)), 2.0f);
    }
    const Vec dia[4] = {V(c.x, c.y - 3.0f), V(c.x + 3.0f, c.y), V(c.x, c.y + 3.0f), V(c.x - 3.0f, c.y)};
    cv.poly(dia, 4);
    cv.end();
}

void chevrons(Canvas& cv, float x, float y, int dir, Color c){
    cv.begin(c);
    for (int i = 0; i < 2; i++){
        const float ox = x + dir * 5.0f * i;
        const Vec p[3] = {V(ox - dir * 2.0f, y - 4.0f), V(ox + dir * 2.0f, y), V(ox - dir * 2.0f, y + 4.0f)};
        cv.stroke(p, 3, 1.6f);
    }
    cv.end();
}

void textLine(Canvas& cv, const char* s, float baseline, const TextStyle& st, Color c){
    if (!s || !*s){
        return;
    }
    cv.begin(c);
    drawText(cv, s, 0.0f, baseline, st, Align::Center);
    cv.end();
}

// ---------------------------------------------------------------- screens

void splash(Canvas& cv, const View& v){
    fullRing(cv, pal::yellow);
    band(cv, "HM-01", pal::yellow);
    reticle(cv, V(0, -6.0f), 17.0f, pal::yellow, pal::text, 0.0f);
    chevrons(cv, -31.0f, -6.0f, -1, pal::yellow);
    chevrons(cv, 31.0f, -6.0f, 1, pal::yellow);
    textLine(cv, "HI MONITOR", 31.0f, kLineSm, pal::text);
    textLine(cv, v.version, 44.0f, kTiny, pal::mid);
}

void setup(Canvas& cv, const View& v){
    spinnerRing(cv, v.spin, 8, pal::cyan);
    band(cv, "SETUP", pal::cyan);
    textLine(cv, "JOIN WIFI", -16.0f, kLineSm, pal::mid);
    textLine(cv, v.line1, 0.0f, {10.0f, 2.0f, 2.0f}, pal::text);
    if (v.line2[0]){
        pair(cv, "PASS", pal::cyan, v.line2, pal::text, 15.0f, kValueSm);
    }
    textLine(cv, v.line3, 29.0f, kLineSm, pal::mid);
}

void linking(Canvas& cv, const View& v){
    spinnerRing(cv, v.spin, 8, pal::cyan);
    band(cv, "LINK", pal::cyan);
    reticle(cv, V(0, -12.0f), 12.0f, pal::dim, pal::cyan, v.spin * (2.0f * kPi / kRingSegments));
    textLine(cv, v.line1, 18.0f, kLine, pal::text);
    textLine(cv, v.line2, 31.0f, kLineSm, pal::mid);
    textLine(cv, v.line3, 42.0f, kTiny, pal::mid);
}

void offline(Canvas& cv, const View& v){
    cv.begin(pal::dim);
    for (int i = 0; i < kRingSegments; i++){
        segment(cv, i);
    }
    cv.end();
    if (v.blink & 1){
        cv.begin(pal::red);
        segment(cv, kRingSegments - 1);
        segment(cv, 0);
        cv.end();
    }
    band(cv, "OFFLINE", pal::red);
    reticle(cv, V(0, -12.0f), 12.0f, pal::dim, pal::red, 0.25f * kPi);
    textLine(cv, v.line1, 18.0f, kLine, pal::text);
    textLine(cv, v.line2, 31.0f, kLineSm, pal::mid);
    textLine(cv, v.line3, 42.0f, kTiny, pal::mid);
}

void idle(Canvas& cv, const View& v){
    quarterRing(cv, pal::yellow);
    band(cv, "READY", pal::yellow);
    textLine(cv, v.clock[0] ? v.clock : "--:--", 6.0f, kClock, pal::text);
    rule(cv, 12.5f, pal::yellow);

    char nz[12], bd[12];
    if (v.nozzle > -1000) snprintf(nz, sizeof(nz), "%d\xB0", v.nozzle); else snprintf(nz, sizeof(nz), "--");
    if (v.bed > -1000) snprintf(bd, sizeof(bd), "%d\xB0", v.bed); else snprintf(bd, sizeof(bd), "--");
    // Two label/value groups on one line
    const float gap = 3.0f, mid = 9.0f;
    const float w1 = textWidth("N", kLabel) + gap + textWidth(nz, kValueSm);
    const float w2 = textWidth("B", kLabel) + gap + textWidth(bd, kValueSm);
    float x = -0.5f * (w1 + mid + w2);
    const float base = 27.0f;
    cv.begin(pal::yellow);
    drawText(cv, "N", x, base, kLabel);
    drawText(cv, "B", x + w1 + mid, base, kLabel);
    cv.end();
    cv.begin(pal::text);
    drawText(cv, nz, x + textWidth("N", kLabel) + gap, base, kValueSm);
    drawText(cv, bd, x + w1 + mid + textWidth("B", kLabel) + gap, base, kValueSm);
    cv.end();
    textLine(cv, v.line1, 40.0f, kTiny, pal::mid);
}

// Printing, paused, done, stopped: the same layout with different colours
void job(Canvas& cv, const View& v){
    Color c = pal::yellow;
    const char* title = "PRINT";
    bool blinkTitle = false;
    switch (v.screen){
        case Screen::Paused:  c = pal::cyan;  title = "PAUSED"; blinkTitle = true; break;
        case Screen::Done:    c = pal::green; title = "DONE"; break;
        case Screen::Stopped: c = pal::red;   title = "STOP"; break;
        case Screen::Error:   c = pal::red;   title = "ERROR"; blinkTitle = true; break;
        default:              if (v.heating) title = "HEAT"; break;
    }
    const bool live = v.screen == Screen::Printing;
    const int pct = v.screen == Screen::Done ? 100 : v.percent;
    // Alert: ring and band flash white
    const bool flash = v.alert && (v.blink & 1);
    progressRing(cv, pct, flash ? pal::text : c, live, v.blink);
    band(cv, title, flash ? pal::text : c, v.alert || !blinkTitle || (v.blink & 1) || v.blink == 0);

    bigPercent(cv, pct, 4.0f);
    rule(cv, 10.5f, c);

    char a[12], b[12];
    if (v.screen == Screen::Printing || v.screen == Screen::Paused){
        // The finish time is what the user looks for: it gets the big line
        pair(cv, "ETA", c, v.eta[0] ? v.eta : "--:--", pal::text, 26.0f, kValue);
        // A code from the printer: ERR in red, in place of LEFT, where it
        // stopped the job (a pause). While printing it is only a notice
        // (the Hi reports 500, an AI detection prompt, through whole normal
        // prints): CODE in grey under the band, and LEFT stays.
        if (v.errorCode != 0 && v.screen == Screen::Paused){
            snprintf(b, sizeof(b), "%d", v.errorCode);
            pair(cv, "ERR", pal::red, b, pal::text, 39.0f, kValueSm);
        } else {
            if (v.remainingS >= 0) formatDuration(v.remainingS, a, sizeof(a)); else snprintf(a, sizeof(a), "-:--");
            pair(cv, "LEFT", c, a, pal::text, 39.0f, kValueSm);
            if (v.errorCode != 0){
                snprintf(b, sizeof(b), "CODE %d", v.errorCode);
                textLine(cv, b, -27.5f, kTiny, pal::mid);
            }
        }
    } else {
        if (v.elapsedS >= 0) formatDuration(v.elapsedS, a, sizeof(a)); else snprintf(a, sizeof(a), "-:--");
        pair(cv, "TIME", c, a, pal::text, 26.0f, kValue);
        if (v.screen == Screen::Error && v.errorCode != 0){
            snprintf(b, sizeof(b), "%d", v.errorCode);
            pair(cv, "CODE", pal::red, b, pal::text, 39.0f, kValueSm);
        } else if (v.eta[0]){
            pair(cv, "AT", c, v.eta, pal::text, 39.0f, kValueSm);
        }
    }
    layerLine(cv, v);
}

}  // namespace

const char* screenName(Screen s){
    switch (s){
        case Screen::Splash:   return "splash";
        case Screen::Setup:    return "setup";
        case Screen::Linking:  return "linking";
        case Screen::Offline:  return "offline";
        case Screen::Idle:     return "idle";
        case Screen::Printing: return "printing";
        case Screen::Paused:   return "paused";
        case Screen::Done:     return "done";
        case Screen::Stopped:  return "stopped";
        case Screen::Error:    return "error";
    }
    return "?";
}

bool View::operator==(const View& o) const {
    return screen == o.screen && percent == o.percent && remainingS == o.remainingS
        && elapsedS == o.elapsedS && strcmp(eta, o.eta) == 0 && strcmp(clock, o.clock) == 0
        && layer == o.layer && layers == o.layers && nozzle == o.nozzle && bed == o.bed
        && heating == o.heating && errorCode == o.errorCode
        && strcmp(line1, o.line1) == 0 && strcmp(line2, o.line2) == 0 && strcmp(line3, o.line3) == 0
        && strcmp(version, o.version) == 0 && alert == o.alert && blink == o.blink && spin == o.spin
        && page == o.page && d == o.d;
}

bool Details::operator==(const Details& o) const {
    return strcmp(job, o.job) == 0 && strcmp(material, o.material) == 0 && sliceS == o.sliceS
        && totalS == o.totalS && strcmp(start, o.start) == 0
        && nozzleTarget == o.nozzleTarget && bedTarget == o.bedTarget
        && nozzleMax == o.nozzleMax && bedMax == o.bedMax
        && chamber == o.chamber && chamberTarget == o.chamberTarget
        && speed == o.speed && flow10 == o.flow10 && feedPct == o.feedPct && flowPct == o.flowPct
        && fanPart == o.fanPart && fanAux == o.fanAux && fanCase == o.fanCase
        && z100 == o.z100 && usedDm == o.usedDm && filePct == o.filePct
        && strcmp(printer, o.printer) == 0 && strcmp(model, o.model) == 0 && strcmp(ip, o.ip) == 0
        && wifiBars == o.wifiBars && battery == o.battery && charging == o.charging;
}

bool animates(const View& v){
    if (v.alert){
        return true;
    }
    switch (v.screen){
        case Screen::Setup:
        case Screen::Linking:
        case Screen::Offline:
        case Screen::Printing:
        case Screen::Paused:
        case Screen::Error:
            return true;
        default:
            return false;
    }
}

void formatDuration(int32_t seconds, char* out, int n){
    if (seconds < 0){
        snprintf(out, n, "-:--");
        return;
    }
    // Round to the minute, so the display does not show 0:00 with a minute left
    int32_t minutes = (seconds + 30) / 60;
    int32_t h = minutes / 60;
    int32_t m = minutes % 60;
    if (h > 99){
        h = 99;
        m = 59;
    }
    snprintf(out, n, "%d:%02d", static_cast<int>(h), static_cast<int>(m));
}

void formatClock(int hour, int minute, char* out, int n){
    snprintf(out, n, "%02d:%02d", hour, minute);
}

void drawScreen(Canvas& cv, const View& v, float angle){
    cv.clear(pal::bezel);
    drawBezel(cv);

    const float c = kScreenSize * 0.5f;
    cv.setXform(Xform::rotate(c, c, angle));
    face(cv);
    switch (v.screen){
        case Screen::Splash:   splash(cv, v); break;
        case Screen::Setup:    setup(cv, v); break;
        case Screen::Linking:  linking(cv, v); break;
        case Screen::Offline:  offline(cv, v); break;
        case Screen::Idle:     idle(cv, v); break;
        case Screen::Printing:
        case Screen::Paused:
        case Screen::Done:
        case Screen::Stopped:
        case Screen::Error:    job(cv, v); break;
    }
    cv.setXform(Xform::identity());
}

}  // namespace hm
