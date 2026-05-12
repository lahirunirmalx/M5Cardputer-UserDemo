"""
generate_icons.py - generate M5Cardputer launcher icons.

Target screen: 1.14" 240x135 IPS LCD (ST7789V2), RGB565.
Output: C headers with `static const uint16_t image_data_<app>_<size>[N]`
arrays consumed by AppIcon_t(...) in each app's _Packer.

Design:
  - 56x56 big icon, 40x40 small icon per app (matches launcher slots)
  - Background 0x3ce7 (bright green) matches the existing icon set
  - Vertical gradients + 1px outlines + tiny specular highlights
    so icons read well on a vivid IPS panel
  - Pixel-sharp edges (no anti-aliasing at this resolution)

Usage:
    python3 tools/generate_icons.py [--preview]

Each draw_* function paints one app icon onto a PIL RGB Image. Add a new
app by:
  1. writing a draw_<name>(im) -> None function,
  2. adding a tuple to ICONS = [...] below,
  3. running this script. Headers land in main/apps/<dir>/assets/.
"""

import argparse
import os
from PIL import Image, ImageDraw

# Resolve repo root from this script's location: tools/generate_icons.py
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ROOT = os.path.join(REPO, "main", "apps")

BG_RGB = (57, 158, 57)   # 0x3ce7
# 1 = pixel-perfect (sharp 1px edges, matches set_wifi/hello/notepad reference style).
# >1 supersamples + LANCZOS-downscales → softer edges, but at 56px that reads as blur
# on the 240x135 IPS panel.
SUPERSAMPLE = 1

# Palette extracted from the default app icons (set_wifi, hello, texteditor, chat,
# repl, timer, keyboard, radio, record, ir, wifi_scan). Bold, saturated, retro
# pixel-art colors — no gradients, no anti-alias mid-tones. Use ONLY these for new
# app icons so the launcher reads as a single set.
P_BLACK    = (  0,   0,   0)
P_WHITE    = (240, 240, 240)
P_LGRAY    = (192, 192, 192)
P_GRAY     = (128, 128, 128)
P_DGRAY    = ( 64,  64,  64)
P_LBLUE    = (160, 208, 240)
P_CYAN     = (  0, 144, 192)
P_TEAL     = ( 96, 192, 176)
P_BLUE     = ( 48,  96, 144)
P_NAVY     = ( 16,  48, 112)
P_MAGENTA  = (192,  64, 144)
P_PINK     = (240, 112, 224)
P_PURPLE   = (128,   0, 128)
P_LAVENDER = (112,  96, 160)
P_BROWN    = (112,  64,  32)
P_TAN      = (192, 128,  32)
P_LIME     = (112, 224,  48)
P_GREEN    = ( 48, 144,  32)
P_DGREEN   = ( 16,  48,  16)
P_ORANGE   = (240,  96,  32)
P_YELLOW   = (224, 224, 192)
P_RED      = (200,   0,   0)

def rgb_to_565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def to_565(im):
    px = im.load()
    w, h = im.size
    return [rgb_to_565(*px[x, y]) for y in range(h) for x in range(w)]

def emit_header(path, varname, side, data, comment):
    n = side * side
    assert len(data) == n
    lines = [
        "/*******************************************************************************",
        f" * {comment}",
        " *******************************************************************************/",
        "#include <stdint.h>",
        "",
        f"static const uint16_t {varname}[{n}] = {{",
    ]
    for r in range(side):
        chunk = data[r*side:(r+1)*side]
        lines.append("    " + ", ".join(f"0x{v:04x}" for v in chunk) + ",")
    lines[-1] = lines[-1].rstrip(",")
    lines += ["};", ""]
    with open(path, "w") as f:
        f.write("\n".join(lines))

def fresh(side):
    return Image.new("RGB", (side, side), BG_RGB)

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)

def vertical_gradient_rect(d, x0, y0, x1, y1, top_c, bot_c):
    # Flat fill (basic colors, no gradient) — keep signature for callers.
    d.rectangle((x0, y0, x1, y1), fill=top_c)

def fill_rounded_gradient(im, x0, y0, x1, y1, radius, top_c, bot_c):
    # Flat rounded fill (basic colors, no gradient) — keep signature for callers.
    ImageDraw.Draw(im).rounded_rectangle((x0, y0, x1, y1), radius=radius, fill=top_c)

