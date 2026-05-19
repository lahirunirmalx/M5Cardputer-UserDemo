/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_claudemeter.h"
#include "assets/claudemeter_big.h"
#include "assets/claudemeter_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

using namespace mooncake;

// ----- NVS-backed config -----------------------------------------------------
static const char* NVS_NS_CLAUDE = "claude";
static const char* NVS_KEY_BASE  = "base";
static const char* NVS_KEY_TOKEN = "bearer";

static constexpr size_t BASE_URL_MAX = 96;
static constexpr size_t BEARER_MAX   = 128;

static char g_base_url[BASE_URL_MAX]            = {0};
static char g_bearer[BEARER_MAX]                = {0};
static char g_auth_header[BEARER_MAX + 16]      = {0};   // "Bearer <token>"

static constexpr uint32_t REFRESH_MS = 5 * 60 * 1000UL;

static bool config_ready()
{
    return g_base_url[0] != '\0' && g_bearer[0] != '\0';
}

static void rebuild_auth_header()
{
    if (g_bearer[0])
        snprintf(g_auth_header, sizeof(g_auth_header), "Bearer %s", g_bearer);
    else
        g_auth_header[0] = '\0';
}

// Build a URL like "<base>/usage" into dest.
static void build_url(char* dest, size_t n, const char* suffix)
{
    if (g_base_url[0] == '\0') {
        dest[0] = '\0';
        return;
    }
    size_t bl = strlen(g_base_url);
    while (bl > 0 && g_base_url[bl - 1] == '/') bl--;
    snprintf(dest, n, "%.*s%s", (int)bl, g_base_url, suffix);
}

static void load_claude_settings_from_nvs()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CLAUDE, NVS_READONLY, &h) != ESP_OK) {
        g_base_url[0] = '\0';
        g_bearer[0]   = '\0';
        rebuild_auth_header();
        return;
    }
    size_t sz = sizeof(g_base_url);
    if (nvs_get_str(h, NVS_KEY_BASE, g_base_url, &sz) != ESP_OK) g_base_url[0] = '\0';
    sz = sizeof(g_bearer);
    if (nvs_get_str(h, NVS_KEY_TOKEN, g_bearer, &sz) != ESP_OK) g_bearer[0] = '\0';
    nvs_close(h);
    rebuild_auth_header();
}

static void save_claude_settings_to_nvs()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CLAUDE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_BASE, g_base_url);
    nvs_set_str(h, NVS_KEY_TOKEN, g_bearer);
    nvs_commit(h);
    nvs_close(h);
    rebuild_auth_header();
}

// ----- layout / colors -------------------------------------------------------
static constexpr int TITLE_Y  = 2;
static constexpr int FOOTER_Y = 100;

static const uint32_t COLOR_ACCENT    = 0x99FF00;
static const uint32_t COLOR_PANEL_BG  = 0x1E1E22;
static const uint32_t COLOR_LABEL     = 0x9A9A9A;
static const uint32_t COLOR_VALUE     = 0xE6E6E6;
static const uint32_t COLOR_OK        = 0x99FF00;
static const uint32_t COLOR_WARN      = 0xFFB060;
static const uint32_t COLOR_DANGER    = 0xFF6464;
static const uint32_t COLOR_BAR_BG    = 0x333338;
static const uint32_t COLOR_CHIP_OFF  = 0xFF6464;
static const uint32_t COLOR_CHIP_WARN = 0xFFB060;

// ============================================================================
// Background polling state — survives close(). Single-instance: the task is
// started on first onOpen() and only torn down by the explicit X shortcut.
// ============================================================================

namespace {

struct Cache {
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t      task  = nullptr;
    volatile bool     running           = false;
    volatile bool     stop_requested    = false;
    volatile bool     refresh_requested = false;
    volatile bool     muted             = false;

    // Last fetched values (-1 = unknown).
    float pct_five_hour    = -1.f;
    float pct_seven_day    = -1.f;
    float pct_seven_opus   = -1.f;
    float pct_seven_sonnet = -1.f;
    float pct_extra        = -1.f;
    bool  extra_enabled    = false;
    time_t reset_five_hour = 0;
    time_t reset_seven_day = 0;

    uint32_t last_fetch_ms = 0;
    int      fetch_state   = 0;     // 0=idle 1=busy 2=ok 3=err
    char     last_err[32]  = {0};

    // /stats
    long total_sessions   = -1;
    long total_messages   = -1;
    long opus_out_tokens   = -1;
    long sonnet_out_tokens = -1;
    long haiku_out_tokens  = -1;
    long today_messages    = -1;
    long today_sessions    = -1;

