/**
 * @file app_gemini.cpp
 * @brief Gemini AI chat (port of nishad2m8/GeminiPuter).
 *
 * UI: title bar with WiFi status chip, rounded chat panel with color-coded
 *     messages, input bar at bottom, footer hint.
 * Keys: Enter send / save, Backspace edit input, Tab toggle focus,
 *       ; / . scroll chat when focus is on response, HOME exit.
 */
#include "app_gemini.h"
#include "../utils/theme/theme_define.h"
#include "../../hal/keyboard/keymap.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace MOONCAKE::APPS;

#define _keyboard _data.hal->keyboard()
#define _canvas _data.hal->canvas()
#define _canvas_update _data.hal->canvas_update
#define _canvas_clear() _canvas->fillScreen(THEME_COLOR_BG)

static const char* NVS_NAMESPACE = "gemini";
static const char* NVS_KEY_API   = "apikey";
static constexpr size_t API_KEY_MAX = 128;
static constexpr size_t REPLY_MAX   = 500;
static const char* GEMINI_MODEL = "gemma-3-1b-it";

/* Layout (canvas 206x109) */
static constexpr int TITLE_Y     = 1;
static constexpr int CHIP_Y      = 5;
static constexpr int PANEL_Y     = 19;
static constexpr int PANEL_H     = 58;
static constexpr int CHAT_LINE_H = 10;
static constexpr int CHAT_PAD_X  = 5;
static constexpr int CHAT_PAD_Y  = 3;
static constexpr int CHAT_LINES  = 5;          /* (PANEL_H - 2*CHAT_PAD_Y) / CHAT_LINE_H */
static constexpr int CHAT_COLS   = 30;         /* wrap width in chars */
static constexpr int INPUT_Y     = 81;
static constexpr int INPUT_H     = 14;
static constexpr int FOOTER_Y    = 100;

static const uint32_t COLOR_ACCENT      = (uint32_t)0x99FF00;
static const uint32_t COLOR_PANEL_BG    = (uint32_t)0x1E1E22;
static const uint32_t COLOR_INPUT_BG    = (uint32_t)0x252528;
static const uint32_t COLOR_BOT         = (uint32_t)0xE6E6E6;
static const uint32_t COLOR_USER        = (uint32_t)0x99FF00;
static const uint32_t COLOR_SYS         = (uint32_t)0xFFB060;
static const uint32_t COLOR_DIM_TEXT    = (uint32_t)0x9A9A9A;
static const uint32_t COLOR_CHIP_OFF    = (uint32_t)0xFF6464;

/* ---------- helpers ---------- */

static void json_escape(std::string& out, const char* s)
{
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') out += '\\';
        else if (*s == '\n') { out += "\\n"; continue; }
        else if (*s == '\r') continue;
        out += *s;
    }
}

static bool extract_text_from_response(const char* json, char* out, size_t out_size)
{
    const char* pat = "\"text\":\"";
    const char* p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    size_t i = 0;
    while (i < out_size - 1 && *p) {
        if (*p == '\\') {
            if (p[1] == 'n') { out[i++] = '\n'; p += 2; continue; }
            if (p[1] == 't') { out[i++] = ' ';  p += 2; continue; }
            if (p[1] == '"' || p[1] == '\\' || p[1] == '/') {
                p++; out[i++] = *p++; continue;
            }
            if (p[1] == 'u' && p[2] && p[3] && p[4] && p[5]) {
                /* skip unicode escapes - keep ASCII fallback */
                out[i++] = '?'; p += 6; continue;
            }
        }
        if (*p == '"') break;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

/* Strip simple markdown (*, #) and trailing whitespace per line, to match the
 * reference GeminiPuter's response cleanup. */
static void strip_markdown(std::string& s)
{
    std::string out;
    out.reserve(s.size());
    bool line_start = true;
    for (char c : s) {
        if (c == '*' || c == '#' || c == '`') continue;
        if (line_start && (c == ' ' || c == '-')) continue;
        if (c == '\n') line_start = true;
        else           line_start = false;
        out += c;
    }
    s.swap(out);
}

/* Wrap a single message paragraph to lines of at most max_chars. */
static void wrap_to(const std::string& text, int max_chars, std::vector<std::string>& out)
{
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string seg = (nl == std::string::npos)
                              ? text.substr(start)
                              : text.substr(start, nl - start);
        while ((int)seg.size() > max_chars) {
            size_t br = seg.rfind(' ', max_chars);
            if (br == std::string::npos || (int)br < max_chars / 2)
                br = max_chars;
            out.push_back(seg.substr(0, br));
            seg = (br < seg.size()) ? seg.substr(br + (seg[br] == ' ' ? 1 : 0)) : std::string();
        }
        out.push_back(seg);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

/* ---------- AppGemini ---------- */

void AppGemini::_load_api_key()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        _data.api_key.clear();
        return;
    }
    char buf[API_KEY_MAX];
    size_t len = API_KEY_MAX;
    if (nvs_get_str(h, NVS_KEY_API, buf, &len) == ESP_OK)
        _data.api_key = buf;
    else
        _data.api_key.clear();
    nvs_close(h);
}

void AppGemini::_save_api_key()
{
    if (_data.api_key.empty()) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_API, _data.api_key.c_str());
    nvs_commit(h);
    nvs_close(h);
}

