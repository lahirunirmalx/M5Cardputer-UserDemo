/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>

class AppTicTacToe : public mooncake::AppAbility {
public:
    AppTicTacToe();
    ~AppTicTacToe();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum Cell : int8_t { C_EMPTY = 0, C_X = 1, C_O = 2 };
    enum GameState : uint8_t { GS_TURN_X, GS_TURN_O, GS_X_WINS, GS_O_WINS, GS_DRAW };

    void draw();
    void reset();
    GameState check_state();
    void ai_move();
    void on_key(int keyCode);

    int _key_slot_id      = -1;
    Cell _board[9]        = {};
    int _cursor           = 4;
    GameState _state      = GS_TURN_X;
    int _win_a            = -1;
    int _win_b            = -1;
    int _win_c            = -1;
    bool _vs_ai           = true;
    uint32_t _x_wins      = 0;
    uint32_t _o_wins      = 0;
    uint32_t _draws       = 0;
};