    // /token
    long  token_expires_in = -1;
    bool  token_expired    = false;
    bool  token_known      = false;
};

Cache g_cache;

void cache_lock()   { if (g_cache.mutex) xSemaphoreTake(g_cache.mutex, portMAX_DELAY); }
void cache_unlock() { if (g_cache.mutex) xSemaphoreGive(g_cache.mutex); }

// ---------- JSON scrape -----------------------------------------------------
bool find_number_after(const char* json, const char* key, float& out)
{
    const char* p = strstr(json, key);
    if (!p) return false;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r') p++;
    if (*p == 'n') return false;
    char* end = nullptr;
    float v   = strtof(p, &end);
    if (end == p) return false;
    out = v;
    return true;
}
bool find_section_utilization(const char* json, const char* section, float& out)
{
    const char* p = strstr(json, section);
    if (!p) return false;
    p = strstr(p, "\"utilization\"");
    if (!p) return false;
    return find_number_after(p, "\"utilization\"", out);
}
const char* find_section_string_after(const char* json, const char* section, const char* key)
{
    const char* p = strstr(json, section);
    if (!p) return nullptr;
    p = strstr(p, key);
    if (!p) return nullptr;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return nullptr;
    return p + 1;
}
bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }
time_t utc_components_to_epoch(int Y, int M, int D, int h, int m, int s)
{
    static const int dpm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (Y < 1970) return 0;
    long days = 0;
    for (int y = 1970; y < Y; y++) days += is_leap(y) ? 366 : 365;
    for (int mm = 0; mm < M - 1; mm++) {
        days += dpm[mm];
        if (mm == 1 && is_leap(Y)) days += 1;
    }
    days += D - 1;
    return (time_t)(days * 86400LL + h * 3600 + m * 60 + s);
}
time_t parse_iso8601_utc(const char* s)
{
    if (!s) return 0;
    int Y, Mo, D, h, m, sec;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &Mo, &D, &h, &m, &sec) != 6) return 0;
    return utc_components_to_epoch(Y, Mo, D, h, m, sec);
}
bool find_section_bool(const char* json, const char* section, const char* key)
{
    const char* p = strstr(json, section);
    if (!p) return false;
    p = strstr(p, key);
    if (!p) return false;
    p = strstr(p, "true");
    return p != nullptr;
}
bool find_long_after(const char* json, const char* key, long& out)
{
    const char* p = strstr(json, key);
    if (!p) return false;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r') p++;
    char* end = nullptr;
    long v    = strtol(p, &end, 10);
    if (end == p) return false;
    out = v;
    return true;
}
bool find_model_output_tokens(const char* json, const char* model_prefix, long& out)
{
    const char* p = strstr(json, model_prefix);
    if (!p) return false;
    const char* k = strstr(p, "\"outputTokens\"");
    if (!k) return false;
    return find_long_after(k, "\"outputTokens\"", out);
}

uint32_t bar_color_for(float pct)
{
    if (pct >= 90.f) return COLOR_DANGER;
    if (pct >= 70.f) return COLOR_WARN;
    return COLOR_OK;
}

