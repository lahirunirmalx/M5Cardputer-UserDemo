/**
 * @file app_tictactoe.cpp
 * @brief Tic-Tac-Toe. Player is X, AI is O (single-player) or 2-player.
 *
 * Keys: arrows = move cursor, Enter/Space = place,
 *       R = restart, T = toggle vs AI / 2-player, HOME = exit.
 */
#include "app_tictactoe.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include <cstdio>
#include <cstdlib>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

static constexpr int BOARD_SIZE = 75;
static constexpr int BOARD_X    = 6;
static constexpr int BOARD_Y    = 19;
static constexpr int CELL_SIZE  = BOARD_SIZE / 3;
static constexpr int FOOTER_Y   = 100;

static const uint32_t COLOR_ACCENT     = (uint32_t)0x99FF00;
static const uint32_t COLOR_PANEL_BG   = (uint32_t)0x1E1E22;
static const uint32_t COLOR_GRID       = (uint32_t)0x8A8A8A;
static const uint32_t COLOR_X          = (uint32_t)0xFF6464;
static const uint32_t COLOR_O          = (uint32_t)0x60A0FF;
static const uint32_t COLOR_CURSOR     = (uint32_t)0x99FF00;
static const uint32_t COLOR_DIM_TEXT   = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_WIN_HL     = (uint32_t)0xFFE040;

static const int LINES[8][3] = {
    {0,1,2},{3,4,5},{6,7,8},   /* rows */
    {0,3,6},{1,4,7},{2,5,8},   /* cols */
    {0,4,8},{2,4,6}            /* diags */
};

void AppTicTacToe::_reset()
{
    for (int i = 0; i < 9; i++) _data.board[i] = C_EMPTY;
    _data.cursor = 4;
    _data.state = GS_TURN_X;
    _data.win_a = _data.win_b = _data.win_c = -1;
}

AppTicTacToe::GameState AppTicTacToe::_check_state()
{
    for (int i = 0; i < 8; i++) {
        int a = LINES[i][0], b = LINES[i][1], c = LINES[i][2];
        if (_data.board[a] != C_EMPTY &&
            _data.board[a] == _data.board[b] &&
            _data.board[b] == _data.board[c]) {
            _data.win_a = a; _data.win_b = b; _data.win_c = c;
            return _data.board[a] == C_X ? GS_X_WINS : GS_O_WINS;
        }
    }
    for (int i = 0; i < 9; i++) if (_data.board[i] == C_EMPTY) {
        return (_data.state == GS_TURN_X) ? GS_TURN_X : GS_TURN_O;
    }
    return GS_DRAW;
}

/* Pick a move for O. Try: win > block > center > corner > random empty. */
void AppTicTacToe::_ai_move()
{
    /* Try to win */
    for (int i = 0; i < 9; i++) {
        if (_data.board[i] != C_EMPTY) continue;
        _data.board[i] = C_O;
        for (int L = 0; L < 8; L++) {
            int a = LINES[L][0], b = LINES[L][1], c = LINES[L][2];
            if (_data.board[a] == C_O && _data.board[b] == C_O && _data.board[c] == C_O) {
                return;
            }
        }
        _data.board[i] = C_EMPTY;
    }
    /* Block */
    for (int i = 0; i < 9; i++) {
        if (_data.board[i] != C_EMPTY) continue;
        _data.board[i] = C_X;
        bool block = false;
        for (int L = 0; L < 8; L++) {
            int a = LINES[L][0], b = LINES[L][1], c = LINES[L][2];
            if (_data.board[a] == C_X && _data.board[b] == C_X && _data.board[c] == C_X) {
                block = true; break;
            }
        }
        _data.board[i] = C_EMPTY;
        if (block) { _data.board[i] = C_O; return; }
    }
    /* Center */
    if (_data.board[4] == C_EMPTY) { _data.board[4] = C_O; return; }
    /* Corners */
    int corners[] = {0, 2, 6, 8};
    for (int i = 0; i < 4; i++) {
        int c = corners[(i + rand()) % 4];
        if (_data.board[c] == C_EMPTY) { _data.board[c] = C_O; return; }
    }
    /* Any empty */
    for (int i = 0; i < 9; i++) {
        if (_data.board[i] == C_EMPTY) { _data.board[i] = C_O; return; }
    }
}