# ---------- Calculator ----------
def draw_calc(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    bx0 = round(s*6/56);  by0 = round(s*3/56)
    bx1 = round(s*50/56); by1 = round(s*53/56)
    r   = round(s*3/56)
    # body — dark gray, black outline
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=r, fill=P_DGRAY, outline=P_BLACK, width=1)
    # LCD strip — black with lime digits
    lx0 = bx0 + round(s*2/56)
    lx1 = bx1 - round(s*2/56)
    ly0 = round(s*8/56)
    ly1 = round(s*19/56)
    d.rounded_rectangle((lx0, ly0, lx1, ly1), radius=max(1, round(s*1/56)),
                        fill=P_BLACK, outline=P_BLACK, width=1)
    # bright '88.' in 7-seg
    seg_h = ly1 - ly0 - round(s*2/56)
    seg_w = round(seg_h * 0.55)
    seg_t = max(1, round(s*1.2/56))
    seg = P_LIME
    seg_dim = P_DGREEN
    def draw_dig(x, y, mask, on, off):
        ih, iw, t = seg_h, seg_w, seg_t
        half = (ih - t) // 2
        # a
        d.rectangle((x+t, y, x+iw-t, y+t-1), fill=on if mask & 0x01 else off)
        # f
        d.rectangle((x, y+t, x+t-1, y+half-1), fill=on if mask & 0x20 else off)
        # b
        d.rectangle((x+iw-t, y+t, x+iw-1, y+half-1), fill=on if mask & 0x02 else off)
        # g
        d.rectangle((x+t, y+half, x+iw-t, y+half+t-1), fill=on if mask & 0x40 else off)
        # e
        d.rectangle((x, y+half+t, x+t-1, y+ih-t-1), fill=on if mask & 0x10 else off)
        # c
        d.rectangle((x+iw-t, y+half+t, x+iw-1, y+ih-t-1), fill=on if mask & 0x04 else off)
        # d
        d.rectangle((x+t, y+ih-t, x+iw-t, y+ih-1), fill=on if mask & 0x08 else off)
    gap = max(1, round(s*1/56))
    digit_y = ly0 + round(s*1/56)
    total = 2*seg_w + gap + max(1, round(s*1.5/56))
    sx = lx1 - round(s*2/56) - total
    draw_dig(sx, digit_y, 0x7F, seg, seg_dim)
    draw_dig(sx + seg_w + gap, digit_y, 0x7F, seg, seg_dim)
    dot_x = sx + 2*seg_w + 2*gap
    dot_y = digit_y + seg_h - seg_t
    d.rectangle((dot_x, dot_y, dot_x + seg_t, dot_y + seg_t), fill=seg)

    # Button grid
    gy0 = round(s*22/56)
    gy1 = round(s*50/56)
    gx0 = bx0 + round(s*2/56)
    gx1 = bx1 - round(s*2/56)
    cols, rows = 4, 4
    gx = max(1, round(s*1/56))
    cw = (gx1 - gx0 - (cols-1)*gx) // cols
    ch = (gy1 - gy0 - (rows-1)*gx) // rows
    digit_c = P_LGRAY
    op_c    = P_ORANGE
    eq_c    = P_LIME
    keys = [
        [digit_c, digit_c, digit_c, op_c],
        [digit_c, digit_c, digit_c, op_c],
        [digit_c, digit_c, digit_c, op_c],
        [digit_c, digit_c, eq_c,    op_c],
    ]
    for rr in range(rows):
        for cc in range(cols):
            x0 = gx0 + cc*(cw + gx)
            y0 = gy0 + rr*(ch + gx)
            d.rounded_rectangle((x0, y0, x0+cw-1, y0+ch-1), radius=1, fill=keys[rr][cc])
    return im

# ---------- Resistor ----------
def draw_resistor(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    mid_y = s // 2
    bx0 = round(s*10/56); bx1 = s - bx0
    by0 = round(s*18/56); by1 = round(s*38/56)

    # leads — light gray
    lead_t = max(2, round(s*3/56))
    ly = mid_y - lead_t//2
    d.rectangle((0, ly, bx0, ly + lead_t - 1), fill=P_LGRAY)
    d.rectangle((bx1, ly, s, ly + lead_t - 1), fill=P_LGRAY)

    # body — tan, brown outline
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=round(s*4/56),
                        fill=P_TAN, outline=P_BROWN, width=1)

    band_colors = [P_RED, P_BLACK, P_RED, P_YELLOW]
    band_y0 = by0 + round(s*2/56)
    band_y1 = by1 - round(s*2/56)
    band_w  = round(s*5/56)
    xs = [
        bx0 + round(s*5/56),
        bx0 + round(s*12/56),
        bx0 + round(s*22/56),
        bx0 + round(s*30/56),
    ]
    for x, c in zip(xs, band_colors):
        d.rectangle((x, band_y0, x + band_w - 1, band_y1), fill=c)
    return im