// ---------- ESP-IDF HTTP client wrapper -------------------------------------
//
// Replaces dev-main's Arduino HTTPClient. Caller-supplied std::string is
// filled with the body iff HTTP status == 200. Return value is the HTTP
// status code, or negative on transport / config error:
//   -1  WiFi not connected
//   -2  esp_http_client init / open / TLS error
//   -3  no URL or no auth header configured
//
// Bundled CA roots (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) are attached so
// https://... URLs get certificate validation against the standard root
// store. Plain http://... works too — the bundle is just unused there.
//
// 8 s total timeout. Body capped at 64 KiB; a streamed JSON that big
// already overflows the JSON-scrape buffers, so an unbounded append is
// pointless and risks heap exhaustion in a long-running bg task.
int http_get(const char* url, std::string& body)
{
    body.clear();

    if (!GetHAL().isWifiConnected()) return -1;
    if (!url || url[0] == '\0' || g_auth_header[0] == '\0') return -3;

    esp_http_client_config_t cfg = {};
    cfg.url                      = url;
    cfg.timeout_ms               = 8000;
    cfg.crt_bundle_attach        = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -2;

    esp_http_client_set_header(client, "Authorization", g_auth_header);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "User-Agent", "CardputerADV-ClaudeMeter/1");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return -2;
    }

    // Forces the response header read; required before esp_http_client_read.
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    if (status == 200) {
        char buf[512];
        constexpr size_t MAX_BODY = 64 * 1024;
        while (body.size() < MAX_BODY) {
            int n = esp_http_client_read(client, buf, sizeof(buf));
            if (n <= 0) break;
            body.append(buf, n);
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

// ---------- /usage fetch ----------------------------------------------------
bool do_fetch_into(Cache& out)
{
    if (!GetHAL().isWifiConnected()) {
        cache_lock();
        snprintf(out.last_err, sizeof(out.last_err), "no wifi");
        cache_unlock();
        return false;
    }
    if (!config_ready()) {
        cache_lock();
        snprintf(out.last_err, sizeof(out.last_err), "no cfg");
        cache_unlock();
        return false;
    }

    char url[BASE_URL_MAX + 16];
    build_url(url, sizeof(url), "/usage");
    std::string body;
    int code = http_get(url, body);
    if (code != 200) {
        cache_lock();
        if (code < 0) snprintf(out.last_err, sizeof(out.last_err), "net err");
        else          snprintf(out.last_err, sizeof(out.last_err), "HTTP %d", code);
        cache_unlock();
        return false;
    }

    const char* j = body.c_str();
    float p5 = -1, p7 = -1, po = -1, ps = -1, pe = -1;
    float v;
    if (find_section_utilization(j, "\"five_hour\"",        v)) p5 = v;
    if (find_section_utilization(j, "\"seven_day\"",        v)) p7 = v;
    if (find_section_utilization(j, "\"seven_day_opus\"",   v)) po = v;
    if (find_section_utilization(j, "\"seven_day_sonnet\"", v)) ps = v;
    if (find_section_utilization(j, "\"extra_usage\"",      v)) pe = v;
    bool ee   = find_section_bool(j, "\"extra_usage\"", "\"is_enabled\"");
    time_t r5 = parse_iso8601_utc(find_section_string_after(j, "\"five_hour\"", "\"resets_at\""));
    time_t r7 = parse_iso8601_utc(find_section_string_after(j, "\"seven_day\"", "\"resets_at\""));

    if (p5 < 0.f && p7 < 0.f) {
        cache_lock();
        snprintf(out.last_err, sizeof(out.last_err), "parse fail");
        cache_unlock();
        return false;
    }
    cache_lock();
    out.pct_five_hour    = p5;
    out.pct_seven_day    = p7;
    out.pct_seven_opus   = po;
    out.pct_seven_sonnet = ps;
    out.pct_extra        = pe;
    out.extra_enabled    = ee;
    out.reset_five_hour  = r5;
    out.reset_seven_day  = r7;
    out.last_err[0]      = '\0';
    cache_unlock();
    return true;
}

// ---------- /stats fetch ----------------------------------------------------
void do_stats_fetch_into(Cache& out)
{
    if (!config_ready()) return;
    char url[BASE_URL_MAX + 16];
    build_url(url, sizeof(url), "/stats");
    std::string body;
    int code = http_get(url, body);
    if (code != 200) return;

    const char* j = body.c_str();
    long total_s = -1, total_m = -1;
    long opus = -1, sonnet = -1, haiku = -1;
    find_long_after(j, "\"totalSessions\"", total_s);
    find_long_after(j, "\"totalMessages\"", total_m);
    find_model_output_tokens(j, "\"claude-opus-",   opus);
    find_model_output_tokens(j, "\"claude-sonnet-", sonnet);
    find_model_output_tokens(j, "\"claude-haiku-",  haiku);

    // The last messageCount/sessionCount entry corresponds to today's
    // dailyActivity row (server returns chronological order).
    long today_m = -1, today_s = -1;
    const char* p    = j;
    const char* last = nullptr;
    while ((p = strstr(p, "\"messageCount\""))) {
        last = p;
        p++;
    }
    if (last) find_long_after(last, "\"messageCount\"", today_m);
    p    = j;
    last = nullptr;
    while ((p = strstr(p, "\"sessionCount\""))) {
        last = p;
        p++;
    }
    if (last) find_long_after(last, "\"sessionCount\"", today_s);

    cache_lock();
    out.total_sessions    = total_s;
    out.total_messages    = total_m;
    out.opus_out_tokens   = opus;
    out.sonnet_out_tokens = sonnet;
    out.haiku_out_tokens  = haiku;
    out.today_messages    = today_m;
    out.today_sessions    = today_s;
    cache_unlock();
}

// ---------- /token fetch ----------------------------------------------------
void do_token_fetch_into(Cache& out)
{
    if (!config_ready()) return;
    char url[BASE_URL_MAX + 16];
    build_url(url, sizeof(url), "/token");
    std::string body;
    int code = http_get(url, body);
    if (code != 200) return;

    const char* j = body.c_str();
    long exp_in  = 0;
    bool ok      = find_long_after(j, "\"expires_in_seconds\"", exp_in);
    bool expired = strstr(j, "\"expired\": true") != nullptr ||
                   strstr(j, "\"expired\":true") != nullptr;
    cache_lock();
    out.token_known      = ok;
    out.token_expires_in = ok ? exp_in : -1;
    out.token_expired    = expired;
    cache_unlock();
}

// ---------- danger beep ------------------------------------------------------
//
// Replaces dev-main's blink_and_beep(). The NeoLED side is dropped (no
// equivalent hardware on Cardputer-ADV); we only keep the audible cue
// for "usage is in the red". Suppressed by user M-toggle (g_cache.muted).
void danger_beep_if_needed(uint32_t severity)
{
    if (severity != COLOR_DANGER) return;
    if (g_cache.muted) return;
    // 880 Hz, 80 ms — same pitch/length as dev-main.
    audio::play_tone(880, 0.08);
}

// ---------- Background task body --------------------------------------------
uint32_t now_ms()
{
    return GetHAL().millis();
}

void bg_task(void*)
{
    bool ok;
    do {
        cache_lock();
        g_cache.fetch_state = 1;
        cache_unlock();

        ok = do_fetch_into(g_cache);
        if (ok) {
            do_stats_fetch_into(g_cache);
            do_token_fetch_into(g_cache);
        }

        cache_lock();
        g_cache.fetch_state = ok ? 2 : 3;
        if (ok) g_cache.last_fetch_ms = now_ms();
        float worst = -1.f;
        if (g_cache.pct_five_hour > worst) worst = g_cache.pct_five_hour;
        if (g_cache.pct_seven_day > worst) worst = g_cache.pct_seven_day;
        cache_unlock();

        uint32_t severity = ok ? bar_color_for(worst < 0 ? 0 : worst) : COLOR_DANGER;
        danger_beep_if_needed(severity);

        // Sleep up to REFRESH_MS in 250 ms slices so stop_requested and
        // refresh_requested are honored promptly.
        g_cache.refresh_requested = false;
        for (uint32_t slept = 0;
             slept < REFRESH_MS && !g_cache.stop_requested && !g_cache.refresh_requested;
             slept += 250) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    } while (!g_cache.stop_requested);

    g_cache.running = false;
    g_cache.task    = nullptr;
    vTaskDelete(NULL);
}

void start_bg_task()
{
    if (g_cache.running) return;
    if (!g_cache.mutex) g_cache.mutex = xSemaphoreCreateMutex();
    g_cache.stop_requested = false;
    g_cache.running        = true;
    xTaskCreatePinnedToCore(bg_task, "claudemeter", 6144, nullptr,
                            1, &g_cache.task, APP_CPU_NUM);
}

void stop_bg_task()
{
    if (!g_cache.running) return;
    g_cache.stop_requested = true;
    // Wait briefly for clean exit; may take up to ~8 s if mid-fetch.
    for (int i = 0; i < 40 && g_cache.running; i++) vTaskDelay(pdMS_TO_TICKS(100));
}

Cache snapshot()
{
    cache_lock();
    Cache c = g_cache;
    cache_unlock();
    return c;
}

void kick_refresh()
{
    g_cache.refresh_requested = true;
}

// ---------- countdown helpers -----------------------------------------------
long seconds_until(time_t target)
{
    if (target <= 0) return -1;
    time_t now = time(nullptr);
    if (now < 1577836800L) return -1;
    long diff = (long)(target - now);
    return diff < 0 ? 0 : diff;
}
void fmt_hms(long secs, char* buf, size_t n)
{
    long h = secs / 3600;
    long m = (secs / 60) % 60;
    long s = secs % 60;
    snprintf(buf, n, "%02ld:%02ld:%02ld", h, m, s);
}
void fmt_compact(long secs, char* buf, size_t n)
{
    if (secs >= 86400) {
        long d = secs / 86400;
        long h = (secs / 3600) % 24;
        snprintf(buf, n, "%ldd %ldh", d, h);
    } else if (secs >= 3600) {
        long h = secs / 3600;
        long m = (secs / 60) % 60;
        snprintf(buf, n, "%ldh %02ldm", h, m);
    } else {
        long m = secs / 60;
        long s = secs % 60;
        snprintf(buf, n, "%02ld:%02ld", m, s);
    }
}

}  // anonymous namespace

// ============================================================================
// Foreground UI
// ============================================================================

AppClaudeMeter::AppClaudeMeter()
{
    setAppInfo().name     = "Claude";
    setAppInfo().userData = new AppIcon_t(image_data_claudemeter_big, image_data_claudemeter_small);
}

AppClaudeMeter::~AppClaudeMeter()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppClaudeMeter::draw_bar(int x, int y, int w, int h, float pct, uint32_t fill)
{
    GetHAL().canvas.fillRoundRect(x, y, w, h, 3, COLOR_BAR_BG);
    if (pct > 0.f) {
        float p = pct;
        if (p > 100.f) p = 100.f;
        int fw = (int)((w - 2) * (p / 100.f));
        if (fw > 0) {
            GetHAL().canvas.fillRoundRect(x + 1, y + 1, fw, h - 2, 3, fill);
        }
    }
    GetHAL().canvas.drawRoundRect(x, y, w, h, 3, COLOR_LABEL);
}

void AppClaudeMeter::draw_title_bar(const char* title)
{
    int cw  = GetHAL().canvas.width();
    Cache c = snapshot();

    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, TITLE_Y);
    GetHAL().canvas.print(title);

    char pager[12];
    snprintf(pager, sizeof(pager), "[%d/%d]", _screen + 1, (int)S_COUNT);

    GetHAL().canvas.setFont(FONT_SMALL);
    const char* chip;
    uint32_t chip_c;
    if (!GetHAL().isWifiConnected()) {
        chip   = "OFFLINE";
        chip_c = COLOR_CHIP_OFF;
    } else if (c.fetch_state == 1) {
        chip   = "FETCH";
        chip_c = COLOR_CHIP_WARN;
    } else if (c.fetch_state == 3) {
        chip   = c.last_err[0] ? c.last_err : "ERR";
        chip_c = COLOR_CHIP_OFF;
    } else if (c.last_fetch_ms == 0) {
        chip   = "WAIT";
        chip_c = COLOR_LABEL;
    } else {
        chip   = "LIVE";
        chip_c = COLOR_ACCENT;
    }
    GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    GetHAL().canvas.drawRightString(pager, cw - 4, TITLE_Y + 2, FONT_SMALL);
    GetHAL().canvas.setTextColor(chip_c, THEME_COLOR_BG);
    GetHAL().canvas.drawRightString(chip, cw - 34, TITLE_Y + 2, FONT_SMALL);
}

