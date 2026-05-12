/**
 * @file app_blepair.cpp
 * @brief BLE pairing-prompt demonstrator.
 *
 * Broadcasts well-known manufacturer-specific BLE adverts that nearby
 * iPhones / Pixels / Galaxies interpret as "device available to pair".
 * One payload is broadcast at a time; the user must explicitly start
 * the broadcast with Space/Enter and a 60-second auto-stop fires no
 * matter what. The intent is to demonstrate the protocol on a phone
 * you control - do not spam strangers' devices.
 *
 * Keys: arrows pick payload, Space/Enter toggle broadcast, HOME exit.
 */
#include "app_blepair.h"
#include "payloads.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include "spdlog/spdlog.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_gap_ble_api.h"
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

/* Layout */
static constexpr int TITLE_Y    = 1;
static constexpr int CHIP_Y     = 5;
static constexpr int PANEL_Y    = 19;
static constexpr int PANEL_H    = 56;
static constexpr int STATUS_Y   = 79;
static constexpr int FOOTER_Y   = 100;

static const uint32_t COLOR_ACCENT     = (uint32_t)0x99FF00;
static const uint32_t COLOR_PANEL_BG   = (uint32_t)0x1E1E22;
static const uint32_t COLOR_DIM_TEXT   = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_ON_AIR     = (uint32_t)0xFF6464;
static const uint32_t COLOR_VENDOR_AP  = (uint32_t)0xC8C8C8;
static const uint32_t COLOR_VENDOR_GG  = (uint32_t)0x66CC66;
static const uint32_t COLOR_VENDOR_SS  = (uint32_t)0x6699FF;

static const char* vendor_tag(ble_vendor_t v) {
    switch (v) {
        case BLE_VENDOR_APPLE:   return "AAPL";
        case BLE_VENDOR_GOOGLE:  return "GOOG";
        case BLE_VENDOR_SAMSUNG: return "SAMS";
    }
    return "?";
}
static uint32_t vendor_color(ble_vendor_t v) {
    switch (v) {
        case BLE_VENDOR_APPLE:   return COLOR_VENDOR_AP;
        case BLE_VENDOR_GOOGLE:  return COLOR_VENDOR_GG;
        case BLE_VENDOR_SAMSUNG: return COLOR_VENDOR_SS;
    }
    return COLOR_DIM_TEXT;
}

/* The spam loop runs in its own FreeRTOS task. Bluedroid cannot be
 * init/deinit'd in a tight loop without crashing in
 * esp_bt_controller_mem_release (try_heap_caps_add_region fails on the
 * second release). So we init the stack ONCE per app session and rotate
 * the BLE random address + advert payload each cycle instead. */
void AppBlePair::_spam_task_entry(void* arg)
{
    static_cast<AppBlePair*>(arg)->_spam_loop();
    vTaskDelete(nullptr);
}

