/**
 * @file app_snake.cpp
 * @brief Snake game on a small grid. Arrows to steer, R to restart.
 *
 * Canvas is 206x109. Play area 200x80 at top, score bar at bottom.
 * Grid cells are 8x8, so 25 x 10 cells.
 */
#include "app_snake.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include <cstdio>
#include <cstdlib>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

static constexpr int CELL    = 8;
static constexpr int COLS    = 25;          /* 200 / 8 */
static constexpr int ROWS    = 10;          /* 80  / 8 */
static constexpr int FIELD_X = 3;
static constexpr int FIELD_Y = 14;
static constexpr int FIELD_W = COLS * CELL;
static constexpr int FIELD_H = ROWS * CELL;
static constexpr int FOOTER_Y = 100;

static const uint32_t COLOR_ACCENT     = (uint32_t)0x99FF00;
static const uint32_t COLOR_FIELD      = (uint32_t)0x121810;
static const uint32_t COLOR_GRID       = (uint32_t)0x1E2418;
static const uint32_t COLOR_BODY       = (uint32_t)0x8CDC3C;
static const uint32_t COLOR_HEAD       = (uint32_t)0xC8FF6E;
static const uint32_t COLOR_FOOD       = (uint32_t)0xE83C3C;
static const uint32_t COLOR_DIM_TEXT   = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_GAMEOVER   = (uint32_t)0xFF6464;

bool AppSnake::_is_on_snake(int x, int y) const
{
    for (const auto& c : _data.body) if (c.x == x && c.y == y) return true;
    return false;
}

void AppSnake::_place_food()
{
    for (int tries = 0; tries < 200; tries++) {
        int x = rand() % COLS;
        int y = rand() % ROWS;
        if (!_is_on_snake(x, y)) {
            _data.food = { (int8_t)x, (int8_t)y };
            return;
        }
    }
}

void AppSnake::_reset()
{
    _data.body.clear();
    int sx = COLS / 4;
    int sy = ROWS / 2;
    for (int i = 0; i < 4; i++)
        _data.body.push_back({ (int8_t)(sx + i), (int8_t)sy });
    _data.dir = D_RIGHT;
    _data.next_dir = D_RIGHT;
    _data.score = 0;
    _data.tick_ms = 180;
    _data.last_tick_ms = (uint32_t)millis();
    _data.state = GS_PLAYING;
    _place_food();
}

void AppSnake::_step()
{
    _data.dir = _data.next_dir;
    Cell head = _data.body.back();
    int nx = head.x, ny = head.y;
    switch (_data.dir) {
        case D_UP:    ny -= 1; break;
        case D_DOWN:  ny += 1; break;
        case D_LEFT:  nx -= 1; break;
        case D_RIGHT: nx += 1; break;
    }
    /* Wall collision */
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
        _data.state = GS_GAMEOVER;
        if (_data.score > _data.hi_score) _data.hi_score = _data.score;
        return;
    }
    /* Self collision (ignore the tail tip which will move) */
    for (size_t i = 0; i + 1 < _data.body.size(); i++) {
        if (_data.body[i].x == nx && _data.body[i].y == ny) {
            _data.state = GS_GAMEOVER;
            if (_data.score > _data.hi_score) _data.hi_score = _data.score;
            return;
        }
    }
    /* Eat food */
    bool ate = (nx == _data.food.x && ny == _data.food.y);
    _data.body.push_back({ (int8_t)nx, (int8_t)ny });
    if (ate) {
        _data.score++;
        if (_data.tick_ms > 80) _data.tick_ms -= 4;
        _place_food();
    } else {
        _data.body.erase(_data.body.begin());
    }
}

void AppSnake::_draw()
{
    _canvas_clear();
    int cw = _canvas->width();

    /* Title + score */
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, 1);
    _canvas->print("Snake");

    char sbuf[24];
    snprintf(sbuf, sizeof(sbuf), "Score %u  Hi %u", (unsigned)_data.score, (unsigned)_data.hi_score);
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->drawRightString(sbuf, cw - 4, 5, FONT_SMALL);

    /* Field */
    _canvas->fillRect(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, COLOR_FIELD);
    _canvas->drawRect(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, COLOR_GRID);

    /* Snake body */
    for (size_t i = 0; i < _data.body.size(); i++) {
        const Cell& c = _data.body[i];
        bool is_head = (i + 1 == _data.body.size());
        int px = FIELD_X + c.x * CELL;
        int py = FIELD_Y + c.y * CELL;
        _canvas->fillRect(px + 1, py + 1, CELL - 2, CELL - 2,
                          is_head ? COLOR_HEAD : COLOR_BODY);
    }

    /* Food */
    int fx = FIELD_X + _data.food.x * CELL;
    int fy = FIELD_Y + _data.food.y * CELL;
    _canvas->fillSmoothRoundRect(fx + 1, fy + 1, CELL - 2, CELL - 2, 2, COLOR_FOOD);

    /* Game over overlay */
    if (_data.state == GS_GAMEOVER) {
        int ox = cw / 2 - 50, oy = FIELD_Y + FIELD_H / 2 - 16;
        _canvas->fillSmoothRoundRect(ox, oy, 100, 32, 4, (uint32_t)0x1E1E22);
        _canvas->drawRect(ox, oy, 100, 32, COLOR_GAMEOVER);
        _canvas->setFont(FONT_REPL);
        _canvas->setTextColor(COLOR_GAMEOVER, (uint32_t)0x1E1E22);
        _canvas->setCursor(ox + 18, oy + 2);
        _canvas->print("Game Over");
        _canvas->setFont(FONT_SMALL);
        _canvas->setTextColor(COLOR_DIM_TEXT, (uint32_t)0x1E1E22);
        _canvas->setCursor(ox + 16, oy + 20);
        _canvas->print("R = restart");
    }

    /* Footer */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("Arrows steer  R restart  HOME exit");

    _canvas_update();
}

void AppSnake::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppSnake::onResume()
{
    ANIM_APP_OPEN();
    srand((unsigned)millis());
    _reset();
    _draw();
}

void AppSnake::onRunning()
{
    /* Input */
    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& st = _keyboard->keysState();
            for (int k : st.hidKey) {
                if (_data.state == GS_PLAYING) {
                    if ((k == KEY_UP    || k == KEY_SEMICOLON) && _data.dir != D_DOWN)  _data.next_dir = D_UP;
                    if ((k == KEY_DOWN  || k == KEY_DOT)       && _data.dir != D_UP)    _data.next_dir = D_DOWN;
                    if ((k == KEY_LEFT  || k == KEY_COMMA)     && _data.dir != D_RIGHT) _data.next_dir = D_LEFT;
                    if ((k == KEY_RIGHT || k == KEY_KPSLASH)   && _data.dir != D_LEFT)  _data.next_dir = D_RIGHT;
                }
                if (k == KEY_R) { _reset(); _draw(); }
            }
            _data.last_key_num = _keyboard->keyList().size();
        } else {
            _data.last_key_num = 0;
        }
    }

    /* Tick */
    uint32_t now = (uint32_t)millis();
    if (_data.state == GS_PLAYING && (now - _data.last_tick_ms) >= _data.tick_ms) {
        _data.last_tick_ms = now;
        _step();
        _draw();
    }

    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        destroyApp();
    }
}

void AppSnake::onDestroy() {}