void AppClaudeMeter::draw_footer()
{
    int cw = GetHAL().canvas.width();
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(3, FOOTER_Y);
    GetHAL().canvas.print("< >  R M  B X");

    Cache c = snapshot();
    if (c.muted) {
        GetHAL().canvas.setTextColor(COLOR_CHIP_WARN, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString("MUTE", cw / 2 + 20, FOOTER_Y, FONT_SMALL);
        GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    }

    char fb[24];
    if (c.last_fetch_ms == 0 && c.fetch_state != 1) {
        snprintf(fb, sizeof(fb), "next: --");
    } else {
        uint32_t now   = GetHAL().millis();
        uint32_t since = now - c.last_fetch_ms;
        int next       = (since >= REFRESH_MS) ? 0 : (int)((REFRESH_MS - since) / 1000);
        snprintf(fb, sizeof(fb), "next %02d:%02d", next / 60, next % 60);
    }
    GetHAL().canvas.drawRightString(fb, cw - 4, FOOTER_Y, FONT_SMALL);
}

void AppClaudeMeter::draw_summary()
{
    int cw = GetHAL().canvas.width();
    draw_title_bar("CLAUDE METER");
    Cache c = snapshot();

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 24);
    GetHAL().canvas.print("5 HOUR");

    char num[12];
    float p5 = c.pct_five_hour;
    if (p5 < 0.f) snprintf(num, sizeof(num), "--");
    else          snprintf(num, sizeof(num), "%d%%", (int)(p5 + 0.5f));
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(2);
    GetHAL().canvas.setTextColor(p5 < 0.f ? COLOR_LABEL : bar_color_for(p5), THEME_COLOR_BG);
    GetHAL().canvas.drawRightString(num, cw - 4, 18, FONT_REPL);
    GetHAL().canvas.setTextSize(1);

    int by5 = 52, bh5 = 14;
    draw_bar(4, by5, cw - 8, bh5, p5, p5 < 0.f ? COLOR_BAR_BG : bar_color_for(p5));

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    long secs5 = seconds_until(c.reset_five_hour);
    char rbuf[16];
    if (secs5 < 0) {
        snprintf(rbuf, sizeof(rbuf), "reset: --");
    } else {
        char tb[12];
        fmt_hms(secs5, tb, sizeof(tb));
        snprintf(rbuf, sizeof(rbuf), "reset %s", tb);
    }
    GetHAL().canvas.setCursor(4, by5 + bh5 + 2);
    GetHAL().canvas.print(rbuf);

    int y7 = 82;
    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, y7);
    GetHAL().canvas.print("7D");

    float p7 = c.pct_seven_day;
    char p7buf[8];
    if (p7 < 0.f) snprintf(p7buf, sizeof(p7buf), "--");
    else          snprintf(p7buf, sizeof(p7buf), "%d%%", (int)(p7 + 0.5f));
    GetHAL().canvas.setTextColor(p7 < 0.f ? COLOR_LABEL : bar_color_for(p7), THEME_COLOR_BG);
    GetHAL().canvas.setCursor(22, y7);
    GetHAL().canvas.print(p7buf);

    int bx7 = 50, bw7 = cw - 50 - 56, by7 = y7 - 1;
    draw_bar(bx7, by7, bw7, 8, p7, p7 < 0.f ? COLOR_BAR_BG : bar_color_for(p7));

    long secs7 = seconds_until(c.reset_seven_day);
    char rb7[14];
    if (secs7 < 0) snprintf(rb7, sizeof(rb7), "--");
    else           fmt_compact(secs7, rb7, sizeof(rb7));
    GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    GetHAL().canvas.drawRightString(rb7, cw - 4, y7, FONT_SMALL);

    draw_footer();
    GetHAL().pushCanvas();
}

