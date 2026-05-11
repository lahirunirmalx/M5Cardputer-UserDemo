/**
 * @file app_snake.h
 * @brief Snake game.
 */
#pragma once
#include <mooncake.h>
#include <vector>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/snake_big.h"
#include "assets/snake_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppSnake : public APP_BASE
{
    enum Dir : uint8_t { D_UP, D_RIGHT, D_DOWN, D_LEFT };
    enum GameState : uint8_t { GS_PLAYING, GS_GAMEOVER };

    struct Cell { int8_t x, y; };

    struct Data_t {
        HAL::Hal* hal = nullptr;
        size_t last_key_num = 0;
        std::vector<Cell> body;
        Dir dir = D_RIGHT;
        Dir next_dir = D_RIGHT;
        Cell food{0, 0};
        uint32_t score = 0;
        uint32_t hi_score = 0;
        uint32_t last_tick_ms = 0;
        uint32_t tick_ms = 180;
        GameState state = GS_PLAYING;
    };
    Data_t _data;

    void _draw();
    void _reset();
    void _step();
    void _place_food();
    bool _is_on_snake(int x, int y) const;

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppSnake_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "Snake"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_snake_big, image_data_snake_small)); }
    void* newApp() override { return new AppSnake; }
    void deleteApp(void* app) override { delete (AppSnake*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
