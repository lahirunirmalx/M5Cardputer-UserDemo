/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_tictactoe.h"
#include "assets/tictactoe_big.h"
#include "assets/tictactoe_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <cstdio>
#include <cstdlib>

using namespace mooncake;

static constexpr int BOARD_SIZE = 75;
static constexpr int BOARD_X    = 6;
static constexpr int BOARD_Y    = 19;
static constexpr int CELL_SIZE  = BOARD_SIZE / 3;
static constexpr int FOOTER_Y   = 100;

static const uint32_t COLOR_ACCENT   = 0x99FF00;
static const uint32_t COLOR_PANEL_BG = 0x1E1E22;
static const uint32_t COLOR_GRID     = 0x8A8A8A;
static const uint32_t COLOR_X        = 0xFF6464;
static const uint32_t COLOR_O        = 0x60A0FF;
static const uint32_t COLOR_CURSOR   = 0x99FF00;
static const uint32_t COLOR_DIM_TEXT = 0x9A9A9A;
static const uint32_t COLOR_WIN_HL   = 0xFFE040;

static const int LINES[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
    {0, 4, 8}, {2, 4, 6},
};

AppTicTacToe::AppTicTacToe()
{
    setAppInfo().name     = "TicTac";
    setAppInfo().userData = new AppIcon_t(image_data_tictactoe_big, image_data_tictactoe_small);
}

AppTicTacToe::~AppTicTacToe()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppTicTacToe::reset()
{
    for (int i = 0; i < 9; i++) _board[i] = C_EMPTY;
    _cursor = 4;
    _state  = GS_TURN_X;
    _win_a = _win_b = _win_c = -1;
}

AppTicTacToe::GameState AppTicTacToe::check_state()
{
    for (int i = 0; i < 8; i++) {
        int a = LINES[i][0];
        int b = LINES[i][1];
        int c = LINES[i][2];
        if (_board[a] != C_EMPTY && _board[a] == _board[b] && _board[b] == _board[c]) {
            _win_a = a;
            _win_b = b;
            _win_c = c;
            return _board[a] == C_X ? GS_X_WINS : GS_O_WINS;
        }
    }
    for (int i = 0; i < 9; i++) {
        if (_board[i] == C_EMPTY) {
            return (_state == GS_TURN_X) ? GS_TURN_X : GS_TURN_O;
        }
    }
    return GS_DRAW;
}

void AppTicTacToe::ai_move()
{
    // Try to win
    for (int i = 0; i < 9; i++) {
        if (_board[i] != C_EMPTY) continue;
        _board[i] = C_O;
        for (int L = 0; L < 8; L++) {
            int a = LINES[L][0], b = LINES[L][1], c = LINES[L][2];
            if (_board[a] == C_O && _board[b] == C_O && _board[c] == C_O) {
                return;
            }
        }
        _board[i] = C_EMPTY;
    }
    // Block
    for (int i = 0; i < 9; i++) {
        if (_board[i] != C_EMPTY) continue;
        _board[i]  = C_X;
        bool block = false;
        for (int L = 0; L < 8; L++) {
            int a = LINES[L][0], b = LINES[L][1], c = LINES[L][2];
            if (_board[a] == C_X && _board[b] == C_X && _board[c] == C_X) {
                block = true;
                break;
            }
        }
        _board[i] = C_EMPTY;
        if (block) {
            _board[i] = C_O;
            return;
        }
    }
    // Center
    if (_board[4] == C_EMPTY) {
        _board[4] = C_O;
        return;
    }
    // Corners
    int corners[] = {0, 2, 6, 8};
    for (int i = 0; i < 4; i++) {
        int c = corners[(i + rand()) % 4];
        if (_board[c] == C_EMPTY) {
            _board[c] = C_O;
            return;
        }
    }
    // Any
    for (int i = 0; i < 9; i++) {
        if (_board[i] == C_EMPTY) {
            _board[i] = C_O;
            return;
        }
    }
}

