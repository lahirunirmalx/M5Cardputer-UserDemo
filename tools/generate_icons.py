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
    h = y1 - y0
    for y in range(y0, y1 + 1):
        t = (y - y0) / max(1, h)
        d.line((x0, y, x1, y), fill=lerp(top_c, bot_c, t))

def fill_rounded_gradient(im, x0, y0, x1, y1, radius, top_c, bot_c):
    """Vertical gradient inside a rounded rect mask."""
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((x0, y0, x1, y1), radius=radius, fill=255)
    grad = Image.new("RGB", im.size, top_c)
    gd = ImageDraw.Draw(grad)
    vertical_gradient_rect(gd, 0, y0, im.size[0]-1, y1, top_c, bot_c)
    im.paste(grad, (0, 0), mask)

# ---------- Calculator ----------
def draw_calc(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    bx0 = round(s*6/56);  by0 = round(s*3/56)
    bx1 = round(s*50/56); by1 = round(s*53/56)
    r   = round(s*3/56)
    # body gradient: lighter top -> darker bottom
    fill_rounded_gradient(im, bx0, by0, bx1, by1, r, (74, 74, 92), (34, 34, 46))
    # 1px outline (sharp)
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=r, outline=(20, 20, 28), width=1)
    # top highlight (specular line)
    d.line((bx0+2, by0+1, bx1-2, by0+1), fill=(120, 120, 150))

    # LCD strip
    lx0 = bx0 + round(s*2/56)
    lx1 = bx1 - round(s*2/56)
    ly0 = round(s*8/56)
    ly1 = round(s*19/56)
    # LCD gradient (top brighter)
    fill_rounded_gradient(im, lx0, ly0, lx1, ly1, max(1, round(s*1/56)),
                          (38, 80, 30), (20, 50, 18))
    d.rounded_rectangle((lx0, ly0, lx1, ly1), radius=max(1, round(s*1/56)),
                        outline=(10, 30, 12), width=1)
    # bright '88.' in 7-seg
    seg_h = ly1 - ly0 - round(s*2/56)
    seg_w = round(seg_h * 0.55)
    seg_t = max(1, round(s*1.2/56))
    seg = (190, 250, 80)
    seg_dim = (50, 90, 25)
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
    digit_top, digit_bot   = (224, 224, 232), (170, 170, 180)
    op_top,    op_bot      = (252, 168,  56), (200, 110,  20)
    eq_top,    eq_bot      = (180, 255,  60), (110, 200,  20)
    palette = [
        [(digit_top, digit_bot)]*3 + [(op_top, op_bot)],
        [(digit_top, digit_bot)]*3 + [(op_top, op_bot)],
        [(digit_top, digit_bot)]*3 + [(op_top, op_bot)],
        [(digit_top, digit_bot), (digit_top, digit_bot), (eq_top, eq_bot), (op_top, op_bot)],
    ]
    for rr in range(rows):
        for cc in range(cols):
            x0 = gx0 + cc*(cw + gx)
            y0 = gy0 + rr*(ch + gx)
            top_c, bot_c = palette[rr][cc]
            fill_rounded_gradient(im, x0, y0, x0+cw-1, y0+ch-1, 1, top_c, bot_c)
    return im

# ---------- Resistor ----------
def draw_resistor(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    mid_y = s // 2
    bx0 = round(s*10/56); bx1 = s - bx0
    by0 = round(s*18/56); by1 = round(s*38/56)

    # leads with subtle metal gradient (top hi, bottom shadow)
    lead_t = max(2, round(s*3/56))
    ly = mid_y - lead_t//2
    for dy in range(lead_t):
        t = dy / max(1, lead_t-1)
        c = lerp((230, 230, 235), (130, 130, 140), t)
        d.line((0, ly+dy, bx0, ly+dy), fill=c)
        d.line((bx1, ly+dy, s, ly+dy), fill=c)

    # body gradient (top warm light, bottom shadow)
    fill_rounded_gradient(im, bx0, by0, bx1, by1, round(s*4/56),
                          (220, 188, 140), (160, 122, 70))
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=round(s*4/56),
                        outline=(96, 64, 24), width=1)

    band_colors = [
        (236,  72,  60),   # red
        ( 16,  16,  20),   # black
        (236,  72,  60),   # red
        (252, 180,  20),   # gold
    ]
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
        # vertical highlight on each band
        for dx in range(band_w):
            t = dx / max(1, band_w-1)
            hi = shade(c, 1.25)
            lo = shade(c, 0.75)
            col = lerp(hi, lo, t)
            d.line((x+dx, band_y0, x+dx, band_y1), fill=col)
    return im

