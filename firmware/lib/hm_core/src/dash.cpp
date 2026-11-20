#include "dash.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mechfont.h"

namespace hm {

namespace {

constexpr float kPi  = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;
constexpr float kSegGap = 2.0f;   // degrees between ring segments

// Text styles: cap height, stroke, spacing (pixels)
const TextStyle kBandT = {10.0f, 2.0f, 1.6f};
const TextStyle kBig   = {28.0f, 4.4f, 3.0f};
const TextStyle kVal   = {14.0f, 2.5f, 2.0f};
const TextStyle kValS  = {10.0f, 1.9f, 1.5f};
const TextStyle kSmall = {8.0f, 1.6f, 1.3f};
const TextStyle kLab   = {7.0f, 1.4f, 1.2f};
const TextStyle kTileL = {6.0f, 1.3f, 1.1f};   // tile labels: two share a 75 px tile
const TextStyle kClk   = {13.0f, 2.3f, 1.8f};

struct Box {
    float x0, y0, x1, y1;
};

// Where everything goes, for each orientation
struct Geo {
    bool      portrait;
    float     w, h;
    float     bandX1;                    // the title band's top-right corner
    bool      codeInHeader;              // the printer's code: header, or the bottom line
    // The gauge of the job screens
    float     gx, gy, face, rim, r0, r1;
    TextStyle huge, hugeU;
    float     progDy, pctDy, ruleDy, layerDy;   // text inside the ring, from its centre
    Box       panel;                     // the job's panel
    Box       side;                      // portrait: the block right of the gauge
    float     tileX[4], tileY[4], tileW, tileH;
    float     lineA, lineB;              // bottom lines; lineA < 0: none
    // The screens without a job
    float     infoGX, infoGY;            // their gauge centre
    Box       info, hint;
    float     hint1, hint2;              // hint baselines, from the hint box top
    float     foot;                      // footer baseline
};

const Geo kLand = {
    false, 320.0f, 240.0f, 112.0f, true,
    72.0f, 97.0f, 66.0f, 64.5f, 54.0f, 62.0f,
    {36.0f, 5.4f, 3.6f}, {15.0f, 2.3f, -1.0f},
    -32.0f, 15.0f, 23.5f, 39.0f,
    {148.0f, 30.0f, 316.0f, 164.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {4.0f, 83.0f, 162.0f, 241.0f}, {168.0f, 168.0f, 168.0f, 168.0f}, 75.0f, 44.0f,
    225.0f, 237.0f,
    72.0f, 97.0f,
    {148.0f, 30.0f, 316.0f, 164.0f}, {4.0f, 168.0f, 316.0f, 212.0f},
    19.0f, 34.0f,
    237.0f,
};

const Geo kPort = {
    true, 240.0f, 320.0f, 84.0f, false,
    62.0f, 89.0f, 57.0f, 55.5f, 46.0f, 53.0f,
    {30.0f, 4.6f, 3.0f}, {13.0f, 2.0f, -1.0f},
    -27.0f, 13.0f, 20.5f, 34.0f,
    {4.0f, 152.0f, 236.0f, 226.0f},
    {128.0f, 30.0f, 236.0f, 149.0f},
    {4.0f, 122.0f, 4.0f, 122.0f}, {230.0f, 230.0f, 270.0f, 270.0f}, 114.0f, 36.0f,
    -1.0f, 317.0f,
    120.0f, 89.0f,
    {4.0f, 154.0f, 236.0f, 272.0f}, {4.0f, 276.0f, 236.0f, 308.0f},
    14.0f, 27.0f,
    317.0f,
};

const Geo* G = &kLand;              // the layout of the frame being drawn
float      s_gx = 0.0f, s_gy = 0.0f;   // the gauge centre of the screen being drawn

Vec V(float x, float y){ return {x, y}; }

struct Look {
    Color       c;            // the state's accent
    const char* title;
    bool        blinkTitle;
};

Look lookOf(const View& v){
    switch (v.screen){
        case Screen::Splash:   return {pal::yellow, "HM-01", false};
        case Screen::Setup:    return {pal::cyan, "SETUP", false};
        case Screen::Linking:  return {pal::cyan, "LINK", false};
        case Screen::Offline:  return {pal::red, "OFFLINE", false};
        case Screen::Idle:     return {pal::yellow, "READY", false};
        case Screen::Printing: return {pal::yellow, v.heating ? "HEAT" : "PRINT", false};
        case Screen::Paused:   return {pal::cyan, "PAUSED", true};
        case Screen::Done:     return {pal::green, "DONE", false};
        case Screen::Stopped:  return {pal::red, "STOP", false};
        case Screen::Error:    return {pal::red, "ERROR", true};
    }
    return {pal::yellow, "HM-01", false};
}

bool ended(Screen s){
    return s == Screen::Done || s == Screen::Stopped || s == Screen::Error;
}

// ---------------------------------------------------------------- text

void text(Canvas& cv, const char* s, float x, float y, const TextStyle& st, Color c,
          Align a = Align::Left){
    if (!s || !*s){
        return;
    }
    cv.begin(c);
    drawText(cv, s, x, y, st, a);
    cv.end();
}

// s cut to fit in maxW
void fit(const char* s, const TextStyle& st, float maxW, char* out, int n){
    snprintf(out, n, "%s", s ? s : "");
    int len = static_cast<int>(strlen(out));
    while (len > 0 && textWidth(out, st) > maxW){
        out[--len] = 0;
    }
    while (len > 0 && out[len - 1] == ' '){
        out[--len] = 0;
    }
}

// A small label over a value
void field(Canvas& cv, float x, float labelBase, float valueBase, const char* label, Color lc,
           const char* value, const TextStyle& vs, Align a = Align::Left){
    text(cv, label, x, labelBase, kLab, lc, a);
    text(cv, value, x, valueBase, vs, pal::text, a);
}

void duration(int32_t s, char* out, int n){
    formatDuration(s, out, n);
}

void orDash(const char* s, char* out, int n, const char* none = "--"){
    snprintf(out, n, "%s", s && *s ? s : none);
}

void pct(int p, char* out, int n){
    if (p < 0) snprintf(out, n, "--"); else snprintf(out, n, "%d%%", p);
}

void temp(int t, char* out, int n){
    if (t <= -1000) snprintf(out, n, "--"); else snprintf(out, n, "%d\xB0", t);
}

void metres(int dm, char* out, int n){
    if (dm >= 0) snprintf(out, n, "%d.%d M", dm / 10, dm % 10); else snprintf(out, n, "--");
}

// ---------------------------------------------------------------- shapes

// Panel with its top-left and bottom-right corners cut at 45 degrees
void panel(Canvas& cv, float x0, float y0, float x1, float y1, float cut, Color accent){
    const Vec p[6] = {V(x0 + cut, y0), V(x1, y0), V(x1, y1 - cut), V(x1 - cut, y1), V(x0, y1), V(x0, y0 + cut)};
    cv.begin(pal::face);
    cv.poly(p, 6);
    cv.end();
    // Outline, inset half a pixel so it stays on the panel
    const float h = 0.5f;
    const Vec o[7] = {V(x0 + cut, y0 + h), V(x1 - h, y0 + h), V(x1 - h, y1 - cut), V(x1 - cut, y1 - h),
                      V(x0 + h, y1 - h), V(x0 + h, y0 + cut), V(x0 + cut, y0 + h)};
    cv.begin(pal::dim);
    cv.stroke(o, 7, 1.0f);
    cv.end();
    // An accent tick along the top edge, from the chamfer, and a block in
    // the bottom-right chamfer
    cv.begin(accent);
    cv.rect(x0 + cut + 2.0f, y0, 14.0f, 2.0f);
    const Vec b[3] = {V(x1, y1 - cut + 3.0f), V(x1, y1), V(x1 - cut + 3.0f, y1)};
    cv.poly(b, 3);
    cv.end();
}

void panel(Canvas& cv, const Box& b, float cut, Color accent){
    panel(cv, b.x0, b.y0, b.x1, b.y1, cut, accent);
}

// An L of two arms from the corner (x, y), pointing along dx / dy (+-1)
void bracket(Canvas& cv, float x, float y, float dx, float dy, float len, float w){
    const Vec p[3] = {V(x + dx * len, y), V(x, y), V(x, y + dy * len)};
    cv.stroke(p, 3, w);
}

// Three slanted speed dashes (the house rule's left end), 12 px long
void dashes(Canvas& cv, float x, float y){
    for (int i = 0; i < 3; i++){
        const float ox = x + 4.0f * i;
        const Vec p[4] = {V(ox, y + 1.0f), V(ox + 2.0f, y - 1.0f), V(ox + 3.5f, y - 1.0f), V(ox + 1.5f, y + 1.0f)};
        cv.poly(p, 4);
    }
}

void rule(Canvas& cv, float x0, float x1, float y, Color accent){
    cv.begin(pal::dim);
    cv.line(V(x0 + 15.0f, y), V(x1, y), 1.0f);
    cv.end();
    cv.begin(accent);
    dashes(cv, x0, y);
    cv.end();
}

// A bar of n slanted segments; frac of them lit
void segBar(Canvas& cv, float x0, float x1, float y, float h, int n, float frac, Color c){
    const float gap = 1.5f, slant = 0.5f * h;
    const float pitch = (x1 - x0 - slant + gap) / n;
    int lit = frac <= 0.0f ? 0 : static_cast<int>(frac * n + 0.5f);
    if (lit > n) lit = n;
    if (frac > 0.0f && lit == 0) lit = 1;
    for (int pass = 0; pass < 2; pass++){
        cv.begin(pass == 0 ? pal::dim : c);
        for (int i = pass == 0 ? lit : 0; i < (pass == 0 ? n : lit); i++){
            const float sx = x0 + i * pitch;
            const Vec p[4] = {V(sx + slant, y), V(sx + slant + pitch - gap, y),
                              V(sx + pitch - gap, y + h), V(sx, y + h)};
            cv.poly(p, 4);
        }
        cv.end();
    }
}

// Reticle: ring, four ticks turned by `turn`, centre diamond
void reticle(Canvas& cv, Vec c, float r, Color ringC, Color tickC, float turn){
    cv.begin(ringC);
    cv.ring(c, r - 2.0f, r);
    cv.end();
    cv.begin(tickC);
    for (int i = 0; i < 4; i++){
        const float a = turn + i * 0.5f * kPi;
        const Vec d = {sinf(a), -cosf(a)};
        cv.line(V(c.x + d.x * (r - 7.0f), c.y + d.y * (r - 7.0f)),
                V(c.x + d.x * (r + 6.0f), c.y + d.y * (r + 6.0f)), 2.4f);
    }
    const Vec dia[4] = {V(c.x, c.y - 3.5f), V(c.x + 3.5f, c.y), V(c.x, c.y + 3.5f), V(c.x - 3.5f, c.y)};
    cv.poly(dia, 4);
    cv.end();
}

void chevrons(Canvas& cv, float x, float y, int dir, Color c){
    cv.begin(c);
    for (int i = 0; i < 2; i++){
        const float ox = x + dir * 6.0f * i;
        const Vec p[3] = {V(ox - dir * 2.5f, y - 5.0f), V(ox + dir * 2.5f, y), V(ox - dir * 2.5f, y + 5.0f)};
        cv.stroke(p, 3, 2.0f);
    }
    cv.end();
}

// Battery, 22 px wide: a chamfered outline, four slanted cells, the
// terminal at the right; a bolt at its left while charging. Low (<= 15 %)
// shows in red.
void batteryIcon(Canvas& cv, float x, float base, int level, bool charging, Color accent){
    const float w = 20.0f, h = 10.0f, y0 = base - h;
    cv.begin(pal::mid);
    const Vec o[6] = {V(x + 3.0f, y0 + 0.5f), V(x + w - 0.5f, y0 + 0.5f), V(x + w - 0.5f, base - 0.5f),
                      V(x + 0.5f, base - 0.5f), V(x + 0.5f, y0 + 3.0f), V(x + 3.0f, y0 + 0.5f)};
    cv.stroke(o, 6, 1.0f);
    cv.rect(x + w, y0 + 3.0f, 2.0f, h - 6.0f);
    cv.end();
    segBar(cv, x + 2.5f, x + w - 2.5f, y0 + 2.5f, h - 5.0f, 4, level / 100.0f, level <= 15 ? pal::red : accent);
    if (charging){
        const float bx = x - 5.0f;
        cv.begin(accent);
        const Vec b1[3] = {V(bx + 1.5f, y0), V(bx - 2.0f, y0 + 5.5f), V(bx + 0.5f, y0 + 5.5f)};
        const Vec b2[3] = {V(bx - 0.5f, y0 + 4.5f), V(bx + 2.0f, y0 + 4.5f), V(bx - 1.5f, base)};
        cv.poly(b1, 3);
        cv.poly(b2, 3);
        cv.end();
    }
}

void wifiIcon(Canvas& cv, float x, float base, int bars, Color on){
    for (int pass = 0; pass < 2; pass++){
        cv.begin(pass == 0 ? pal::dim : on);
        for (int i = 0; i < 4; i++){
            const bool lit = i < bars;
            if (lit != (pass == 1)){
                continue;
            }
            const float h = 4.0f + 3.0f * i;
            cv.rect(x + 5.0f * i, base - h, 3.0f, h);
        }
        cv.end();
    }
}

// "ERR 2839" in red where the job stopped on it (an error, a pause);
// otherwise "CODE 500" in grey, a notice: the Hi reports 500 (an AI
// detection prompt) through whole normal prints. Right-aligned at x.
void codeTag(Canvas& cv, const View& v, float x, float base){
    const bool stop = v.screen == Screen::Error || v.screen == Screen::Paused;
    char code[12];
    snprintf(code, sizeof(code), "%d", v.errorCode);
    text(cv, code, x, base, kLab, stop ? pal::text : pal::mid, Align::Right);
    text(cv, stop ? "ERR" : "CODE", x - textWidth(code, kLab) - 4.0f, base, kLab,
         stop ? pal::red : pal::mid, Align::Right);
}

float codeTagWidth(const View& v){
    char code[12];
    snprintf(code, sizeof(code), "%d", v.errorCode);
    const bool stop = v.screen == Screen::Error || v.screen == Screen::Paused;
    return textWidth(code, kLab) + 4.0f + textWidth(stop ? "ERR" : "CODE", kLab);
}

// ---------------------------------------------------------------- header

// Title band from the left edge: the label knocked out, then hazard
// stripes parallel to the 45-degree end, as many as fit
void band(Canvas& cv, const char* label, Color c, bool show){
    if (!show){
        return;
    }
    const float y0 = 3.0f, y1 = 21.0f, h = y1 - y0, x1 = G->bandX1;
    cv.begin(c);
    const Vec p[4] = {V(0.0f, y0), V(x1, y0), V(x1 + h, y1), V(0.0f, y1)};
    cv.poly(p, 4);
    cv.end();

    cv.begin(pal::bezel);
    const float base = y0 + 0.5f * (h - kBandT.cap) + kBandT.cap;
    drawText(cv, label, 10.0f, base, kBandT);
    const float textEnd = 10.0f + textWidth(label, kBandT);
    for (int i = 0; i < 16; i++){
        const float in0 = 6.0f + 7.0f * i;
        const float in1 = in0 + 3.5f;
        const float xt0 = x1 - in0, xt1 = x1 - in1;   // at the top edge
        if (xt1 < textEnd + 9.0f){
            break;
        }
        const Vec s[4] = {V(xt1, y0), V(xt0, y0), V(xt0 + h, y1), V(xt1 + h, y1)};
        cv.poly(s, 4);
    }
    cv.end();
}

void header(Canvas& cv, const View& v, const Look& lk){
    const bool flash = v.alert && (v.blink & 1);
    band(cv, lk.title, flash ? pal::text : lk.c, v.alert || !lk.blinkTitle || (v.blink & 1) || v.blink == 0);

    // From the right: clock, Wi-Fi bars, battery, then the printer's code
    const Color icon = lk.c == pal::red ? pal::text : lk.c;
    float x = G->w - 6.0f;
    if (v.clock[0]){
        text(cv, v.clock, x, 18.0f, kClk, pal::text, Align::Right);
        x -= textWidth(v.clock, kClk) + 9.0f;
    }
    if (v.screen != Screen::Splash){
        wifiIcon(cv, x - 18.0f, 18.0f, v.d.wifiBars, icon);
        x -= 18.0f + 9.0f;
    }
    if (v.d.battery >= 0){
        char b[8];
        snprintf(b, sizeof(b), "%d%%", v.d.battery);
        text(cv, b, x, 17.0f, kLab, v.d.battery <= 15 ? pal::red : pal::text, Align::Right);
        x -= textWidth(b, kLab) + 4.0f + 22.0f;
        batteryIcon(cv, x, 17.0f, v.d.battery, v.d.charging, icon);
        x -= (v.d.charging ? 8.0f : 0.0f) + 9.0f;
    }
    if (G->codeInHeader && v.errorCode != 0 && v.screen != Screen::Idle){
        codeTag(cv, v, x, 17.0f);
    }

    rule(cv, 4.0f, G->w - 4.0f, 25.5f, lk.c);
}

// ---------------------------------------------------------------- gauge

void seg(Canvas& cv, int i){
    const float pitch = 360.0f / kRingSegments;
    const float a0 = (i * pitch + 0.5f * kSegGap) * kDeg;
    const float a1 = ((i + 1) * pitch - 0.5f * kSegGap) * kDeg;
    cv.sector(V(s_gx, s_gy), G->r0, G->r1, a0, a1);
}

void gaugeFace(Canvas& cv, Color accent){
    cv.begin(pal::face);
    cv.disc(V(s_gx, s_gy), G->face);
    cv.end();
    cv.begin(pal::dim);
    cv.ring(V(s_gx, s_gy), G->rim, G->face);
    cv.end();
    // Brackets at the corners of the gauge's box
    const float e = G->face + 3.0f;
    cv.begin(accent);
    bracket(cv, s_gx - e, s_gy - e, 1, 1, 9.0f, 2.0f);
    bracket(cv, s_gx + e, s_gy - e, -1, 1, 9.0f, 2.0f);
    bracket(cv, s_gx - e, s_gy + e, 1, -1, 9.0f, 2.0f);
    bracket(cv, s_gx + e, s_gy + e, -1, -1, 9.0f, 2.0f);
    cv.end();
}

void progressRing(Canvas& cv, int percent, Color c, bool blinkNext, uint8_t blink){
    int lit = percent < 0 ? 0 : percent * kRingSegments / 100;
    if (lit > kRingSegments) lit = kRingSegments;
    cv.begin(pal::dim);
    for (int i = lit; i < kRingSegments; i++) seg(cv, i);
    cv.end();
    if (lit > 0){
        cv.begin(c);
        for (int i = 0; i < lit; i++) seg(cv, i);
        cv.end();
    }
    if (blinkNext && lit < kRingSegments && (blink & 1)){
        cv.begin(c, 150);
        seg(cv, lit);
        cv.end();
    }
}

void spinnerRing(Canvas& cv, int head, int len, Color c){
    cv.begin(pal::dim);
    for (int i = 0; i < kRingSegments; i++){
        if ((head - i + kRingSegments) % kRingSegments >= len) seg(cv, i);
    }
    cv.end();
    for (int k = 0; k < len; k++){
        cv.begin(c, static_cast<uint8_t>(255 - k * (200 / len)));
        seg(cv, (head - k + kRingSegments) % kRingSegments);
        cv.end();
    }
}

void plainRing(Canvas& cv, Color c, int every, Color marks){
    cv.begin(c);
    for (int i = 0; i < kRingSegments; i++){
        if (every <= 0 || i % every != 0) seg(cv, i);
    }
    cv.end();
    if (every > 0){
        cv.begin(marks);
        for (int i = 0; i < kRingSegments; i += every) seg(cv, i);
        cv.end();
    }
}

void bigPercent(Canvas& cv, int percent, float baseline, Color unit){
    char num[8];
    if (percent < 0) snprintf(num, sizeof(num), "--");
    else snprintf(num, sizeof(num), "%d", percent > 100 ? 100 : percent);
    const float gap = 3.0f;
    const float nw = textWidth(num, G->huge);
    const float uw = textWidth("%", G->hugeU);
    const float x0 = s_gx - 0.5f * (nw + gap + uw);
    text(cv, num, x0, baseline, G->huge, pal::text);
    text(cv, "%", x0 + nw + gap, baseline - G->huge.cap + G->hugeU.cap, G->hugeU, unit);
}

void jobGauge(Canvas& cv, const View& v, const Look& lk){
    s_gx = G->gx;
    s_gy = G->gy;
    gaugeFace(cv, lk.c);
    const bool flash = v.alert && (v.blink & 1);
    const int p = v.screen == Screen::Done ? 100 : v.percent;
    progressRing(cv, p, flash ? pal::text : lk.c, v.screen == Screen::Printing, v.blink);
    text(cv, ended(v.screen) ? "FINAL" : "PROGRESS", s_gx, s_gy + G->progDy, kLab, pal::mid, Align::Center);
    bigPercent(cv, p, s_gy + G->pctDy, lk.c);
    const float ry = s_gy + G->ruleDy;
    const float half = G->r0 - 28.0f;   // 26 px in landscape
    cv.begin(pal::dim);
    cv.line(V(s_gx - half, ry), V(s_gx + half + 4.0f, ry), 1.0f);
    cv.end();
    cv.begin(lk.c);
    dashes(cv, s_gx - half - 14.0f, ry);
    cv.end();
    if (v.layers > 0){
        char s[16];
        if (v.layer >= 0) snprintf(s, sizeof(s), "L%d/%d", v.layer, v.layers);
        else snprintf(s, sizeof(s), "%d LAYERS", v.layers);
        text(cv, s, s_gx, s_gy + G->layerDy, kSmall, pal::mid, Align::Center);
    }
}

// ---------------------------------------------------------------- job panel

float layerFrac(const View& v){
    if (ended(v.screen)){
        const int p = v.screen == Screen::Done ? 100 : v.percent;
        return p < 0 ? 0.0f : p / 100.0f;
    }
    return (v.layers > 0 && v.layer > 0) ? static_cast<float>(v.layer) / v.layers : 0.0f;
}

// Landscape: everything right of the gauge
void jobPanel(Canvas& cv, const View& v, const Look& lk){
    const Box& b = G->panel;
    panel(cv, b, 10.0f, lk.c);
    const float x0 = b.x0 + 10.0f, x1 = b.x1 - 8.0f;
    const bool end = ended(v.screen);
    const Details& d = v.d;
    char a[24], bb[24], c[24];

    // The finish time, big; the time left (or the print time) at the right
    text(cv, end ? "ENDED" : "ETA", x0, 45.0f, kSmall, lk.c);
    text(cv, v.eta[0] ? v.eta : "--:--", x0, 77.0f, kBig, pal::text);
    duration(end ? v.elapsedS : v.remainingS, a, sizeof(a));
    field(cv, x1, 45.0f, 77.0f, end ? "TIME" : "LEFT", lk.c, a, kVal, Align::Right);

    rule(cv, x0, x1, 87.5f, lk.c);

    // Two rows of three fields
    const float cx[3] = {x0, x0 + 52.0f, x0 + 104.0f};
    if (!end){
        duration(v.elapsedS, a, sizeof(a));
        duration(d.totalS, bb, sizeof(bb));
        orDash(d.start, c, sizeof(c), "--:--");
        field(cv, cx[0], 101.0f, 115.0f, "TIME", pal::mid, a, kValS);
        field(cv, cx[1], 101.0f, 115.0f, "TOTAL", pal::mid, bb, kValS);
        field(cv, cx[2], 101.0f, 115.0f, "START", pal::mid, c, kValS);

        if (v.layers > 0 && v.layer >= 0) snprintf(a, sizeof(a), "%d/%d", v.layer, v.layers);
        else snprintf(a, sizeof(a), "--");
        if (d.z100 >= 0) snprintf(bb, sizeof(bb), "%d.%02d", d.z100 / 100, d.z100 % 100);
        else snprintf(bb, sizeof(bb), "--");
        pct(d.filePct, c, sizeof(c));
        field(cv, cx[0], 129.0f, 143.0f, "LAYER", pal::mid, a, kValS);
        field(cv, cx[1], 129.0f, 143.0f, "Z MM", pal::mid, bb, kValS);
        field(cv, cx[2], 129.0f, 143.0f, "FILE", pal::mid, c, kValS);
    } else {
        orDash(d.start, a, sizeof(a), "--:--");
        duration(d.sliceS, bb, sizeof(bb));
        metres(d.usedDm, c, sizeof(c));
        field(cv, cx[0], 101.0f, 115.0f, "START", pal::mid, a, kValS);
        field(cv, cx[1], 101.0f, 115.0f, "SLICER", pal::mid, bb, kValS);
        field(cv, cx[2], 101.0f, 115.0f, "FILAMENT", pal::mid, c, kValS);

        if (v.layers > 0) snprintf(a, sizeof(a), "%d", v.layers); else snprintf(a, sizeof(a), "--");
        orDash(d.material, bb, sizeof(bb));
        if (v.screen == Screen::Error && v.errorCode != 0) snprintf(c, sizeof(c), "%d", v.errorCode);
        else snprintf(c, sizeof(c), "--");
        field(cv, cx[0], 129.0f, 143.0f, "LAYERS", pal::mid, a, kValS);
        field(cv, cx[1], 129.0f, 143.0f, "MATERIAL", pal::mid, bb, kValS);
        field(cv, cx[2], 129.0f, 143.0f, "CODE", v.screen == Screen::Error ? pal::red : pal::mid, c, kValS);
    }
    segBar(cv, x0, x1, 151.0f, 6.0f, 30, layerFrac(v), lk.c);
}

// Portrait: the block right of the gauge, then the panel under both
void jobSide(Canvas& cv, const View& v, const Look& lk){
    const Box& b = G->side;
    const float x0 = b.x0, x1 = b.x1, x2 = b.x0 + 58.0f;
    const bool end = ended(v.screen);
    const Details& d = v.d;
    char a[24], c[24];

    text(cv, end ? "ENDED" : "ETA", x0, 44.0f, kSmall, lk.c);
    text(cv, v.eta[0] ? v.eta : "--:--", x0, 76.0f, kBig, pal::text);
    rule(cv, x0, x1, 87.5f, lk.c);
    if (!end){
        duration(v.remainingS, a, sizeof(a));
        field(cv, x0, 101.0f, 118.0f, "LEFT", lk.c, a, kVal);
        duration(d.totalS, a, sizeof(a));
        field(cv, x2, 101.0f, 118.0f, "TOTAL", pal::mid, a, kValS);
        orDash(d.start, a, sizeof(a), "--:--");
        field(cv, x0, 132.0f, 146.0f, "START", pal::mid, a, kValS);
        pct(d.filePct, c, sizeof(c));
        field(cv, x2, 132.0f, 146.0f, "FILE", pal::mid, c, kValS);
    } else {
        duration(v.elapsedS, a, sizeof(a));
        field(cv, x0, 101.0f, 118.0f, "TIME", lk.c, a, kVal);
        orDash(d.start, a, sizeof(a), "--:--");
        field(cv, x2, 101.0f, 118.0f, "START", pal::mid, a, kValS);
        duration(d.sliceS, a, sizeof(a));
        field(cv, x0, 132.0f, 146.0f, "SLICER", pal::mid, a, kValS);
        metres(d.usedDm, c, sizeof(c));
        field(cv, x2, 132.0f, 146.0f, "FILAMENT", pal::mid, c, kValS);
    }
}

void jobPanelPortrait(Canvas& cv, const View& v, const Look& lk){
    const Box& b = G->panel;
    panel(cv, b, 9.0f, lk.c);
    const float cx[3] = {b.x0 + 10.0f, b.x0 + 86.0f, b.x0 + 162.0f};
    const float l1 = b.y0 + 16.0f, v1 = b.y0 + 30.0f, l2 = b.y0 + 44.0f, v2 = b.y0 + 58.0f;
    const Details& d = v.d;
    char a[24], bb[24], c[24];
    if (!ended(v.screen)){
        duration(v.elapsedS, a, sizeof(a));
        if (v.layers > 0 && v.layer >= 0) snprintf(bb, sizeof(bb), "%d/%d", v.layer, v.layers);
        else snprintf(bb, sizeof(bb), "--");
        if (d.z100 >= 0) snprintf(c, sizeof(c), "%d.%02d", d.z100 / 100, d.z100 % 100);
        else snprintf(c, sizeof(c), "--");
        field(cv, cx[0], l1, v1, "TIME", pal::mid, a, kValS);
        field(cv, cx[1], l1, v1, "LAYER", pal::mid, bb, kValS);
        field(cv, cx[2], l1, v1, "Z MM", pal::mid, c, kValS);

        if (d.fanPart >= 0) snprintf(a, sizeof(a), "%d/%d/%d", d.fanPart, d.fanAux < 0 ? 0 : d.fanAux,
                                     d.fanCase < 0 ? 0 : d.fanCase);
        else snprintf(a, sizeof(a), "--");
        metres(d.usedDm, bb, sizeof(bb));
        duration(d.sliceS, c, sizeof(c));
        field(cv, cx[0], l2, v2, "FANS %", pal::mid, a, kValS);
        field(cv, cx[1], l2, v2, "FILAMENT", pal::mid, bb, kValS);
        field(cv, cx[2], l2, v2, "SLICER", pal::mid, c, kValS);
    } else {
        if (v.layers > 0) snprintf(a, sizeof(a), "%d", v.layers); else snprintf(a, sizeof(a), "--");
        orDash(d.material, bb, sizeof(bb));
        if (v.screen == Screen::Error && v.errorCode != 0) snprintf(c, sizeof(c), "%d", v.errorCode);
        else snprintf(c, sizeof(c), "--");
        field(cv, cx[0], l1, v1, "LAYERS", pal::mid, a, kValS);
        field(cv, cx[1], l1, v1, "MATERIAL", pal::mid, bb, kValS);
        field(cv, cx[2], l1, v1, "CODE", v.screen == Screen::Error ? pal::red : pal::mid, c, kValS);
        duration(d.totalS, a, sizeof(a));
        field(cv, cx[0], l2, v2, "TOTAL", pal::mid, a, kValS);
    }
    segBar(cv, b.x0 + 10.0f, b.x1 - 8.0f, b.y1 - 11.0f, 5.0f, 26, layerFrac(v), lk.c);
}

// ---------------------------------------------------------------- tiles

// Offsets inside a tile: the full height (44) or the compact one
struct TileRows {
    float label, value, bar, barH, dashY;
};

TileRows tileRows(float h){
    if (h >= 42.0f) return {13.0f, 31.0f, 35.0f, 4.0f, 37.5f};
    return {11.0f, 27.0f, 29.0f, 3.0f, 31.5f};
}

void tileTemp(Canvas& cv, float x0, float y0, float x1, float h, const char* label, int now, int target,
              int max, Color accent){
    panel(cv, x0, y0, x1, y0 + h, 7.0f, accent);
    const TileRows r = tileRows(h);
    char a[12], b[12];
    temp(now, a, sizeof(a));
    text(cv, label, x0 + 8.0f, y0 + r.label, kTileL, pal::mid);
    text(cv, a, x0 + 8.0f, y0 + r.value, kVal, pal::text);
    // The target at the right of the label: "/220", or OFF when not heating
    if (target > 0) snprintf(b, sizeof(b), "/%d", target);
    else snprintf(b, sizeof(b), target == 0 ? "OFF" : "");
    text(cv, b, x1 - 6.0f, y0 + r.label, kTileL, target > 0 ? pal::text : pal::mid, Align::Right);
    const float span = static_cast<float>(max > 0 ? max : 300);
    segBar(cv, x0 + 8.0f, x1 - 6.0f, y0 + r.bar, r.barH, 10, now > -1000 ? now / span : 0.0f, accent);
    // The target, as a tick under the bar
    if (target > 0){
        const float tx = x0 + 8.0f + (x1 - x0 - 14.0f) * (target > span ? 1.0f : target / span);
        const float ty = y0 + r.bar + r.barH;
        cv.begin(pal::text);
        const Vec t[3] = {V(tx, ty), V(tx + 2.5f, ty + 3.5f), V(tx - 2.5f, ty + 3.5f)};
        cv.poly(t, 3);
        cv.end();
    }
}

void tileRate(Canvas& cv, float x0, float y0, float x1, float h, const char* label, const char* value,
              const char* unit, int overridePct, Color accent){
    panel(cv, x0, y0, x1, y0 + h, 7.0f, accent);
    const TileRows r = tileRows(h);
    char p[12];
    text(cv, label, x0 + 8.0f, y0 + r.label, kTileL, pal::mid);
    if (overridePct >= 0){
        snprintf(p, sizeof(p), "%d%%", overridePct);
        text(cv, p, x1 - 6.0f, y0 + r.label, kTileL, overridePct == 100 ? pal::mid : accent, Align::Right);
    }
    text(cv, value, x0 + 8.0f, y0 + r.value, kVal, pal::text);
    text(cv, unit, x1 - 6.0f, y0 + r.value, kLab, pal::mid, Align::Right);
    cv.begin(pal::dim);
    cv.line(V(x0 + 23.0f, y0 + r.dashY), V(x1 - 6.0f, y0 + r.dashY), 1.0f);
    cv.end();
    cv.begin(accent);
    dashes(cv, x0 + 8.0f, y0 + r.dashY);
    cv.end();
}

void jobTiles(Canvas& cv, const View& v, const Look& lk){
    const Details& d = v.d;
    const float w = G->tileW, h = G->tileH;
    tileTemp(cv, G->tileX[0], G->tileY[0], G->tileX[0] + w, h, "NOZZLE", v.nozzle, d.nozzleTarget, d.nozzleMax, lk.c);
    tileTemp(cv, G->tileX[1], G->tileY[1], G->tileX[1] + w, h, "BED", v.bed, d.bedTarget,
             d.bedMax > 0 ? d.bedMax : 120, lk.c);
    char s[12], f[12];
    const bool end = ended(v.screen);
    if (d.speed >= 0 && !end) snprintf(s, sizeof(s), "%d", d.speed); else snprintf(s, sizeof(s), "--");
    if (d.flow10 >= 0 && !end) snprintf(f, sizeof(f), "%d.%d", d.flow10 / 10, d.flow10 % 10);
    else snprintf(f, sizeof(f), "--");
    tileRate(cv, G->tileX[2], G->tileY[2], G->tileX[2] + w, h, "SPEED", s, "MM/S", end ? -1 : d.feedPct, lk.c);
    tileRate(cv, G->tileX[3], G->tileY[3], G->tileX[3] + w, h, "FLOW", f, "MM3/S", end ? -1 : d.flowPct, lk.c);
}

// ---------------------------------------------------------------- bottom lines

// Label/value pairs from x, until the line is full
struct LinePen {
    Canvas& cv;
    float   x;
    float   base;
    float   limit;
    void pair(const char* label, Color lc, const char* value){
        const float lw = textWidth(label, kLab), vw = textWidth(value, kLab);
        if (x + lw + 4.0f + vw > limit){
            x = limit + 1.0f;
            return;
        }
        text(cv, label, x, base, kLab, lc);
        text(cv, value, x + lw + 4.0f, base, kLab, pal::text);
        x += lw + 4.0f + vw + 11.0f;
    }
};

// The job: name, material, the slicer's estimate (landscape); the printer,
// or its code in portrait, at the right
void jobLine(Canvas& cv, const View& v, const Look& lk, float base, bool withEst){
    const Details& d = v.d;
    const float xr = G->w - 6.0f;
    float right;
    if (!G->codeInHeader && v.errorCode != 0){
        codeTag(cv, v, xr, base);
        right = xr - codeTagWidth(v);
    } else {
        const char* who = d.printer[0] ? d.printer : "HM-01";
        text(cv, who, xr, base, kLab, pal::mid, Align::Right);
        right = xr - textWidth(who, kLab);
    }
    char tail[24] = "";
    if (withEst && d.sliceS >= 0){
        char t[12];
        duration(d.sliceS, t, sizeof(t));
        snprintf(tail, sizeof(tail), "EST %s", t);
    }
    const float tailW = tail[0] ? textWidth(tail, kLab) + 10.0f : 0.0f;
    const float matW = d.material[0] ? textWidth(d.material, kLab) + 10.0f : 0.0f;
    char name[32];
    fit(d.job[0] ? d.job : "--", kLab, right - 16.0f - 6.0f - tailW - matW, name, sizeof(name));
    float x = 6.0f;
    text(cv, name, x, base, kLab, pal::text);
    x += textWidth(name, kLab) + 10.0f;
    if (d.material[0]){
        text(cv, d.material, x, base, kLab, lk.c);
        x += matW;
    }
    text(cv, tail, x, base, kLab, pal::mid);
}

void jobLines(Canvas& cv, const View& v, const Look& lk){
    const Details& d = v.d;
    if (G->lineA >= 0.0f){
        char a[16];
        LinePen pen{cv, 6.0f, G->lineA, G->w - 6.0f};
        if (!ended(v.screen)){
            pct(d.fanPart, a, sizeof(a)); pen.pair("FAN", lk.c, a);
            pct(d.fanAux, a, sizeof(a));  pen.pair("AUX", lk.c, a);
            pct(d.fanCase, a, sizeof(a)); pen.pair("CASE", lk.c, a);
        }
        metres(d.usedDm, a, sizeof(a));
        pen.pair("FILAMENT", lk.c, a);
        if (d.chamber > -1000){
            temp(d.chamber, a, sizeof(a));
            pen.pair("CHAMBER", lk.c, a);
        }
    }
    jobLine(cv, v, lk, G->lineB, !G->portrait);
}

// ---------------------------------------------------------------- other screens

// The info panel: up to three labelled lines, largest first
void infoPanel(Canvas& cv, const Look& lk, const char* l1, const char* v1, const char* l2,
               const char* v2, const char* l3, const char* v3){
    const Box& b = G->info;
    panel(cv, b, 10.0f, lk.c);
    const float x0 = b.x0 + 10.0f, w = b.x1 - 8.0f - x0, y = b.y0;
    char s[32];
    text(cv, l1, x0, y + 16.0f, kLab, lk.c);
    fit(v1, kVal, w, s, sizeof(s));
    text(cv, s, x0, y + 36.0f, kVal, pal::text);
    rule(cv, x0, b.x1 - 8.0f, y + 47.5f, lk.c);
    // Portrait has less height: the two small lines sit closer
    const float k = G->portrait ? 0.82f : 1.0f;
    text(cv, l2, x0, y + 47.5f + 15.5f * k, kLab, pal::mid);
    fit(v2, kValS, w, s, sizeof(s));
    text(cv, s, x0, y + 47.5f + 31.5f * k, kValS, pal::text);
    text(cv, l3, x0, y + 47.5f + 49.5f * k, kLab, pal::mid);
    fit(v3, kValS, w, s, sizeof(s));
    text(cv, s, x0, y + 47.5f + 65.5f * k, kValS, pal::text);
}

// A wide panel with hazard stripes and a two-line hint
void hintPanel(Canvas& cv, const Look& lk, const char* hint1, const char* hint2){
    const Box& b = G->hint;
    panel(cv, b, 7.0f, lk.c);
    const float top = b.y0 + 0.5f * (b.y1 - b.y0 - 24.0f);
    cv.begin(lk.c);
    for (int i = 0; i < 4; i++){
        const float x = b.x0 + 10.0f + 7.0f * i;
        const Vec p[4] = {V(x + 10.0f, top), V(x + 13.5f, top), V(x + 3.5f, top + 24.0f), V(x, top + 24.0f)};
        cv.poly(p, 4);
    }
    cv.end();
    const TextStyle& st = G->portrait ? kLab : kSmall;
    const float tx = b.x0 + (G->portrait ? 50.0f : 56.0f);
    char s[48];
    fit(hint1, st, b.x1 - 8.0f - tx, s, sizeof(s));
    text(cv, s, tx, b.y0 + G->hint1, st, pal::text);
    fit(hint2, st, b.x1 - 8.0f - tx, s, sizeof(s));
    text(cv, s, tx, b.y0 + G->hint2, st, pal::mid);
}

void footer(Canvas& cv, const View& v, const char* left){
    char s[24];
    snprintf(s, sizeof(s), "HM-01 %s", v.version);
    const char* right = v.version[0] ? s : "HM-01";
    text(cv, right, G->w - 6.0f, G->foot, kLab, pal::dim, Align::Right);
    char l[32];
    fit(left, kLab, G->w - 12.0f - textWidth(right, kLab) - 10.0f, l, sizeof(l));
    text(cv, l, 6.0f, G->foot, kLab, pal::mid);
}

void splashScreen(Canvas& cv, const View& v, const Look& lk){
    s_gx = G->infoGX;
    s_gy = G->infoGY;
    gaugeFace(cv, lk.c);
    plainRing(cv, lk.c, 0, lk.c);
    reticle(cv, V(s_gx, s_gy), 20.0f, lk.c, pal::text, 0.0f);
    chevrons(cv, s_gx - 36.0f, s_gy, -1, lk.c);
    chevrons(cv, s_gx + 36.0f, s_gy, 1, lk.c);

    const Box& b = G->info;
    panel(cv, b, 10.0f, lk.c);
    const float x0 = b.x0 + 10.0f;
    // Landscape: 50, 62.5, 84, 104, 120 below the top; portrait a little tighter
    const float k = G->portrait ? 0.85f : 1.0f;
    text(cv, "HM-01", x0, b.y0 + 50.0f * k, kBig, pal::text);
    rule(cv, x0, b.x1 - 8.0f, b.y0 + 62.5f * k, lk.c);
    text(cv, "HI MONITOR", x0, b.y0 + 84.0f * k, kVal, pal::text);
    text(cv, "M5STACK CORES3", x0, b.y0 + 104.0f * k, kSmall, pal::mid);
    text(cv, v.version, x0, b.y0 + 120.0f * k, kSmall, lk.c);
    hintPanel(cv, lk, "LIVE STATUS OF A CREALITY HI", "PORT 9999 - NO PC IN BETWEEN");
    footer(cv, v, "");
}

void linkScreens(Canvas& cv, const View& v, const Look& lk){
    s_gx = G->infoGX;
    s_gy = G->infoGY;
    gaugeFace(cv, lk.c);
    if (v.screen == Screen::Offline){
        plainRing(cv, pal::dim, 0, pal::dim);
        if (v.blink & 1){
            cv.begin(pal::red);
            seg(cv, kRingSegments - 1);
            seg(cv, 0);
            cv.end();
        }
        reticle(cv, V(s_gx, s_gy), 18.0f, pal::dim, pal::red, 0.25f * kPi);
    } else {
        spinnerRing(cv, v.spin, 8, lk.c);
        const float turn = v.screen == Screen::Linking ? v.spin * (2.0f * kPi / kRingSegments) : 0.0f;
        reticle(cv, V(s_gx, s_gy), 18.0f, pal::dim, lk.c, turn);
    }

    switch (v.screen){
        case Screen::Setup:
            infoPanel(cv, lk, "JOIN THIS WIFI", v.line1, "PASSWORD", v.line2, "THEN OPEN", v.line3);
            hintPanel(cv, lk, "PICK YOUR 2.4 GHZ WIFI", "THEN PRINTER + TIME ZONE");
            footer(cv, v, "HOLD THE SCREEN 3 S: SETUP");
            break;
        case Screen::Linking:
            infoPanel(cv, lk, "LINKING", v.line1, "", v.line2, "", v.line3);
            hintPanel(cv, lk, "LOOKING FOR THE PRINTER", "WEBSOCKET PORT 9999");
            footer(cv, v, v.d.ip);
            break;
        default:
            infoPanel(cv, lk, "NO ANSWER", v.line1, "ADDRESS", v.line2, "", v.line3);
            hintPanel(cv, lk, "THE PRINTER DOES NOT ANSWER", "IS IT ON THIS NETWORK");
            footer(cv, v, v.d.ip);
            break;
    }
}

void idleScreen(Canvas& cv, const View& v, const Look& lk){
    const Details& d = v.d;
    s_gx = G->portrait ? G->infoGX : G->gx;
    s_gy = G->portrait ? G->infoGY : G->gy;
    gaugeFace(cv, lk.c);
    plainRing(cv, pal::dim, kRingSegments / 4, lk.c);
    reticle(cv, V(s_gx, s_gy - 8.0f), 16.0f, pal::dim, lk.c, 0.0f);
    text(cv, "IDLE", s_gx, s_gy + (G->portrait ? 26.0f : 30.0f), kSmall, pal::mid, Align::Center);

    // Clock, printer, model, our address
    Box b = G->portrait ? Box{4.0f, 154.0f, 236.0f, 252.0f} : G->panel;
    panel(cv, b, 10.0f, lk.c);
    const float x0 = b.x0 + 10.0f, x1 = b.x1 - 8.0f, y = b.y0;
    text(cv, "CLOCK", x0, y + 15.0f, kSmall, lk.c);
    text(cv, v.clock[0] ? v.clock : "--:--", x0, y + 47.0f, kBig, pal::text);
    rule(cv, x0, x1, y + 57.5f, lk.c);
    char a[24];
    orDash(d.printer, a, sizeof(a));
    field(cv, x0, y + 71.0f, y + 85.0f, "PRINTER", pal::mid, a, kValS);
    orDash(d.model, a, sizeof(a));
    field(cv, x0 + (G->portrait ? 110.0f : 104.0f), y + 71.0f, y + 85.0f, "MODEL", pal::mid, a, kValS);
    if (!G->portrait){
        orDash(d.ip, a, sizeof(a));
        field(cv, x0, y + 99.0f, y + 113.0f, "HM-01 ADDRESS", pal::mid, a, kValS);
    }

    // Temperatures in wide tiles (three with a chamber)
    const bool box = d.chamber > -1000;
    const float n = box ? 3.0f : 2.0f;
    const float gap = 4.0f, w = (G->w - 8.0f - gap * (n - 1.0f)) / n;
    const float ty = G->portrait ? 256.0f : G->tileY[0];
    const float th = G->portrait ? 40.0f : G->tileH;
    float x = 4.0f;
    tileTemp(cv, x, ty, x + w, th, "NOZZLE", v.nozzle, d.nozzleTarget, d.nozzleMax, lk.c);
    x += w + gap;
    tileTemp(cv, x, ty, x + w, th, "BED", v.bed, d.bedTarget, d.bedMax > 0 ? d.bedMax : 120, lk.c);
    if (box){
        x += w + gap;
        tileTemp(cv, x, ty, x + w, th, "CHAMBER", d.chamber, d.chamberTarget, 80, lk.c);
    }
    if (G->portrait){
        footer(cv, v, d.ip);
    } else {
        text(cv, "READY FOR A JOB", 6.0f, G->lineA, kLab, pal::mid);
        footer(cv, v, "");
    }
}

}  // namespace

void drawDash(Canvas& cv, const View& v, int top, bool portrait){
    G = portrait ? &kPort : &kLand;
    cv.clear(pal::bezel);
    Xform x = Xform::identity();
    x.oy = static_cast<float>(-top);
    cv.setXform(x);
    const Look lk = lookOf(v);
    header(cv, v, lk);
    switch (v.screen){
        case Screen::Splash:
            splashScreen(cv, v, lk);
            break;
        case Screen::Setup:
        case Screen::Linking:
        case Screen::Offline:
            linkScreens(cv, v, lk);
            break;
        case Screen::Idle:
            idleScreen(cv, v, lk);
            break;
        case Screen::Printing:
        case Screen::Paused:
        case Screen::Done:
        case Screen::Stopped:
        case Screen::Error:
            jobGauge(cv, v, lk);
            if (G->portrait){
                jobSide(cv, v, lk);
                jobPanelPortrait(cv, v, lk);
            } else {
                jobPanel(cv, v, lk);
            }
            jobTiles(cv, v, lk);
            jobLines(cv, v, lk);
            break;
    }
}

}  // namespace hm