void AppGemini::_append_message(Role role, const std::string& text)
{
    _data.transcript.push_back({ role, text });
    _scroll_to_bottom();
}

void AppGemini::_scroll_to_bottom()
{
    /* compute total wrapped lines, set scroll_offset to last page */
    int total = 0;
    std::vector<std::string> tmp;
    for (const auto& m : _data.transcript) {
        tmp.clear();
        std::string s;
        if (m.role == Role_User) s = "You: " + m.text;
        else if (m.role == Role_Bot) s = "G: " + m.text;
        else s = "[" + m.text + "]";
        wrap_to(s, CHAT_COLS, tmp);
        total += (int)tmp.size();
    }
    _data.scroll_offset = total > CHAT_LINES ? total - CHAT_LINES : 0;
}

std::string AppGemini::_send_to_gemini(const std::string& message)
{
    if (_data.api_key.empty())           return "API key not set.";
    if (WiFi.status() != WL_CONNECTED)   return "WiFi not connected.";

    std::string escaped;
    json_escape(escaped, message.c_str());
    char url[320];
    snprintf(url, sizeof(url),
             "https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
             GEMINI_MODEL, _data.api_key.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setTimeout(10000);
    if (!https.begin(client, url))
        return "Connection failed.";

    https.addHeader("Content-Type", "application/json");
    std::string body = "{\"contents\":[{\"parts\":[{\"text\":\"";
    body += escaped;
    body += "\"}]}],\"generationConfig\":{\"maxOutputTokens\":100}}";

    std::vector<uint8_t> payload(body.begin(), body.end());
    int code = https.POST(payload.data(), (int)payload.size());
    String respStr = https.getString();
    std::string response(respStr.c_str(), respStr.length());
    https.end();

    if (code != 200) {
        char msg[32];
        snprintf(msg, sizeof(msg), "HTTP %d", code);
        return msg;
    }

    char text_buf[REPLY_MAX + 32];
    if (!extract_text_from_response(response.c_str(), text_buf, sizeof(text_buf)))
        return "Bad response.";
    std::string text(text_buf);
    strip_markdown(text);
    if (text.size() > REPLY_MAX) text = text.substr(0, REPLY_MAX - 3) + "...";
    return text;
}

void AppGemini::_draw_status_chip()
{
    int cw = _canvas->width();
    _canvas->setFont(FONT_SMALL);
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);
    bool key_ok  = !_data.api_key.empty();
    const char* chip = wifi_ok ? (key_ok ? "ONLINE" : "NO KEY") : "OFFLINE";
    uint32_t col   = wifi_ok ? (key_ok ? COLOR_ACCENT : COLOR_SYS) : COLOR_CHIP_OFF;
    _canvas->setTextColor(col, THEME_COLOR_BG);
    _canvas->drawRightString(chip, cw - 4, CHIP_Y, FONT_SMALL);
}

