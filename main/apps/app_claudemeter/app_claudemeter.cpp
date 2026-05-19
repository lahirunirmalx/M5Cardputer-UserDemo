/**
 * @file app_claudemeter.cpp
 * @brief Polls /usage every 5 min in a background FreeRTOS task. Big bars +
 *        huge percentages, swipable screens, LED blink + (optional) beep on
 *        every refresh. App can exit to background (poll keeps running) or
 *        stop entirely.
 *
 * Keys:  , / [ / ←   prev screen
 *        . / / / ] / → next screen
 *        R   force refresh
 *        M   toggle mute (suppress red beep)
 *        S / E   open Settings + start editing (bearer / base URL)
 *        ; / .   switch field on Settings screen (cardputer up/down)
 *        T   switch field on Settings screen (also works while editing)
 *        Enter (in edit) save + commit to NVS
 *        B   exit to background (task keeps polling)
 *        X   stop polling + exit
 *        HOME exit to background (same as B)
 */
#include "app_claudemeter.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "mdns.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "neoled.h"

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

/* Endpoint base URL and bearer token live in NVS now (namespace "claude").
 * Defaults below are empty — provision via the Settings screen (E) or via
 * tools/nvs_keys.csv + tools/flash_nvs.sh. */
static const char* NVS_NS_CLAUDE = "claude";
static const char* NVS_KEY_BASE  = "base";
static const char* NVS_KEY_TOKEN = "bearer";

static constexpr size_t BASE_URL_MAX = 96;
static constexpr size_t BEARER_MAX   = 128;

static char g_base_url[BASE_URL_MAX] = {0};
static char g_bearer[BEARER_MAX]     = {0};
static char g_auth_header[BEARER_MAX + 16] = {0};   // "Bearer <token>"

static constexpr uint32_t REFRESH_MS = 5 * 60 * 1000UL;

static bool config_ready() {
    return g_base_url[0] != '\0' && g_bearer[0] != '\0';
}

static void rebuild_auth_header() {
    if (g_bearer[0])
        snprintf(g_auth_header, sizeof(g_auth_header), "Bearer %s", g_bearer);
    else
        g_auth_header[0] = '\0';
}

/* Build a URL like "<base>/usage" into dest. */
static void build_url(char* dest, size_t n, const char* suffix) {
    if (g_base_url[0] == '\0') { dest[0] = '\0'; return; }
    size_t bl = strlen(g_base_url);
    /* trim trailing slash */
    while (bl > 0 && g_base_url[bl - 1] == '/') bl--;
    snprintf(dest, n, "%.*s%s", (int)bl, g_base_url, suffix);
}

static void load_claude_settings_from_nvs() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CLAUDE, NVS_READONLY, &h) != ESP_OK) {
        g_base_url[0] = '\0';
        g_bearer[0]   = '\0';
        rebuild_auth_header();
        return;
    }
    size_t sz = sizeof(g_base_url);
    if (nvs_get_str(h, NVS_KEY_BASE,  g_base_url, &sz) != ESP_OK) g_base_url[0] = '\0';
    sz = sizeof(g_bearer);
    if (nvs_get_str(h, NVS_KEY_TOKEN, g_bearer,   &sz) != ESP_OK) g_bearer[0]   = '\0';
    nvs_close(h);
    rebuild_auth_header();
}

static void save_claude_settings_to_nvs() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CLAUDE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_BASE,  g_base_url);
    nvs_set_str(h, NVS_KEY_TOKEN, g_bearer);
    nvs_commit(h);
    nvs_close(h);
    rebuild_auth_header();
}

/* Layout (canvas 206x109) */
static constexpr int TITLE_Y    = 2;
static constexpr int FOOTER_Y   = 100;

static const uint32_t COLOR_ACCENT   = (uint32_t)0x99FF00;   // lime
static const uint32_t COLOR_PANEL_BG = (uint32_t)0x1E1E22;
static const uint32_t COLOR_LABEL    = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_VALUE    = (uint32_t)0xE6E6E6;
static const uint32_t COLOR_OK       = (uint32_t)0x99FF00;
static const uint32_t COLOR_WARN     = (uint32_t)0xFFB060;
static const uint32_t COLOR_DANGER   = (uint32_t)0xFF6464;
static const uint32_t COLOR_BAR_BG   = (uint32_t)0x333338;
static const uint32_t COLOR_CHIP_OFF = (uint32_t)0xFF6464;
static const uint32_t COLOR_CHIP_WARN= (uint32_t)0xFFB060;

/* =================================================================
 * Background polling state — survives destroyApp. Single-instance:
 * the task is started once on first onCreate() and only torn down
 * by the explicit "stop" shortcut (X).
 * ================================================================= */

namespace {

struct Cache {
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t      task  = nullptr;
    HAL::Hal*         hal   = nullptr;
    volatile bool     running          = false;
    volatile bool     stop_requested   = false;
    volatile bool     refresh_requested = false;
    volatile bool     muted = false;

    /* Last fetched values (-1 = unknown). */
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

    /* /stats */
    long total_sessions = -1;
    long total_messages = -1;
    long opus_out_tokens   = -1;
    long sonnet_out_tokens = -1;
    long haiku_out_tokens  = -1;
    long today_messages    = -1;
    long today_sessions    = -1;

