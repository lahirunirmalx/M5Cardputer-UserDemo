/**
 * @file app_calculator.cpp
 * @brief Calculator with on-screen keypad, formula bar, error state.
 *
 * Keys:  0-9 . digits/decimal   + - * /  operators  (use shift for +)
 *        Enter / =       equals
 *        Del / Bksp      backspace (clears full result if result is shown)
 *        Esc / C         clear all
 *        S               sign toggle (+/-)
 *        HOME            exit
 */
#include "app_calculator.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

/* Layout (canvas is 206x109) */
static constexpr int TITLE_Y      = 1;
static constexpr int FORMULA_Y    = 5;
static constexpr int DISPLAY_Y    = 19;
static constexpr int DISPLAY_H    = 25;
static constexpr int SEG_Y        = 22;   /* DISPLAY_Y + (DISPLAY_H - SEG_DH)/2 */
static constexpr int GRID_Y0      = 47;
static constexpr int GRID_ROWS    = 4;
static constexpr int GRID_COLS    = 5;
static constexpr int BTN_W        = 36;
static constexpr int BTN_H        = 12;
static constexpr int BTN_GX       = 4;
static constexpr int BTN_GY       = 1;
static constexpr int GRID_X0      = 5;
static constexpr int FOOTER_Y     = 100;
static constexpr size_t MAX_DISPLAY_LEN = 11;

/* 7-segment digit geometry */
static constexpr int SEG_DW       = 12;   /* digit width  */
static constexpr int SEG_DH       = 19;   /* digit height */
static constexpr int SEG_ST       = 3;    /* segment thickness */
static constexpr int SEG_GAP      = 2;    /* gap between digits */
static constexpr int SEG_DOT_W    = 3;    /* decimal-point square side */
static constexpr int SEG_HALF     = (SEG_DH - SEG_ST) / 2;  /* y of middle bar */
static constexpr int SEG_VLEN     = SEG_HALF - SEG_ST;       /* vertical segment length */

static const uint32_t COLOR_ACCENT      = (uint32_t)0x99FF00;
static const uint32_t COLOR_DISPLAY_BG  = (uint32_t)0x1E1E22;
static const uint32_t COLOR_SEG_OFF     = (uint32_t)0x252528;  /* faint "ghost" segments */
static const uint32_t COLOR_SEG_ERR_ON  = (uint32_t)0xFF6464;
static const uint32_t COLOR_SEG_ERR_OFF = (uint32_t)0x2A1F1F;
static const uint32_t COLOR_BTN_BG      = (uint32_t)0x404048;
static const uint32_t COLOR_BTN_OP_BG   = (uint32_t)0x7A4A1F;
static const uint32_t COLOR_BTN_CTRL_BG = (uint32_t)0x2A3A60;
static const uint32_t COLOR_BTN_TEXT    = (uint32_t)0xE6E6E6;
static const uint32_t COLOR_DIM_TEXT    = (uint32_t)0x9A9A9A;

enum CellKind : uint8_t { K_DIGIT, K_OP, K_CTRL, K_EQ };
struct Cell { const char* label; CellKind kind; char op_char; };

static const Cell GRID[GRID_ROWS][GRID_COLS] = {
    { {"7", K_DIGIT, 0}, {"8", K_DIGIT, 0}, {"9", K_DIGIT, 0}, {"/", K_OP, '/'}, {"C",   K_CTRL, 0} },
    { {"4", K_DIGIT, 0}, {"5", K_DIGIT, 0}, {"6", K_DIGIT, 0}, {"x", K_OP, '*'}, {"Bk",  K_CTRL, 0} },
    { {"1", K_DIGIT, 0}, {"2", K_DIGIT, 0}, {"3", K_DIGIT, 0}, {"-", K_OP, '-'}, {"+/-", K_CTRL, 0} },
    { {"0", K_DIGIT, 0}, {".", K_DIGIT, 0}, {"=", K_EQ,    0}, {"+", K_OP, '+'}, {"Hm",  K_CTRL, 0} },
};

