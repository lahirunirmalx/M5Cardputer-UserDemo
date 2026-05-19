/**
 * @file app_set_wifi.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.6
 * @date 2023-09-20
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "app_set_wifi.h"
#include "spdlog/spdlog.h"
 
// #include "../utils/wifi_connect_wrap/wifi_connect_wrap.h"
// #include "../utils/sntp_wrap/sntp_wrap.h"
#include <WiFi.h>
#include "esp_sntp.h"

using namespace MOONCAKE::APPS; 

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

/* Live snapshots of the HAL's WiFi creds (which are NVS-loaded). The original
 * code seeded these from compiled-in WIFI_SSID/WIFI_PASS; with secrets out of
 * source those are now empty strings, and we read the real values from HAL
 * each time we re-enter the SSID/Password prompts. */
static char _wifi_ssid[50] = {0};
static char _wifi_password[50] = {0};

static void _sync_creds_from_hal(HAL::Hal* hal) {
    const char* s = hal->getWifiSSID();
    const char* p = hal->getWifiPassword();
    strncpy(_wifi_ssid, s ? s : "", sizeof(_wifi_ssid) - 1);
    _wifi_ssid[sizeof(_wifi_ssid) - 1] = '\0';
    strncpy(_wifi_password, p ? p : "", sizeof(_wifi_password) - 1);
    _wifi_password[sizeof(_wifi_password) - 1] = '\0';
}

void AppSetWiFi::_update_input() {
    // spdlog::info("{} {}", _keyboard->keyList().size(), _data.last_key_num);

    // If changed
    if (_keyboard->keyList().size() != _data.last_key_num) {
        // If key pressed
        if (_keyboard->keyList().size() != 0) {
            // Update states and values
            _keyboard->updateKeysState();

            // If enter
            if (_keyboard->keysState().enter) {
                // New line
                _canvas->print(" \n");

                _update_state();
            }

            // If space
            else if (_keyboard->keysState().space) {
                _canvas->print(' ');
                _data.repl_input_buffer += ' ';
            }

            // If delete
            else if (_keyboard->keysState().del) {
                if (_data.repl_input_buffer.size()) {
                    // Pop input buffer
                    _data.repl_input_buffer.pop_back();

                    // Pop canvas display
                    int cursor_x = _canvas->getCursorX();
                    int cursor_y = _canvas->getCursorY();

                    if (cursor_x - FONT_REPL_WIDTH < 0) {
                        // Last line
                        cursor_y -= FONT_REPL_HEIGHT;
                        cursor_x = _canvas->width() - FONT_REPL_WIDTH;
                    } else {
                        cursor_x -= FONT_REPL_WIDTH;
                    }

                    spdlog::info("new cursor {} {}", cursor_x, cursor_y);

                    _canvas->setCursor(cursor_x, cursor_y);
                    _canvas->print("  ");
                    _canvas->setCursor(cursor_x, cursor_y);
                }
            }

            // Normal chars
            else {
                for (auto& i : _keyboard->keysState().values) {
                    // spdlog::info("{}", i);

                    _canvas->print(i);
                    _data.repl_input_buffer += i;
                }
            }

            _canvas_update();

            // Update last key num
            _data.last_key_num = _keyboard->keyList().size();
        }
        else {
            // Reset last key num
            _data.last_key_num = 0;
        }
    }
}


void AppSetWiFi::_update_cursor() {
    if ((millis() - _data.cursor_update_time_count) > _data.cursor_update_period) {
        // Get cursor
        int cursor_x = _canvas->getCursorX();
        int cursor_y = _canvas->getCursorY();

        // spdlog::info("cursor {} {}", cursor_x, cursor_y);

        _canvas->print(_data.cursor_state ? '_' : ' ');
        _canvas->setCursor(cursor_x, cursor_y);
        _canvas_update();

        _data.cursor_state = !_data.cursor_state;
        _data.cursor_update_time_count = millis();
    }
}




