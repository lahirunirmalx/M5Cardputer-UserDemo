/**
 * @file app_tictactoe.h
 * @brief Tic-Tac-Toe vs a simple AI.
 */
#pragma once
#include <mooncake.h>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/tictactoe_big.h"
#include "assets/tictactoe_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppTicTacToe : public APP_BASE
{
    enum Cell : int8_t { C_EMPTY = 0, C_X = 1, C_O = 2 };
    enum GameState : uint8_t { GS_TURN_X, GS_TURN_O, GS_X_WINS, GS_O_WINS, GS_DRAW };

    struct Data_t {
        HAL::Hal* hal = nullptr;
        size_t last_key_num = 0;
        Cell  board[9];
        int   cursor = 4;
        GameState state = GS_TURN_X;
        int   win_a = -1, win_b = -1, win_c = -1;  /* winning line for highlight */
        bool  vs_ai = true;
        uint32_t x_wins = 0, o_wins = 0, draws = 0;
    };
    Data_t _data;

    void _draw();
    void _reset();
    GameState _check_state();
    void _ai_move();

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppTicTacToe_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "TicTac"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_tictactoe_big, image_data_tictactoe_small)); }
    void* newApp() override { return new AppTicTacToe; }
    void deleteApp(void* app) override { delete (AppTicTacToe*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
