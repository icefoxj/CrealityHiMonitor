// Host-side renderer: draws every screen with the firmware's own drawing
// code (firmware/lib/hm_core) and writes them as binary PPM files, which
// tools/previews.py turns into the PNGs and the GIF in docs/img.
//
//   g++ -std=gnu++14 -O2 -Ifirmware/lib/hm_core/src tools/preview.cpp
//       firmware/lib/hm_core/src/{gfx,mechfont,screens}.cpp -o preview
//   ./preview <out dir>
//
// (tools/previews.py does all of this.)
//
// Output: <name>.ppm (one per screen, upright) and rot_<nn>.ppm (the
// printing screen at angles from 0 to 90 degrees, for the rotation GIF).

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dash.h"
#include "gfx.h"
#include "screens.h"

using namespace hm;

namespace {

Color  g_px[kScreenSize * kScreenSize];
uint8_t g_mask[kScreenSize * kScreenSize];
Color  g_dpx[kDashW * kDashH];
uint8_t g_dmask[kDashW * kDashH];

bool writePpm(const char* dir, const char* name, const Canvas& cv){
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE* f = fopen(path, "wb");
    if (!f){
        fprintf(stderr, "cannot write %s\n", path);
        return false;
    }
    fprintf(f, "P6\n%d %d\n255\n", cv.width(), cv.height());
    const Color* p = cv.pixels();
    for (int i = 0; i < cv.width() * cv.height(); i++){
        const Color c = p[i];
        unsigned char rgb[3] = {
            static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31),
            static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63),
            static_cast<unsigned char>((c & 31) * 255 / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}

View printing(){
    View v;
    v.screen = Screen::Printing;
    v.percent = 48;
    v.remainingS = 6480;          // 1:48
    v.elapsedS = 4895;
    strcpy(v.eta, "18:47");
    strcpy(v.clock, "16:59");
    v.layer = 36;
    v.layers = 148;
    v.nozzle = 220;
    v.bed = 60;
    v.blink = 1;
    return v;
}

// The CoreS3 dashboard, with the numbers of a real job on the user's Hi
// (captured 2026-11-19, layer 37 of 50)
View dashPrinting(){
    View v;
    v.screen = Screen::Printing;
    v.percent = 86;
    v.remainingS = 2234;          // 0:37
    v.elapsedS = 14620;           // 4:04
    strcpy(v.eta, "19:25");
    strcpy(v.clock, "18:48");
    v.layer = 37;
    v.layers = 50;
    v.nozzle = 220;
    v.bed = 60;
    v.blink = 1;
    Details& d = v.d;
    strcpy(d.job, "WO01 PANEL 8X8");
    strcpy(d.material, "PLA");
    d.sliceS = 16854;             // 4h40m54s
    d.totalS = 16854;
    strcpy(d.start, "13:48");
    d.nozzleTarget = 220;
    d.bedTarget = 60;
    d.nozzleMax = 300;
    d.bedMax = 100;
    d.speed = 250;
    d.flow10 = 54;
    d.feedPct = 100;
    d.flowPct = 100;
    d.fanPart = 100;
    d.fanAux = 0;
    d.fanCase = 0;
    d.z100 = 756;
    d.usedDm = 333;
    d.filePct = 85;
    strcpy(d.printer, "HI-5EBC");
    strcpy(d.model, "F018");
    strcpy(d.ip, "192.168.1.60");
    d.wifiBars = 3;
    d.battery = 78;
    return v;
}

// Every dashboard screen, into <prefix><name>.ppm; portrait: the canvas is
// kDashH wide and kDashW tall
int dashShots(Canvas& cv, const char* dir, bool& ok, const char* prefix, bool portrait){
    struct Shot {
        const char* name;
        View v;
    };
    Shot shots[12];
    int n = 0;
    { View v; v.screen = Screen::Splash; strcpy(v.version, "V0.2.0"); shots[n++] = {"splash", v}; }
    { View v; v.screen = Screen::Setup; strcpy(v.line1, "HM-01-5EBC"); strcpy(v.line2, "HM015EBC");
      strcpy(v.line3, "192.168.4.1"); v.spin = 12; strcpy(v.version, "V0.2.0"); shots[n++] = {"setup", v}; }
    { View v; v.screen = Screen::Linking; strcpy(v.line1, "PRINTER"); strcpy(v.line2, "192.168.1.50");
      strcpy(v.line3, "PORT 9999"); v.spin = 25; strcpy(v.clock, "18:41"); v.d.wifiBars = 3;
      strcpy(v.d.ip, "192.168.1.60"); strcpy(v.version, "V0.2.0"); shots[n++] = {"linking", v}; }
    { View v; v.screen = Screen::Offline; strcpy(v.line1, "PRINTER"); strcpy(v.line2, "192.168.1.50");
      strcpy(v.line3, "RETRYING"); v.blink = 1; strcpy(v.clock, "18:41"); v.d.wifiBars = 3;
      strcpy(v.d.ip, "192.168.1.60"); strcpy(v.version, "V0.2.0"); shots[n++] = {"offline", v}; }
    { View v = dashPrinting(); v.screen = Screen::Idle; v.percent = -1; v.remainingS = -1; v.elapsedS = -1;
      v.layer = -1; strcpy(v.clock, "16:42"); v.nozzle = 26; v.bed = 24; v.d.nozzleTarget = 0; v.d.bedTarget = 0;
      v.d.battery = 96; v.d.charging = true;
      v.blink = 0; strcpy(v.version, "V0.2.0"); shots[n++] = {"idle", v}; }
    shots[n++] = {"printing", dashPrinting()};
    { View v = dashPrinting(); v.heating = true; v.percent = 0; v.remainingS = 16854; v.elapsedS = 0;
      strcpy(v.eta, "18:29"); strcpy(v.clock, "13:48"); v.layer = 0; v.nozzle = 168; v.bed = 58;
      v.d.speed = 0; v.d.flow10 = 0; v.d.z100 = 0; v.d.usedDm = 0; v.d.filePct = 0; v.d.fanPart = 0;
      shots[n++] = {"heating", v}; }
    { View v = dashPrinting(); v.screen = Screen::Paused; v.eta[0] = 0; v.d.speed = 0; v.d.flow10 = 0;
      v.d.fanPart = 0; shots[n++] = {"paused", v}; }
    { View v = dashPrinting(); v.screen = Screen::Done; v.percent = 100; v.elapsedS = 16968;
      strcpy(v.eta, "19:27"); strcpy(v.clock, "19:31"); v.layer = -1; v.remainingS = -1; v.blink = 0;
      v.d.usedDm = 391; v.d.totalS = 16968; v.nozzle = 74; v.bed = 52; v.d.nozzleTarget = 0; v.d.bedTarget = 0;
      shots[n++] = {"done", v}; }
    { View v = dashPrinting(); v.screen = Screen::Stopped; v.percent = 37; v.elapsedS = 6120;
      strcpy(v.eta, "15:30"); v.layer = -1; v.remainingS = -1; v.blink = 0; v.d.usedDm = 141;
      v.d.nozzleTarget = 0; v.d.bedTarget = 0; shots[n++] = {"stopped", v}; }
    { View v = dashPrinting(); v.screen = Screen::Error; v.percent = 52; v.elapsedS = 8460; v.errorCode = 2839;
      strcpy(v.eta, "16:09"); v.layer = -1; v.remainingS = -1; v.blink = 1; v.d.usedDm = 198;
      shots[n++] = {"error", v}; }

    for (int i = 0; i < n; i++){
        drawDash(cv, shots[i].v, 0, portrait);
        char name[40];
        snprintf(name, sizeof(name), "%s%s", prefix, shots[i].name);
        ok &= writePpm(dir, name, cv);
    }
    return n;
}

}  // namespace

int main(int argc, char** argv){
    const char* dir = argc > 1 ? argv[1] : ".";
    Canvas cv(g_px, g_mask, kScreenSize, kScreenSize);
    bool ok = true;

    struct Shot {
        const char* name;
        View v;
    };
    Shot shots[12];
    int n = 0;

    { View v; v.screen = Screen::Splash; strcpy(v.version, "V0.2.0"); shots[n++] = {"splash", v}; }
    { View v; v.screen = Screen::Setup; strcpy(v.line1, "HM-01-5EBC"); strcpy(v.line2, "HM015EBC");
      strcpy(v.line3, "192.168.4.1"); v.spin = 12; shots[n++] = {"setup", v}; }
    { View v; v.screen = Screen::Linking; strcpy(v.line1, "PRINTER"); strcpy(v.line2, "192.168.1.216");
      strcpy(v.line3, "PORT 9999"); v.spin = 25; shots[n++] = {"linking", v}; }
    { View v; v.screen = Screen::Offline; strcpy(v.line1, "PRINTER"); strcpy(v.line2, "192.168.1.216");
      strcpy(v.line3, "RETRYING"); v.blink = 1; shots[n++] = {"offline", v}; }
    { View v; v.screen = Screen::Idle; strcpy(v.clock, "16:42"); v.nozzle = 26; v.bed = 24;
      strcpy(v.line1, "HI-5EBC"); shots[n++] = {"idle", v}; }
    shots[n++] = {"printing", printing()};
    { View v = printing(); v.heating = true; v.percent = 0; v.remainingS = 9060; strcpy(v.eta, "19:31");
      v.layer = 0; shots[n++] = {"heating", v}; }
    { View v = printing(); v.screen = Screen::Paused; v.eta[0] = 0; shots[n++] = {"paused", v}; }
    { View v = printing(); v.screen = Screen::Done; v.percent = 100; v.elapsedS = 11400; strcpy(v.eta, "19:12");
      v.layer = -1; v.blink = 0; shots[n++] = {"done", v}; }
    { View v = printing(); v.screen = Screen::Stopped; v.percent = 37; v.elapsedS = 3720; strcpy(v.eta, "17:02");
      v.layer = -1; v.blink = 0; shots[n++] = {"stopped", v}; }
    { View v = printing(); v.screen = Screen::Error; v.percent = 52; v.elapsedS = 5100; v.errorCode = 2839;
      v.layer = -1; v.blink = 1; shots[n++] = {"error", v}; }

    for (int i = 0; i < n; i++){
        drawScreen(cv, shots[i].v, 0.0f);
        ok &= writePpm(dir, shots[i].name, cv);
    }

    // The printing screen while the cube is turned a quarter turn
    const View pv = printing();
    for (int k = 0; k <= 18; k++){
        const float deg = 5.0f * k;
        drawScreen(cv, pv, deg * 3.14159265f / 180.0f);
        char name[16];
        snprintf(name, sizeof(name), "rot_%02d", k);
        ok &= writePpm(dir, name, cv);
    }
    // The CoreS3 dashboard, landscape and portrait (same buffers)
    Canvas dv(g_dpx, g_dmask, kDashW, kDashH);
    const int nd = dashShots(dv, dir, ok, "cores3_", false);
    Canvas pv2(g_dpx, g_dmask, kDashH, kDashW);
    dashShots(pv2, dir, ok, "cores3p_", true);

    printf("%d screens, 19 rotation frames, %d CoreS3 screens -> %s\n", n, nd, dir);
    return ok ? 0 : 1;
}