void AppSetWiFi::_update_state() {
    if (_data.current_state == state_init) {
        _canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        _canvas->printf("WiFi SSID:\n");
        _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
        _canvas->printf(">>> ");
        _canvas_update();

        _data.current_state = state_wait_ssid;

        // wifi_connect_get_config(_wifi_ssid, _wifi_password);
        if (*_wifi_ssid) {
            // spdlog::info("std::string(_wifi_ssid) [{}]", std::string(_wifi_ssid));
            _data.repl_input_buffer = std::string(_wifi_ssid);
            _canvas->print(_wifi_ssid);
            _canvas_update();
        }

    } else if (_data.current_state == state_wait_ssid) {
        _canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        _canvas->printf("WiFi Password:\n");
        _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
        _canvas->printf(">>> ");
        _canvas_update();

        _data.wifi_ssid = _data.repl_input_buffer;
        /* Stage the SSID locally — don't touch NVS yet. We commit both
         * fields atomically via hal->addWifiCredentials() once the password
         * is entered, so the LRU rotation only happens once and partial
         * input never pollutes a slot. */

        // Reset buffer
        _data.repl_input_buffer = "";
        _data.current_state = state_wait_password;
        spdlog::info("wifi ssid staged: {}", _data.wifi_ssid);

        if (*_wifi_password) {
            _data.repl_input_buffer = std::string(_wifi_password);
            _canvas->print(_wifi_password);
            _canvas_update();
        }
    } else if (_data.current_state == state_wait_password) {
        _data.wifi_password = _data.repl_input_buffer;
        // Reset buffer
        _data.repl_input_buffer = "";
        _data.current_state = state_connect;

        /* Commit both fields atomically — pushes onto slot 0 with LRU
         * rotation. If the new SSID already lives in a later slot, it
         * gets promoted; if all five slots are full, the oldest (slot 4)
         * is evicted. */
        _data.hal->addWifiCredentials(_data.wifi_ssid.c_str(),
                                     _data.wifi_password.c_str());
        spdlog::info("wifi creds saved (rotated): ssid={}", _data.wifi_ssid);
        _canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        _canvas->printf("WiFi config:\n- %s\n- %s\nConnecting...\n", _data.wifi_ssid.c_str(), _data.wifi_password.c_str());
        _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
        _canvas_update();

    }



    if (_data.current_state == state_connect) {
        /* Use whatever the user just typed if non-empty; fall back to the
         * stored credentials only when both prompts were skipped. Previous
         * version unconditionally clobbered user input with the defaults. */
        if (_data.wifi_ssid.empty())     _data.wifi_ssid     = _wifi_ssid;
        if (_data.wifi_password.empty()) _data.wifi_password = _wifi_password;

        if (!_data._alreay_connected) {
            WiFi.begin(_data.wifi_ssid.c_str(), _data.wifi_password.c_str());
            /* 8-second cap; longer waits make the UI feel hung. */
            WiFi.waitForConnectResult(8 * 1000);
        }

        // if (wifi_connect_wrap_is_wifi_connect_success() != 0)
        if (WiFi.status() == WL_CONNECTED) {
            _canvas->setTextColor(TFT_GREENYELLOW, THEME_COLOR_BG);
            // _canvas->printf("Connected\nSNTP adjusting...\n");
            _canvas->printf("Connected\n");
            _canvas->println("IP: " + WiFi.localIP().toString());

            // sntp_warp_init();

            if (!esp_sntp_enabled()) {
                setenv("TZ", TIME_ZONE, 1);
                tzset();
                esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
                esp_sntp_setservername(0, "pool.ntp.org");
                esp_sntp_init();
                _canvas->printf("SNTP started\n");
            }
            _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
            _canvas_update();

            _data.hal->setSntpAdjusted(true);

            _canvas->setTextColor(TFT_GREENYELLOW, THEME_COLOR_BG);
            _canvas->printf("Done\n");
            _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
            _canvas->printf(">>> ");
            _canvas_update();
        } else {
            /* Don't auto-exit on failed connect — that feels like a crash.
             * Drop back to the SSID prompt so the user can try different
             * credentials. They can press HOME if they want to quit. */
            _canvas->setTextColor(TFT_RED, THEME_COLOR_BG);
            _canvas->printf("Failed. Re-enter:\n");
            _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
            _canvas_update();
            _data.wifi_ssid.clear();
            _data.wifi_password.clear();
            _data.repl_input_buffer.clear();
            _data.current_state = state_init;
            _update_state();
            return;
        }

        // wifi_connect_wrap_disconnect();
        // if (!_data._alreay_connected) {
        //     WiFi.disconnect(true);
        // }

        _data.current_state = state_wait_quit;

    }

    if (_data.current_state == state_already_connected) {
        /* Show what's actually associated right now (from the WiFi stack,
         * not just slot 0 of NVS — they can differ briefly while the
         * launcher's auto-sweep is still trying alternates). Plus a count
         * of how many credentials are saved in the multi-slot store, so
         * the user knows how many fallbacks are configured. */
        String live_ssid = WiFi.SSID();
        const char* live = live_ssid.c_str();
        _canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        _canvas->printf("WiFi already set\n");
        _canvas->setTextColor(TFT_GREENYELLOW, THEME_COLOR_BG);
        _canvas->printf("Connected: %s\n", (live && *live) ? live : "(unknown)");
        if (WiFi.status() == WL_CONNECTED) {
            _canvas->printf("IP: %s\n", WiFi.localIP().toString().c_str());
        }
        _canvas->setTextColor(TFT_LIGHTGREY, THEME_COLOR_BG);
        _canvas->printf("Saved networks: %u/%u\n",
                        (unsigned)_data.hal->getWifiSlotCount(),
                        (unsigned)WIFI_SLOT_MAX);
        _canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        _canvas->printf("[y]off [e]edit [n]quit\n");
        _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
        _canvas->printf(">>> ");
        _data.current_state = state_whether_disable_wifi;

    } else if (_data.current_state == state_whether_disable_wifi) {
        if (_data.repl_input_buffer == "y") {
            WiFi.disconnect(true);
            _data.hal->setWifiConnected(false);
            _canvas->setTextColor(TFT_ORANGE, THEME_COLOR_BG);
            _canvas->printf("WiFi off\n");
            _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
            _canvas->printf(">>> ");
            _data.current_state = state_wait_quit;
        } else if (_data.repl_input_buffer == "e" || _data.repl_input_buffer == "E") {
            /* Re-enter SSID/password without dropping the active link until
             * the new creds are committed (state_connect calls WiFi.begin). */
            _data.repl_input_buffer.clear();
            _data.wifi_ssid.clear();
            _data.wifi_password.clear();
            _data._alreay_connected = false;
            _data.current_state = state_init;
            _update_state();
            return;
        } else {
            _canvas->printf("cancel\n");
            destroyApp();
            return;
        }
    }
}