    /* /token */
    long  token_expires_in = -1;     // seconds (may be negative)
    bool  token_expired    = false;
    bool  token_known      = false;
};

Cache g_cache;

void cache_lock()   { if (g_cache.mutex) xSemaphoreTake(g_cache.mutex, portMAX_DELAY); }
void cache_unlock() { if (g_cache.mutex) xSemaphoreGive(g_cache.mutex); }

/* ---------- JSON scrape ---------- */
bool find_number_after(const char* json, const char* key, float& out)
{
    const char* p = strstr(json, key);
    if (!p) return false;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r') p++;
    if (*p == 'n') return false;
    char* end = nullptr;
    float v = strtof(p, &end);
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
    static const int dpm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
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

uint32_t bar_color_for(float pct)
{
    if (pct >= 90.f) return COLOR_DANGER;
    if (pct >= 70.f) return COLOR_WARN;
    return COLOR_OK;
}

/* Find an integer (possibly negative) after a key. */
bool find_long_after(const char* json, const char* key, long& out)
{
    const char* p = strstr(json, key);
    if (!p) return false;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r') p++;
    char* end = nullptr;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    out = v;
    return true;
}

/* outputTokens count following a model-name substring (e.g. "claude-opus-"). */
bool find_model_output_tokens(const char* json, const char* model_prefix, long& out)
{
    const char* p = strstr(json, model_prefix);
    if (!p) return false;
    const char* k = strstr(p, "\"outputTokens\"");
    if (!k) return false;
    return find_long_after(k, "\"outputTokens\"", out);
}

/* mDNS bootstrap. Idempotent — mdns_init() rejects re-entry, so the
 * first caller wins and subsequent calls are cheap no-ops. Done lazily
 * here (rather than in HAL init) so the rest of the firmware doesn't
 * pay for mDNS unless ClaudeMeter actually runs. */
static void ensure_mdns_inited()
{
    static bool inited = false;
    if (inited) return;
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set("cardputer");
    }
    inited = true;   /* even on failure — avoid retry spin */
}

/* If `url` has a host ending in `.local`, query mDNS for an A record and
 * rewrite `url` in place to use the resolved IP. No-op for URLs whose
 * hostname is already an IP literal or a non-`.local` name (Arduino's
 * gethostbyname handles those).
 *
 * Buffer rewrite is bounded by `url_size`; if the IP + rest wouldn't fit,
 * the URL is left untouched and the caller hits the same connection error
 * it would have hit before. */
static void resolve_mdns_in_url(char* url, size_t url_size)
{
    char* scheme_end = strstr(url, "://");
    if (!scheme_end) return;
    char* host_start = scheme_end + 3;

    char* host_end = host_start;
    while (*host_end && *host_end != ':' && *host_end != '/') host_end++;

    size_t host_len = host_end - host_start;
    if (host_len < 7) return;   /* shortest possible: "a.local" = 7 chars */
    if (strncmp(host_end - 6, ".local", 6) != 0) return;

    /* Bare hostname = without the ".local" suffix. mdns_query_a expects it
     * that way: e.g. "ohrm-dev-lahiru" not "ohrm-dev-lahiru.local". */
    char bare[64];
    size_t bare_len = host_len - 6;
    if (bare_len == 0 || bare_len >= sizeof(bare)) return;
    memcpy(bare, host_start, bare_len);
    bare[bare_len] = '\0';

    ensure_mdns_inited();

    esp_ip4_addr_t addr;
    if (mdns_query_a(bare, 3000, &addr) != ESP_OK) return;   /* leave URL as-is */

    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&addr));

    size_t prefix_len = host_start - url;
    size_t rest_len   = strlen(host_end);
    size_t ip_len     = strlen(ip);
    if (prefix_len + ip_len + rest_len + 1 > url_size) return;

    memmove(url + prefix_len + ip_len, host_end, rest_len + 1);
    memcpy(url + prefix_len, ip, ip_len);
}

/* Authenticated GET; returns HTTP code (or negative on transport error). */
int http_get(const char* url, String& body)
{
    if (WiFi.status() != WL_CONNECTED) return -1;
    if (!url || url[0] == '\0' || g_auth_header[0] == '\0') return -3;

    /* Copy URL into a writable buffer and pre-resolve any `.local` host. */
    char resolved[BASE_URL_MAX + 16];
    strncpy(resolved, url, sizeof(resolved) - 1);
    resolved[sizeof(resolved) - 1] = '\0';
    resolve_mdns_in_url(resolved, sizeof(resolved));

    HTTPClient http;
    http.setTimeout(8000);
    if (!http.begin(resolved)) return -2;
    http.addHeader("Authorization", g_auth_header);
    int code = http.GET();
    if (code == 200) body = http.getString();
    http.end();
    return code;
}