static double _parse_num(const std::string& s)
{
    if (s.empty() || s == "-") return 0;
    return strtod(s.c_str(), nullptr);
}

void AppCalculator::_clear()
{
    _data.display_str = "0";
    _data.input_str.clear();
    _data.stored_val = 0;
    _data.pending_op = 0;
    _data.error = false;
    _data.result_shown = false;
}

void AppCalculator::_apply_op()
{
    std::string src = _data.input_str.empty() ? _data.display_str : _data.input_str;
    if (src.empty() || src == "-") return;
    double b = _parse_num(src);
    if (_data.pending_op) {
        switch (_data.pending_op) {
            case '+': _data.stored_val += b; break;
            case '-': _data.stored_val -= b; break;
            case '*': _data.stored_val *= b; break;
            case '/':
                if (b == 0.0) { _data.error = true; _data.display_str = "Error"; }
                else _data.stored_val /= b;
                break;
            default: break;
        }
    } else {
        _data.stored_val = b;
    }
    _data.pending_op = 0;
    _data.input_str.clear();
}

void AppCalculator::_do_equals()
{
    if (_data.error) return;
    if (_data.input_str.empty() && !_data.pending_op) return;
    _apply_op();
    if (_data.error) { _draw(); return; }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.10g", _data.stored_val);
    _data.display_str = buf;
    _data.input_str = _data.display_str;
    _data.result_shown = true;
    _draw();
}

void AppCalculator::_on_digit(char c)
{
    if (_data.error) _clear();
    if (_data.result_shown) {
        _data.result_shown = false;
        _data.input_str.clear();
    }
    if (c == '.') {
        if (_data.input_str.find('.') != std::string::npos) return;
        if (_data.input_str.empty() || _data.input_str == "-") _data.input_str += "0";
    } else {
        if (_data.input_str == "0") _data.input_str.clear();
        else if (_data.input_str == "-0") _data.input_str = "-";
    }
    if (_data.input_str.size() >= MAX_DISPLAY_LEN) return;
    _data.input_str += c;
    _data.display_str = _data.input_str;
    _draw();
}

void AppCalculator::_on_op(char op)
{
    if (_data.error) return;
    _data.result_shown = false;
    _apply_op();
    if (_data.error) { _draw(); return; }
    _data.pending_op = op;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.10g", _data.stored_val);
    _data.display_str = buf;
    _draw();
}

void AppCalculator::_on_sign()
{
    if (_data.error) return;
    if (!_data.input_str.empty() && _data.input_str != "0") {
        if (_data.input_str[0] == '-') _data.input_str.erase(0, 1);
        else _data.input_str = "-" + _data.input_str;
        _data.display_str = _data.input_str;
    } else if (_data.result_shown) {
        _data.stored_val = -_data.stored_val;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.10g", _data.stored_val);
        _data.display_str = buf;
        _data.input_str = _data.display_str;
    }
    _draw();
}

void AppCalculator::_on_backspace()
{
    if (_data.error || _data.result_shown) { _clear(); _draw(); return; }
    if (!_data.input_str.empty()) {
        _data.input_str.pop_back();
        if (_data.input_str == "-") _data.input_str.clear();
        _data.display_str = _data.input_str.empty() ? "0" : _data.input_str;
    } else if (_data.pending_op) {
        _data.pending_op = 0;
    }
    _draw();
}

void AppCalculator::_on_clear()
{
    _clear();
    _draw();
}

/* Render a single 7-segment glyph at (x, y). Top-left origin.
 * Segment bit map:  a=0x01  b=0x02  c=0x04  d=0x08  e=0x10  f=0x20  g=0x40
 *
 *   aaaa
 *  f    b
 *  f    b
 *   gggg
 *  e    c
 *  e    c
 *   dddd
 */
