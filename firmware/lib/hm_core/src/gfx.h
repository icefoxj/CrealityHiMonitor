#pragma once

// Anti-aliased vector drawing into an RGB565 frame buffer, with no
// dependency on the display library, so the same code draws the screens on
// the device and the PNG previews on a PC (tools/previews.py).
//
// Every shape is an intersection of half-planes and circles. A pixel's
// coverage is 0.5 + its signed distance to the nearest boundary, clamped to
// 0..1. Shapes add their coverage to an 8-bit mask; end() paints the mask in
// the layer's colour. Two shapes that share an edge (the segments of one
// stroke) add up to full coverage there, so a layer has no seams.
//
// Shapes are given in local units and go through a similarity transform
// (rotation about a centre, uniform scale). Rotating the HUD is just a new
// transform: the vertices turn, the pixels are rasterised again, and text
// stays sharp at any angle.

#include <stdint.h>

namespace hm {

using Color = uint16_t;   // RGB565, native byte order

constexpr Color rgb(int r, int g, int b){
    return static_cast<Color>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

// Mixes two colours, a = 0 (all bg) .. 255 (all fg)
Color blend(Color bg, Color fg, int a);

struct Vec {
    float x;
    float y;
};

// screen = origin + scale * R(angle) * local. The angle is clockwise
// (screen y points down), the same convention as claude-monitor's icons.
struct Xform {
    float ox = 0.0f;
    float oy = 0.0f;
    float c  = 1.0f;   // scale * cos
    float s  = 0.0f;   // scale * sin
    float k  = 1.0f;   // scale

    static Xform identity();
    static Xform rotate(float originX, float originY, float angle, float scale = 1.0f);

    Vec map(Vec p) const { return {ox + c * p.x - s * p.y, oy + s * p.x + c * p.y}; }
    Vec mapDir(Vec d) const { return {(c * d.x - s * d.y) / k, (s * d.x + c * d.y) / k}; }
};

class Canvas {
public:
    // pixels: width*height colours; mask: width*height bytes, all zero
    Canvas(Color* pixels, uint8_t* mask, int width, int height);

    int    width() const { return w_; }
    int    height() const { return h_; }
    Color* pixels() { return px_; }
    const Color* pixels() const { return px_; }

    void clear(Color c);

    void setXform(const Xform& x) { xf_ = x; }
    const Xform& xform() const { return xf_; }

    // A layer: the shapes between begin() and end() are painted in one
    // colour. alpha < 255 paints them translucent.
    void begin(Color c, uint8_t alpha = 255);
    void end();

    // Shapes, in local units. Call between begin() and end().
    void poly(const Vec* pts, int n);                  // convex polygon, either winding
    void rect(float x, float y, float w, float h);
    void disc(Vec c, float r);
    void ring(Vec c, float r0, float r1);              // annulus r0..r1
    // Annular sector; angles in radians, 0 = up, clockwise; a1 - a0 < pi
    void sector(Vec c, float r0, float r1, float a0, float a1);
    // Polyline with mitred joints and square ends, closed when the last
    // point repeats the first. clipMin/clipMax: optional box (local units)
    // that cuts the stroke, as the mech font clips each glyph to its cell.
    void stroke(const Vec* pts, int n, float width,
                const Vec* clipMin = nullptr, const Vec* clipMax = nullptr);
    void line(Vec a, Vec b, float width);              // square ends

private:
    static constexpr int kMaxPlanes  = 8;
    static constexpr int kMaxCircles = 2;

    struct Plane {
        float a, b, c;   // signed distance a*x + b*y + c, positive inside
    };
    struct Circle {
        float x, y, r;
        bool  inside;    // true: inside the circle; false: outside it
    };

    Plane plane(Vec localPoint, Vec localInwardNormal) const;
    void  raster(const Plane* pl, int np, const Circle* cl, int nc,
                 float x0, float y0, float x1, float y1);

    Color*   px_;
    uint8_t* mask_;
    int      w_;
    int      h_;
    Xform    xf_;
    Color    ink_   = 0;
    uint8_t  alpha_ = 255;
    bool     open_  = false;
    int      bx0_ = 0, by0_ = 0, bx1_ = -1, by1_ = -1;   // dirty box of the layer
};

}  // namespace hm