void AppBlePair::_spam_loop()
{
    /* Bring up Bluedroid through the arduino wrapper just to install the
     * default stack + GAP callbacks, then drive esp_ble_gap_* directly so
     * we can set own_addr_type = RANDOM (the arduino BLEAdvertising class
     * hard-codes PUBLIC, which makes iOS dedupe on the fixed factory MAC). */
    BLEDevice::init("");
    spdlog::info("blepair: stack up, payload count={}", BLE_PAYLOAD_COUNT);

    esp_ble_adv_params_t params = {};
    params.adv_int_min       = 0x20;                          /* 20 ms */
    params.adv_int_max       = 0x30;                          /* 30 ms */
    /* Non-connectable / non-scannable. Real AirPods broadcast IND, but if we
     * advertise IND the iPhone can fire CONNECT_IND or SCAN_REQ at us mid-
     * cycle. We have no GATT services / scan-response set up so the
     * Bluedroid controller asserts and resets the chip (no panic dump
     * because the assert fires on the controller core). NONCONN_IND blocks
     * all back-channel from the peer — adverts are still scan-readable. */
    params.adv_type          = ADV_TYPE_NONCONN_IND;
    params.own_addr_type     = BLE_ADDR_TYPE_RANDOM;
    params.peer_addr_type    = BLE_ADDR_TYPE_PUBLIC;
    params.channel_map       = ADV_CHNL_ALL;
    params.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    int cycle = 0;
    while (_data.spam_running) {
        int idx = _data.spam_cursor;
        if (idx < 0 || idx >= BLE_PAYLOAD_COUNT) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        const ble_payload_t& p = BLE_PAYLOADS[idx];

        /* Fresh random static address each cycle. Top 2 bits of byte[0]
         * = 0b11 marks it as a Random Static address per BT Core spec. */
        esp_bd_addr_t addr;
        esp_fill_random(addr, 6);
        addr[0] |= 0xC0;
        esp_err_t r1 = esp_ble_gap_set_rand_addr(addr);

        /* Raw AD payload: [02 01 06]  +  [len][FF][company id + body]. */
        uint8_t raw[31];
        int n = 0;
        raw[n++] = 0x02;
        raw[n++] = 0x01;
        raw[n++] = 0x06;
        if ((int)p.mfg_len + 4 > (int)sizeof(raw)) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        raw[n++] = (uint8_t)(p.mfg_len + 1);
        raw[n++] = 0xFF;
        memcpy(&raw[n], p.mfg_data, p.mfg_len);

        /* iOS 17.2+ filters AirPods/Beats proximity-pair frames whose
         * trailing "encrypted hash" bytes look static. For type 0x07
         * (Proximity Pair) only, randomise the tail so each cycle looks
         * like a different device session. Type 0x04 (Nearby Action)
         * carries fixed protocol constants (60 4C 95 ...) that MUST NOT
         * be randomised — corrupting them is what crashed earlier. */
        if (p.vendor == BLE_VENDOR_APPLE && p.mfg_len >= 13) {
            int type_off = n + 2;
            if (raw[type_off] == 0x07) {
                int hash_off = n + 2 + 1 + 1 + 9; /* skip 4C 00 + type + len + 9 fixed */
                int hash_end = n + (int)p.mfg_len;
                if (hash_off < hash_end)
                    esp_fill_random(&raw[hash_off], hash_end - hash_off);
            }
        }
        n += p.mfg_len;

        esp_err_t r2 = esp_ble_gap_config_adv_data_raw(raw, n);
        vTaskDelay(pdMS_TO_TICKS(80));   /* wait for DATA_RAW_SET_COMPLETE */
        esp_err_t r3 = esp_ble_gap_start_advertising(&params);

        if (cycle < 3 || (cycle % 20) == 0) {
            spdlog::info("blepair[{}] {} rand={:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x} "
                         "raw_len={} set_addr={} cfg_data={} start={}",
                         cycle, p.label,
                         addr[0], addr[1], addr[2], addr[3], addr[4], addr[5],
                         n, (int)r1, (int)r2, (int)r3);
        }

        vTaskDelay(pdMS_TO_TICKS(200));   /* ~7 frames per MAC at 30ms interval */
        esp_ble_gap_stop_advertising();
        vTaskDelay(pdMS_TO_TICKS(20));
        cycle++;
    }

    esp_ble_gap_stop_advertising();
    spdlog::info("blepair: loop exited after {} cycles", cycle);
    /* Keep BLEDevice initialized — releasing here would call into
     * esp_bt_controller_mem_release and trip the heap-region abort. */
    _data.spam_task = nullptr;
}

void AppBlePair::_start_broadcast()
{
    if (_data.spam_running) return;
    _data.spam_cursor = _data.cursor;
    _data.spam_running = true;
    _data.broadcasting = true;
    _data.broadcast_start_ms = (uint32_t)millis();
    _data.last_label = BLE_PAYLOADS[_data.cursor].label;
    /* Bluedroid is hungry — give the task a generous 8 KB stack and pin
     * it to core 1 so it doesn't fight the UI loop on core 0. */
    xTaskCreatePinnedToCore(_spam_task_entry, "ble_spam", 8192, this,
                            5, &_data.spam_task, 1);
}

