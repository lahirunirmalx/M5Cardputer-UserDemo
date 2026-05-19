/**
 * @file seven_seg.cpp
 * @brief Implementation of the shared 7-segment renderer.
 */
#include "seven_seg.h"
#include <string.h>

namespace SEVEN_SEG {

/* Segment bit map per character (a=0x01 b=0x02 c=0x04 d=0x08 e=0x10 f=0x20 g=0x40)
 *
 *    aaaa
 *   f    b
 *   f    b
 *    gggg
 *   e    c
 *   e    c
 *    dddd
 */
static const uint8_t SEGS_0_9[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

int str_width(const char* s)
{
    if (!s) return 0;
    int total = 0;
    int n = (int)strlen(s);
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == ':')      total += ST + GAP;
        else if (c == '.') total += DOT_W + GAP;
        else if (c == ' ') total += DW / 2 + GAP;
        else               total += DW + GAP;
    }
    if (total > 0) total -= GAP;
    return total;
}

void draw_char(LGFX_Sprite* canvas, char c, int x, int y,
               uint32_t on, uint32_t off)
{
    if (!canvas) return;
    uint8_t m;
    if (c >= '0' && c <= '9') m = SEGS_0_9[c - '0'];
    else if (c == '-')        m = 0x40;
    else if (c == 'E')        m = 0x79;
    else if (c == 'r')        m = 0x50;
    else                      m = 0;

    uint32_t ca = (m & 0x01) ? on : off;
    uint32_t cb = (m & 0x02) ? on : off;
    uint32_t cc = (m & 0x04) ? on : off;
    uint32_t cd = (m & 0x08) ? on : off;
    uint32_t ce = (m & 0x10) ? on : off;
    uint32_t cf = (m & 0x20) ? on : off;
    uint32_t cg = (m & 0x40) ? on : off;

    int iw = DW - 2 * ST;
    canvas->fillRect(x + ST,        y,                iw, ST,   ca);
    canvas->fillRect(x,             y + ST,           ST, VLEN,            cf);
    canvas->fillRect(x + DW - ST,   y + ST,           ST, VLEN,            cb);
    canvas->fillRect(x + ST,        y + HALF,         iw, ST,   cg);
    canvas->fillRect(x,             y + HALF + ST,    ST, VLEN,            ce);
    canvas->fillRect(x + DW - ST,   y + HALF + ST,    ST, VLEN,            cc);
    canvas->fillRect(x + ST,        y + DH - ST,      iw, ST,   cd);
}

void draw_str(LGFX_Sprite* canvas, const char* s, int right_x, int y,
              uint32_t on, uint32_t off)
{
    if (!canvas || !s) return;
    int x = right_x - str_width(s);
    int n = (int)strlen(s);
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == ':') {
            int dot_y1 = y + ST + (VLEN - ST) / 2;
            int dot_y2 = y + HALF + ST + (VLEN - ST) / 2;
            canvas->fillRect(x, dot_y1, ST, ST, on);
            canvas->fillRect(x, dot_y2, ST, ST, on);
            x += ST + GAP;
        } else if (c == '.') {
            canvas->fillRect(x, y + DH - ST, DOT_W, ST, on);
            x += DOT_W + GAP;
        } else if (c == ' ') {
            x += DW / 2 + GAP;
        } else {
            draw_char(canvas, c, x, y, on, off);
            x += DW + GAP;
        }
    }
}

} // namespace SEVEN_SEG