void AppClaudeMeter::draw_big_pct_screen(const char* title, float pct)
{
    int cw = GetHAL().canvas.width();
    draw_title_bar(title);

    char num[16];
    if (pct < 0.f) snprintf(num, sizeof(num), "--");
    else           snprintf(num, sizeof(num), "%d%%", (int)(pct + 0.5f));

    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(3);
    uint32_t numc = pct < 0.f ? COLOR_LABEL : bar_color_for(pct);
    GetHAL().canvas.setTextColor(numc, THEME_COLOR_BG);
    GetHAL().canvas.drawCenterString(num, cw / 2, 18, FONT_REPL);
    GetHAL().canvas.setTextSize(1);

    int bx = 4, bw = cw - 8, by = 72, bh = 18;
    draw_bar(bx, by, bw, bh, pct, pct < 0.f ? COLOR_BAR_BG : bar_color_for(pct));

    draw_footer();
    GetHAL().pushCanvas();
}

void AppClaudeMeter::draw_opus_sonnet()
{
    int cw = GetHAL().canvas.width();
    draw_title_bar("OPUS / SONNET (7D)");
    Cache c = snapshot();

    auto row = [&](int y, const char* label, float pct, uint32_t hl) {
        GetHAL().canvas.setFont(FONT_REPL);
        GetHAL().canvas.setTextSize(1);
        GetHAL().canvas.setTextColor(COLOR_VALUE, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(4, y);
        GetHAL().canvas.print(label);

        char num[12];
        if (pct < 0.f) snprintf(num, sizeof(num), "--");
        else           snprintf(num, sizeof(num), "%d%%", (int)(pct + 0.5f));
        GetHAL().canvas.setTextSize(2);
        GetHAL().canvas.setTextColor(pct < 0.f ? COLOR_LABEL : hl, THEME_COLOR_BG);
        GetHAL().canvas.drawRightString(num, cw - 4, y - 4, FONT_REPL);
        GetHAL().canvas.setTextSize(1);

        draw_bar(4, y + 18, cw - 8, 12, pct, pct < 0.f ? COLOR_BAR_BG : hl);
    };

    row(20, "OPUS",   c.pct_seven_opus,   bar_color_for(c.pct_seven_opus));
    row(58, "SONNET", c.pct_seven_sonnet, bar_color_for(c.pct_seven_sonnet));

    draw_footer();
    GetHAL().pushCanvas();
}

void AppClaudeMeter::draw_detail()
{
    int cw = GetHAL().canvas.width();
    draw_title_bar("DETAIL");
    Cache c = snapshot();

    GetHAL().canvas.fillSmoothRoundRect(2, 18, cw - 4, 78, 4, COLOR_PANEL_BG);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);

    auto row = [&](int idx, const char* label, const char* value, uint32_t value_color) {
        int y = 22 + idx * 14;
        GetHAL().canvas.setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
        GetHAL().canvas.setCursor(6, y);
        GetHAL().canvas.print(label);
        GetHAL().canvas.setTextColor(value_color, COLOR_PANEL_BG);
        GetHAL().canvas.setCursor(86, y);
        GetHAL().canvas.print(value);
    };

    char buf[40];
    if (c.pct_five_hour < 0.f) snprintf(buf, sizeof(buf), "--");
    else                       snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_five_hour + 0.5f));
    row(0, "5 Hour:", buf, c.pct_five_hour < 0 ? COLOR_LABEL : bar_color_for(c.pct_five_hour));

    if (c.pct_seven_day < 0.f) snprintf(buf, sizeof(buf), "--");
    else                       snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_seven_day + 0.5f));
    row(1, "7 Day:", buf, c.pct_seven_day < 0 ? COLOR_LABEL : bar_color_for(c.pct_seven_day));

    if (c.pct_seven_opus < 0.f) snprintf(buf, sizeof(buf), "--");
    else                        snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_seven_opus + 0.5f));
    row(2, "Opus:", buf, COLOR_VALUE);

    if (c.pct_seven_sonnet < 0.f) snprintf(buf, sizeof(buf), "--");
    else                          snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_seven_sonnet + 0.5f));
    row(3, "Sonnet:", buf, COLOR_VALUE);

    if (c.extra_enabled && c.pct_extra >= 0.f) {
        snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_extra + 0.5f));
    } else {
        snprintf(buf, sizeof(buf), c.extra_enabled ? "--" : "off");
    }
    row(4, "Extra:", buf, c.extra_enabled ? COLOR_VALUE : COLOR_LABEL);

    if (c.last_fetch_ms == 0) {
        snprintf(buf, sizeof(buf), "never");
    } else {
        uint32_t age_s = (GetHAL().millis() - c.last_fetch_ms) / 1000;
        if (age_s < 60) snprintf(buf, sizeof(buf), "%us ago", (unsigned)age_s);
        else            snprintf(buf, sizeof(buf), "%um %us ago",
                                 (unsigned)(age_s / 60), (unsigned)(age_s % 60));
    }
    row(5, "Updated:", buf, COLOR_VALUE);

    draw_footer();
    GetHAL().pushCanvas();
}

