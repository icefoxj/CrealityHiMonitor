#include "mechfont.h"

namespace hm {

namespace {

constexpr float kCap = 6.0f;   // grid units from baseline to cap line

// Each glyph: polylines as {count, x0, y0, x1, y1, ...}, then 0. y is up
// from the baseline. A polyline whose last point repeats the first is
// closed. Copied from FilamentDryer cad/mech.py; '%' and the degree sign
// are new.
struct Glyph {
    unsigned char ch;
    float cols;
    const float* lines;
};

const float G_space[] = {0};
const float G_dash[]  = {2, 0, 3, 3, 3, 0};
const float G_slash[] = {2, 0, 0, 3, 6, 0};
const float G_dot[]   = {2, 0, 0, 0, 0.6f, 0};
const float G_colon[] = {2, 0, 0.6f, 0, 1.4f, 2, 0, 4.0f, 0, 4.8f, 0};
const float G_gt[]    = {3, 0, 6, 3, 3, 0, 0, 0};
const float G_lt[]    = {3, 3, 6, 0, 3, 3, 0, 0};
const float G_plus[]  = {2, 0, 3, 4, 3, 2, 2, 1, 2, 5, 0};
const float G_pct[]   = {2, 0.4f, 0, 3.6f, 6,
                         5, 0, 4.2f, 1.6f, 4.2f, 1.6f, 6, 0, 6, 0, 4.2f,
                         5, 2.4f, 0, 4, 0, 4, 1.8f, 2.4f, 1.8f, 2.4f, 0, 0};
const float G_deg[]   = {5, 0, 4.5f, 1.5f, 4.5f, 1.5f, 6, 0, 6, 0, 4.5f, 0};
const float G_0[] = {7, 1, 0, 4, 0, 4, 5, 3, 6, 0, 6, 0, 1, 1, 0, 0};
const float G_1[] = {3, 0, 4.5f, 2, 6, 2, 0, 0};
const float G_2[] = {5, 0, 6, 3, 6, 4, 5, 4, 4, 0, 0, 2, 0, 0, 4, 0, 0};
const float G_3[] = {6, 0, 6, 3, 6, 4, 5, 4, 1, 3, 0, 0, 0, 2, 1.5f, 3, 4, 3, 0};
const float G_4[] = {3, 3, 0, 3, 6, 0, 2, 2, 0, 2, 4, 2, 0};
const float G_5[] = {8, 4, 6, 0, 6, 0, 3, 3, 3, 4, 2, 4, 1, 3, 0, 0, 0, 0};
const float G_6[] = {6, 4, 6, 1, 6, 0, 5, 0, 0, 4, 0, 4, 3, 2, 4, 3, 0, 3, 0};
const float G_7[] = {4, 0, 6, 4, 6, 4, 5, 1.5f, 0, 0};
const float G_8[] = {9, 1, 0, 3, 0, 4, 1, 4, 5, 3, 6, 1, 6, 0, 5, 0, 1, 1, 0, 2, 0, 3, 4, 3, 0};
const float G_9[] = {6, 0, 0, 3, 0, 4, 1, 4, 6, 0, 6, 0, 3, 2, 0, 3, 4, 3, 0};
const float G_A[] = {6, 0, 0, 0, 5, 1, 6, 3, 6, 4, 5, 4, 0, 2, 0, 2.5f, 4, 2.5f, 0};
const float G_B[] = {2, 0, 0, 0, 6,
                     6, 0, 6, 3, 6, 4, 5, 4, 4, 3, 3, 0, 3,
                     5, 3, 3, 4, 2, 4, 1, 3, 0, 0, 0, 0};
const float G_C[] = {6, 4, 6, 1, 6, 0, 5, 0, 1, 1, 0, 4, 0, 0};
const float G_D[] = {7, 0, 0, 3, 0, 4, 1, 4, 5, 3, 6, 0, 6, 0, 0, 0};
const float G_E[] = {4, 4, 6, 0, 6, 0, 0, 4, 0, 2, 0, 3, 3, 3, 0};
const float G_F[] = {3, 4, 6, 0, 6, 0, 0, 2, 0, 3, 3, 3, 0};
const float G_G[] = {9, 4, 5, 3, 6, 1, 6, 0, 5, 0, 1, 1, 0, 4, 0, 4, 3, 2, 3, 0};
const float G_H[] = {2, 0, 0, 0, 6, 2, 4, 0, 4, 6, 2, 0, 3, 4, 3, 0};
const float G_I[] = {2, 0, 0, 0, 6, 0};
const float G_J[] = {5, 4, 6, 4, 1, 3, 0, 1, 0, 0, 1, 0};
const float G_K[] = {2, 0, 0, 0, 6, 3, 4, 6, 1, 3, 4, 0, 2, 0, 3, 1, 3, 0};
const float G_L[] = {3, 0, 6, 0, 0, 4, 0, 0};
const float G_M[] = {5, 0, 0, 0, 6, 2, 3.5f, 4, 6, 4, 0, 0};
const float G_N[] = {4, 0, 0, 0, 6, 4, 0, 4, 6, 0};
const float G_O[] = {9, 1, 0, 3, 0, 4, 1, 4, 5, 3, 6, 1, 6, 0, 5, 0, 1, 1, 0, 0};
const float G_P[] = {2, 0, 0, 0, 6, 6, 0, 6, 3, 6, 4, 5, 4, 4, 3, 3, 0, 3, 0};
const float G_Q[] = {9, 1, 0, 3, 0, 4, 1, 4, 5, 3, 6, 1, 6, 0, 5, 0, 1, 1, 0, 2, 2.5f, 1.5f, 4, 0, 0};
const float G_R[] = {2, 0, 0, 0, 6,
                     6, 0, 6, 3, 6, 4, 5, 4, 4, 3, 3, 0, 3,
                     3, 3, 3, 4, 2, 4, 0, 0};
const float G_S[] = {10, 4, 6, 1, 6, 0, 5, 0, 4, 1, 3, 3, 3, 4, 2, 4, 1, 3, 0, 0, 0, 0};
const float G_T[] = {2, 0, 6, 4, 6, 2, 2, 6, 2, 0, 0};
const float G_U[] = {6, 0, 6, 0, 1, 1, 0, 3, 0, 4, 1, 4, 6, 0};
const float G_V[] = {5, 0, 6, 0, 2, 2, 0, 4, 2, 4, 6, 0};
const float G_W[] = {5, 0, 6, 0, 0, 2, 2, 4, 0, 4, 6, 0};
const float G_X[] = {2, 0, 0, 4, 6, 2, 0, 6, 4, 0, 0};
const float G_Y[] = {3, 0, 6, 2, 3, 4, 6, 2, 2, 3, 2, 0, 0};
const float G_Z[] = {4, 0, 6, 4, 6, 0, 0, 4, 0, 0};

const Glyph kGlyphs[] = {
    {' ', 3, G_space}, {'-', 3, G_dash}, {'/', 3, G_slash}, {'.', 0, G_dot},
    {':', 0, G_colon}, {'>', 3, G_gt},   {'<', 3, G_lt},    {'+', 4, G_plus},
    {'%', 4, G_pct},   {0xB0, 1.5f, G_deg},
    {'0', 4, G_0}, {'1', 2, G_1}, {'2', 4, G_2}, {'3', 4, G_3}, {'4', 4, G_4},
    {'5', 4, G_5}, {'6', 4, G_6}, {'7', 4, G_7}, {'8', 4, G_8}, {'9', 4, G_9},
    {'A', 4, G_A}, {'B', 4, G_B}, {'C', 4, G_C}, {'D', 4, G_D}, {'E', 4, G_E},
    {'F', 4, G_F}, {'G', 4, G_G}, {'H', 4, G_H}, {'I', 0, G_I}, {'J', 4, G_J},
    {'K', 4, G_K}, {'L', 4, G_L}, {'M', 4, G_M}, {'N', 4, G_N}, {'O', 4, G_O},
    {'P', 4, G_P}, {'Q', 4, G_Q}, {'R', 4, G_R}, {'S', 4, G_S}, {'T', 4, G_T},
    {'U', 4, G_U}, {'V', 4, G_V}, {'W', 4, G_W}, {'X', 4, G_X}, {'Y', 4, G_Y},
    {'Z', 4, G_Z},
};

const Glyph* find(char c){
    const unsigned char u = static_cast<unsigned char>(c);
    for (const Glyph& g : kGlyphs){
        if (g.ch == u){
            return &g;
        }
    }
    return &kGlyphs[0];   // space
}

float spacingOf(const TextStyle& st){
    return st.spacing < 0.0f ? st.weight : st.spacing;
}

float advance(const Glyph& g, const TextStyle& st){
    const float u = (st.cap - st.weight) / kCap;
    return g.cols * u + st.weight + spacingOf(st);
}

}  // namespace

bool hasGlyph(char c){
    const Glyph* g = find(c);
    return g != &kGlyphs[0] || c == ' ';
}

float textWidth(const char* s, const TextStyle& st){
    float w = 0.0f;
    bool any = false;
    for (; *s; s++){
        w += advance(*find(*s), st);
        any = true;
    }
    return any ? w - spacingOf(st) : 0.0f;
}

void drawText(Canvas& cv, const char* s, float x, float y, const TextStyle& st, Align align){
    const float width = textWidth(s, st);
    if (align == Align::Center){
        x -= 0.5f * width;
    } else if (align == Align::Right){
        x -= width;
    }
    const float u = (st.cap - st.weight) / kCap;
    const float h = 0.5f * st.weight;

    Vec pts[12];
    for (; *s; s++){
        const Glyph& g = *find(*s);
        const Vec cellMin = {x, y - st.cap};
        const Vec cellMax = {x + g.cols * u + st.weight, y};
        const float* d = g.lines;
        while (*d > 0.0f){
            const int n = static_cast<int>(*d++);
            for (int i = 0; i < n && i < 12; i++){
                pts[i] = {x + d[2 * i] * u + h, y - (d[2 * i + 1] * u + h)};
            }
            cv.stroke(pts, n < 12 ? n : 12, st.weight, &cellMin, &cellMax);
            d += 2 * n;
        }
        x += advance(g, st);
    }
}

}  // namespace hm