/* ---------- /usage fetch (primary, drives the LED severity) ---------- */
bool do_fetch_into(Cache& out)
{
    if (WiFi.status() != WL_CONNECTED) {
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
    String body;
    int code = http_get(url, body);
    if (code != 200) {
        cache_lock();
        /* Surface the actual HTTPClient transport-error code (negative
         * values like -1 = CONNECTION_REFUSED, -4 = NOT_CONNECTED,
         * -11 = READ_TIMEOUT) instead of a generic "net err" so the chip
         * actually hints at WHY the fetch failed. Positive values are
         * regular HTTP status codes. */
        if (code < 0) snprintf(out.last_err, sizeof(out.last_err), "net %d", code);
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
    bool ee = find_section_bool(j, "\"extra_usage\"", "\"is_enabled\"");
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

/* ---------- /stats fetch (sessions / messages / model output tokens) ---------- */
void do_stats_fetch_into(Cache& out)
{
    if (!config_ready()) return;
    char url[BASE_URL_MAX + 16];
    build_url(url, sizeof(url), "/stats");
    String body;
    int code = http_get(url, body);
    if (code != 200) return;             // silent: /usage already shows error chip

    const char* j = body.c_str();
    long total_s = -1, total_m = -1;
    long opus = -1, sonnet = -1, haiku = -1;
    find_long_after(j, "\"totalSessions\"", total_s);
    find_long_after(j, "\"totalMessages\"", total_m);
    /* modelUsage contains canonical keys "claude-opus-*", "claude-sonnet-*",
     * "claude-haiku-*". The version suffix changes over time so we match on
     * the family prefix. */
    find_model_output_tokens(j, "\"claude-opus-",   opus);
    find_model_output_tokens(j, "\"claude-sonnet-", sonnet);
    find_model_output_tokens(j, "\"claude-haiku-",  haiku);

    /* dailyActivity is an array; the last entry is the most recent day. We
     * walk to the last `"messageCount":` / `"sessionCount":` occurrence — the
     * top-level totals are sourced separately, so the LAST match in the doc
     * corresponds to today (provided the server returns chronological order,
     * which it does). */
    long today_m = -1, today_s = -1;
    const char* p = j; const char* last = nullptr;
    while ((p = strstr(p, "\"messageCount\""))) { last = p; p++; }
    if (last) find_long_after(last, "\"messageCount\"", today_m);
    p = j; last = nullptr;
    while ((p = strstr(p, "\"sessionCount\""))) { last = p; p++; }
    if (last) find_long_after(last, "\"sessionCount\"", today_s);

    cache_lock();
    out.total_sessions   = total_s;
    out.total_messages   = total_m;
    out.opus_out_tokens  = opus;
    out.sonnet_out_tokens= sonnet;
    out.haiku_out_tokens = haiku;
    out.today_messages   = today_m;
    out.today_sessions   = today_s;
    cache_unlock();
}

/* ---------- /token fetch (OAuth expiry) ---------- */
void do_token_fetch_into(Cache& out)
{
    if (!config_ready()) return;
    char url[BASE_URL_MAX + 16];
    build_url(url, sizeof(url), "/token");
    String body;
    int code = http_get(url, body);
    if (code != 200) return;

    const char* j = body.c_str();
    long exp_in = 0;
    bool ok = find_long_after(j, "\"expires_in_seconds\"", exp_in);
    bool exp = strstr(j, "\"expired\": true") != nullptr ||
               strstr(j, "\"expired\":true")  != nullptr;
    cache_lock();
    out.token_known      = ok;
    out.token_expires_in = ok ? exp_in : -1;
    out.token_expired    = exp;
    cache_unlock();
}

/* ---------- LED blink + beep (runs in bg task) ----------
 * NeoLED and Speaker both contend for I2S0. Order: beep -> wait -> mic off
 * -> NeoLED::init -> N pulses -> NeoLED::destroy -> mic restore.
 */
void blink_and_beep(uint32_t severity)
{
    NeoLED::Pixel on; on.red = on.green = on.blue = 0;
    if (severity == COLOR_OK)        { on.green = 60; }
    else if (severity == COLOR_WARN) { on.red = 60; on.green = 30; }
    else                             { on.red = 80; }   /* danger */
    NeoLED::Pixel off = NeoLED::makePixel(0, 0, 0);

    bool do_beep = (severity == COLOR_DANGER) && !g_cache.muted;
    if (do_beep && g_cache.hal && g_cache.hal->Speaker()) {
        g_cache.hal->Speaker()->setVolume(120);
        g_cache.hal->Speaker()->tone(880.f, 80);
        vTaskDelay(pdMS_TO_TICKS(110));
    }

    bool restore_mic = false;
    if (g_cache.hal && g_cache.hal->mic() && g_cache.hal->mic()->isRunning()) {
        g_cache.hal->mic()->end();
        restore_mic = true;
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    if (NeoLED::init()) {
        /* 5 pulses, ~3 s total */
        for (int i = 0; i < 5 && !g_cache.stop_requested; i++) {
            NeoLED::update(&on);
            vTaskDelay(pdMS_TO_TICKS(280));
            NeoLED::update(&off);
            vTaskDelay(pdMS_TO_TICKS(260));
        }
        /* Latch off twice for reliability, then pin the data line low. */
        NeoLED::update(&off);
        vTaskDelay(pdMS_TO_TICKS(20));
        NeoLED::update(&off);
        vTaskDelay(pdMS_TO_TICKS(20));
        NeoLED::destroy();
        gpio_reset_pin((gpio_num_t)I2S_DO_IO);
        gpio_set_direction((gpio_num_t)I2S_DO_IO, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)I2S_DO_IO, 0);
    }

    if (restore_mic && g_cache.hal && g_cache.hal->mic()) {
        g_cache.hal->mic()->begin();
    }
}

/* ---------- Background task body ---------- */
void bg_task(void*)
{
    /* Immediate first fetch. */
    bool ok;
    do {
        cache_lock(); g_cache.fetch_state = 1; cache_unlock();
        ok = do_fetch_into(g_cache);
        if (ok) {
            do_stats_fetch_into(g_cache);
            do_token_fetch_into(g_cache);
        }
        cache_lock();
        g_cache.fetch_state = ok ? 2 : 3;
        if (ok) g_cache.last_fetch_ms = (uint32_t)millis();
        float worst = -1.f;
        if (g_cache.pct_five_hour > worst) worst = g_cache.pct_five_hour;
        if (g_cache.pct_seven_day > worst) worst = g_cache.pct_seven_day;
        cache_unlock();

        uint32_t severity = ok ? bar_color_for(worst < 0 ? 0 : worst) : COLOR_DANGER;
        blink_and_beep(severity);

        /* Sleep up to REFRESH_MS in 250ms slices so stop_requested and
         * refresh_requested are honored promptly. */
        g_cache.refresh_requested = false;
        for (uint32_t slept = 0;
             slept < REFRESH_MS && !g_cache.stop_requested && !g_cache.refresh_requested;
             slept += 250) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    } while (!g_cache.stop_requested);

    g_cache.running = false;
    g_cache.task = nullptr;
    vTaskDelete(NULL);
}

void start_bg_task(HAL::Hal* hal)
{
    if (g_cache.running) return;
    if (!g_cache.mutex) g_cache.mutex = xSemaphoreCreateMutex();
    g_cache.hal = hal;
    g_cache.stop_requested = false;
    g_cache.running = true;
    xTaskCreatePinnedToCore(bg_task, "claudemeter", 6144, nullptr,
                            1, &g_cache.task, APP_CPU_NUM);
}

void stop_bg_task()
{
    if (!g_cache.running) return;
    g_cache.stop_requested = true;
    /* Wait briefly for clean exit; if the task is mid-fetch this could take
     * up to ~8s (HTTP timeout), but the user pressed X so they want out. */
    for (int i = 0; i < 40 && g_cache.running; i++) vTaskDelay(pdMS_TO_TICKS(100));
}

/* Snapshot the cache into a local copy for the UI draw. */
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

/* ---------- countdown helpers ---------- */
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

} // anonymous namespace

/* =================================================================
 * Foreground UI
 * ================================================================= */

void AppClaudeMeter::_draw_bar(int x, int y, int w, int h, float pct, uint32_t fill)
{
    _canvas->fillRoundRect(x, y, w, h, 3, COLOR_BAR_BG);
    if (pct > 0.f) {
        float p = pct; if (p > 100.f) p = 100.f;
        int fw = (int)((w - 2) * (p / 100.f));
        if (fw > 0)
            _canvas->fillRoundRect(x + 1, y + 1, fw, h - 2, 3, fill);
    }
    _canvas->drawRoundRect(x, y, w, h, 3, COLOR_LABEL);
}

void AppClaudeMeter::_draw_title_bar(const char* title)
{
    int cw = _canvas->width();
    Cache c = snapshot();

    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print(title);

    char pager[12];
    snprintf(pager, sizeof(pager), "[%d/%d]", _data.screen + 1, (int)S_COUNT);

    _canvas->setFont(FONT_SMALL);
    const char* chip;
    uint32_t chip_c;
    if (WiFi.status() != WL_CONNECTED) {
        chip = "OFFLINE"; chip_c = COLOR_CHIP_OFF;
    } else if (c.fetch_state == 1) {
        chip = "FETCH";   chip_c = COLOR_CHIP_WARN;
    } else if (c.fetch_state == 3) {
        chip = c.last_err[0] ? c.last_err : "ERR";
        chip_c = COLOR_CHIP_OFF;
    } else if (c.last_fetch_ms == 0) {
        chip = "WAIT";    chip_c = COLOR_LABEL;
    } else {
        chip = "LIVE";    chip_c = COLOR_ACCENT;
    }
    _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    _canvas->drawRightString(pager, cw - 4, TITLE_Y + 2, FONT_SMALL);
    _canvas->setTextColor(chip_c, THEME_COLOR_BG);
    _canvas->drawRightString(chip, cw - 34, TITLE_Y + 2, FONT_SMALL);
}

void AppClaudeMeter::_draw_footer()
{
    int cw = _canvas->width();
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("< >  R M  B X");

    Cache c = snapshot();
    if (c.muted) {
        _canvas->setTextColor(COLOR_CHIP_WARN, THEME_COLOR_BG);
        _canvas->drawCenterString("MUTE", cw / 2 + 20, FOOTER_Y, FONT_SMALL);
        _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    }

    char fb[24];
    if (c.last_fetch_ms == 0 && c.fetch_state != 1) {
        snprintf(fb, sizeof(fb), "next: --");
    } else {
        uint32_t now = (uint32_t)millis();
        uint32_t since = now - c.last_fetch_ms;
        int next = (since >= REFRESH_MS) ? 0 : (int)((REFRESH_MS - since) / 1000);
        snprintf(fb, sizeof(fb), "next %02d:%02d", next / 60, next % 60);
    }
    _canvas->drawRightString(fb, cw - 4, FOOTER_Y, FONT_SMALL);
}

void AppClaudeMeter::_draw_summary()
{
    int cw = _canvas->width();
    _draw_title_bar("CLAUDE METER");
    Cache c = snapshot();

    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(TFT_WHITE, THEME_COLOR_BG);
    _canvas->setCursor(4, 24);
    _canvas->print("5 HOUR");

    char num[12];
    float p5 = c.pct_five_hour;
    if (p5 < 0.f) snprintf(num, sizeof(num), "--");
    else          snprintf(num, sizeof(num), "%d%%", (int)(p5 + 0.5f));
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(2); // keep this big but not huge, to fit 100% and avoid crowding the bar
    _canvas->setTextColor(p5 < 0.f ? COLOR_LABEL : bar_color_for(p5), THEME_COLOR_BG);
    _canvas->drawRightString(num, cw - 4, 18, FONT_REPL);
    _canvas->setTextSize(1);

    int by5 = 52, bh5 = 14;
    _draw_bar(4, by5, cw - 8, bh5, p5, p5 < 0.f ? COLOR_BAR_BG : bar_color_for(p5));

    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    long secs5 = seconds_until(c.reset_five_hour);
    char rbuf[16];
    if (secs5 < 0) snprintf(rbuf, sizeof(rbuf), "reset: --");
    else { char tb[12]; fmt_hms(secs5, tb, sizeof(tb)); snprintf(rbuf, sizeof(rbuf), "reset %s", tb); }
    _canvas->setCursor(4, by5 + bh5 + 2);
    _canvas->print(rbuf);

    int y7 = 82;
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    _canvas->setCursor(4, y7);
    _canvas->print("7D");

    float p7 = c.pct_seven_day;
    char p7buf[8];
    if (p7 < 0.f) snprintf(p7buf, sizeof(p7buf), "--");
    else          snprintf(p7buf, sizeof(p7buf), "%d%%", (int)(p7 + 0.5f));
    _canvas->setTextColor(p7 < 0.f ? COLOR_LABEL : bar_color_for(p7), THEME_COLOR_BG);
    _canvas->setCursor(22, y7);
    _canvas->print(p7buf);

    int bx7 = 50, bw7 = cw - 50 - 56, by7 = y7 - 1;
    _draw_bar(bx7, by7, bw7, 8, p7, p7 < 0.f ? COLOR_BAR_BG : bar_color_for(p7));

    long secs7 = seconds_until(c.reset_seven_day);
    char rb7[14];
    if (secs7 < 0) snprintf(rb7, sizeof(rb7), "--");
    else           fmt_compact(secs7, rb7, sizeof(rb7));
    _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    _canvas->drawRightString(rb7, cw - 4, y7, FONT_SMALL);

    _draw_footer();
    _canvas_update();
}

void AppClaudeMeter::_draw_big_pct_screen(const char* title, float pct)
{
    int cw = _canvas->width();
    _draw_title_bar(title);

    char num[16];
    if (pct < 0.f) snprintf(num, sizeof(num), "--");
    else           snprintf(num, sizeof(num), "%d%%", (int)(pct + 0.5f));

    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(3);
    uint32_t numc = pct < 0.f ? COLOR_LABEL : bar_color_for(pct);
    _canvas->setTextColor(numc, THEME_COLOR_BG);
    _canvas->drawCenterString(num, cw / 2, 18, FONT_REPL);
    _canvas->setTextSize(1);

    int bx = 4, bw = cw - 8, by = 72, bh = 18;
    _draw_bar(bx, by, bw, bh, pct, pct < 0.f ? COLOR_BAR_BG : bar_color_for(pct));

    _draw_footer();
    _canvas_update();
}

void AppClaudeMeter::_draw_opus_sonnet()
{
    int cw = _canvas->width();
    _draw_title_bar("OPUS / SONNET (7D)");
    Cache c = snapshot();

    auto row = [&](int y, const char* label, float pct, uint32_t hl) {
        _canvas->setFont(FONT_REPL);
        _canvas->setTextSize(1);
        _canvas->setTextColor(COLOR_VALUE, THEME_COLOR_BG);
        _canvas->setCursor(4, y);
        _canvas->print(label);

        char num[12];
        if (pct < 0.f) snprintf(num, sizeof(num), "--");
        else           snprintf(num, sizeof(num), "%d%%", (int)(pct + 0.5f));
        _canvas->setTextSize(2);
        _canvas->setTextColor(pct < 0.f ? COLOR_LABEL : hl, THEME_COLOR_BG);
        _canvas->drawRightString(num, cw - 4, y - 4, FONT_REPL);
        _canvas->setTextSize(1);

        _draw_bar(4, y + 18, cw - 8, 12, pct, pct < 0.f ? COLOR_BAR_BG : hl);
    };

    row(20, "OPUS",   c.pct_seven_opus,   bar_color_for(c.pct_seven_opus));
    row(58, "SONNET", c.pct_seven_sonnet, bar_color_for(c.pct_seven_sonnet));

    _draw_footer();
    _canvas_update();
}

void AppClaudeMeter::_draw_detail()
{
    int cw = _canvas->width();
    _draw_title_bar("DETAIL");
    Cache c = snapshot();

    _canvas->fillSmoothRoundRect(2, 18, cw - 4, 78, 4, COLOR_PANEL_BG);
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);

    auto row = [&](int idx, const char* label, const char* value, uint32_t value_color) {
        int y = 22 + idx * 14;
        _canvas->setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
        _canvas->setCursor(6, y);
        _canvas->print(label);
        _canvas->setTextColor(value_color, COLOR_PANEL_BG);
        _canvas->setCursor(86, y);
        _canvas->print(value);
    };

    char buf[40];
    if (c.pct_five_hour < 0.f) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_five_hour + 0.5f));
    row(0, "5 Hour:", buf, c.pct_five_hour < 0 ? COLOR_LABEL : bar_color_for(c.pct_five_hour));

    if (c.pct_seven_day < 0.f) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_seven_day + 0.5f));
    row(1, "7 Day:", buf, c.pct_seven_day < 0 ? COLOR_LABEL : bar_color_for(c.pct_seven_day));

    if (c.pct_seven_opus < 0.f) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_seven_opus + 0.5f));
    row(2, "Opus:", buf, COLOR_VALUE);

    if (c.pct_seven_sonnet < 0.f) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_seven_sonnet + 0.5f));
    row(3, "Sonnet:", buf, COLOR_VALUE);

    if (c.extra_enabled && c.pct_extra >= 0.f)
        snprintf(buf, sizeof(buf), "%d%%", (int)(c.pct_extra + 0.5f));
    else
        snprintf(buf, sizeof(buf), c.extra_enabled ? "--" : "off");
    row(4, "Extra:", buf, c.extra_enabled ? COLOR_VALUE : COLOR_LABEL);

    if (c.last_fetch_ms == 0) {
        snprintf(buf, sizeof(buf), "never");
    } else {
        uint32_t age_s = ((uint32_t)millis() - c.last_fetch_ms) / 1000;
        if (age_s < 60) snprintf(buf, sizeof(buf), "%us ago", (unsigned)age_s);
        else            snprintf(buf, sizeof(buf), "%um %us ago", (unsigned)(age_s / 60), (unsigned)(age_s % 60));
    }
    row(5, "Updated:", buf, COLOR_VALUE);

    _draw_footer();
    _canvas_update();
}

