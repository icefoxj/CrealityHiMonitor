#pragma once

// The chamfered straight-line stroke font of the house mech livery (the
// same glyphs as cad/mech.py in FilamentDryer and mech_font.py on the Tab5
// stand), drawn on the display instead of inlaid in plastic.
//
// Glyphs are centre lines on a grid 6 units tall and up to 4 wide. A glyph
// is stroked with mitred joints and square ends, then clipped to its cell,
// so stroke ends that meet another stroke stay inside it. Upper case,
// digits and - / . : < > + % and the degree sign ('\xB0').

#include "gfx.h"

namespace hm {

enum class Align { Left, Center, Right };

struct TextStyle {
    float cap;       // cap height, outside the strokes (local units)
    float weight;    // stroke width
    float spacing;   // gap between glyph cells; < 0 means "same as weight"
};

// Width of the string in local units
float textWidth(const char* s, const TextStyle& st);

// Draws the string with its baseline at y. x is the left edge, centre or
// right edge depending on align. Call between Canvas::begin() and end().
void drawText(Canvas& cv, const char* s, float x, float y, const TextStyle& st,
              Align align = Align::Left);

// True when the font has a glyph for c (other characters draw as a space)
bool hasGlyph(char c);

}  // namespace hm
