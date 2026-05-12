/**
 * @file app_tvbgone.h
 * @brief TV-B-Gone universal IR power-off cycler.
 *
 * Code set:
 *   - 143 NA + 140 EU TV power codes from Ken Shirriff's Arduino-TV-B-Gone
 *     port (Mitch Altman / Limor Fried original, MIT-style license in the
 *     header of world_ir_codes.h)
 *   - 4 Midea AC functions (user-supplied known-good codes)
 */
#pragma once
#include <mooncake.h>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/tvbgone_big.h"
#include "assets/tvbgone_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppTvbgone : public APP_BASE
{
    enum Mode  : uint8_t { M_IDLE, M_FIRING, M_SINGLE };
    enum Group : uint8_t { G_NA = 0, G_EU = 1, G_MIDEA = 2 };

    struct Data_t {
        HAL::Hal* hal = nullptr;
        size_t last_key_num = 0;
        bool   rmt_inited = false;
        uint32_t cur_carrier_hz = 0;
        Group  group = G_NA;
        int    cursor = 0;            /* index inside current group */
        int    fire_idx = 0;
        Mode   mode = M_IDLE;
        uint32_t next_send_ms = 0;
        char   last_label[24] = "";
    };
    Data_t _data;

    void _draw();
    int  _group_size() const;
    void _set_carrier(uint32_t hz);
    void _send_at(int idx);
    void _begin_rmt();
    void _end_rmt();

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppTvbgone_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "TVBGo"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_tvbgone_big, image_data_tvbgone_small)); }
    void* newApp() override { return new AppTvbgone; }
    void deleteApp(void* app) override { delete (AppTvbgone*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