static void fmt_si_count(long n, char* buf, size_t sz)
{
    if (n < 0)                        snprintf(buf, sz, "--");
    else if (n < 1000)                snprintf(buf, sz, "%ld",   n);
    else if (n < 1000 * 1000)         snprintf(buf, sz, "%.1fK", n / 1000.0);
    else if (n < 1000L * 1000 * 1000) snprintf(buf, sz, "%.1fM", n / 1.0e6);
    else                              snprintf(buf, sz, "%.1fB", n / 1.0e9);
}

void AppClaudeMeter::draw_stats()
{
    int cw = GetHAL().canvas.width();
    draw_title_bar("STATS (TOTAL)");
    Cache c = snapshot();

    GetHAL().canvas.fillSmoothRoundRect(2, 18, cw - 4, 78, 4, COLOR_PANEL_BG);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);

    auto row = [&](int idx, const char* label, const char* value, uint32_t value_color) {
        int y = 22 + idx * 12;
        GetHAL().canvas.setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
        GetHAL().canvas.setCursor(6, y);
        GetHAL().canvas.print(label);
        GetHAL().canvas.setTextColor(value_color, COLOR_PANEL_BG);
        GetHAL().canvas.drawRightString(value, cw - 8, y, FONT_REPL);
    };

    char buf[32];
    fmt_si_count(c.total_sessions, buf, sizeof(buf));
    row(0, "Sessions", buf, COLOR_ACCENT);

    fmt_si_count(c.total_messages, buf, sizeof(buf));
    row(1, "Messages", buf, COLOR_ACCENT);

    fmt_si_count(c.opus_out_tokens, buf, sizeof(buf));
    row(2, "Opus out", buf, COLOR_VALUE);

    fmt_si_count(c.sonnet_out_tokens, buf, sizeof(buf));
    row(3, "Sonnet out", buf, COLOR_VALUE);

    fmt_si_count(c.haiku_out_tokens, buf, sizeof(buf));
    row(4, "Haiku out", buf, COLOR_VALUE);

    if (c.today_messages >= 0 || c.today_sessions >= 0) {
        char tm[16], ts[16];
        fmt_si_count(c.today_messages, tm, sizeof(tm));
        fmt_si_count(c.today_sessions, ts, sizeof(ts));
        char combo[40];
        snprintf(combo, sizeof(combo), "%s msg / %s ses", tm, ts);
        row(5, "Today", combo, COLOR_VALUE);
    } else {
        row(5, "Today", "--", COLOR_LABEL);
    }

    draw_footer();
    GetHAL().pushCanvas();
}

