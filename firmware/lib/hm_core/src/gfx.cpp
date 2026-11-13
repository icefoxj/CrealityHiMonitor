#include "gfx.h"

#include <math.h>
#include <string.h>

namespace hm {

namespace {

constexpr float kPi = 3.14159265f;

inline float fmin2(float a, float b){ return a < b ? a : b; }
inline float fmax2(float a, float b){ return a > b ? a : b; }

inline Vec add(Vec a, Vec b){ return {a.x + b.x, a.y + b.y}; }
inline Vec sub(Vec a, Vec b){ return {a.x - b.x, a.y - b.y}; }
inline Vec mul(Vec a, float k){ return {a.x * k, a.y * k}; }
inline float dot(Vec a, Vec b){ return a.x * b.x + a.y * b.y; }

inline Vec unit(Vec v){
    float l = sqrtf(v.x * v.x + v.y * v.y);
    if (l < 1e-6f){
        return {0.0f, 0.0f};
    }
    return {v.x / l, v.y / l};
}

// Local point at angle a (0 = up, clockwise) and radius r around c
inline Vec polar(Vec c, float r, float a){
    return {c.x + r * sinf(a), c.y - r * cosf(a)};
}

}  // namespace

Color blend(Color bg, Color fg, int a){
    if (a >= 255) return fg;
    if (a <= 0) return bg;
    const int na = 255 - a;
    const int r = ((fg >> 11) * a + (bg >> 11) * na + 127) / 255;
    const int g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * na + 127) / 255;
    const int b = ((fg & 31) * a + (bg & 31) * na + 127) / 255;
    return static_cast<Color>((r << 11) | (g << 5) | b);
}

Xform Xform::identity(){
    return Xform{};
}

Xform Xform::rotate(float originX, float originY, float angle, float scale){
    Xform x;
    x.ox = originX;
    x.oy = originY;
    x.c  = scale * cosf(angle);
    x.s  = scale * sinf(angle);
    x.k  = scale;
    return x;
}

Canvas::Canvas(Color* pixels, uint8_t* mask, int width, int height)
    : px_(pixels), mask_(mask), w_(width), h_(height) {}

void Canvas::clear(Color c){
    const int n = w_ * h_;
    for (int i = 0; i < n; i++){
        px_[i] = c;
    }
}

void Canvas::begin(Color c, uint8_t alpha){
    if (open_){
        end();
    }
    ink_   = c;
    alpha_ = alpha;
    open_  = true;
    bx0_ = w_;
    by0_ = h_;
    bx1_ = -1;
    by1_ = -1;
}

void Canvas::end(){
    if (!open_){
        return;
    }
    open_ = false;
    // The box of a ring is mostly empty: skip the mask four bytes at a time
    const int ax0 = bx0_ & ~3;
    for (int y = by0_; y <= by1_; y++){
        uint8_t* m = mask_ + y * w_;
        Color*   p = px_ + y * w_;
        for (int x = ax0; x <= bx1_; x++){
            if ((x & 3) == 0 && x + 3 < w_){
                uint32_t word;
                memcpy(&word, m + x, 4);
                if (word == 0){
                    x += 3;
                    continue;
                }
            }
            const int cov = m[x];
            if (cov == 0){
                continue;
            }
            m[x] = 0;
            const int a = alpha_ == 255 ? cov : (cov * alpha_ + 127) / 255;
            p[x] = blend(p[x], ink_, a);
        }
    }
}

Canvas::Plane Canvas::plane(Vec p, Vec n) const {
    const Vec P = xf_.map(p);
    const Vec N = xf_.mapDir(n);
    return {N.x, N.y, -(N.x * P.x + N.y * P.y)};
}

