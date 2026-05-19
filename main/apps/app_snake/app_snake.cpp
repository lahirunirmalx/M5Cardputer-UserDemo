/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_snake.h"
#include "assets/snake_big.h"
#include "assets/snake_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <cstdio>
#include <cstdlib>

using namespace mooncake;

static constexpr int CELL     = 8;
static constexpr int COLS     = 25;
static constexpr int ROWS     = 10;
static constexpr int FIELD_X  = 3;
static constexpr int FIELD_Y  = 14;
static constexpr int FIELD_W  = COLS * CELL;
static constexpr int FIELD_H  = ROWS * CELL;
static constexpr int FOOTER_Y = 100;

static const uint32_t COLOR_ACCENT   = 0x99FF00;
static const uint32_t COLOR_FIELD    = 0x121810;
static const uint32_t COLOR_GRID     = 0x1E2418;
static const uint32_t COLOR_BODY     = 0x8CDC3C;
static const uint32_t COLOR_HEAD     = 0xC8FF6E;
static const uint32_t COLOR_FOOD     = 0xE83C3C;
static const uint32_t COLOR_DIM_TEXT = 0x9A9A9A;
static const uint32_t COLOR_GAMEOVER = 0xFF6464;

AppSnake::AppSnake()
{
    setAppInfo().name     = "Snake";
    setAppInfo().userData = new AppIcon_t(image_data_snake_big, image_data_snake_small);
}

AppSnake::~AppSnake()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

bool AppSnake::is_on_snake(int x, int y) const
{
    for (const auto& c : _body) {
        if (c.x == x && c.y == y) return true;
    }
    return false;
}

void AppSnake::place_food()
{
    for (int tries = 0; tries < 200; tries++) {
        int x = rand() % COLS;
        int y = rand() % ROWS;
        if (!is_on_snake(x, y)) {
            _food = {(int8_t)x, (int8_t)y};
            return;
        }
    }
}

void AppSnake::reset()
{
    _body.clear();
    int sx = COLS / 4;
    int sy = ROWS / 2;
    for (int i = 0; i < 4; i++) {
        _body.push_back({(int8_t)(sx + i), (int8_t)sy});
    }
    _dir           = D_RIGHT;
    _next_dir      = D_RIGHT;
    _score         = 0;
    _tick_ms       = 180;
    _last_tick_ms  = GetHAL().millis();
    _state         = GS_PLAYING;
    place_food();
}

void AppSnake::step()
{
    _dir      = _next_dir;
    Cell head = _body.back();
    int nx    = head.x;
    int ny    = head.y;
    switch (_dir) {
        case D_UP:    ny -= 1; break;
        case D_DOWN:  ny += 1; break;
        case D_LEFT:  nx -= 1; break;
        case D_RIGHT: nx += 1; break;
    }
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
        _state = GS_GAMEOVER;
        if (_score > _hi_score) _hi_score = _score;
        return;
    }
    for (size_t i = 0; i + 1 < _body.size(); i++) {
        if (_body[i].x == nx && _body[i].y == ny) {
            _state = GS_GAMEOVER;
            if (_score > _hi_score) _hi_score = _score;
            return;
        }
    }
    bool ate = (nx == _food.x && ny == _food.y);
    _body.push_back({(int8_t)nx, (int8_t)ny});
    if (ate) {
        _score++;
        if (_tick_ms > 80) _tick_ms -= 4;
        place_food();
    } else {
        _body.erase(_body.begin());
    }
}

void AppSnake::draw()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    int cw = GetHAL().canvas.width();

    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, 1);
    GetHAL().canvas.print("Snake");

    char sbuf[24];
    snprintf(sbuf, sizeof(sbuf), "Score %u  Hi %u",
             (unsigned)_score, (unsigned)_hi_score);
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.drawRightString(sbuf, cw - 4, 5, FONT_SMALL);

    GetHAL().canvas.fillRect(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, COLOR_FIELD);
    GetHAL().canvas.drawRect(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, COLOR_GRID);

    for (size_t i = 0; i < _body.size(); i++) {
        const Cell& c = _body[i];
        bool is_head  = (i + 1 == _body.size());
        int px        = FIELD_X + c.x * CELL;
        int py        = FIELD_Y + c.y * CELL;
        GetHAL().canvas.fillRect(px + 1, py + 1, CELL - 2, CELL - 2,
                                 is_head ? COLOR_HEAD : COLOR_BODY);
    }

    int fx = FIELD_X + _food.x * CELL;
    int fy = FIELD_Y + _food.y * CELL;
    GetHAL().canvas.fillSmoothRoundRect(fx + 1, fy + 1, CELL - 2, CELL - 2, 2, COLOR_FOOD);

    if (_state == GS_GAMEOVER) {
        int ox = cw / 2 - 50;
        int oy = FIELD_Y + FIELD_H / 2 - 16;
        GetHAL().canvas.fillSmoothRoundRect(ox, oy, 100, 32, 4, (uint32_t)0x1E1E22);
        GetHAL().canvas.drawRect(ox, oy, 100, 32, COLOR_GAMEOVER);
        GetHAL().canvas.setFont(FONT_REPL);
        GetHAL().canvas.setTextColor(COLOR_GAMEOVER, (uint32_t)0x1E1E22);
        GetHAL().canvas.setCursor(ox + 18, oy + 2);
        GetHAL().canvas.print("Game Over");
        GetHAL().canvas.setFont(FONT_SMALL);
        GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, (uint32_t)0x1E1E22);
        GetHAL().canvas.setCursor(ox + 16, oy + 20);
        GetHAL().canvas.print("R = restart");
    }

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("Arrows steer  R restart  HOME exit");

    GetHAL().pushCanvas();
}

void AppSnake::on_key(int keyCode)
{
    if (_state == GS_PLAYING) {
        if (keyCode == KEY_UP    && _dir != D_DOWN)  _next_dir = D_UP;
        if (keyCode == KEY_DOWN  && _dir != D_UP)    _next_dir = D_DOWN;
        if (keyCode == KEY_LEFT  && _dir != D_RIGHT) _next_dir = D_LEFT;
        if (keyCode == KEY_RIGHT && _dir != D_LEFT)  _next_dir = D_RIGHT;
    }
    if (keyCode == KEY_R) {
        reset();
        draw();
    }
}

void AppSnake::onOpen()
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

void AppSnake::onRunning()
{
    uint32_t now = GetHAL().millis();
    if (_state == GS_PLAYING && (now - _last_tick_ms) >= _tick_ms) {
        _last_tick_ms = now;
        step();
        draw();
    }

    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppSnake::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
}