void AppClaudeMeter::draw_token()
{
    int cw = GetHAL().canvas.width();
    draw_title_bar("OAUTH TOKEN");
    Cache c = snapshot();

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    GetHAL().canvas.drawCenterString("EXPIRES IN", cw / 2, 20, FONT_SMALL);

    char num[16];
    uint32_t numc;
    if (!c.token_known) {
        snprintf(num, sizeof(num), "--");
        numc = COLOR_LABEL;
    } else if (c.token_expired || c.token_expires_in <= 0) {
        snprintf(num, sizeof(num), "EXPIRED");
        numc = COLOR_DANGER;
    } else {
        long s  = c.token_expires_in;
        long h  = s / 3600;
        long m  = (s / 60) % 60;
        long ss = s % 60;
        if (h > 0) snprintf(num, sizeof(num), "%ld:%02ld:%02ld", h, m, ss);
        else       snprintf(num, sizeof(num), "%02ld:%02ld", m, ss);
        numc = (s < 300) ? COLOR_DANGER : (s < 1800 ? COLOR_WARN : COLOR_OK);
    }
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(c.token_expired ? 2 : 3);
    GetHAL().canvas.setTextColor(numc, THEME_COLOR_BG);
    GetHAL().canvas.drawCenterString(num, cw / 2, 36, FONT_REPL);
    GetHAL().canvas.setTextSize(1);

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    const char* hint;
    if (!c.token_known)                hint = "run claude /login";
    else if (c.token_expired)          hint = "run: claude /login";
    else if (c.token_expires_in < 300) hint = "expires soon!";
    else                               hint = "auto-refreshed by CLI";
    GetHAL().canvas.drawCenterString(hint, cw / 2, 82, FONT_SMALL);

    draw_footer();
    GetHAL().pushCanvas();
}

// Mask all but the last 4 chars of a sensitive value for the read-only view.
static void mask_secret(const char* in, char* out, size_t n)
{
    size_t len = strlen(in);
    if (len == 0) {
        snprintf(out, n, "(empty)");
        return;
    }
    if (len <= 4) {
        snprintf(out, n, "****");
        return;
    }
    size_t tail = 4;
    size_t mask = len - tail;
    if (mask > 8) mask = 8;
    size_t off = 0;
    for (size_t i = 0; i < mask && off < n - 1; i++) out[off++] = '*';
    for (size_t i = len - tail; i < len && off < n - 1; i++) out[off++] = in[i];
    out[off] = '\0';
}

void AppClaudeMeter::draw_settings()
{
    int cw = GetHAL().canvas.width();
    draw_title_bar("SETTINGS");

    GetHAL().canvas.fillSmoothRoundRect(2, 18, cw - 4, 78, 4, COLOR_PANEL_BG);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);

    auto draw_field = [&](int idx, const char* label, const char* value, bool secret, bool selected) {
        int y = 22 + idx * 22;
        GetHAL().canvas.setTextColor(selected ? COLOR_ACCENT : COLOR_LABEL, COLOR_PANEL_BG);
        GetHAL().canvas.setCursor(6, y);
        GetHAL().canvas.print(label);
        GetHAL().canvas.setTextColor(COLOR_VALUE, COLOR_PANEL_BG);
        GetHAL().canvas.setCursor(6, y + 11);
        char buf[64];
        if (_editing && selected) {
            const char* src = _edit_buffer.c_str();
            size_t L        = strlen(src);
            const char* shown = (L > 28) ? src + (L - 28) : src;
            snprintf(buf, sizeof(buf), "%s_", shown);
        } else if (secret) {
            mask_secret(value, buf, sizeof(buf));
        } else if (value[0] == '\0') {
            snprintf(buf, sizeof(buf), "(empty)");
        } else {
            strncpy(buf, value, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
        }
        GetHAL().canvas.print(buf);
    };

    draw_field(0, "Bearer token", g_bearer,   true,  _edit_field == SF_Bearer);
    draw_field(1, "Base URL",     g_base_url, false, _edit_field == SF_Base);

    GetHAL().canvas.setFont(FONT_SMALL);
    GetHAL().canvas.setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
    GetHAL().canvas.setCursor(6, 18 + 78 - 12);
    if (_editing) GetHAL().canvas.print("Enter=save  T=switch field");
    else          GetHAL().canvas.print("E=edit  T=switch field");

    draw_footer();
    GetHAL().pushCanvas();
}