void AppTicTacToe::_draw()
{
    _canvas_clear();
    int cw = _canvas->width();

    /* Title */
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, 1);
    _canvas->print("Tic-Tac-Toe");

    /* Mode + score chip in top-right */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    char chip[28];
    snprintf(chip, sizeof(chip), "%s  %u-%u-%u",
             _data.vs_ai ? "AI" : "2P",
             (unsigned)_data.x_wins, (unsigned)_data.draws, (unsigned)_data.o_wins);
    _canvas->drawRightString(chip, cw - 4, 5, FONT_SMALL);

    /* Board panel */
    _canvas->fillSmoothRoundRect(BOARD_X - 2, BOARD_Y - 2,
                                 BOARD_SIZE + 4, BOARD_SIZE + 4, 3, COLOR_PANEL_BG);

    /* Grid */
    for (int i = 1; i < 3; i++) {
        int x = BOARD_X + i * CELL_SIZE;
        int y = BOARD_Y + i * CELL_SIZE;
        _canvas->drawLine(x, BOARD_Y + 2, x, BOARD_Y + BOARD_SIZE - 2, COLOR_GRID);
        _canvas->drawLine(BOARD_X + 2, y, BOARD_X + BOARD_SIZE - 2, y, COLOR_GRID);
    }

    /* Marks */
    for (int i = 0; i < 9; i++) {
        int cx = BOARD_X + (i % 3) * CELL_SIZE + CELL_SIZE / 2;
        int cy = BOARD_Y + (i / 3) * CELL_SIZE + CELL_SIZE / 2;
        bool in_winning = (i == _data.win_a || i == _data.win_b || i == _data.win_c);
        if (_data.board[i] == C_X) {
            uint32_t c = in_winning ? COLOR_WIN_HL : COLOR_X;
            for (int t = -1; t <= 1; t++) {
                _canvas->drawLine(cx - 8, cy - 8 + t, cx + 8, cy + 8 + t, c);
                _canvas->drawLine(cx - 8, cy + 8 + t, cx + 8, cy - 8 + t, c);
            }
        } else if (_data.board[i] == C_O) {
            uint32_t c = in_winning ? COLOR_WIN_HL : COLOR_O;
            for (int r = 7; r <= 9; r++)
                _canvas->drawCircle(cx, cy, r, c);
        }
    }

    /* Cursor */
    if (_data.state == GS_TURN_X || _data.state == GS_TURN_O) {
        int i = _data.cursor;
        int x = BOARD_X + (i % 3) * CELL_SIZE;
        int y = BOARD_Y + (i / 3) * CELL_SIZE;
        for (int t = 0; t < 2; t++)
            _canvas->drawRect(x + t, y + t, CELL_SIZE - 2*t, CELL_SIZE - 2*t, COLOR_CURSOR);
    }

    /* Right side: turn / result */
    int rx = BOARD_X + BOARD_SIZE + 8;
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(rx, BOARD_Y);
    _canvas->print("Turn:");
    _canvas->setFont(FONT_REPL);
    if (_data.state == GS_TURN_X) {
        _canvas->setTextColor(COLOR_X, THEME_COLOR_BG);
        _canvas->setCursor(rx, BOARD_Y + 10);
        _canvas->print("X");
    } else if (_data.state == GS_TURN_O) {
        _canvas->setTextColor(COLOR_O, THEME_COLOR_BG);
        _canvas->setCursor(rx, BOARD_Y + 10);
        _canvas->print("O");
    } else if (_data.state == GS_X_WINS) {
        _canvas->setTextColor(COLOR_X, THEME_COLOR_BG);
        _canvas->setCursor(rx, BOARD_Y + 10);
        _canvas->print("X wins!");
    } else if (_data.state == GS_O_WINS) {
        _canvas->setTextColor(COLOR_O, THEME_COLOR_BG);
        _canvas->setCursor(rx, BOARD_Y + 10);
        _canvas->print("O wins!");
    } else {
        _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
        _canvas->setCursor(rx, BOARD_Y + 10);
        _canvas->print("Draw");
    }

    if (_data.state >= GS_X_WINS) {
        _canvas->setFont(FONT_SMALL);
        _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
        _canvas->setCursor(rx, BOARD_Y + 36);
        _canvas->print("R: new");
    }

    /* Footer */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("Arrows  Ent place  R new  T mode  HOME");

    _canvas_update();
}

void AppTicTacToe::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppTicTacToe::onResume()
{
    ANIM_APP_OPEN();
    srand((unsigned)millis());
    _reset();
    _draw();
}

void AppTicTacToe::onRunning()
{
    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& st = _keyboard->keysState();

            for (int k : st.hidKey) {
                if (_data.state == GS_TURN_X || _data.state == GS_TURN_O) {
                    int c = _data.cursor;
                    if (k == KEY_UP    && c >= 3) _data.cursor = c - 3;
                    if (k == KEY_DOWN  && c <= 5) _data.cursor = c + 3;
                    if (k == KEY_LEFT  && (c % 3) > 0) _data.cursor = c - 1;
                    if (k == KEY_RIGHT && (c % 3) < 2) _data.cursor = c + 1;
                }
                if (k == KEY_R) { _reset(); _draw(); goto done; }
                if (k == KEY_T) {
                    _data.vs_ai = !_data.vs_ai;
                    _reset();
                    _draw();
                    goto done;
                }
            }

            if (st.enter || st.space) {
                if ((_data.state == GS_TURN_X || _data.state == GS_TURN_O) &&
                    _data.board[_data.cursor] == C_EMPTY) {
                    _data.board[_data.cursor] = (_data.state == GS_TURN_X) ? C_X : C_O;
                    _data.state = (_data.state == GS_TURN_X) ? GS_TURN_O : GS_TURN_X;
                    GameState s = _check_state();
                    _data.state = s;
                    if (s == GS_X_WINS) _data.x_wins++;
                    else if (s == GS_O_WINS) _data.o_wins++;
                    else if (s == GS_DRAW) _data.draws++;

                    /* AI plays O */
                    if (_data.vs_ai && s == GS_TURN_O) {
                        _ai_move();
                        _data.state = GS_TURN_X;
                        s = _check_state();
                        _data.state = s;
                        if (s == GS_X_WINS) _data.x_wins++;
                        else if (s == GS_O_WINS) _data.o_wins++;
                        else if (s == GS_DRAW) _data.draws++;
                    }
                    _draw();
                }
            } else {
                _draw();
            }
done:
            _data.last_key_num = _keyboard->keyList().size();
        } else {
            _data.last_key_num = 0;
        }
    }

    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        destroyApp();
    }
}

void AppTicTacToe::onDestroy() {}
