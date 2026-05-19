/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_sysinfo.h"
#include "assets/sysinfo_big.h"
#include "assets/sysinfo_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <cstdio>
#include <esp_chip_info.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <rom/ets_sys.h>

using namespace mooncake;

static constexpr int TITLE_Y  = 1;
static constexpr int PANEL_Y  = 19;
static constexpr int PANEL_H  = 78;
static constexpr int ROW_H    = 10;
static constexpr int FOOTER_Y = 100;

static const uint32_t COLOR_ACCENT   = 0x99FF00;
static const uint32_t COLOR_PANEL_BG = 0x1E1E22;
static const uint32_t COLOR_LABEL    = 0x9A9A9A;
static const uint32_t COLOR_VALUE    = 0xE6E6E6;
static const uint32_t COLOR_OK       = 0x99FF00;
static const uint32_t COLOR_WARN     = 0xFFB060;

AppSysinfo::AppSysinfo()
{
    setAppInfo().name     = "SysInfo";
    setAppInfo().userData = new AppIcon_t(image_data_sysinfo_big, image_data_sysinfo_small);
}

AppSysinfo::~AppSysinfo()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

static const char* chip_model_name(esp_chip_model_t m)
{
    switch (m) {
        case CHIP_ESP32:   return "ESP32";
        case CHIP_ESP32S2: return "ESP32-S2";
        case CHIP_ESP32S3: return "ESP32-S3";
        case CHIP_ESP32C3: return "ESP32-C3";
        case CHIP_ESP32C6: return "ESP32-C6";
        case CHIP_ESP32H2: return "ESP32-H2";
        default:           return "ESP32";
    }
}

void AppSysinfo::draw()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    int cw = GetHAL().canvas.width();

    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, TITLE_Y);
    GetHAL().canvas.print("System Info");

    esp_chip_info_t info;
    esp_chip_info(&info);
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    GetHAL().canvas.drawRightString(chip_model_name(info.model), cw - 4, 5, FONT_SMALL);

    GetHAL().canvas.fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    auto row = [&](int idx, const char* label, const char* value, uint32_t value_color) {
        int y = PANEL_Y + 4 + idx * ROW_H;
        GetHAL().canvas.setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
        GetHAL().canvas.setCursor(6, y);
        GetHAL().canvas.print(label);
        GetHAL().canvas.setTextColor(value_color, COLOR_PANEL_BG);
        GetHAL().canvas.setCursor(72, y);
        GetHAL().canvas.print(value);
    };

    uint32_t up_s = GetHAL().millis() / 1000;
    int h         = up_s / 3600;
    int m         = (up_s / 60) % 60;
    int s         = up_s % 60;
    char buf[40];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    row(0, "Uptime:", buf, COLOR_VALUE);

    uint32_t free_h = esp_get_free_heap_size();
    uint32_t min_h  = esp_get_minimum_free_heap_size();
    snprintf(buf, sizeof(buf), "%u K (min %u)",
             (unsigned)(free_h / 1024), (unsigned)(min_h / 1024));
    row(1, "Free heap:", buf, (free_h < 30 * 1024) ? COLOR_WARN : COLOR_VALUE);

    bool wifi_ok = GetHAL().isWifiConnected();
    if (wifi_ok) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            snprintf(buf, sizeof(buf), "%.16s", (const char*)ap.ssid);
            row(2, "WiFi:", buf, COLOR_OK);
            snprintf(buf, sizeof(buf), "%d dBm", (int)ap.rssi);
            row(4, "RSSI:", buf, COLOR_VALUE);
        } else {
            row(2, "WiFi:", "(no AP info)", COLOR_WARN);
            row(4, "RSSI:", "-", COLOR_LABEL);
        }

        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            uint32_t a = ip_info.ip.addr;
            snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                     (unsigned)(a & 0xFF),
                     (unsigned)((a >> 8) & 0xFF),
                     (unsigned)((a >> 16) & 0xFF),
                     (unsigned)((a >> 24) & 0xFF));
            row(3, "IP:", buf, COLOR_VALUE);
        } else {
            row(3, "IP:", "-", COLOR_LABEL);
        }
    } else {
        row(2, "WiFi:", "disconnected", COLOR_WARN);
        row(3, "IP:", "-", COLOR_LABEL);
        row(4, "RSSI:", "-", COLOR_LABEL);
    }

    uint8_t bat = GetHAL().getBatLevel();
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)bat);
    row(5, "Battery:", buf, (bat < 20) ? COLOR_WARN : COLOR_OK);

    // Use the configured target CPU clock; runtime helper varies by IDF version.
    // CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ is in sdkconfig and is a compile-time int.
#ifdef CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
    int cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#else
    int cpu_mhz = 240;
#endif
    snprintf(buf, sizeof(buf), "%d core @ %d MHz", info.cores, cpu_mhz);
    row(6, "CPU:", buf, COLOR_VALUE);

    GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("Live refresh every 1s  HOME exit");

    GetHAL().pushCanvas();
}

void AppSysinfo::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _last_redraw_ms = 0;
    draw();
}

void AppSysinfo::onRunning()
{
    uint32_t now = GetHAL().millis();
    if (now - _last_redraw_ms >= 1000) {
        _last_redraw_ms = now;
        draw();
    }

    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppSysinfo::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}