static void fmt_si_count(long n, char* buf, size_t sz)
{
    if (n < 0)                snprintf(buf, sz, "--");
    else if (n < 1000)        snprintf(buf, sz, "%ld",   n);
    else if (n < 1000 * 1000) snprintf(buf, sz, "%.1fK", n / 1000.0);
    else if (n < 1000L * 1000 * 1000) snprintf(buf, sz, "%.1fM", n / 1.0e6);
    else                              snprintf(buf, sz, "%.1fB", n / 1.0e9);
}

void AppClaudeMeter::_draw_stats()
{
    int cw = _canvas->width();
    _draw_title_bar("STATS (TOTAL)");
    Cache c = snapshot();

    _canvas->fillSmoothRoundRect(2, 18, cw - 4, 78, 4, COLOR_PANEL_BG);
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);

    auto row = [&](int idx, const char* label, const char* value, uint32_t value_color) {
        int y = 22 + idx * 12;
        _canvas->setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
        _canvas->setCursor(6, y);
        _canvas->print(label);
        _canvas->setTextColor(value_color, COLOR_PANEL_BG);
        _canvas->drawRightString(value, cw - 8, y, FONT_REPL);
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

    /* Today's messages and sessions on the last row. */
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

    _draw_footer();
    _canvas_update();
}