void AppClaudeMeter::draw()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    switch (_screen) {
        case S_Summary:    draw_summary();     break;
        case S_FiveHour:   draw_big_pct_screen("5 HOUR USAGE", snapshot().pct_five_hour); break;
        case S_SevenDay:   draw_big_pct_screen("7 DAY USAGE",  snapshot().pct_seven_day); break;
        case S_OpusSonnet: draw_opus_sonnet(); break;
        case S_Stats:      draw_stats();       break;
        case S_Token:      draw_token();       break;
        case S_Detail:     draw_detail();      break;
        case S_Settings:   draw_settings();    break;
        default:           draw_summary();     break;
    }
}

void AppClaudeMeter::next_screen(int delta)
{
    int n   = (int)S_COUNT;
    _screen = ((_screen + delta) % n + n) % n;
    draw();
}

void AppClaudeMeter::on_key(int keyCode, const char* keyName)
{
    // Settings edit mode: swallow all input until Enter (save). HOME exits
    // via onRunning()'s home-button handler.
    if (_editing) {
        if (keyCode == KEY_ENTER) {
            if (_edit_field == SF_Bearer) {
                strncpy(g_bearer, _edit_buffer.c_str(), sizeof(g_bearer) - 1);
                g_bearer[sizeof(g_bearer) - 1] = '\0';
            } else {
                strncpy(g_base_url, _edit_buffer.c_str(), sizeof(g_base_url) - 1);
                g_base_url[sizeof(g_base_url) - 1] = '\0';
            }
            save_claude_settings_to_nvs();
            _editing = false;
            _edit_buffer.clear();
            kick_refresh();
            draw();
            return;
        }
        if (keyCode == KEY_BACKSPACE || keyCode == KEY_DELETE) {
            if (!_edit_buffer.empty()) _edit_buffer.pop_back();
            draw();
            return;
        }
        if (keyCode == KEY_SPACE) {
            _edit_buffer += ' ';
            draw();
            return;
        }
        // T cycles field even in edit mode.
        if (keyCode == KEY_T) {
            _edit_field  = (_edit_field + 1) % SF_COUNT;
            _edit_buffer = (_edit_field == SF_Bearer) ? g_bearer : g_base_url;
            draw();
            return;
        }
        // Any other printable single-char key: append.
        if (keyName != nullptr && keyName[0] != '\0' && keyName[1] == '\0') {
            _edit_buffer += keyName[0];
            draw();
        }
        return;
    }

    // Navigation
    if (keyCode == KEY_LEFT)  { next_screen(-1); return; }
    if (keyCode == KEY_RIGHT) { next_screen(+1); return; }

    if (keyName != nullptr && keyName[0] != '\0' && keyName[1] == '\0') {
        char c = keyName[0];
        if (c == ',' || c == '[')             { next_screen(-1); return; }
        if (c == '.' || c == '/' || c == ']') { next_screen(+1); return; }
    }

    switch (keyCode) {
        case KEY_R:
            kick_refresh();
            draw();
            return;
        case KEY_M:
            cache_lock();
            g_cache.muted = !g_cache.muted;
            cache_unlock();
            draw();
            return;
        case KEY_E:
            _screen      = S_Settings;
            _editing     = true;
            _edit_buffer = (_edit_field == SF_Bearer) ? g_bearer : g_base_url;
            draw();
            return;
        case KEY_T:
            if (_screen == S_Settings) {
                _edit_field = (_edit_field + 1) % SF_COUNT;
                draw();
            }
            return;
        case KEY_B:
            // exit to background — task keeps running
            audio::play_random_tone();
            close();
            return;
        case KEY_X:
            // stop background polling and exit
            audio::play_random_tone();
            stop_bg_task();
            close();
            return;
        default:
            return;
    }
}

void AppClaudeMeter::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    load_claude_settings_from_nvs();
    _screen         = S_Summary;
    _editing        = false;
    _edit_buffer.clear();
    _last_redraw_ms = 0;

    // Idempotent: no-op if task is already running (e.g., the user pressed B
    // earlier and is re-opening the app to inspect the cached data).
    start_bg_task();
    draw();

    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) {
            if (!keyEvent.state || keyEvent.isModifier) return;
            on_key(keyEvent.keyCode, keyEvent.keyName);
        });
}

void AppClaudeMeter::onRunning()
{
    uint32_t now = GetHAL().millis();
    // Tick once a second for countdown + "next refresh" footer.
    if ((now - _last_redraw_ms) >= 1000) {
        _last_redraw_ms = now;
        draw();
    }

    // HOME = exit to background (same as B). bg task keeps polling.
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppClaudeMeter::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
    // Intentionally do NOT stop the bg task here — exit-to-background is the
    // default. The user must press X to fully stop.
}
