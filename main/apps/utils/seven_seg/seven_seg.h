/**
 * @file seven_seg.h
 * @brief Faux 7-segment digit renderer for LGFX_Sprite canvases.
 *
 * Shared by Calculator, Resistor, MP3 / WinAmp. Geometry is fixed (12x19
 * digit, 3px segment thickness) - matches the original per-app helpers so
 * existing layouts stay pixel-identical.
 *
 * Renderable characters:  0-9  .  -  :  space  E  r
 * ('E' and 'r' are used by Calculator for the "Err" divide-by-zero state.
 * ':' is used by the MP3 time display.)
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <M5GFX.h>

namespace SEVEN_SEG {

static constexpr int DW    = 12;
static constexpr int DH    = 19;
static constexpr int ST    = 3;
static constexpr int GAP   = 2;
static constexpr int DOT_W = 3;
static constexpr int HALF  = (DH - ST) / 2;
static constexpr int VLEN  = HALF - ST;

/* Width of a rendered string in pixels (no trailing gap). */
int str_width(const char* s);

/* Render one glyph at (x, y). 'on' = lit segment color, 'off' = ghost. */
void draw_char(LGFX_Sprite* canvas, char c, int x, int y,
               uint32_t on, uint32_t off);

/* Render a right-aligned string. Width is computed internally; the final
 * digit's right edge sits at right_x - 1. */
void draw_str(LGFX_Sprite* canvas, const char* s, int right_x, int y,
              uint32_t on, uint32_t off);

} // namespace SEVEN_SEG