void AppClaudeMeter::_draw_token()
{
    int cw = _canvas->width();
    _draw_title_bar("OAUTH TOKEN");
    Cache c = snapshot();

    /* Big "EXPIRES IN" label */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    _canvas->drawCenterString("EXPIRES IN", cw / 2, 20, FONT_SMALL);

    /* Huge countdown HH:MM:SS or "EXPIRED" */
    char num[16];
    uint32_t numc;
    if (!c.token_known) {
        snprintf(num, sizeof(num), "--");
        numc = COLOR_LABEL;
    } else if (c.token_expired || c.token_expires_in <= 0) {
        snprintf(num, sizeof(num), "EXPIRED");
        numc = COLOR_DANGER;
    } else {
        long s = c.token_expires_in;
        long h = s / 3600;
        long m = (s / 60) % 60;
        long ss = s % 60;
        if (h > 0)      snprintf(num, sizeof(num), "%ld:%02ld:%02ld", h, m, ss);
        else            snprintf(num, sizeof(num), "%02ld:%02ld", m, ss);
        numc = (s < 300) ? COLOR_DANGER : (s < 1800 ? COLOR_WARN : COLOR_OK);
    }
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(c.token_expired ? 2 : 3);
    _canvas->setTextColor(numc, THEME_COLOR_BG);
    _canvas->drawCenterString(num, cw / 2, 36, FONT_REPL);
    _canvas->setTextSize(1);

    /* Hint */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_LABEL, THEME_COLOR_BG);
    const char* hint;
    if (!c.token_known)                hint = "run claude /login";
    else if (c.token_expired)          hint = "run: claude /login";
    else if (c.token_expires_in < 300) hint = "expires soon!";
    else                               hint = "auto-refreshed by CLI";
    _canvas->drawCenterString(hint, cw / 2, 82, FONT_SMALL);

    _draw_footer();
    _canvas_update();
}