void Canvas::raster(const Plane* pl, int np, const Circle* cl, int nc,
                    float fx0, float fy0, float fx1, float fy1){
    if (!open_){
        return;
    }
    int x0 = static_cast<int>(floorf(fx0)) - 1;
    int y0 = static_cast<int>(floorf(fy0)) - 1;
    int x1 = static_cast<int>(ceilf(fx1)) + 1;
    int y1 = static_cast<int>(ceilf(fy1)) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w_ - 1) x1 = w_ - 1;
    if (y1 > h_ - 1) y1 = h_ - 1;
    if (x0 > x1 || y0 > y1){
        return;
    }

    // A constraint at distance >= 0.5 inside cannot lower the coverage, so
    // each row is cut to the span where every constraint is > -0.5, and a
    // circle's square root is only taken within half a pixel of its edge
    bool touched = false;
    float d[kMaxPlanes];
    for (int y = y0; y <= y1; y++){
        const float qy = y + 0.5f;
        float xs = x0 + 0.5f;   // pixel-centre range that may be covered
        float xe = x1 + 0.5f;
        bool empty = false;
        for (int i = 0; i < np && !empty; i++){
            // a*qx + b*qy + c > -0.5
            const float rhs = -0.5f - pl[i].b * qy - pl[i].c;
            if (pl[i].a > 1e-6f){
                xs = fmax2(xs, rhs / pl[i].a);
            } else if (pl[i].a < -1e-6f){
                xe = fmin2(xe, rhs / pl[i].a);
            } else if (rhs >= 0.0f){
                empty = true;
            }
        }
        float holeA = 1e9f, holeB = -1e9f;   // span fully inside a hole (outside-circle constraint)
        for (int i = 0; i < nc && !empty; i++){
            const float dy = qy - cl[i].y;
            if (cl[i].inside){
                const float rr = cl[i].r + 0.5f;
                const float t = rr * rr - dy * dy;
                if (t <= 0.0f){
                    empty = true;
                    break;
                }
                const float h = sqrtf(t);
                xs = fmax2(xs, cl[i].x - h);
                xe = fmin2(xe, cl[i].x + h);
            } else {
                const float rr = cl[i].r - 0.5f;
                const float t = rr > 0.0f ? rr * rr - dy * dy : -1.0f;
                if (t > 0.0f){
                    const float h = sqrtf(t);
                    holeA = cl[i].x - h;
                    holeB = cl[i].x + h;
                }
            }
        }
        if (empty || xs > xe){
            continue;
        }
        int ix0 = static_cast<int>(ceilf(xs - 0.5f));
        int ix1 = static_cast<int>(floorf(xe - 0.5f));
        if (ix0 < x0) ix0 = x0;
        if (ix1 > x1) ix1 = x1;
        if (ix0 > ix1){
            continue;
        }

        // The stretch where every constraint is >= 0.5 inside is fully
        // covered: one memset (the big HUD disc is almost all of it)
        int f0 = 1, f1 = 0;
        bool hasHole = false;
        for (int i = 0; i < nc; i++){
            hasHole |= !cl[i].inside;
        }
        if (!hasHole){
            float fs = xs, fe = xe;
            bool none = false;
            for (int i = 0; i < np && !none; i++){
                const float rhs = 0.5f - pl[i].b * qy - pl[i].c;
                if (pl[i].a > 1e-6f){
                    fs = fmax2(fs, rhs / pl[i].a);
                } else if (pl[i].a < -1e-6f){
                    fe = fmin2(fe, rhs / pl[i].a);
                } else if (rhs > 0.0f){
                    none = true;
                }
            }
            for (int i = 0; i < nc && !none; i++){
                const float rr = cl[i].r - 0.5f;
                const float dy = qy - cl[i].y;
                const float t = rr > 0.0f ? rr * rr - dy * dy : -1.0f;
                if (t <= 0.0f){
                    none = true;
                    break;
                }
                const float h = sqrtf(t);
                fs = fmax2(fs, cl[i].x - h);
                fe = fmin2(fe, cl[i].x + h);
            }
            if (!none && fs <= fe){
                f0 = static_cast<int>(ceilf(fs - 0.5f));
                f1 = static_cast<int>(floorf(fe - 0.5f));
            }
        }

        // Up to two runs: left and right of the hole
        int runA0 = ix0, runA1 = ix1, runB0 = 1, runB1 = 0;
        if (holeB > holeA){
            const int h0 = static_cast<int>(floorf(holeA - 0.5f)) + 1;   // first centre inside the hole
            const int h1 = static_cast<int>(ceilf(holeB - 0.5f)) - 1;    // last centre inside
            if (h0 <= h1){
                runA1 = h0 - 1 < ix1 ? h0 - 1 : ix1;
                runB0 = h1 + 1 > ix0 ? h1 + 1 : ix0;
                runB1 = ix1;
            }
        }
        uint8_t* m = mask_ + y * w_;
        for (int run = 0; run < 2; run++){
            const int rx0 = run == 0 ? runA0 : runB0;
            const int rx1 = run == 0 ? runA1 : runB1;
            if (rx0 > rx1){
                continue;
            }
            float qx = rx0 + 0.5f;
            for (int i = 0; i < np; i++){
                d[i] = pl[i].a * qx + pl[i].b * qy + pl[i].c;
            }
            int x = rx0;
            while (x <= rx1){
                if (x >= f0 && x <= f1){
                    const int e = f1 < rx1 ? f1 : rx1;
                    const int cnt = e - x + 1;
                    memset(m + x, 255, cnt);
                    touched = true;
                    for (int i = 0; i < np; i++){
                        d[i] += pl[i].a * cnt;
                    }
                    qx += cnt;
                    x = e + 1;
                    continue;
                }
                float dm = 1e9f;
                for (int i = 0; i < np; i++){
                    dm = fmin2(dm, d[i]);
                    d[i] += pl[i].a;
                }
                if (dm > -0.5f){
                    for (int i = 0; i < nc; i++){
                        const float dx = qx - cl[i].x;
                        const float dy = qy - cl[i].y;
                        const float r2 = dx * dx + dy * dy;
                        const float rin = cl[i].r - 0.5f;
                        const float rout = cl[i].r + 0.5f;
                        if (cl[i].inside ? (rin > 0.0f && r2 <= rin * rin) : r2 >= rout * rout){
                            continue;   // half a pixel or more inside this constraint
                        }
                        const float r = sqrtf(r2);
                        dm = fmin2(dm, cl[i].inside ? cl[i].r - r : r - cl[i].r);
                    }
                    if (dm > -0.5f){
                        const float cov = dm >= 0.5f ? 1.0f : dm + 0.5f;
                        const int v = m[x] + static_cast<int>(cov * 255.0f + 0.5f);
                        m[x] = static_cast<uint8_t>(v > 255 ? 255 : v);
                        touched = true;
                    }
                }
                x++;
                qx += 1.0f;
            }
        }
    }
    if (touched){
        if (x0 < bx0_) bx0_ = x0;
        if (y0 < by0_) by0_ = y0;
        if (x1 > bx1_) bx1_ = x1;
        if (y1 > by1_) by1_ = y1;
    }
}

