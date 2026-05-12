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

/* 7-segment geometry lives in the shared SEVEN_SEG namespace */
#include "../utils/seven_seg/seven_seg.h"

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
    SEVEN_SEG::draw_str(_canvas, show.c_str(), cw - 6, SEG_Y, seg_on, seg_off);

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