void AppGemini::_draw_setup()
{
    _canvas_clear();
    int cw = _canvas->width();

    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print("Gemini Setup");

    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, 22);
    _canvas->print("Get a key at ai.google.dev,");
    _canvas->setCursor(3, 33);
    _canvas->print("then paste it below:");

    /* Input panel (mask the typed key with bullets) */
    _canvas->fillSmoothRoundRect(2, 48, cw - 4, 24, 4, COLOR_PANEL_BG);
    _canvas->setTextColor(COLOR_ACCENT, COLOR_PANEL_BG);
    _canvas->setFont(FONT_REPL);
    _canvas->setCursor(6, 53);
    std::string masked(_data.input_buffer.size(), '*');
    if (masked.size() > 24) masked = "..." + masked.substr(masked.size() - 21);
    _canvas->print(masked.c_str());
    if (!_data.loading) _canvas->print("_");

    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, 80);
    char info[40];
    snprintf(info, sizeof(info), "Key length: %u", (unsigned)_data.input_buffer.size());
    _canvas->print(info);

    _canvas->setCursor(3, FOOTER_Y);
    _canvas->print("Enter save  Bksp del  HOME exit");

    _canvas_update();
}

void AppGemini::_draw_chat()
{
    _canvas_clear();
    int cw = _canvas->width();

    /* Title + status chip */
    _canvas->setFont(FONT_REPL);
    _canvas->setTextSize(1);
    _canvas->setTextColor(COLOR_ACCENT, THEME_COLOR_BG);
    _canvas->setCursor(3, TITLE_Y);
    _canvas->print("Gemini");
    _draw_status_chip();

    /* Chat panel */
    _canvas->fillSmoothRoundRect(2, PANEL_Y, cw - 4, PANEL_H, 4, COLOR_PANEL_BG);

    /* Build flat list of (color, line) from transcript and scroll window */
    struct FlatLine { uint32_t color; std::string text; };
    std::vector<FlatLine> flat;
    flat.reserve(32);
    for (const auto& m : _data.transcript) {
        std::vector<std::string> wrapped;
        uint32_t c;
        std::string s;
        if (m.role == Role_User) { c = COLOR_USER; s = "You: " + m.text; }
        else if (m.role == Role_Bot) { c = COLOR_BOT;  s = "G: " + m.text; }
        else                         { c = COLOR_SYS;  s = "[" + m.text + "]"; }
        wrap_to(s, CHAT_COLS, wrapped);
        for (auto& w : wrapped) flat.push_back({ c, std::move(w) });
    }
    if (_data.loading) flat.push_back({ COLOR_SYS, "..." });

    int total = (int)flat.size();
    int off = _data.scroll_offset;
    if (off < 0) off = 0;
    if (off > total - 1) off = total > 0 ? total - 1 : 0;
    _data.scroll_offset = off;

    _canvas->setFont(FONT_SMALL);
    for (int i = 0; i < CHAT_LINES; i++) {
        int idx = off + i;
        if (idx >= total) break;
        const FlatLine& fl = flat[idx];
        _canvas->setTextColor(fl.color, COLOR_PANEL_BG);
        _canvas->setCursor(PANEL_Y > 0 ? CHAT_PAD_X : 0,
                           PANEL_Y + CHAT_PAD_Y + i * CHAT_LINE_H);
        _canvas->print(fl.text.c_str());
    }

    /* Scroll indicators */
    if (off > 0) {
        _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
        _canvas->setCursor(cw - 10, PANEL_Y + 2);
        _canvas->print("^");
    }
    if (off + CHAT_LINES < total) {
        _canvas->setTextColor(COLOR_DIM_TEXT, COLOR_PANEL_BG);
        _canvas->setCursor(cw - 10, PANEL_Y + PANEL_H - 10);
        _canvas->print("v");
    }

    /* Input bar */
    _canvas->fillSmoothRoundRect(2, INPUT_Y, cw - 4, INPUT_H, 3, COLOR_INPUT_BG);
    _canvas->setFont(FONT_REPL);
    uint32_t prompt_color = _data.focus_input ? COLOR_ACCENT : COLOR_DIM_TEXT;
    _canvas->setTextColor(prompt_color, COLOR_INPUT_BG);
    _canvas->setCursor(5, INPUT_Y + 1);
    _canvas->print(">");
    _canvas->setTextColor(COLOR_BOT, COLOR_INPUT_BG);
    std::string shown = _data.input_buffer;
    if (shown.size() > 22) shown = "..." + shown.substr(shown.size() - 19);
    _canvas->setCursor(14, INPUT_Y + 1);
    _canvas->print(shown.c_str());
    if (_data.focus_input && !_data.loading) _canvas->print("_");

    /* Footer */
    _canvas->setFont(FONT_SMALL);
    _canvas->setTextColor(COLOR_DIM_TEXT, THEME_COLOR_BG);
    _canvas->setCursor(3, FOOTER_Y);
    if (_data.focus_input)
        _canvas->print("Enter send  Tab focus  HOME exit");
    else
        _canvas->print(";/. scroll  Tab focus  HOME exit");

    _canvas_update();
}

