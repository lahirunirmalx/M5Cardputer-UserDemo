/**
 * @file port.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-09-18
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "../launcher.h"
#include "../../utils/common_define.h"
#include "../../utils/boot_sound/boot_sound_1.h"
#include "../../utils/boot_sound/boot_sound_2.h"
#include "esp_wifi.h"
// #include <WiFi.h>

// #define NO_BOOT_PLAY_STUFF


using namespace MOONCAKE::APPS;


void Launcher::_port_wait_enter()
{
    // delay(600);

    _data.hal->Speaker()->setVolume(64);
    // bool stuff = false;

    _data.hal->keyboard()->updateKeyList();
    if (_data.hal->keyboard()->keyList().size())
    {
        // silence mode
        auto cfg = _data.hal->Speaker()->config();
        cfg.magnification = 1;
        _data.hal->Speaker()->config(cfg);
        // _data.hal->Speaker()->playWav(boot_sound_1, sizeof(boot_sound_1));
    }
    else
    {
        // stuff = true;
        #ifndef NO_BOOT_PLAY_STUFF
        _data.hal->Speaker()->playWav(boot_sound_1, sizeof(boot_sound_1));
        // _data.hal->Speaker()->playWav(boot_sound_2, sizeof(boot_sound_2));
        #endif
    }

    // Hold till release
    while (_data.hal->keyboard()->keyList().size())
        _data.hal->keyboard()->updateKeyList();


    while (1)
    {
        _data.hal->keyboard()->updateKeyList();
        if (_data.hal->keyboard()->keyList().size())
        {
            _data.hal->playNextSound();

            // Hold till release
            while (_data.hal->keyboard()->keyList().size())
                _data.hal->keyboard()->updateKeyList();

            break;
        }

        delay(50);
    }

    // if (stuff)
    //     _data.hal->Speaker()->playWav(boot_sound_2, sizeof(boot_sound_2));
    // else
    //     _data.hal->Speaker()->playWav(boot_sound_1, sizeof(boot_sound_1));
}


bool Launcher::_port_check_next_pressed()
{
    if (_data.hal->keyboard()->isKeyPressing(55) || _data.hal->keyboard()->isKeyPressing(54))
    {
        // _data.hal->playNextSound();

        // Hold till release
        while (_data.hal->keyboard()->isKeyPressing(55) || _data.hal->keyboard()->isKeyPressing(54))
        {
            _data.menu->update(millis());
            _data.hal->canvas_update();
            _data.hal->keyboard()->updateKeyList();
        }

        return true;
    }

    return false;
}


bool Launcher::_port_check_last_pressed()
{
    if (_data.hal->keyboard()->isKeyPressing(53) || _data.hal->keyboard()->isKeyPressing(40))
    {
        // _data.hal->playLastSound();

        // Hold till release
        while (_data.hal->keyboard()->isKeyPressing(53) || _data.hal->keyboard()->isKeyPressing(40))
        {
            _data.menu->update(millis());
            _data.hal->canvas_update();
            _data.hal->keyboard()->updateKeyList();
        }

        return true;
    }

    return false;
}


bool Launcher::_port_check_key_pressed(int keynum)
{
    if (_data.hal->keyboard()->isKeyPressing(keynum))
    {
        // _data.hal->playNextSound();

        // Hold till release
        while (_data.hal->keyboard()->isKeyPressing(keynum))
        {
            _data.menu->update(millis());
            _data.hal->canvas_update();
            _data.hal->keyboard()->updateKeyList();
        }

        return true;
    }

    return false;
}


static int _last_key_num = 0;
static bool _last_caps_lock_state = false;
static uint32_t _last_caps_lock_click_time = 0;
static bool _is_special_key_pressed = false;

void Launcher::_port_update_keyboard_state()
{
    // Update key list
    _data.hal->keyboard()->updateKeyList();
    _data.hal->keyboard()->updateKeysState();

    // Check double clicked lock stuff
    // If (last 1 && now 0), a clicked
    // spdlog::info("{} {} {}", _last_caps_lock_state, _data.hal->keyboard()->keysState().shift, _last_caps_lock_state && !_data.hal->keyboard()->keysState().shift);
    if (_last_caps_lock_state && !_data.hal->keyboard()->keysState().shift)
    {
        // spdlog::info("clicked");
        // Check interval to last clicked
        if ((millis() - _last_caps_lock_click_time) < 500)
        {
            // spdlog::info("double clicked");
            // Toggle state
            _data.hal->keyboard()->setCapsLocked(!_data.hal->keyboard()->capslocked());

            spdlog::info("caps lock: {}", _data.hal->keyboard()->capslocked());

            // Avoid trple clicked liked stuff
            _last_caps_lock_click_time = 0;
        }
        else
        {
            _last_caps_lock_click_time = millis();
        }
    }


    // Reset state
    _data.keybaord_state.reset();
    _last_caps_lock_state = false;
    _is_special_key_pressed = false;

    // Keyboard bar icon stuff
    if (_data.hal->keyboard()->keyList().size())
    {
        // _data.hal->keyboard()->updateKeysState();
        if (_data.hal->keyboard()->keysState().shift)
        {
            _data.keybaord_state.caps_lock = true;
            _last_caps_lock_state = true;
            _is_special_key_pressed = true;
        }
        if (_data.hal->keyboard()->keysState().fn)
        {
            _data.keybaord_state.fn = true;
            _is_special_key_pressed = true;
        }

        if (_data.hal->keyboard()->keysState().ctrl)
        {
            _data.keybaord_state.ctrl = true;
            _is_special_key_pressed = true;
        }
        if (_data.hal->keyboard()->keysState().opt)
        {
            _data.keybaord_state.opt = true;
            _is_special_key_pressed = true;
        }
        if (_data.hal->keyboard()->keysState().alt)
        {
            _data.keybaord_state.alt = true;
            _is_special_key_pressed = true;
        }
    }
    if (_data.hal->keyboard()->capslocked())
    {
        _data.keybaord_state.caps_lock = true;
    }


    // Key sound stuff
    if (_data.hal->keyboard()->keyList().size() != _last_key_num)
    {
        if (_data.hal->keyboard()->keyList().size() == 0)
            _data.hal->playLastSound();
        else
        {
            if (_is_special_key_pressed)
                _data.hal->playKeyboardSound();
            else
                _data.hal->playNextSound();
        }

        _last_key_num = _data.hal->keyboard()->keyList().size();
    }
}



uint32_t _bat_update_time_count = 0;
uint32_t _wifi_update_time_count = 0;


void Launcher::_port_update_system_state()
{
    _data.system_state.wifi_state = 1;
    // _data.system_state.bat_state = 2;
    // _data.system_state.time = "22:33";



    // Time stuff
    if (_data.hal->isSntpAdjusted())
    {
        static time_t now;
        static struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        // spdlog::info("{} {}", timeinfo.tm_hour, timeinfo.tm_min);
        snprintf(_data.string_buffer, sizeof(_data.string_buffer), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    }
    else
    {
        // Fake time
        snprintf(_data.string_buffer, sizeof(_data.string_buffer), "%02lld:%02lld", (millis() / 3600000) % 60, (millis() / 60000) % 60);
    }
    _data.system_state.time = _data.string_buffer;
    // spdlog::info("time: {}", _data.system_state.time);


    if ((millis() - _wifi_update_time_count) > 1000 || _wifi_update_time_count == 0)
    {
        wifi_mode_t mode;
        if (esp_wifi_get_mode(&mode) == ESP_OK) {
            if (mode == WIFI_MODE_AP) {
                _data.system_state.wifi_state = 1;
            } else if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    // connected
                    if (ap_info.rssi > -65) {
                        // strong singal
                        _data.system_state.wifi_state = 1;
                    } else if (ap_info.rssi > -90) {
                        // strong singal
                        _data.system_state.wifi_state = 2;
                    } else {
                        // bad singal
                        if (millis() / 1000 % 2)
                            _data.system_state.wifi_state = 2;
                        else
                            _data.system_state.wifi_state = 3;
                    }
                } else {
                    // not connected
                    _data.system_state.wifi_state = 3;
                }
            } else {
                _data.system_state.wifi_state = 3;
            }

        } else {
            // wifi not on
            _data.system_state.wifi_state = 4;
        }
        // auto wl_stat = WiFi.status();
        // spdlog::info("wifi stat {}", wl_stat);

        // if (wl_stat == WL_CONNECTED)
        //     _data.system_state.wifi_state = 1;
        // else if (wl_stat == WL_IDLE_STATUS)
        //     _data.system_state.wifi_state = 3;
        // else
        //     _data.system_state.wifi_state = 4;

        _wifi_update_time_count = millis();
    }

    // Bat stuff
    if ((millis() - _bat_update_time_count) > 5000 || _bat_update_time_count == 0)
    {
        auto bat_level = _data.hal->getBatLevel();
        spdlog::info("get bat level: {}", bat_level);

        // if (bat_level >= 75)
        //     _data.system_state.bat_state = 1;
        // else if (bat_level >= 50)
        //     _data.system_state.bat_state = 2;
        // else if (bat_level >= 25)
        //     _data.system_state.bat_state = 3;
        // else
        //     _data.system_state.bat_state = 4;

        if (bat_level >= 100)
            _data.system_state.bat_state = 1;
        else if (bat_level >= 75)
            _data.system_state.bat_state = 2;
        else if (bat_level >= 50)
            _data.system_state.bat_state = 3;
        else if (bat_level >= 25)
            _data.system_state.bat_state = 4;
        else
            _data.system_state.bat_state = 5;
        _bat_update_time_count = millis();
    }

}