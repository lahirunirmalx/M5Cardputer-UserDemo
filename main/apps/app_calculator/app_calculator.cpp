/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_calculator.h"
#include "assets/calc_big.h"
#include "assets/calc_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <apps/utils/seven_seg/seven_seg.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace mooncake;

static constexpr int TITLE_Y           = 1;
static constexpr int FORMULA_Y         = 5;
static constexpr int DISPLAY_Y         = 19;
static constexpr int DISPLAY_H         = 25;
static constexpr int SEG_Y             = 22;
static constexpr int GRID_Y0           = 47;
static constexpr int GRID_ROWS         = 4;
static constexpr int GRID_COLS         = 5;
static constexpr int BTN_W             = 36;
static constexpr int BTN_H             = 12;
static constexpr int BTN_GX            = 4;
static constexpr int BTN_GY            = 1;
static constexpr int GRID_X0           = 5;
static constexpr int FOOTER_Y          = 100;
static constexpr size_t MAX_DISPLAY_LEN = 11;

static const uint32_t COLOR_ACCENT      = 0x99FF00;
static const uint32_t COLOR_DISPLAY_BG  = 0x1E1E22;
static const uint32_t COLOR_SEG_OFF     = 0x252528;
static const uint32_t COLOR_SEG_ERR_ON  = 0xFF6464;
static const uint32_t COLOR_SEG_ERR_OFF = 0x2A1F1F;
static const uint32_t COLOR_BTN_BG      = 0x404048;
static const uint32_t COLOR_BTN_OP_BG   = 0x7A4A1F;
static const uint32_t COLOR_BTN_CTRL_BG = 0x2A3A60;
static const uint32_t COLOR_BTN_TEXT    = 0xE6E6E6;
static const uint32_t COLOR_DIM_TEXT    = 0x9A9A9A;

enum CellKind : uint8_t { K_DIGIT, K_OP, K_CTRL, K_EQ };
struct Cell {
    const char* label;
    CellKind kind;
    char op_char;
};

static const Cell GRID[GRID_ROWS][GRID_COLS] = {
    {{"7", K_DIGIT, 0}, {"8", K_DIGIT, 0}, {"9", K_DIGIT, 0}, {"/", K_OP, '/'},  {"C",   K_CTRL, 0}},
    {{"4", K_DIGIT, 0}, {"5", K_DIGIT, 0}, {"6", K_DIGIT, 0}, {"x", K_OP, '*'},  {"Bk",  K_CTRL, 0}},
    {{"1", K_DIGIT, 0}, {"2", K_DIGIT, 0}, {"3", K_DIGIT, 0}, {"-", K_OP, '-'},  {"+/-", K_CTRL, 0}},
    {{"0", K_DIGIT, 0}, {".", K_DIGIT, 0}, {"=", K_EQ,    0}, {"+", K_OP, '+'},  {"Hm",  K_CTRL, 0}},
};

static double parse_num(const std::string& s)
{
    if (s.empty() || s == "-") return 0;
    return strtod(s.c_str(), nullptr);
}

AppCalculator::AppCalculator()
{
    setAppInfo().name     = "Calc";
    setAppInfo().userData = new AppIcon_t(image_data_calc_big, image_data_calc_small);
}

AppCalculator::~AppCalculator()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppCalculator::clear_state()
{
    _display_str  = "0";
    _input_str.clear();
    _stored_val   = 0;
    _pending_op   = 0;
    _error        = false;
    _result_shown = false;
}

void AppCalculator::apply_op()
{
    std::string src = _input_str.empty() ? _display_str : _input_str;
    if (src.empty() || src == "-") return;
    double b = parse_num(src);
    if (_pending_op) {
        switch (_pending_op) {
            case '+': _stored_val += b; break;
            case '-': _stored_val -= b; break;
            case '*': _stored_val *= b; break;
            case '/':
                if (b == 0.0) {
                    _error       = true;
                    _display_str = "Error";
                } else {
                    _stored_val /= b;
                }
                break;
            default: break;
        }
    } else {
        _stored_val = b;
    }
    _pending_op = 0;
    _input_str.clear();
}