void AppCalculator::_draw_7seg_char(char c, int x, int y, uint32_t on, uint32_t off)
{
    static const uint8_t SEGS[10] = {
        0x3F /*0*/, 0x06 /*1*/, 0x5B /*2*/, 0x4F /*3*/, 0x66 /*4*/,
        0x6D /*5*/, 0x7D /*6*/, 0x07 /*7*/, 0x7F /*8*/, 0x6F /*9*/
    };
    uint8_t m;
    if (c >= '0' && c <= '9') m = SEGS[c - '0'];
    else if (c == '-')        m = 0x40;             /* just middle bar */
    else if (c == 'E')        m = 0x79;             /* for "Err" */
    else if (c == 'r')        m = 0x50;
    else if (c == ' ')        return;
    else                      m = 0;

    uint32_t ca = (m & 0x01) ? on : off;
    uint32_t cb = (m & 0x02) ? on : off;
    uint32_t cc = (m & 0x04) ? on : off;
    uint32_t cd = (m & 0x08) ? on : off;
    uint32_t ce = (m & 0x10) ? on : off;
    uint32_t cf = (m & 0x20) ? on : off;
    uint32_t cg = (m & 0x40) ? on : off;

    int inner_w = SEG_DW - 2 * SEG_ST;
    _canvas->fillRect(x + SEG_ST,          y,                          inner_w, SEG_ST,    ca);
    _canvas->fillRect(x,                   y + SEG_ST,                 SEG_ST,  SEG_VLEN,  cf);
    _canvas->fillRect(x + SEG_DW - SEG_ST, y + SEG_ST,                 SEG_ST,  SEG_VLEN,  cb);
    _canvas->fillRect(x + SEG_ST,          y + SEG_HALF,               inner_w, SEG_ST,    cg);
    _canvas->fillRect(x,                   y + SEG_HALF + SEG_ST,      SEG_ST,  SEG_VLEN,  ce);
    _canvas->fillRect(x + SEG_DW - SEG_ST, y + SEG_HALF + SEG_ST,      SEG_ST,  SEG_VLEN,  cc);
    _canvas->fillRect(x + SEG_ST,          y + SEG_DH - SEG_ST,        inner_w, SEG_ST,    cd);
}

/* Draw a right-aligned 7-segment string. Handles '0'-'9', '-', '.', ' ', 'E', 'r'. */
void AppCalculator::_draw_7seg_str(const char* s, int right_x, int y, uint32_t on, uint32_t off)
{
    int n = (int)strlen(s);
    int total_w = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '.')      total_w += SEG_DOT_W + SEG_GAP;
        else if (c == ' ') total_w += SEG_DW / 2 + SEG_GAP;
        else               total_w += SEG_DW + SEG_GAP;
    }
    if (total_w > 0) total_w -= SEG_GAP;

    int x = right_x - total_w;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '.') {
            _canvas->fillRect(x, y + SEG_DH - SEG_ST, SEG_DOT_W, SEG_ST, on);
            x += SEG_DOT_W + SEG_GAP;
        } else if (c == ' ') {
            x += SEG_DW / 2 + SEG_GAP;
        } else {
            _draw_7seg_char(c, x, y, on, off);
            x += SEG_DW + SEG_GAP;
        }
    }
}

void AppCalculator::_draw_grid()
{
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextSize(1);
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int x = GRID_X0 + c * (BTN_W + BTN_GX);
            int y = GRID_Y0 + r * (BTN_H + BTN_GY);
            const Cell& cell = GRID[r][c];
            uint32_t bg = COLOR_BTN_BG;
            switch (cell.kind) {
                case K_OP:   bg = COLOR_BTN_OP_BG;   break;
                case K_CTRL: bg = COLOR_BTN_CTRL_BG; break;
                case K_EQ:   bg = COLOR_ACCENT;     break;
                default: break;
            }
            bool highlight = (cell.kind == K_OP && cell.op_char == _data.pending_op);
            if (highlight) bg = COLOR_ACCENT;
            _canvas->fillSmoothRoundRect(x, y, BTN_W, BTN_H, 2, bg);
            uint32_t fg = (cell.kind == K_EQ || highlight)
                              ? (uint32_t)THEME_COLOR_BG
                              : COLOR_BTN_TEXT;
            _canvas->setTextColor(fg, bg);
            int tw = _canvas->textWidth(cell.label);
            _canvas->setCursor(x + (BTN_W - tw) / 2, y + (BTN_H - 8) / 2);
            _canvas->print(cell.label);
        }
    }
}