/* Mask all but the last 4 chars of a sensitive value for the read-only view. */
static void mask_secret(const char* in, char* out, size_t n)
{
    size_t len = strlen(in);
    if (len == 0)        { snprintf(out, n, "(empty)"); return; }
    if (len <= 4)        { snprintf(out, n, "****"); return; }
    size_t tail = 4;
    size_t mask = len - tail;
    if (mask > 8) mask = 8;
    size_t off = 0;
    for (size_t i = 0; i < mask && off < n - 1; i++) out[off++] = '*';
    for (size_t i = len - tail; i < len && off < n - 1; i++) out[off++] = in[i];
    out[off] = '\0';
}

void AppClaudeMeter::_draw_settings()
{
    int cw = _canvas->width();
    _draw_title_bar("SETTINGS");

    _canvas->fillSmoothRoundRect(2, 18, cw - 4, 78, 4, COLOR_PANEL_BG);
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);

    const char* label = (_data.edit_field == SF_Bearer) ? "Bearer token" : "Base URL";
    bool secret      = (_data.edit_field == SF_Bearer);
    const char* val  = (_data.edit_field == SF_Bearer) ? g_bearer : g_base_url;

    /* Field label + "1/2" position indicator */
    _canvas->setTextColor(COLOR_ACCENT, COLOR_PANEL_BG);
    _canvas->setCursor(6, 22);
    _canvas->print(label);
    char idx[8];
    snprintf(idx, sizeof(idx), "%d/%d", _data.edit_field + 1, (int)SF_COUNT);
    _canvas->setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
    _canvas->drawRightString(idx, cw - 8, 22, FONT_REPL);

    /* Value display, wrapped over up to 4 lines so the bearer token doesn't
     * collide with the hint row. If the value still overflows, we show the
     * trailing window so the user can see what they're typing. */
    char buf[200];
    if (_data.editing) {
        const char* src = _data.edit_buffer.c_str();
        snprintf(buf, sizeof(buf), "%s_", src);
    } else if (secret) {
        mask_secret(val, buf, sizeof(buf));
    } else if (val[0] == '\0') {
        snprintf(buf, sizeof(buf), "(empty)");
    } else {
        strncpy(buf, val, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }

    const int CHARS_PER_LINE = 24;
    const int MAX_LINES      = 4;
    const int line_h         = 12;
    const int line_y0        = 36;

    int len = (int)strlen(buf);
    int window = CHARS_PER_LINE * MAX_LINES;
    int start  = (len > window) ? (len - window) : 0;

    _canvas->setTextColor(COLOR_VALUE, COLOR_PANEL_BG);
    for (int line = 0; line < MAX_LINES; line++) {
        int off = start + line * CHARS_PER_LINE;
        if (off >= len) break;
        int chunk = len - off;
        if (chunk > CHARS_PER_LINE) chunk = CHARS_PER_LINE;
        char tmp[CHARS_PER_LINE + 1];
        memcpy(tmp, buf + off, chunk);
        tmp[chunk] = '\0';
        _canvas->setCursor(6, line_y0 + line * line_h);
        _canvas->print(tmp);
    }

    /* Bottom hint */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_LABEL, COLOR_PANEL_BG);
    _canvas->setCursor(6, 18 + 78 - 11);
    if (_data.editing)
        _canvas->print("Enter=save, then ;/. switch");
    else
        _canvas->print("S=edit  ;/.=switch");

    _draw_footer();
    _canvas_update();
}