void AppSetWiFi::onCreate() {
    spdlog::info("{} onCreate", getAppName());

    // Get hal
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
    
}


void AppSetWiFi::onResume() {
    ANIM_APP_OPEN();
    /* Pick up whatever is currently in NVS so the prompt can pre-fill. */
    _sync_creds_from_hal(_data.hal);

    _canvas_clear();
    _canvas->setTextScroll(true);
    _canvas->setBaseColor(THEME_COLOR_BG);
    _canvas->setTextColor(THEME_COLOR_REPL_TEXT, THEME_COLOR_BG);
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(FONT_SIZE_REPL);
    _canvas->setCursor(0, 0);

    _data._alreay_connected = (WiFi.status() == WL_CONNECTED);
    _data.hal->setWifiConnected(_data._alreay_connected);
    if (_data._alreay_connected) {
        if (!esp_sntp_enabled())
            _data.current_state = state_connect;
        else
            _data.current_state = state_already_connected;
    } else {
         if (_wifi_ssid[0] != '\0' && _wifi_password[0] != '\0') {
            _data.current_state = state_connect;       
        }else{
        _data.current_state = state_init; 
        }
        
    }
   
    _update_state();
}


void AppSetWiFi::onRunning() {
    if (_data.current_state != state_wait_quit)
        _update_input();
    _update_cursor();

    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        spdlog::info("quit set wifi");
        destroyApp();
    }
    if (_data.hal->keyboard()->keysState().fn){
        _data.hal->playNextSound();
        _data.current_state = state_init;
        _data.hal->setWifiConnected(false); 
        _data.hal->setSntpAdjusted(false); 
    }
}


void AppSetWiFi::onDestroy() {
    _canvas->setTextScroll(false);
}