void AppCalculator::_draw()
{
    _canvas_clear();
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);

    /* Title */
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print("Calculator");

    /* Formula chip on top-right: stored value + pending op */
    if (_data.pending_op && !_data.error) {
        char fbuf[24];
        snprintf(fbuf, sizeof(fbuf), "%.8g %c",
                 _data.stored_val,
                 _data.pending_op == '*' ? 'x' : _data.pending_op);
        _canvas->setFont(FONT_SMALL);
        _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
        _canvas->drawRightString(fbuf, _canvas->width() - 4, FORMULA_Y, FONT_SMALL);
        _canvas->setFont(FONT_REPL);
    }

    /* Display panel */
    int cw = _canvas->width();
    _canvas->fillSmoothRoundRect(2, DISPLAY_Y, cw - 4, DISPLAY_H, 4, COLOR_DISPLAY_BG);

    /* 7-segment display */
    std::string show;
    if (_data.error) {
        show = "Err";
    } else {
        show = _data.display_str.empty() ? "0" : _data.display_str;
        if (show.size() > MAX_DISPLAY_LEN)
            show = show.substr(show.size() - MAX_DISPLAY_LEN);
    }
    uint32_t seg_on  = _data.error ? COLOR_SEG_ERR_ON  : COLOR_ACCENT;
    uint32_t seg_off = _data.error ? COLOR_SEG_ERR_OFF : COLOR_SEG_OFF;
    _draw_7seg_str(show.c_str(), cw - 6, SEG_Y, seg_on, seg_off);

    /* Button grid */
    _draw_grid();

    /* Footer hint */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("S +/-  C clear  Bk del  HOME exit");

    _canvas_update();
}

void AppCalculator::_handle_key()
{
    const auto& state = _keyboard->keysState();
    bool shift = state.shift;

    if (state.enter) { _do_equals(); return; }
    if (state.del)   { _on_backspace(); return; }

    for (int k : state.hidKey) {
        if (k == KEY_ESC) { _on_clear(); return; }
        if (k == KEY_C && !shift) { _on_clear(); return; }

        if (k >= KEY_1 && k <= KEY_9) { _on_digit((char)('1' + (k - KEY_1))); return; }
        if (k == KEY_0)               { _on_digit('0'); return; }
        if (k >= KEY_KP1 && k <= KEY_KP9) { _on_digit((char)('1' + (k - KEY_KP1))); return; }
        if (k == KEY_KP0)             { _on_digit('0'); return; }
        if (k == KEY_DOT || k == KEY_KPDOT) { _on_digit('.'); return; }

        if (k == KEY_S && !shift)     { _on_sign(); return; }

        if (k == KEY_KPENTER || k == KEY_KPEQUAL) { _do_equals(); return; }
        if (k == KEY_KPPLUS || (k == KEY_EQUAL && shift)) { _on_op('+'); return; }
        if (k == KEY_MINUS || k == KEY_KPMINUS) { _on_op('-'); return; }
        if (k == KEY_KPASTERISK) { _on_op('*'); return; }
        if (k == KEY_SLASH || k == KEY_KPSLASH) { _on_op('/'); return; }
        if (k == KEY_EQUAL && !shift) { _do_equals(); return; }
    }
}

void AppCalculator::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppCalculator::onResume()
{
    ANIM_APP_OPEN();
    _clear();
    _draw();
}

void AppCalculator::onRunning()
{
    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            _handle_key();
        }
        _data.last_key_num = _keyboard->keyList().size();
    }

    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        destroyApp();
    }
}

void AppCalculator::onDestroy() {}