void AppClaudeMeter::_draw()
{
    _canvas_clear();
    switch (_data.screen) {
        case S_Summary:     _draw_summary();     break;
        case S_FiveHour:    _draw_big_pct_screen("5 HOUR USAGE",  snapshot().pct_five_hour); break;
        case S_SevenDay:    _draw_big_pct_screen("7 DAY USAGE",   snapshot().pct_seven_day); break;
        case S_OpusSonnet:  _draw_opus_sonnet(); break;
        case S_Stats:       _draw_stats();       break;
        case S_Token:       _draw_token();       break;
        case S_Detail:      _draw_detail();      break;
        case S_Settings:    _draw_settings();    break;
        default:            _draw_summary();     break;
    }
}

void AppClaudeMeter::_next_screen(int delta)
{
    int n = (int)S_COUNT;
    _data.screen = ((_data.screen + delta) % n + n) % n;
    _draw();
}

/* ---------- lifecycle ---------- */

void AppClaudeMeter::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
    /* Pull bearer + base URL from NVS. If empty, the bg task will report
     * "no cfg" and the user is steered to the Settings screen (E). */
    load_claude_settings_from_nvs();
}

void AppClaudeMeter::onResume()
{
    ANIM_APP_OPEN();
    _data.last_redraw_ms = 0;
    _data.screen = S_Summary;
    /* Start background task on first entry; if already running this is a
     * no-op and we just attach to existing cached data. */
    start_bg_task(_data.hal);
    _draw();
}