void AppCalculator::do_equals()
{
    if (_error) return;
    if (_input_str.empty() && !_pending_op) return;
    apply_op();
    if (_error) {
        draw();
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.10g", _stored_val);
    _display_str  = buf;
    _input_str    = _display_str;
    _result_shown = true;
    draw();
}

void AppCalculator::on_digit(char c)
{
    if (_error) clear_state();
    if (_result_shown) {
        _result_shown = false;
        _input_str.clear();
    }
    if (c == '.') {
        if (_input_str.find('.') != std::string::npos) return;
        if (_input_str.empty() || _input_str == "-") _input_str += "0";
    } else {
        if (_input_str == "0")       _input_str.clear();
        else if (_input_str == "-0") _input_str = "-";
    }
    if (_input_str.size() >= MAX_DISPLAY_LEN) return;
    _input_str += c;
    _display_str = _input_str;
    draw();
}

void AppCalculator::on_op(char op)
{
    if (_error) return;
    _result_shown = false;
    apply_op();
    if (_error) {
        draw();
        return;
    }
    _pending_op = op;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.10g", _stored_val);
    _display_str = buf;
    draw();
}

void AppCalculator::on_sign()
{
    if (_error) return;
    if (!_input_str.empty() && _input_str != "0") {
        if (_input_str[0] == '-') _input_str.erase(0, 1);
        else                      _input_str = "-" + _input_str;
        _display_str = _input_str;
    } else if (_result_shown) {
        _stored_val = -_stored_val;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.10g", _stored_val);
        _display_str = buf;
        _input_str   = _display_str;
    }
    draw();
}

void AppCalculator::on_backspace()
{
    if (_error || _result_shown) {
        clear_state();
        draw();
        return;
    }
    if (!_input_str.empty()) {
        _input_str.pop_back();
        if (_input_str == "-") _input_str.clear();
        _display_str = _input_str.empty() ? "0" : _input_str;
    } else if (_pending_op) {
        _pending_op = 0;
    }
    draw();
}

void AppCalculator::on_clear()
{
    clear_state();
    draw();
}

void AppCalculator::draw_grid()
{
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextSize(1);
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            int x            = GRID_X0 + c * (BTN_W + BTN_GX);
            int y            = GRID_Y0 + r * (BTN_H + BTN_GY);
            const Cell& cell = GRID[r][c];
            uint32_t bg      = COLOR_BTN_BG;
            switch (cell.kind) {
                case K_OP:   bg = COLOR_BTN_OP_BG;   break;
                case K_CTRL: bg = COLOR_BTN_CTRL_BG; break;
                case K_EQ:   bg = COLOR_ACCENT;      break;
                default: break;
            }
            bool highlight = (cell.kind == K_OP && cell.op_char == _pending_op);
            if (highlight) bg = COLOR_ACCENT;
            GetHAL().canvas.fillSmoothRoundRect(x, y, BTN_W, BTN_H, 2, bg);
            uint32_t fg = (cell.kind == K_EQ || highlight)
                            ? (uint32_t)THEME_COLOR_BG
                            : COLOR_BTN_TEXT;
            GetHAL().canvas.setTextColor(fg, bg);
            int tw = GetHAL().canvas.textWidth(cell.label);
            GetHAL().canvas.setCursor(x + (BTN_W - tw) / 2, y + (BTN_H - 8) / 2);
            GetHAL().canvas.print(cell.label);
        }
    }
}

void AppCalculator::draw()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);

    GetHAL().canvas.setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, TITLE_Y);
    GetHAL().canvas.print("Calculator");

    if (_pending_op && !_error) {
        char fbuf[24];
        snprintf(fbuf, sizeof(fbuf), "%.8g %c",
                 _stored_val,
                 _pending_op == '*' ? 'x' : _pending_op);
        GetHAL().canvas.setFont(FONT_SMALL);
        GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
        GetHAL().canvas.drawRightString(fbuf, GetHAL().canvas.width() - 4, FORMULA_Y, FONT_SMALL);
        GetHAL().canvas.setFont(FONT_REPL);
    }

    int cw = GetHAL().canvas.width();
    GetHAL().canvas.fillSmoothRoundRect(2, DISPLAY_Y, cw - 4, DISPLAY_H, 4, COLOR_DISPLAY_BG);

    std::string show;
    if (_error) {
        show = "Err";
    } else {
        show = _display_str.empty() ? "0" : _display_str;
        if (show.size() > MAX_DISPLAY_LEN) {
            show = show.substr(show.size() - MAX_DISPLAY_LEN);
        }
    }
    uint32_t seg_on  = _error ? COLOR_SEG_ERR_ON  : COLOR_ACCENT;
    uint32_t seg_off = _error ? COLOR_SEG_ERR_OFF : COLOR_SEG_OFF;
    SEVEN_SEG::draw_str(&GetHAL().canvas, show.c_str(), cw - 6, SEG_Y, seg_on, seg_off);

    draw_grid();

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("S +/-  C clear  Bk del  HOME exit");

    GetHAL().pushCanvas();
}

void AppCalculator::on_key(int keyCode, const char* keyName)
{
    if (keyCode == KEY_ENTER) {
        do_equals();
        return;
    }
    if (keyCode == KEY_BACKSPACE || keyCode == KEY_DELETE) {
        on_backspace();
        return;
    }
    if (keyCode == KEY_ESC) {
        on_clear();
        return;
    }

    // Match by post-shift character where possible (keyName is e.g. "+", "*", "5")
    if (keyName == nullptr || keyName[0] == '\0' || keyName[1] != '\0') {
        // Multi-char or empty (modifier-only / F-keys) — try keyCode for letters.
        if (keyCode == KEY_C) { on_clear(); return; }
        if (keyCode == KEY_S) { on_sign();  return; }
        return;
    }

    char c = keyName[0];

    if (c >= '0' && c <= '9') { on_digit(c); return; }
    if (c == '.')             { on_digit('.'); return; }
    if (c == '+' || c == '-' || c == '*' || c == '/') {
        on_op(c);
        return;
    }
    if (c == '=') { do_equals(); return; }
    if (c == 's' || c == 'S') { on_sign();  return; }
    if (c == 'c' || c == 'C') { on_clear(); return; }
}

void AppCalculator::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    clear_state();
    draw();

    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) {
            if (!keyEvent.state || keyEvent.isModifier) {
                return;
            }
            on_key(keyEvent.keyCode, keyEvent.keyName);
        });
}

void AppCalculator::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppCalculator::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
}
