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

#include "gfx.h"
#include "screens.h"

using namespace hm;

namespace {

Color  g_px[kScreenSize * kScreenSize];
uint8_t g_mask[kScreenSize * kScreenSize];

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

    { View v; v.screen = Screen::Splash; strcpy(v.version, "V0.1.0"); shots[n++] = {"splash", v}; }
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
    printf("%d screens, 19 rotation frames -> %s\n", n, dir);
    return ok ? 0 : 1;
}