void AppTicTacToe::draw()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    int cw = GetHAL().canvas.width();

    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, 1);
    GetHAL().canvas.print("Tic-Tac-Toe");

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    char chip[28];
    snprintf(chip, sizeof(chip), "%s  %u-%u-%u",
             _vs_ai ? "AI" : "2P",
             (unsigned)_x_wins, (unsigned)_draws, (unsigned)_o_wins);
    GetHAL().canvas.drawRightString(chip, cw - 4, 5, FONT_SMALL);

    GetHAL().canvas.fillSmoothRoundRect(BOARD_X - 2, BOARD_Y - 2,
                                        BOARD_SIZE + 4, BOARD_SIZE + 4, 3, COLOR_PANEL_BG);

    for (int i = 1; i < 3; i++) {
        int x = BOARD_X + i * CELL_SIZE;
        int y = BOARD_Y + i * CELL_SIZE;
        GetHAL().canvas.drawLine(x, BOARD_Y + 2, x, BOARD_Y + BOARD_SIZE - 2, COLOR_GRID);
        GetHAL().canvas.drawLine(BOARD_X + 2, y, BOARD_X + BOARD_SIZE - 2, y, COLOR_GRID);
    }

    for (int i = 0; i < 9; i++) {
        int cx          = BOARD_X + (i % 3) * CELL_SIZE + CELL_SIZE / 2;
        int cy          = BOARD_Y + (i / 3) * CELL_SIZE + CELL_SIZE / 2;
        bool in_winning = (i == _win_a || i == _win_b || i == _win_c);
        if (_board[i] == C_X) {
            uint32_t c = in_winning ? COLOR_WIN_HL : COLOR_X;
            for (int t = -1; t <= 1; t++) {
                GetHAL().canvas.drawLine(cx - 8, cy - 8 + t, cx + 8, cy + 8 + t, c);
                GetHAL().canvas.drawLine(cx - 8, cy + 8 + t, cx + 8, cy - 8 + t, c);
            }
        } else if (_board[i] == C_O) {
            uint32_t c = in_winning ? COLOR_WIN_HL : COLOR_O;
            for (int r = 7; r <= 9; r++) {
                GetHAL().canvas.drawCircle(cx, cy, r, c);
            }
        }
    }

    if (_state == GS_TURN_X || _state == GS_TURN_O) {
        int i = _cursor;
        int x = BOARD_X + (i % 3) * CELL_SIZE;
        int y = BOARD_Y + (i / 3) * CELL_SIZE;
        for (int t = 0; t < 2; t++) {
            GetHAL().canvas.drawRect(x + t, y + t, CELL_SIZE - 2 * t, CELL_SIZE - 2 * t, COLOR_CURSOR);
        }
    }

    int rx = BOARD_X + BOARD_SIZE + 8;
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(rx, BOARD_Y);
    GetHAL().canvas.print("Turn:");
    GetHAL().canvas.setFont(FONT_REPL);
    if (_state == GS_TURN_X) {
        GetHAL().canvas.setTextColor(COLOR_X, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(rx, BOARD_Y + 10);
        GetHAL().canvas.print("X");
    } else if (_state == GS_TURN_O) {
        GetHAL().canvas.setTextColor(COLOR_O, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(rx, BOARD_Y + 10);
        GetHAL().canvas.print("O");
    } else if (_state == GS_X_WINS) {
        GetHAL().canvas.setTextColor(COLOR_X, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(rx, BOARD_Y + 10);
        GetHAL().canvas.print("X wins!");
    } else if (_state == GS_O_WINS) {
        GetHAL().canvas.setTextColor(COLOR_O, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(rx, BOARD_Y + 10);
        GetHAL().canvas.print("O wins!");
    } else {
        GetHAL().canvas.setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(rx, BOARD_Y + 10);
        GetHAL().canvas.print("Draw");
    }

    if (_state >= GS_X_WINS) {
        GetHAL().canvas.setFont(FONT_SMALL);
        GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(rx, BOARD_Y + 36);
        GetHAL().canvas.print("R: new");
    }

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("Arrows  Ent place  R new  T mode  HOME");

    GetHAL().pushCanvas();
}

void AppTicTacToe::on_key(int keyCode)
{
    if (_state == GS_TURN_X || _state == GS_TURN_O) {
        int c = _cursor;
        if (keyCode == KEY_UP    && c >= 3)         _cursor = c - 3;
        if (keyCode == KEY_DOWN  && c <= 5)         _cursor = c + 3;
        if (keyCode == KEY_LEFT  && (c % 3) > 0)    _cursor = c - 1;
        if (keyCode == KEY_RIGHT && (c % 3) < 2)    _cursor = c + 1;
    }
    if (keyCode == KEY_R) {
        reset();
        draw();
        return;
    }
    if (keyCode == KEY_T) {
        _vs_ai = !_vs_ai;
        reset();
        draw();
        return;
    }

    if ((keyCode == KEY_ENTER || keyCode == KEY_SPACE) &&
        (_state == GS_TURN_X || _state == GS_TURN_O) &&
        _board[_cursor] == C_EMPTY) {
        _board[_cursor] = (_state == GS_TURN_X) ? C_X : C_O;
        _state          = (_state == GS_TURN_X) ? GS_TURN_O : GS_TURN_X;
        GameState s     = check_state();
        _state          = s;
        if (s == GS_X_WINS)      _x_wins++;
        else if (s == GS_O_WINS) _o_wins++;
        else if (s == GS_DRAW)   _draws++;

        if (_vs_ai && s == GS_TURN_O) {
            ai_move();
            _state = GS_TURN_X;
            s      = check_state();
            _state = s;
            if (s == GS_X_WINS)      _x_wins++;
            else if (s == GS_O_WINS) _o_wins++;
            else if (s == GS_DRAW)   _draws++;
        }
    }
    draw();
}

void AppTicTacToe::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    srand((unsigned)GetHAL().millis());
    reset();
    draw();
    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) {
            if (!keyEvent.state || keyEvent.isModifier) return;
            on_key(keyEvent.keyCode);
        });
}

void AppTicTacToe::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppTicTacToe::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
}