void AppBlePair::_stop_broadcast()
{
    _data.spam_running = false;
    _data.broadcasting = false;
    /* Wait for the task to exit so the next start gets a clean stack. */
    while (_data.spam_task != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void AppBlePair::_draw()
{
    _canvas_clear();
    int cw = _canvas->width();

    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print("BLE Pair");

    /* Status chip top-right */
    _canvas->setFont(FONT_SMALL);
    char chip[24];
    if (_data.broadcasting) {
        uint32_t elapsed = ((uint32_t)millis() - _data.broadcast_start_ms) / 1000;
        snprintf(chip, sizeof(chip), "ON-AIR %us", (unsigned)elapsed);
        _canvas->setTextColor(COLOR_ON_AIR, THEME_COLOR_BG);
    } else {
        snprintf(chip, sizeof(chip), "%d/%d", _data.cursor + 1, BLE_PAYLOAD_COUNT);
        _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    }
    _canvas->drawRightString(chip, cw - 4, CHIP_Y, FONT_SMALL);

    /* List panel */
    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);
    int window = 5;
    int start = _data.cursor - window / 2;
    if (start < 0) start = 0;
    if (start > BLE_PAYLOAD_COUNT - window) start = BLE_PAYLOAD_COUNT - window;
    if (start < 0) start = 0;
    int line_h = (PANEL_H - 6) / window;
    for (int i = 0; i < window; i++) {
        int idx = start + i;
        if (idx >= BLE_PAYLOAD_COUNT) break;
        const ble_payload_t& p = BLE_PAYLOADS[idx];
        int y = PANEL_Y + 3 + i * line_h;
        bool active = (idx == _data.cursor);
        uint32_t row_bg = COLOR_PANEL_BG;
        if (active) {
            row_bg = _data.broadcasting ? (uint32_t)0x4A2828 : (uint32_t)0x3A3A60;
            _canvas->fillSmoothRoundRect(4, y - 1, cw - 8, line_h, 2, row_bg);
        }
        _canvas->setTextColor(active ? COLOR_ACCENT : (uint32_t)0xE6E6E6, row_bg);
        _canvas->setCursor(8, y + 1);
        _canvas->print(p.label);
        _canvas->setTextColor(vendor_color(p.vendor), row_bg);
        _canvas->drawRightString(vendor_tag(p.vendor), cw - 8, y + 1, FONT_SMALL);
    }

    /* Status / warning */
    _canvas->setFont(FONT_SMALL);
    _canvas->setCursor(3, STATUS_Y);
    if (_data.broadcasting) {
        _canvas->setTextColor(COLOR_ON_AIR, THEME_COLOR_BG);
        char sbuf[40];
        snprintf(sbuf, sizeof(sbuf), "Broadcasting: %s", _data.last_label);
        _canvas->print(sbuf);
    } else {
        _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
        _canvas->print("Affects nearby phones. Use responsibly.");
    }

    /* Footer */
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    if (_data.broadcasting)
        _canvas->print("SPACE stop   HOME exit");
    else
        _canvas->print("^v pick  SPACE start  HOME");

    _canvas_update();
}

void AppBlePair::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppBlePair::onResume()
{
    ANIM_APP_OPEN();
    _data.cursor = 0;
    _data.broadcasting = false;
    _data.spam_running = false;
    _data.spam_task = nullptr;
    _data.last_label = "";
    _draw();
}

void AppBlePair::onRunning()
{
    /* Hard-stop after auto_stop_ms to prevent accidental long-running adverts */
    if (_data.broadcasting &&
        ((uint32_t)millis() - _data.broadcast_start_ms) > _data.auto_stop_ms) {
        _stop_broadcast();
        _draw();
    }

    /* Tick the on-air timer every ~500ms */
    static uint32_t s_last_tick = 0;
    uint32_t now = (uint32_t)millis();
    if (_data.broadcasting && (now - s_last_tick) > 500) {
        s_last_tick = now;
        _draw();
    }

    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& st = _keyboard->keysState();
            bool changed = false;
            for (int k : st.hidKey) {
                if ((k == KEY_UP || k == KEY_SEMICOLON) && _data.cursor > 0) {
                    if (_data.broadcasting) _stop_broadcast();
                    _data.cursor--;
                    changed = true;
                }
                if ((k == KEY_DOWN || k == KEY_DOT) && _data.cursor < BLE_PAYLOAD_COUNT - 1) {
                    if (_data.broadcasting) _stop_broadcast();
                    _data.cursor++;
                    changed = true;
                }
                if (k == KEY_ESC && _data.broadcasting) {
                    _stop_broadcast();
                    changed = true;
                }
            }
            if (st.enter || st.space) {
                if (_data.broadcasting) _stop_broadcast();
                else                    _start_broadcast();
                changed = true;
            }
            if (changed) _draw();
            _data.last_key_num = _keyboard->keyList().size();
        } else {
            _data.last_key_num = 0;
        }
    }

    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        destroyApp();
    }
}

void AppBlePair::onDestroy()
{
    if (_data.spam_running) _stop_broadcast();
    /* deinit(false) keeps the BT controller's RAM allocated. Passing true
     * triggers esp_bt_controller_mem_release which aborts on a second call. */
    BLEDevice::deinit(false);
}
