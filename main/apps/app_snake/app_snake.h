/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <vector>

class AppSnake : public mooncake::AppAbility {
public:
    AppSnake();
    ~AppSnake();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum Dir : uint8_t { D_UP, D_RIGHT, D_DOWN, D_LEFT };
    enum GameState : uint8_t { GS_PLAYING, GS_GAMEOVER };
    struct Cell { int8_t x, y; };

    void draw();
    void reset();
    void step();
    void place_food();
    bool is_on_snake(int x, int y) const;
    void on_key(int keyCode);

    int _key_slot_id          = -1;
    std::vector<Cell> _body;
    Dir _dir                  = D_RIGHT;
    Dir _next_dir             = D_RIGHT;
    Cell _food                = {0, 0};
    uint32_t _score           = 0;
    uint32_t _hi_score        = 0;
    uint32_t _last_tick_ms    = 0;
    uint32_t _tick_ms         = 180;
    GameState _state          = GS_PLAYING;
};