void Canvas::poly(const Vec* pts, int n){
    if (n < 3 || n > kMaxPlanes){
        return;
    }
    // Winding from the signed area, so the normals point inwards either way
    float area = 0.0f;
    for (int i = 0; i < n; i++){
        const Vec& a = pts[i];
        const Vec& b = pts[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    const float sgn = area >= 0.0f ? 1.0f : -1.0f;

    Plane pl[kMaxPlanes];
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++){
        const Vec a = pts[i];
        const Vec b = pts[(i + 1) % n];
        const Vec e = unit(sub(b, a));
        // With y down, positive area = clockwise on screen; the inside is on
        // the right of each edge for sgn > 0
        const Vec inward = {-e.y * sgn, e.x * sgn};
        pl[i] = plane(a, inward);
        const Vec s = xf_.map(a);
        x0 = fmin2(x0, s.x);
        y0 = fmin2(y0, s.y);
        x1 = fmax2(x1, s.x);
        y1 = fmax2(y1, s.y);
    }
    raster(pl, n, nullptr, 0, x0, y0, x1, y1);
}

void Canvas::rect(float x, float y, float w, float h){
    const Vec p[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    poly(p, 4);
}

void Canvas::disc(Vec c, float r){
    const Vec C = xf_.map(c);
    const float R = r * xf_.k;
    const Circle cl = {C.x, C.y, R, true};
    raster(nullptr, 0, &cl, 1, C.x - R, C.y - R, C.x + R, C.y + R);
}

void Canvas::ring(Vec c, float r0, float r1){
    const Vec C = xf_.map(c);
    const Circle cl[2] = {{C.x, C.y, r1 * xf_.k, true}, {C.x, C.y, r0 * xf_.k, false}};
    const float R = r1 * xf_.k;
    raster(nullptr, 0, cl, 2, C.x - R, C.y - R, C.x + R, C.y + R);
}

void Canvas::sector(Vec c, float r0, float r1, float a0, float a1){
    if (a1 <= a0){
        return;
    }
    if (a1 - a0 >= kPi){
        const float mid = 0.5f * (a0 + a1);
        sector(c, r0, r1, a0, mid);
        sector(c, r0, r1, mid, a1);
        return;
    }
    const float s0 = sinf(a0), c0 = cosf(a0);
    const float s1 = sinf(a1), c1 = cosf(a1);
    Plane pl[2];
    // Radial edge at a0: inside is clockwise of it (towards larger angles)
    pl[0] = plane(c, {c0, s0});
    pl[1] = plane(c, {-c1, -s1});
    const Vec C = xf_.map(c);
    const Circle cl[2] = {{C.x, C.y, r1 * xf_.k, true}, {C.x, C.y, r0 * xf_.k, false}};

    // Box: the four corners and the outer arc's middle pushed out to its
    // tangent lines' meeting point (covers the bulge of the arc)
    const float half = 0.5f * (a1 - a0);
    const float am = a0 + half;
    const float rm = r1 / cosf(half);
    const Vec pts[5] = {
        {c.x + r0 * s0, c.y - r0 * c0}, {c.x + r1 * s0, c.y - r1 * c0},
        {c.x + r0 * s1, c.y - r0 * c1}, {c.x + r1 * s1, c.y - r1 * c1},
        {c.x + rm * sinf(am), c.y - rm * cosf(am)},
    };
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (const Vec& p : pts){
        const Vec s = xf_.map(p);
        x0 = fmin2(x0, s.x);
        y0 = fmin2(y0, s.y);
        x1 = fmax2(x1, s.x);
        y1 = fmax2(y1, s.y);
    }
    raster(pl, 2, cl, 2, x0, y0, x1, y1);
}

void Canvas::stroke(const Vec* p, int n, float width, const Vec* clipMin, const Vec* clipMax){
    if (n < 2){
        return;
    }
    const bool closed = n > 2 && p[0].x == p[n - 1].x && p[0].y == p[n - 1].y;
    const int  m      = closed ? n - 1 : n;      // distinct points
    const int  segs   = closed ? m : m - 1;
    const float h     = 0.5f * width;

    auto dirOf = [&](int j) -> Vec {             // direction of segment j
        return unit(sub(p[(j + 1) % m], p[j % m]));
    };

    for (int j = 0; j < segs; j++){
        const Vec a = p[j];
        const Vec b = p[(j + 1) % m];
        const Vec d = dirOf(j);
        if (d.x == 0.0f && d.y == 0.0f){
            continue;
        }
        const Vec nl = {-d.y, d.x};

        Plane pl[kMaxPlanes];
        int np = 0;
        pl[np++] = plane(add(a, mul(nl, h)), mul(nl, -1.0f));
        pl[np++] = plane(sub(a, mul(nl, h)), nl);

        // Start: mitre with the previous segment, or a square end
        if (closed || j > 0){
            const Vec dp = dirOf((j + segs - 1) % segs);
            const Vec mm = unit(add(dp, d));
            if (mm.x != 0.0f || mm.y != 0.0f){
                pl[np++] = plane(a, mm);
            } else {
                pl[np++] = plane(a, d);
            }
        } else {
            pl[np++] = plane(sub(a, mul(d, h)), d);
        }
        // End: mitre with the next segment, or a square end
        if (closed || j < segs - 1){
            const Vec dn = dirOf((j + 1) % segs);
            const Vec mm = unit(add(d, dn));
            if (mm.x != 0.0f || mm.y != 0.0f){
                pl[np++] = plane(b, mul(mm, -1.0f));
            } else {
                pl[np++] = plane(b, mul(d, -1.0f));
            }
        } else {
            pl[np++] = plane(add(b, mul(d, h)), mul(d, -1.0f));
        }

        // Bounding box: the segment grown by a generous mitre allowance,
        // cut by the clip box when there is one
        const float grow = 4.0f * h;
        float lx0 = fmin2(a.x, b.x) - grow, ly0 = fmin2(a.y, b.y) - grow;
        float lx1 = fmax2(a.x, b.x) + grow, ly1 = fmax2(a.y, b.y) + grow;
        if (clipMin && clipMax){
            pl[np++] = plane(*clipMin, {1.0f, 0.0f});
            pl[np++] = plane(*clipMin, {0.0f, 1.0f});
            pl[np++] = plane(*clipMax, {-1.0f, 0.0f});
            pl[np++] = plane(*clipMax, {0.0f, -1.0f});
            lx0 = fmax2(lx0, clipMin->x);
            ly0 = fmax2(ly0, clipMin->y);
            lx1 = fmin2(lx1, clipMax->x);
            ly1 = fmin2(ly1, clipMax->y);
            if (lx0 >= lx1 || ly0 >= ly1){
                continue;
            }
        }
        const Vec corners[4] = {{lx0, ly0}, {lx1, ly0}, {lx1, ly1}, {lx0, ly1}};
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const Vec& cv : corners){
            const Vec s = xf_.map(cv);
            x0 = fmin2(x0, s.x);
            y0 = fmin2(y0, s.y);
            x1 = fmax2(x1, s.x);
            y1 = fmax2(y1, s.y);
        }
        raster(pl, np, nullptr, 0, x0, y0, x1, y1);
    }
}

void Canvas::line(Vec a, Vec b, float width){
    const Vec p[2] = {a, b};
    stroke(p, 2, width);
}

}  // namespace hm