void AppGemini::_draw()
{
    if (_data.state == State_Setup) _draw_setup();
    else                            _draw_chat();
}

void AppGemini::onCreate()
{
    _data.hal = mcAppGetDatabase()->Get("HAL")->value<HAL::Hal*>();
}

void AppGemini::onResume()
{
    ANIM_APP_OPEN();
    _load_api_key();
    _data.input_buffer.clear();
    if (_data.api_key.empty()) {
        _data.state = State_Setup;
    } else {
        _data.state = State_Chat;
        if (_data.transcript.empty())
            _data.transcript.push_back({ Role_System, "Type and Enter to send." });
        _scroll_to_bottom();
    }
    _draw();
}

void AppGemini::onRunning()
{
    if (_data.loading) return;

    if (_keyboard->keyList().size() != _data.last_key_num) {
        if (_keyboard->keyList().size() != 0) {
            _keyboard->updateKeysState();
            const auto& st = _keyboard->keysState();

            /* Tab toggles focus (chat state only) */
            bool key_tab = false;
            bool key_semi = false, key_dot = false;
            for (int k : st.hidKey) {
                if (k == KEY_TAB)       key_tab  = true;
                else if (k == KEY_SEMICOLON) key_semi = true;
                else if (k == KEY_DOT)  key_dot  = true;
            }

            if (st.enter) {
                if (_data.state == State_Setup) {
                    if (!_data.input_buffer.empty()) {
                        _data.api_key = _data.input_buffer;
                        _data.input_buffer.clear();
                        _save_api_key();
                        _data.state = State_Chat;
                        _data.transcript.clear();
                        _data.transcript.push_back({ Role_System, "Type and Enter to send." });
                        _scroll_to_bottom();
                        _draw();
                    }
                } else if (_data.focus_input && !_data.input_buffer.empty()) {
                    std::string msg = _data.input_buffer;
                    _data.input_buffer.clear();
                    _append_message(Role_User, msg);
                    _data.loading = true;
                    _draw();
                    std::string reply = _send_to_gemini(msg);
                    _data.loading = false;
                    if (!reply.empty() && reply[0] == '[') {
                        /* error-style messages already bracketed */
                        _append_message(Role_System, reply);
                    } else if (reply.find("HTTP ") == 0 || reply == "WiFi not connected." ||
                               reply == "API key not set." || reply == "Bad response." ||
                               reply == "Connection failed.") {
                        _append_message(Role_System, reply);
                    } else {
                        _append_message(Role_Bot, reply);
                    }
                    /* trim transcript history to last ~30 messages */
                    if (_data.transcript.size() > 30)
                        _data.transcript.erase(_data.transcript.begin(),
                                               _data.transcript.begin() + (_data.transcript.size() - 30));
                    _scroll_to_bottom();
                    _draw();
                }
                _data.last_key_num = _keyboard->keyList().size();
                return;
            }

            if (st.del) {
                if (!_data.input_buffer.empty()) {
                    _data.input_buffer.pop_back();
                    _draw();
                }
                _data.last_key_num = _keyboard->keyList().size();
                return;
            }

            if (_data.state == State_Chat && key_tab) {
                _data.focus_input = !_data.focus_input;
                _draw();
                _data.last_key_num = _keyboard->keyList().size();
                return;
            }

            if (_data.state == State_Chat && !_data.focus_input) {
                if (key_semi) { _data.scroll_offset--; if (_data.scroll_offset < 0) _data.scroll_offset = 0; _draw(); }
                else if (key_dot) { _data.scroll_offset++; _draw(); }
                _data.last_key_num = _keyboard->keyList().size();
                return;
            }

            /* Character input */
            for (char c : st.values) {
                _data.input_buffer += c;
                _draw();
                break;
            }
            if (st.space) {
                _data.input_buffer += ' ';
                _draw();
            }
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

void AppGemini::onDestroy() {}