# ---------- MP3 ----------
def draw_mp3(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    bx0 = round(s*5/56);  by0 = round(s*5/56)
    bx1 = round(s*51/56); by1 = round(s*51/56)
    # body gradient
    fill_rounded_gradient(im, bx0, by0, bx1, by1, round(s*4/56),
                          (42, 42, 52), (16, 16, 22))
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=round(s*4/56),
                        outline=(8, 8, 12), width=1)
    # top highlight
    d.line((bx0+3, by0+1, bx1-3, by0+1), fill=(80, 80, 96))

    # screen with vertical gradient + glow
    scr_x0 = bx0 + round(s*3/56)
    scr_x1 = bx1 - round(s*3/56)
    scr_y0 = by0 + round(s*3/56)
    scr_y1 = round(s*22/56)
    fill_rounded_gradient(im, scr_x0, scr_y0, scr_x1, scr_y1, 1,
                          (14, 32, 10), (4, 12, 4))
    d.rounded_rectangle((scr_x0, scr_y0, scr_x1, scr_y1), radius=1,
                        outline=(0, 0, 0), width=1)
    # MP3 pixel text
    seg = (190, 250, 80)
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

    # play triangle with gradient
    tri_cx = (bx0 + bx1)//2
    tri_y0 = round(s*26/56)
    tri_y1 = round(s*46/56)
    tri_size = (tri_y1 - tri_y0)
    p = [
        (tri_cx - tri_size//2, tri_y0),
        (tri_cx - tri_size//2, tri_y1),
        (tri_cx + tri_size//2 + tri_size//4, (tri_y0 + tri_y1)//2),
    ]
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).polygon(p, fill=255)
    grad = Image.new("RGB", im.size, (153, 255, 0))
    gd = ImageDraw.Draw(grad)
    vertical_gradient_rect(gd, 0, tri_y0, s-1, tri_y1, (200, 255, 110), (110, 200, 0))
    im.paste(grad, (0, 0), mask)

    # LED indicator with halo
    led_r = max(1, round(s*1.5/56))
    led_x = bx1 - round(s*4/56)
    led_y = by0 + round(s*3/56)
    d.ellipse((led_x - led_r - 1, led_y - led_r - 1, led_x + led_r + 1, led_y + led_r + 1),
              fill=(120, 30, 30))
    d.ellipse((led_x - led_r, led_y - led_r, led_x + led_r, led_y + led_r),
              fill=(240, 80, 80))
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
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).polygon(pts, fill=255)
    grad = Image.new("RGB", im.size, (96, 165, 250))
    gd = ImageDraw.Draw(grad)
    top    = (110, 180, 255)
    mid    = (170, 110, 240)
    bottom = (240,  84, 168)
    for y in range(s):
        t = y / max(1, s-1)
        c = lerp(top, mid, t*2) if t < 0.5 else lerp(mid, bottom, (t-0.5)*2)
        gd.line((0, y, s, y), fill=c)
    im.paste(grad, (0, 0), mask)

    # 1px dark outline for crisp edge on IPS
    d.polygon(pts, outline=(40, 30, 80))

    # bright twinkle highlight
    hl_r = max(1, round(s*2.5/56))
    hl_x = cx - round(s*5/56)
    hl_y = cy - round(s*8/56)
    d.ellipse((hl_x - hl_r, hl_y - hl_r, hl_x + hl_r, hl_y + hl_r), fill=(255, 255, 255))
    return im

# ---------- LED ----------
def draw_led(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    cx = cy = s // 2
    r_outer = round(s*23/56)
    r_core  = round(s*6/56)

    # outer shadow ring
    d.ellipse((cx - r_outer - 2, cy - r_outer - 2, cx + r_outer + 2, cy + r_outer + 2),
              fill=(20, 20, 28))
    # dark border ring
    d.ellipse((cx - r_outer - 1, cy - r_outer - 1, cx + r_outer + 1, cy + r_outer + 1),
              fill=(40, 40, 50))
    # quadrants
    red     = (236,  72,  72)
    yellow  = (252, 204,  64)
    cyan    = ( 64, 196, 240)
    magenta = (216,  88, 200)
    bbox = (cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer)
    d.pieslice(bbox, start=180, end=270, fill=red)
    d.pieslice(bbox, start=270, end=360, fill=yellow)
    d.pieslice(bbox, start=0,   end=90,  fill=cyan)
    d.pieslice(bbox, start=90,  end=180, fill=magenta)
    # quadrant highlight wedges (top arc gets a slight gradient towards white)
    # cross-hair separators
    sep = (16, 16, 24)
    d.line((cx, cy - r_outer, cx, cy + r_outer), fill=sep, width=1)
    d.line((cx - r_outer, cy, cx + r_outer, cy), fill=sep, width=1)
    # outer rim highlight (top-left arc)
    d.arc(bbox, start=200, end=290, fill=(255, 255, 255), width=1)
    # white hot core with outer glow
    glow_r = r_core + 2
    d.ellipse((cx - glow_r, cy - glow_r, cx + glow_r, cy + glow_r), fill=(255, 255, 220))
    d.ellipse((cx - r_core, cy - r_core, cx + r_core, cy + r_core), fill=(255, 255, 255))
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

    # drop shadow
    sd = max(1, round(s*1/56))
    d.rounded_rectangle(
        (body_left + sd, body_top + sd, body_right + sd, body_bot + sd),
        radius=round(s*2/56), fill=(20, 20, 28))

    # back tab (slightly darker than front)
    fill_rounded_gradient(im, tab_left, tab_top, tab_right, tab_bottom + 4,
                          round(s*1/56), (220, 156, 32), (170, 110, 14))
    d.rounded_rectangle((tab_left, tab_top, tab_right, tab_bottom + 4),
                        radius=round(s*1/56), outline=(100, 60, 12), width=1)

    # white paper sheet peeking
    paper_x0 = body_left + round(s*3/56)
    paper_x1 = body_right - round(s*3/56)
    paper_y0 = body_top - round(s*2/56)
    paper_y1 = body_top + round(s*6/56)
    fill_rounded_gradient(im, paper_x0, paper_y0, paper_x1, paper_y1,
                          1, (252, 252, 248), (210, 210, 200))
    d.rounded_rectangle((paper_x0, paper_y0, paper_x1, paper_y1),
                        radius=1, outline=(150, 150, 140), width=1)

    # front folder body with bright gradient
    fy0 = body_top + round(s*2/56)
    fill_rounded_gradient(im, body_left, fy0, body_right, body_bot,
                          round(s*2/56), (252, 196,  72), (196, 132,  20))
    d.rounded_rectangle((body_left, fy0, body_right, body_bot),
                        radius=round(s*2/56), outline=(112, 72, 12), width=1)
    # specular top edge
    d.line((body_left + 2, fy0 + 1, body_right - 2, fy0 + 1), fill=(255, 232, 144))
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
    fill_mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(fill_mask).polygon(body, fill=255)
    grad = Image.new("RGB", im.size, (60, 60, 70))
    gd = ImageDraw.Draw(grad)
    vertical_gradient_rect(gd, 0, by0, s-1, by1, (110, 110, 130), (40, 40, 52))
    im.paste(grad, (0, 0), fill_mask)
    d.polygon(body, outline=(20, 20, 28))
    # bezel ring
    bz_y = by0 + round(s * 3 / 56)
    d.rectangle((cx - body_top_w // 2, by0, cx + body_top_w // 2, bz_y), fill=(180, 180, 195))
    d.rectangle((cx - body_top_w // 2, by0, cx + body_top_w // 2, bz_y), outline=(40, 40, 52))
    # beam cone (yellow gradient)
    bm0 = round(s * 14 / 56)
    bm1 = by0 - 1
    beam = [
        (cx - body_top_w // 2, by0),
        (cx + body_top_w // 2, by0),
        (cx + round(s * 14 / 56), bm0),
        (cx - round(s * 14 / 56), bm0),
    ]
    bm_mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(bm_mask).polygon(beam, fill=160)
    bg = Image.new("RGB", im.size, (255, 240, 120))
    bgd = ImageDraw.Draw(bg)
    vertical_gradient_rect(bgd, 0, bm0, s-1, by0, (255, 255, 220), (255, 200, 80))
    im.paste(bg, (0, 0), bm_mask)
    # bright lamp dot
    d.ellipse((cx - 3, by0 - 1, cx + 3, by0 + 4), fill=(255, 255, 230))
    return im

# ---------- Sysinfo ----------
def draw_sysinfo(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Chip (square IC body)
    cx = cy = s // 2
    chip_r = round(s * 18 / 56)
    fill_rounded_gradient(im, cx - chip_r, cy - chip_r, cx + chip_r, cy + chip_r,
                          round(s * 2 / 56), (60, 60, 72), (28, 28, 36))
    d.rounded_rectangle((cx - chip_r, cy - chip_r, cx + chip_r, cy + chip_r),
                        radius=round(s * 2 / 56), outline=(16, 16, 22), width=1)
    # IC pins (cyan/silver)
    pin = (180, 200, 220)
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
    d.ellipse((cx - chip_r + 3, cy - chip_r + 3, cx - chip_r + 7, cy - chip_r + 7), fill=(220, 220, 220))
    # Green text label "i" (info) in center
    label = "i"
    # simple block 'i'
    iw = round(s * 4 / 56)
    ih = round(s * 12 / 56)
    bx = cx - iw // 2
    by = cy - ih // 2
    d.rectangle((bx, by, bx + iw, by + max(1, round(s * 2 / 56))), fill=(153, 255, 0))      # dot
    d.rectangle((bx, by + round(s * 4 / 56), bx + iw, by + ih), fill=(153, 255, 0))         # stem
    return im

# ---------- Snake ----------
def draw_snake(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Black play field
    fx0 = round(s * 6 / 56);  fy0 = round(s * 6 / 56)
    fx1 = round(s * 50 / 56); fy1 = round(s * 50 / 56)
    d.rounded_rectangle((fx0, fy0, fx1, fy1), radius=round(s * 2 / 56), fill=(18, 24, 16))
    d.rounded_rectangle((fx0, fy0, fx1, fy1), radius=round(s * 2 / 56),
                        outline=(60, 90, 50), width=1)
    # Grid lines (faint)
    gline = (28, 38, 22)
    steps = 6
    step_w = (fx1 - fx0) // steps
    for i in range(1, steps):
        x = fx0 + i * step_w
        d.line((x, fy0 + 1, x, fy1 - 1), fill=gline)
        d.line((fx0 + 1, fy0 + i * step_w, fx1 - 1, fy0 + i * step_w), fill=gline)
    # Snake body (green segments)
    body_c = (140, 220, 60)
    head_c = (200, 255, 110)
    eye_c  = (16, 16, 16)
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
    d.ellipse((ax, ay, ax + cell, ay + cell), fill=(232, 60, 60))
    d.rectangle((ax + cell // 2 - 1, ay - 2, ax + cell // 2, ay), fill=(80, 160, 40))
    return im

# ---------- Tic-Tac-Toe ----------
def draw_tictactoe(im):
    d = ImageDraw.Draw(im)
    s = im.size[0]
    # Board background (light card)
    bx0 = round(s * 6 / 56);  by0 = round(s * 6 / 56)
    bx1 = round(s * 50 / 56); by1 = round(s * 50 / 56)
    fill_rounded_gradient(im, bx0, by0, bx1, by1, round(s * 2 / 56),
                          (245, 240, 230), (210, 200, 184))
    d.rounded_rectangle((bx0, by0, bx1, by1), radius=round(s * 2 / 56),
                        outline=(80, 70, 50), width=1)
    grid_c = (60, 50, 30)
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
           fill=(220, 60, 60), width=max(2, round(s * 3 / 56)))
    d.line((cx0 + cell_w - pad, cy0 + pad, cx0 + pad, cy0 + cell_h - pad),
           fill=(220, 60, 60), width=max(2, round(s * 3 / 56)))
    # O in center
    ocx = bx0 + gw // 2
    ocy = by0 + gh // 2
    ro = cell_w // 2 - pad
    d.ellipse((ocx - ro, ocy - ro, ocx + ro, ocy + ro),
              outline=(60, 120, 220), width=max(2, round(s * 3 / 56)))
    # X in bottom-right
    bx_x = bx0 + 2 * cell_w + 2
    bx_y = by0 + 2 * cell_h + 2
    d.line((bx_x + pad, bx_y + pad, bx_x + cell_w - pad, bx_y + cell_h - pad),
           fill=(220, 60, 60), width=max(2, round(s * 3 / 56)))
    d.line((bx_x + cell_w - pad, bx_y + pad, bx_x + pad, bx_y + cell_h - pad),
           fill=(220, 60, 60), width=max(2, round(s * 3 / 56)))
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
            im = fresh(side)
            fn(im)
            data = to_565(im)
            path = os.path.join(ROOT, asset_dir, f"{file_prefix}_{suffix}.h")
            if args.dry_run:
                print(f"would write {path}")
            else:
                emit_header(path, f"{var_prefix}_{suffix}", side, data,
                            f"{key} icon (IPS-tuned: gradients/highlights). R5G6B5.")
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