void AppClaudeMeter::onRunning()
{
    uint32_t now = (uint32_t)millis();

    /* Tick once a second for the countdown + "next refresh" footer. */
    if ((now - _data.last_redraw_ms) >= 1000) {
        _data.last_redraw_ms = now;
        _draw();
    }

    /* Keys */
    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& st = _keyboard->keysState();

            /* Settings edit mode swallows all input until Enter (save) or
             * ESC/HOME (cancel). */
            if (_data.editing) {
                /* UP/DOWN switch field while editing — discards the in-flight
                 * edit_buffer in favor of the new field's persisted value. */
                bool field_switched = false;
                for (int hk : st.hidKey) {
                    if (hk == KEY_UP || hk == KEY_DOWN) {
                        int n = (int)SF_COUNT;
                        int dir = (hk == KEY_UP) ? -1 : +1;
                        _data.edit_field = ((_data.edit_field + dir) % n + n) % n;
                        _data.edit_buffer = (_data.edit_field == SF_Bearer) ? g_bearer : g_base_url;
                        field_switched = true;
                        break;
                    }
                }
                if (field_switched) {
                    _draw();
                    _data.last_key_num = _keyboard->keyList().size();
                    return;
                }
                if (st.enter) {
                    /* Commit edit_buffer into the active field, persist. */
                    if (_data.edit_field == SF_Bearer) {
                        strncpy(g_bearer, _data.edit_buffer.c_str(), sizeof(g_bearer) - 1);
                        g_bearer[sizeof(g_bearer) - 1] = '\0';
                    } else {
                        strncpy(g_base_url, _data.edit_buffer.c_str(), sizeof(g_base_url) - 1);
                        g_base_url[sizeof(g_base_url) - 1] = '\0';
                    }
                    save_claude_settings_to_nvs();
                    _data.editing = false;
                    _data.edit_buffer.clear();
                    /* Kick a refresh now that config may be complete. */
                    kick_refresh();
                    _draw();
                } else if (st.del) {
                    if (!_data.edit_buffer.empty()) _data.edit_buffer.pop_back();
                    _draw();
                } else if (st.space) {
                    _data.edit_buffer += ' ';
                    _draw();
                } else {
                    for (char c : st.values) {
                        _data.edit_buffer += c;
                    }
                    _draw();
                }
                _data.last_key_num = _keyboard->keyList().size();
                /* HOME still exits regardless. */
                if (_data.hal->homeButton()->pressed()) {
                    _data.editing = false;
                    _data.hal->playNextSound();
                    destroyApp();
                }
                return;
            }

            for (int hk : st.hidKey) {
                if (hk == KEY_LEFT)  { _next_screen(-1); break; }
                if (hk == KEY_RIGHT) { _next_screen(+1); break; }
                if (_data.screen == S_Settings && (hk == KEY_UP || hk == KEY_DOWN)) {
                    int n = (int)SF_COUNT;
                    int dir = (hk == KEY_UP) ? -1 : +1;
                    _data.edit_field = ((_data.edit_field + dir) % n + n) % n;
                    _draw();
                    break;
                }
            }

            for (char c : st.values) {
                /* Settings screen: ';' = field up, '.' = field down (matches
                 * cardputer-native up/down convention used by app_files etc.).
                 * Must run before the global ',/.' screen-nav handler so '.'
                 * doesn't fall through to next-screen. */
                if (_data.screen == S_Settings && (c == ';' || c == '.')) {
                    int n = (int)SF_COUNT;
                    int dir = (c == ';') ? -1 : +1;
                    _data.edit_field = ((_data.edit_field + dir) % n + n) % n;
                    _draw();
                    break;
                }
                if (c == ',' || c == '[')  { _next_screen(-1); break; }
                if (c == '/' || c == ']' || c == '.') { _next_screen(+1); break; }
                if (c == 'r' || c == 'R')  { kick_refresh(); _draw(); break; }
                if (c == 'm' || c == 'M')  {
                    cache_lock(); g_cache.muted = !g_cache.muted; cache_unlock();
                    _draw();
                    break;
                }
                if (c == 'e' || c == 'E' || c == 's' || c == 'S')  {
                    /* Jump to Settings screen and start editing. Tab cycles
                     * between bearer / base url. */
                    _data.screen = S_Settings;
                    if (!_data.editing) {
                        _data.editing = true;
                        _data.edit_buffer = (_data.edit_field == SF_Bearer) ? g_bearer : g_base_url;
                    }
                    _draw();
                    break;
                }
                if (c == 't' || c == 'T') {
                    /* Cycle field when on the Settings screen */
                    if (_data.screen == S_Settings) {
                        _data.edit_field = (_data.edit_field + 1) % SF_COUNT;
                        if (_data.editing) {
                            _data.edit_buffer = (_data.edit_field == SF_Bearer) ? g_bearer : g_base_url;
                        }
                        _draw();
                        break;
                    }
                }
                if (c == 'b' || c == 'B')  {
                    /* exit to background — task keeps running */
                    _data.hal->playNextSound();
                    destroyApp();
                    return;
                }
                if (c == 'x' || c == 'X')  {
                    /* stop background polling and exit */
                    _data.hal->playNextSound();
                    stop_bg_task();
                    destroyApp();
                    return;
                }
            }
            _data.last_key_num = _keyboard->keyList().size();
        } else {
            _data.last_key_num = 0;
        }
    }

    /* HOME = exit to background (same as B). */
    if (_data.hal->homeButton()->pressed()) {
        _data.hal->playNextSound();
        destroyApp();
    }
}

void AppClaudeMeter::onDestroy()
{
    /* Intentionally do NOT stop the bg task here — exit-to-background is the
     * default. The user must press X to fully stop. */
}