# ---------- MP3 ----------
def draw_mp3(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    bx0 = round(s*5/56);  by0 = round(s*5/56)
    bx1 = round(s*51/56); by1 = round(s*51/56)
    # body — dark gray
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=round(s*4/56),
                        fill=P_DGRAY, outline=P_BLACK, width=1)

    # screen — black with lime text
    scr_x0 = bx0 + round(s*3/56)
    scr_x1 = bx1 - round(s*3/56)
    scr_y0 = by0 + round(s*3/56)
    scr_y1 = round(s*22/56)
    d.rounded_rectangle((scr_x0, scr_y0, scr_x1, scr_y1), radius=1,
                        fill=P_BLACK, outline=P_BLACK, width=1)
    # MP3 pixel text
    seg = P_LIME
    GLYPHS = {
        'M': ["#...#","##.##","#.#.#","#...#","#...#","#...#","#...#"],
        'P': ["####.","#...#","#...#","####.","#....","#....","#...."],
        '3': [".###.","#...#","....#",".###.","....#","#...#",".###."],
    }
    px_w = max(1, round(s*1/56))
    px_h = max(1, round(s*1/56))
    text = "MP3"
    total_w = len(text) * 5 * px_w + (len(text)-1) * px_w
    tx = (scr_x0 + scr_x1)//2 - total_w//2
    ty = (scr_y0 + scr_y1)//2 - 7*px_h//2
    cur_x = tx
    for ch in text:
        for ry, row in enumerate(GLYPHS[ch]):
            for cx, cell in enumerate(row):
                if cell == '#':
                    x0 = cur_x + cx*px_w
                    y0 = ty + ry*px_h
                    d.rectangle((x0, y0, x0+px_w-1, y0+px_h-1), fill=seg)
        cur_x += 5*px_w + px_w

    # play triangle (flat green)
    tri_cx = (bx0 + bx1)//2
    tri_y0 = round(s*26/56)
    tri_y1 = round(s*46/56)
    tri_size = (tri_y1 - tri_y0)
    p = [
        (tri_cx - tri_size//2, tri_y0),
        (tri_cx - tri_size//2, tri_y1),
        (tri_cx + tri_size//2 + tri_size//4, (tri_y0 + tri_y1)//2),
    ]
    d.polygon(p, fill=P_LIME)

    # LED indicator (flat red)
    led_r = max(1, round(s*1.5/56))
    led_x = bx1 - round(s*4/56)
    led_y = by0 + round(s*3/56)
    d.ellipse((led_x - led_r, led_y - led_r, led_x + led_r, led_y + led_r), fill=P_RED)
    return im

# ---------- Gemini ----------
def draw_gemini(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    cx = cy = s // 2
    r_out = round(s*26/56)
    r_in  = round(s*7/56)
    pts = [
        (cx,           cy - r_out),
        (cx + r_in,    cy - r_in),
        (cx + r_out,   cy),
        (cx + r_in,    cy + r_in),
        (cx,           cy + r_out),
        (cx - r_in,    cy + r_in),
        (cx - r_out,   cy),
        (cx - r_in,    cy - r_in),
    ]
    d.polygon(pts, fill=P_MAGENTA, outline=P_BLACK)
    return im

# ---------- LED ----------
def draw_led(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    cx = cy = s // 2
    r_outer = round(s*23/56)
    r_core  = round(s*6/56)

    # outer black border ring
    d.ellipse((cx - r_outer - 1, cy - r_outer - 1, cx + r_outer + 1, cy + r_outer + 1),
              fill=P_BLACK)
    # quadrants
    bbox = (cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer)
    d.pieslice(bbox, start=180, end=270, fill=P_RED)
    d.pieslice(bbox, start=270, end=360, fill=P_ORANGE)
    d.pieslice(bbox, start=0,   end=90,  fill=P_CYAN)
    d.pieslice(bbox, start=90,  end=180, fill=P_MAGENTA)
    # cross-hair separators
    d.line((cx, cy - r_outer, cx, cy + r_outer), fill=P_BLACK, width=1)
    d.line((cx - r_outer, cy, cx + r_outer, cy), fill=P_BLACK, width=1)
    # white hot core
    d.ellipse((cx - r_core, cy - r_core, cx + r_core, cy + r_core), fill=P_WHITE)
    return im

# ---------- Files ----------
def draw_files(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    body_top   = round(s*16/56)
    body_bot   = round(s*47/56)
    body_left  = round(s*7/56)
    body_right = round(s*49/56)
    tab_left   = body_left
    tab_right  = round(s*26/56)
    tab_top    = round(s*10/56)
    tab_bottom = body_top + 2

    # back tab — tan
    d.rounded_rectangle((tab_left, tab_top, tab_right, tab_bottom + 4),
                        radius=round(s*1/56), fill=P_TAN, outline=P_BLACK, width=1)

    # white paper sheet peeking
    paper_x0 = body_left + round(s*3/56)
    paper_x1 = body_right - round(s*3/56)
    paper_y0 = body_top - round(s*2/56)
    paper_y1 = body_top + round(s*6/56)
    d.rounded_rectangle((paper_x0, paper_y0, paper_x1, paper_y1),
                        radius=1, fill=P_WHITE, outline=P_BLACK, width=1)

    # front folder body — orange
    fy0 = body_top + round(s*2/56)
    d.rounded_rectangle((body_left, fy0, body_right, body_bot),
                        radius=round(s*2/56), fill=P_ORANGE, outline=P_BLACK, width=1)
    return im

# ---------- Torch (flashlight) ----------
def draw_torch(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    cx = cy = s // 2
    # Body (dark): tapered torch outline
    body_top_w = round(s * 14 / 56)
    body_bot_w = round(s * 22 / 56)
    by0 = round(s * 22 / 56)
    by1 = round(s * 50 / 56)
    body = [
        (cx - body_top_w // 2, by0),
        (cx + body_top_w // 2, by0),
        (cx + body_bot_w // 2, by1),
        (cx - body_bot_w // 2, by1),
    ]
    d.polygon(body, fill=P_DGRAY, outline=P_BLACK)
    # bezel ring
    bz_y = by0 + round(s * 3 / 56)
    d.rectangle((cx - body_top_w // 2, by0, cx + body_top_w // 2, bz_y),
                fill=P_LGRAY, outline=P_BLACK)
    # beam cone — orange/yellow
    bm0 = round(s * 14 / 56)
    beam = [
        (cx - body_top_w // 2, by0),
        (cx + body_top_w // 2, by0),
        (cx + round(s * 14 / 56), bm0),
        (cx - round(s * 14 / 56), bm0),
    ]
    d.polygon(beam, fill=P_ORANGE)
    return im

# ---------- Sysinfo ----------
def draw_sysinfo(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Chip (square IC body)
    cx = cy = s // 2
    chip_r = round(s * 18 / 56)
    d.rounded_rectangle((cx - chip_r, cy - chip_r, cx + chip_r, cy + chip_r),
                        radius=round(s * 2 / 56), fill=P_DGRAY, outline=P_BLACK, width=1)
    # IC pins — light gray
    pin = P_LGRAY
    pin_n = 5
    pin_pad = round(s * 4 / 56)
    pin_step = (2 * chip_r - 2 * pin_pad) // (pin_n - 1)
    pin_len = round(s * 5 / 56)
    pin_w = max(1, round(s * 2 / 56))
    for i in range(pin_n):
        p = cy - chip_r + pin_pad + i * pin_step
        # left
        d.rectangle((cx - chip_r - pin_len, p, cx - chip_r - 1, p + pin_w - 1), fill=pin)
        # right
        d.rectangle((cx + chip_r + 1, p, cx + chip_r + pin_len, p + pin_w - 1), fill=pin)
        # top
        d.rectangle((cy - chip_r + pin_pad + i * pin_step, cy - chip_r - pin_len,
                     cy - chip_r + pin_pad + i * pin_step + pin_w - 1, cy - chip_r - 1), fill=pin)
        # bottom
        d.rectangle((cy - chip_r + pin_pad + i * pin_step, cy + chip_r + 1,
                     cy - chip_r + pin_pad + i * pin_step + pin_w - 1, cy + chip_r + pin_len), fill=pin)
    # Pin-1 indicator dot
    d.ellipse((cx - chip_r + 3, cy - chip_r + 3, cx - chip_r + 7, cy - chip_r + 7), fill=P_LGRAY)
    # Lime text label "i" (info) in center
    iw = round(s * 4 / 56)
    ih = round(s * 12 / 56)
    bx = cx - iw // 2
    by = cy - ih // 2
    d.rectangle((bx, by, bx + iw, by + max(1, round(s * 2 / 56))), fill=P_LIME)        # dot
    d.rectangle((bx, by + round(s * 4 / 56), bx + iw, by + ih), fill=P_LIME)           # stem
    return im

# ---------- Snake ----------
def draw_snake(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Black play field
    fx0 = round(s * 6 / 56);  fy0 = round(s * 6 / 56)
    fx1 = round(s * 50 / 56); fy1 = round(s * 50 / 56)
    d.rounded_rectangle((fx0, fy0, fx1, fy1), radius=round(s * 2 / 56),
                        fill=P_BLACK, outline=P_DGREEN, width=1)
    # Grid lines (faint dark green)
    gline = P_DGREEN
    steps = 6
    step_w = (fx1 - fx0) // steps
    for i in range(1, steps):
        x = fx0 + i * step_w
        d.line((x, fy0 + 1, x, fy1 - 1), fill=gline)
        d.line((fx0 + 1, fy0 + i * step_w, fx1 - 1, fy0 + i * step_w), fill=gline)
    # Snake body (lime segments)
    body_c = P_GREEN
    head_c = P_LIME
    eye_c  = P_BLACK
    cell = max(3, round(s * 6 / 56))
    cx0 = fx0 + step_w
    cy0 = fy0 + step_w * 3
    seg_xy = [
        (cx0 + 0 * cell, cy0),
        (cx0 + 1 * cell, cy0),
        (cx0 + 2 * cell, cy0),
        (cx0 + 2 * cell, cy0 - cell),
        (cx0 + 3 * cell, cy0 - cell),
    ]
    for (x, y) in seg_xy[:-1]:
        d.rounded_rectangle((x, y, x + cell - 1, y + cell - 1), radius=1, fill=body_c)
    # head
    hx, hy = seg_xy[-1]
    d.rounded_rectangle((hx, hy, hx + cell - 1, hy + cell - 1), radius=1, fill=head_c)
    # eyes
    d.rectangle((hx + cell - 3, hy + 1, hx + cell - 2, hy + 2), fill=eye_c)
    d.rectangle((hx + cell - 3, hy + cell - 3, hx + cell - 2, hy + cell - 2), fill=eye_c)
    # Apple (red dot)
    ax = fx0 + step_w * 4
    ay = fy0 + step_w * 4
    d.ellipse((ax, ay, ax + cell, ay + cell), fill=P_RED)
    d.rectangle((ax + cell // 2 - 1, ay - 2, ax + cell // 2, ay), fill=P_GREEN)
    return im

# ---------- Tic-Tac-Toe ----------
def draw_tictactoe(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Board background (light card)
    bx0 = round(s * 6 / 56);  by0 = round(s * 6 / 56)
    bx1 = round(s * 50 / 56); by1 = round(s * 50 / 56)
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=round(s * 2 / 56),
                        fill=P_WHITE, outline=P_BLACK, width=1)
    grid_c = P_BLACK
    # vertical lines
    gw = (bx1 - bx0)
    gh = (by1 - by0)
    x_a = bx0 + gw // 3
    x_b = bx0 + 2 * gw // 3
    y_a = by0 + gh // 3
    y_b = by0 + 2 * gh // 3
    d.line((x_a, by0 + 3, x_a, by1 - 3), fill=grid_c, width=2)
    d.line((x_b, by0 + 3, x_b, by1 - 3), fill=grid_c, width=2)
    d.line((bx0 + 3, y_a, bx1 - 3, y_a), fill=grid_c, width=2)
    d.line((bx0 + 3, y_b, bx1 - 3, y_b), fill=grid_c, width=2)
    # X in top-left
    cell_w = gw // 3
    cell_h = gh // 3
    cx0 = bx0 + 2
    cy0 = by0 + 2
    pad = max(2, round(s * 2 / 56))
    d.line((cx0 + pad, cy0 + pad, cx0 + cell_w - pad, cy0 + cell_h - pad),
           fill=P_RED, width=max(2, round(s * 3 / 56)))
    d.line((cx0 + cell_w - pad, cy0 + pad, cx0 + pad, cy0 + cell_h - pad),
           fill=P_RED, width=max(2, round(s * 3 / 56)))
    # O in center
    ocx = bx0 + gw // 2
    ocy = by0 + gh // 2
    ro = cell_w // 2 - pad
    d.ellipse((ocx - ro, ocy - ro, ocx + ro, ocy + ro),
              outline=P_CYAN, width=max(2, round(s * 3 / 56)))
    # X in bottom-right
    bx_x = bx0 + 2 * cell_w + 2
    bx_y = by0 + 2 * cell_h + 2
    d.line((bx_x + pad, bx_y + pad, bx_x + cell_w - pad, bx_y + cell_h - pad),
           fill=P_RED, width=max(2, round(s * 3 / 56)))
    d.line((bx_x + cell_w - pad, bx_y + pad, bx_x + pad, bx_y + cell_h - pad),
           fill=P_RED, width=max(2, round(s * 3 / 56)))
    return im

# ---------- TV-B-Gone ----------
def draw_tvbgone(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # IR remote (dark, vertical) on the left side, pointing right.
    rem_x0 = round(s * 4  / 56)
    rem_x1 = round(s * 22 / 56)
    rem_y0 = round(s * 12 / 56)
    rem_y1 = round(s * 48 / 56)
    d.rounded_rectangle((rem_x0, rem_y0, rem_x1, rem_y1),
                        radius=round(s*3/56), fill=P_DGRAY, outline=P_BLACK, width=1)
    # red power button at top of remote
    pcx = (rem_x0 + rem_x1) // 2
    pcy = rem_y0 + round(s*4/56)
    pr  = round(s*2.5/56)
    d.ellipse((pcx - pr, pcy - pr, pcx + pr, pcy + pr), fill=P_RED)
    # button matrix
    btn_c = P_LGRAY
    for ry in range(3):
        for cx in range(2):
            bx = rem_x0 + round(s*3/56) + cx * round(s*7/56)
            by = rem_y0 + round(s*12/56) + ry * round(s*8/56)
            d.rounded_rectangle((bx, by, bx + round(s*4/56), by + round(s*4/56)),
                                radius=1, fill=btn_c)
    # IR emitter + 3 concentric arc beams shooting to the right
    em_cx = rem_x1 + 1
    em_cy = rem_y0 + round(s*5/56)
    d.ellipse((em_cx - 2, em_cy - 2, em_cx + 2, em_cy + 2), fill=P_YELLOW)
    for r in (round(s*6/56), round(s*11/56), round(s*16/56)):
        d.arc((em_cx - r, em_cy - r, em_cx + r, em_cy + r),
              start=300, end=60, fill=P_LIME, width=max(1, round(s*1/56)))
    # TV on the right
    tv_x0 = round(s * 34 / 56)
    tv_x1 = round(s * 52 / 56)
    tv_y0 = round(s * 24 / 56)
    tv_y1 = round(s * 44 / 56)
    d.rounded_rectangle((tv_x0, tv_y0, tv_x1, tv_y1),
                        radius=round(s*2/56), fill=P_BLACK, outline=P_BLACK, width=1)
    # tv stand legs
    leg_y0 = tv_y1 + 1
    leg_y1 = tv_y1 + round(s*3/56)
    d.rectangle((tv_x0 + 2, leg_y0, tv_x0 + 4, leg_y1), fill=P_DGRAY)
    d.rectangle((tv_x1 - 4, leg_y0, tv_x1 - 2, leg_y1), fill=P_DGRAY)
    # red dot on tv = "off"
    pcx2 = (tv_x0 + tv_x1) // 2
    pcy2 = (tv_y0 + tv_y1) // 2
    pr2  = round(s*2.5/56)
    d.ellipse((pcx2 - pr2, pcy2 - pr2, pcx2 + pr2, pcy2 + pr2), fill=P_RED)
    return im

# ---------- BLE Pair (sparkle / broadcast) ----------
def draw_blepair(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Phone in the bottom-right corner (target)
    ph_x0 = round(s * 28 / 56)
    ph_x1 = round(s * 50 / 56)
    ph_y0 = round(s * 14 / 56)
    ph_y1 = round(s * 50 / 56)
    d.rounded_rectangle((ph_x0, ph_y0, ph_x1, ph_y1),
                        radius=round(s*3/56), fill=P_DGRAY, outline=P_BLACK, width=1)
    # phone screen
    scr_x0 = ph_x0 + round(s*1.5/56)
    scr_x1 = ph_x1 - round(s*1.5/56)
    scr_y0 = ph_y0 + round(s*3/56)
    scr_y1 = ph_y1 - round(s*3/56)
    d.rounded_rectangle((scr_x0, scr_y0, scr_x1, scr_y1), radius=1, fill=P_LBLUE)
    # "pair" prompt bar
    pr_x0 = scr_x0 + 2
    pr_x1 = scr_x1 - 2
    pr_y0 = (scr_y0 + scr_y1) // 2 - round(s*3/56)
    pr_y1 = (scr_y0 + scr_y1) // 2 + round(s*3/56)
    d.rectangle((pr_x0, pr_y0, pr_x1, pr_y1), fill=P_BLUE)
    # BLE antenna on left
    ant_cx = round(s * 12 / 56)
    ant_cy = round(s * 28 / 56)
    d.line((ant_cx, ant_cy - round(s*6/56), ant_cx, ant_cy + round(s*6/56)),
           fill=P_CYAN, width=max(2, round(s*2/56)))
    d.ellipse((ant_cx - 2, ant_cy - round(s*8/56),
               ant_cx + 2, ant_cy - round(s*5/56)), fill=P_CYAN)
    # broadcast arcs
    for r in (round(s*7/56), round(s*12/56), round(s*17/56)):
        d.arc((ant_cx - r, ant_cy - r, ant_cx + r, ant_cy + r),
              start=300, end=60, fill=P_CYAN, width=max(1, round(s*1/56)))
    return im

# ---------- ENV (BMP280 + SHT4x: temp + humidity) ----------
def draw_env(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Thermometer (left): stem + bulb
    stem_w = round(s * 7 / 56)
    stem_x = round(s * 16 / 56)
    stem_y0 = round(s * 8 / 56)
    bulb_r  = round(s * 7 / 56)
    bulb_cx = stem_x + stem_w // 2
    bulb_cy = round(s * 44 / 56)
    # Outer black casing (rounded top + bulb)
    d.rounded_rectangle((stem_x, stem_y0, stem_x + stem_w, bulb_cy),
                        radius=stem_w // 2, fill=P_BLACK)
    d.ellipse((bulb_cx - bulb_r, bulb_cy - bulb_r,
               bulb_cx + bulb_r, bulb_cy + bulb_r), fill=P_BLACK)
    # Inner white glass
    glass_pad = max(1, round(s * 1.5 / 56))
    d.rounded_rectangle((stem_x + glass_pad, stem_y0 + glass_pad,
                         stem_x + stem_w - glass_pad, bulb_cy - 1),
                        radius=(stem_w - 2 * glass_pad) // 2, fill=P_WHITE)
    # Mercury column — red
    merc_y0 = round(s * 22 / 56)
    d.rounded_rectangle((stem_x + glass_pad + 1, merc_y0,
                         stem_x + stem_w - glass_pad - 1, bulb_cy - 1),
                        radius=1, fill=P_RED)
    # Mercury bulb — red
    inner_r = bulb_r - glass_pad
    d.ellipse((bulb_cx - inner_r, bulb_cy - inner_r,
               bulb_cx + inner_r, bulb_cy + inner_r), fill=P_RED)
    # Scale ticks on the right of the stem
    tick_x0 = stem_x + stem_w + 1
    tick_n = 5
    tick_top = stem_y0 + 2
    tick_bot = merc_y0 + (bulb_cy - merc_y0) // 2
    for i in range(tick_n):
        ty = tick_top + i * (tick_bot - tick_top) // (tick_n - 1)
        tw = max(2, round(s * 3 / 56)) if i % 2 == 0 else max(1, round(s * 2 / 56))
        d.line((tick_x0, ty, tick_x0 + tw, ty), fill=P_BLACK)

    # Humidity water drop — cyan, black outline
    drop_cx = round(s * 40 / 56)
    drop_cy = round(s * 32 / 56)
    drop_r  = round(s * 9 / 56)
    drop_pts = [
        (drop_cx, drop_cy - drop_r - round(s * 6 / 56)),     # apex
        (drop_cx - drop_r, drop_cy - drop_r // 3),
        (drop_cx - drop_r, drop_cy + drop_r - 1),
        (drop_cx + drop_r, drop_cy + drop_r - 1),
        (drop_cx + drop_r, drop_cy - drop_r // 3),
    ]
    d.polygon(drop_pts, fill=P_CYAN, outline=P_BLACK)
    d.ellipse((drop_cx - drop_r, drop_cy - drop_r,
               drop_cx + drop_r, drop_cy + drop_r), fill=P_CYAN, outline=P_BLACK, width=1)
    return im

# ---------- Scales (digital weighing scale) ----------
def draw_scales(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Base / platter top — wide rounded plate
    plat_x0 = round(s * 5  / 56)
    plat_x1 = round(s * 51 / 56)
    plat_y0 = round(s * 12 / 56)
    plat_y1 = round(s * 22 / 56)
    d.rounded_rectangle((plat_x0, plat_y0, plat_x1, plat_y1),
                        radius=round(s * 2 / 56), fill=P_LGRAY, outline=P_BLACK, width=1)

    # Scale body — white front face
    body_x0 = round(s * 8  / 56)
    body_x1 = round(s * 48 / 56)
    body_y0 = plat_y1
    body_y1 = round(s * 50 / 56)
    d.rounded_rectangle((body_x0, body_y0, body_x1, body_y1),
                        radius=round(s * 3 / 56), fill=P_WHITE, outline=P_BLACK, width=1)

    # LCD screen — black, lime digits
    lcd_x0 = body_x0 + round(s * 4  / 56)
    lcd_x1 = body_x1 - round(s * 4  / 56)
    lcd_y0 = body_y0 + round(s * 5  / 56)
    lcd_y1 = body_y0 + round(s * 18 / 56)
    d.rounded_rectangle((lcd_x0, lcd_y0, lcd_x1, lcd_y1),
                        radius=max(1, round(s * 1 / 56)),
                        fill=P_BLACK, outline=P_BLACK, width=1)
    # 7-seg "0.0" digits
    seg_h = lcd_y1 - lcd_y0 - round(s * 2 / 56)
    seg_w = round(seg_h * 0.55)
    seg_t = max(1, round(s * 1.2 / 56))
    seg = P_LIME
    def draw_dig(x, y, mask):
        ih, iw, t = seg_h, seg_w, seg_t
        half = (ih - t) // 2
        if mask & 0x01: d.rectangle((x+t, y, x+iw-t, y+t-1), fill=seg)
        if mask & 0x20: d.rectangle((x, y+t, x+t-1, y+half-1), fill=seg)
        if mask & 0x02: d.rectangle((x+iw-t, y+t, x+iw-1, y+half-1), fill=seg)
        if mask & 0x40: d.rectangle((x+t, y+half, x+iw-t, y+half+t-1), fill=seg)
        if mask & 0x10: d.rectangle((x, y+half+t, x+t-1, y+ih-t-1), fill=seg)
        if mask & 0x04: d.rectangle((x+iw-t, y+half+t, x+iw-1, y+ih-t-1), fill=seg)
        if mask & 0x08: d.rectangle((x+t, y+ih-t, x+iw-t, y+ih-1), fill=seg)
    gap = max(1, round(s * 1 / 56))
    total_w = 2 * seg_w + gap + max(1, round(s * 2 / 56))
    sx = (lcd_x0 + lcd_x1) // 2 - total_w // 2
    sy = lcd_y0 + max(1, round(s * 1 / 56))
    draw_dig(sx, sy, 0x3F)   # 0
    dot_x = sx + seg_w + 1
    d.rectangle((dot_x, sy + seg_h - seg_t, dot_x + seg_t, sy + seg_h), fill=seg)
    draw_dig(sx + seg_w + gap + max(1, round(s * 2 / 56)), sy, 0x3F)   # 0

    # Front feet
    d.rectangle((body_x0 + 2, body_y1 - 2, body_x0 + 5, body_y1), fill=P_DGRAY)
    d.rectangle((body_x1 - 5, body_y1 - 2, body_x1 - 2, body_y1), fill=P_DGRAY)
    return im

# ---------- Claude Meter (usage gauge) ----------
def draw_claudemeter(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Body — black rounded panel
    bx0 = round(s * 4 / 56);  by0 = round(s * 6 / 56)
    bx1 = round(s * 52 / 56); by1 = round(s * 50 / 56)
    d.rounded_rectangle((bx0, by0, bx1, by1),
                        radius=round(s * 3 / 56), fill=P_BLACK, outline=P_BLACK, width=1)
    # Two horizontal usage bars: 5H (lime, ~50%) and 7D (orange, ~75%)
    bar_left  = bx0 + round(s * 4 / 56)
    bar_right = bx1 - round(s * 4 / 56)
    bar_w     = bar_right - bar_left
    bar_h     = round(s * 7 / 56)
    bar1_y    = by0 + round(s * 7 / 56)
    bar2_y    = bar1_y + bar_h + round(s * 6 / 56)
    # Bar 1 — 5H @ ~50%
    d.rectangle((bar_left, bar1_y, bar_right, bar1_y + bar_h), fill=P_DGRAY)
    d.rectangle((bar_left, bar1_y, bar_left + bar_w // 2, bar1_y + bar_h), fill=P_LIME)
    # Bar 2 — 7D @ ~75%
    d.rectangle((bar_left, bar2_y, bar_right, bar2_y + bar_h), fill=P_DGRAY)
    d.rectangle((bar_left, bar2_y, bar_left + (bar_w * 3) // 4, bar2_y + bar_h), fill=P_ORANGE)
    return im

ICONS = [
    ("calc",        draw_calc,       "app_calculator/assets", "calc",      "image_data_calc"),
    ("resistor",    draw_resistor,   "app_resistor/assets",   "resistor",  "image_data_resistor"),
    ("mp3",         draw_mp3,        "app_mp3/assets",        "mp3",       "image_data_mp3"),
    ("gemini",      draw_gemini,     "app_gemini/assets",     "gemini",    "image_data_gemini"),
    ("led",         draw_led,        "app_led/assets",        "led",       "image_data_led"),
    ("filemanager", draw_files,      "app_files/assets",      "files",     "image_data_filemanager"),
    ("torch",       draw_torch,      "app_torch/assets",      "torch",     "image_data_torch"),
    ("sysinfo",     draw_sysinfo,    "app_sysinfo/assets",    "sysinfo",   "image_data_sysinfo"),
    ("snake",       draw_snake,      "app_snake/assets",      "snake",     "image_data_snake"),
    ("tictactoe",   draw_tictactoe,  "app_tictactoe/assets",  "tictactoe", "image_data_tictactoe"),
    ("tvbgone",     draw_tvbgone,    "app_tvbgone/assets",    "tvbgone",   "image_data_tvbgone"),
    ("blepair",     draw_blepair,    "app_blepair/assets",    "blepair",   "image_data_blepair"),
    ("env",         draw_env,        "app_env/assets",        "env",       "image_data_env"),
    ("scales",      draw_scales,     "app_scales/assets",     "scales",    "image_data_scales"),
    ("claudemeter", draw_claudemeter,"app_claudemeter/assets","claudemeter","image_data_claudemeter"),
]

def main():
    ap = argparse.ArgumentParser(description="Generate M5Cardputer launcher icons")
    ap.add_argument("--preview", metavar="PATH", default=None,
                    help="also write a 4x-scaled preview PNG of all big icons")
    ap.add_argument("--only", metavar="NAME", action="append", default=None,
                    help="only generate this icon key (repeatable)")
    ap.add_argument("--dry-run", action="store_true",
                    help="print what would be written without touching files")
    args = ap.parse_args()

    todo = [t for t in ICONS if not args.only or t[0] in args.only]
    if args.only:
        unknown = set(args.only) - {t[0] for t in ICONS}
        if unknown:
            raise SystemExit(f"unknown icon name(s): {sorted(unknown)}")

    preview = None
    pd = None
    if args.preview:
        preview = Image.new("RGB", (len(todo) * 56*4 + (len(todo)+1)*8, 56*4 + 18),
                            (50, 50, 53))
        pd = ImageDraw.Draw(preview)

    for slot, (key, fn, asset_dir, file_prefix, var_prefix) in enumerate(todo):
        for is_big in (True, False):
            side = 56 if is_big else 40
            suffix = "big" if is_big else "small"
            im_ss = fresh(side * SUPERSAMPLE)
            fn(im_ss)
            im = im_ss.resize((side, side), Image.LANCZOS)
            data = to_565(im)
            path = os.path.join(ROOT, asset_dir, f"{file_prefix}_{suffix}.h")
            if args.dry_run:
                print(f"would write {path}")
            else:
                emit_header(path, f"{var_prefix}_{suffix}", side, data,
                            f"{key} icon (flat basic colors). R5G6B5.")
                print(f"wrote {path}")
            if is_big and preview is not None:
                scaled = im.resize((56*4, 56*4), Image.NEAREST)
                x = 8 + slot * (56*4 + 8)
                preview.paste(scaled, (x, 8))
                pd.text((x + 4, 8 + 56*4 + 2), key, fill=(220, 220, 220))

    if preview is not None:
        preview.save(args.preview)
        print(f"preview: {args.preview}")


if __name__ == "__main__":
    main()
