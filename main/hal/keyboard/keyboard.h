/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "keymap.h"
#include "../utils/adafruit_tca8418/Adafruit_TCA8418.h"
#include <mooncake_log_signal.h>

class Keyboard {
public:
    struct KeyEventRaw_t {
        bool state  = false;
        uint8_t row = 0;
        uint8_t col = 0;
    };

    struct KeyEvent_t {
        bool state              = false;
        bool isModifier         = false;
        KeScanCode_t keyCode    = KEY_NONE;
        const char* keyName     = "";
        uint8_t extraModifiers  = 0;  // HID modifier bits to OR into the report in addition to the
                                      // physical modifier keys (used when Fn injects LSHIFT for A-Z)
    };

    mclog::Signal<const KeyEventRaw_t&> onKeyEventRaw;
    mclog::Signal<const KeyEvent_t&> onKeyEvent;

    bool init();
    void update();
    inline uint8_t getModifierMask()
    {
        return _modifier_mask;
    }
    inline const KeyEvent_t& getLatestKeyEvent()
    {
        return _key_event_buffer;
    }
    inline const KeyEventRaw_t& getLatestKeyEventRaw()
    {
        return _key_event_raw_buffer;
    }
    void clearKeyEvent();
    KeyEvent_t convertToKeyEvent(const KeyEventRaw_t& key);

private:
    Adafruit_TCA8418* _tca8418 = nullptr;
    uint8_t _modifier_mask     = 0;
    bool _fn_state             = false;
    KeyEventRaw_t _key_event_raw_buffer;
    KeyEvent_t _key_event_buffer;

    KeyEventRaw_t get_key_event_raw(const uint8_t& eventRaw);
    void remap(KeyEventRaw_t& key);
    void update_modifier_mask(const KeyEventRaw_t& key);
};
