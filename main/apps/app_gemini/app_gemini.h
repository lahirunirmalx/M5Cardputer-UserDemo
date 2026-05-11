/**
 * @file app_gemini.h
 * @brief Gemini AI chat (port of nishad2m8/GeminiPuter). WiFi + API key, send prompt, show response.
 */
#pragma once
#include <mooncake.h>
#include <string>
#include <vector>
#include "../../hal/hal.h"
#include "../utils/theme/theme_define.h"
#include "../utils/anim/anim_define.h"
#include "../utils/icon/icon_define.h"
#include "assets/gemini_big.h"
#include "assets/gemini_small.h"

namespace MOONCAKE
{
namespace APPS
{

class AppGemini : public APP_BASE
{
public:
    enum State { State_Setup, State_Chat };
    enum Role  { Role_User, Role_Bot, Role_System };

    struct Message { Role role; std::string text; };

    struct Data_t
    {
        HAL::Hal* hal = nullptr;
        State state = State_Setup;
        std::string api_key;
        std::vector<Message> transcript;
        std::string input_buffer;
        size_t last_key_num = 0;
        bool loading = false;
        bool focus_input = true;
        int  scroll_offset = 0;       /* in wrapped-lines units */
    };
    Data_t _data;

    void _draw();
    void _draw_setup();
    void _draw_chat();
    void _draw_status_chip();
    void _save_api_key();
    void _load_api_key();
    void _append_message(Role role, const std::string& text);
    void _scroll_to_bottom();
    std::string _send_to_gemini(const std::string& message);

public:
    void onCreate() override;
    void onResume() override;
    void onRunning() override;
    void onDestroy() override;
};

class AppGemini_Packer : public APP_PACKER_BASE
{
public:
    std::string getAppName() override { return "Gemini"; }
    void* getAppIcon() override { return (void*)(new AppIcon_t(image_data_gemini_big, image_data_gemini_small)); }
    void* newApp() override { return new AppGemini; }
    void deleteApp(void* app) override { delete (AppGemini*)app; }
};

} // namespace APPS
} // namespace MOONCAKE
