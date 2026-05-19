/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_stringir_toolkit.h"
#include "rs_toolkit/rs_encoder.h"
#include "rs_toolkit/rs_decoder.h"
#include "assets/stringir_toolkit_big.h"
#include "assets/stringir_toolkit_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>

using namespace mooncake;

AppStringIRToolKit::AppStringIRToolKit()
{
    setAppInfo().name     = "StringIR";
    setAppInfo().userData = new AppIcon_t(image_data_stringir_toolkit_big, image_data_stringir_toolkit_small);
}

AppStringIRToolKit::~AppStringIRToolKit()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppStringIRToolKit::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _screen_state       = ScreenState::Main;
    const auto now      = GetHAL().millis();
    _cursor_update_time = now;

    // Setup keyboard event handler
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    // Initialize IR
    GetHAL().irInit();

    // Initial render
    render_main_interface();
}

void AppStringIRToolKit::onRunning()
{
    const auto now = GetHAL().millis();

    if (_screen_state == ScreenState::Error) {
        if (now - _error_display_start_time >= 1500) {
            _screen_state = ScreenState::Main;
            render_main_interface();
        }
        handle_input();
        return;
    }

    // Update cursor blinking in main interface
    if (_screen_state == ScreenState::Main) {
        update_cursor();
    }

    // Process sending state
    if (_screen_state == ScreenState::Sending) {
        process_sending();
    }

    // Auto close send complete screen after 1 second
    if (_screen_state == ScreenState::SendComplete && now - _sending_start_time >= 1000) {
        _screen_state = ScreenState::Main;
        render_main_interface();
    }

    handle_input();
}

void AppStringIRToolKit::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    // Disconnect keyboard event handler
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
    }
}

void AppStringIRToolKit::render_main_interface()
{
    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setTextScroll(false);
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);

    render_text_input_line();
    render_style_line();
    render_help_line();

    GetHAL().pushCanvas();
}

void AppStringIRToolKit::render_text_input_line()
{
    const int line_y      = 10;
    const int line_height = 20;

    // Clear line area
    GetHAL().canvas.fillRect(0, line_y, GetHAL().canvas.width(), line_height, THEME_COLOR_BG);

    // Draw border if focused
    if (_focus_line == FocusLine::TextInput) {
        GetHAL().canvas.drawRect(2, line_y - 2, GetHAL().canvas.width() - 4, line_height + 4, TFT_WHITE);
        GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
    } else {
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    }

    GetHAL().canvas.setCursor(5, line_y);
    GetHAL().canvas.print(_input_text.c_str());

    // Draw cursor if focused
    if (_focus_line == FocusLine::TextInput && _cursor_state) {
        GetHAL().canvas.print("_");
    }

    // Draw underline
    GetHAL().canvas.drawLine(5, line_y + 18, GetHAL().canvas.width() - 5, line_y + 18, TFT_WHITE);
}

void AppStringIRToolKit::render_style_line()
{
    const int line_y      = 45;
    const int line_height = 20;

    // Clear line area
    GetHAL().canvas.fillRect(0, line_y, GetHAL().canvas.width(), line_height, THEME_COLOR_BG);

    // Draw border if focused
    if (_focus_line == FocusLine::Style) {
        GetHAL().canvas.drawRect(2, line_y - 2, GetHAL().canvas.width() - 4, line_height + 4, TFT_WHITE);
        GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
    } else {
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    }

    GetHAL().canvas.setCursor(5, line_y);
    GetHAL().canvas.printf("< %s >", _style_options[_style_index].c_str());
}

void AppStringIRToolKit::render_help_line()
{
    const int line_y = 70;

    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(5, line_y);
    GetHAL().canvas.println("FN+Arrow: navigate");
    GetHAL().canvas.setCursor(5, line_y + 16);
    GetHAL().canvas.print("Enter: send");
}

void AppStringIRToolKit::update_cursor()
{
    if (GetHAL().millis() - _cursor_update_time > CURSOR_BLINK_PERIOD) {
        _cursor_state       = !_cursor_state;
        _cursor_update_time = GetHAL().millis();

        if (_screen_state == ScreenState::Main) {
            render_text_input_line();
            GetHAL().pushCanvas();
        }
    }
}

void AppStringIRToolKit::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    // Only handle key press events for non-modifier keys
    if (!keyEvent.state || keyEvent.isModifier) {
        return;
    }

    // Only process other keys in main interface
    if (_screen_state != ScreenState::Main) {
        return;
    }

    // Get modifier mask to check if FN (Shift) is pressed
    uint8_t modifier_mask = GetHAL().keyboard.getModifierMask();
    bool fn_pressed       = (modifier_mask & (KEY_MOD_LSHIFT | KEY_MOD_RSHIFT)) != 0;

    // Get raw key event for arrow key detection
    auto raw_event = GetHAL().keyboard.getLatestKeyEventRaw();

    // Handle Enter key
    if (keyEvent.keyCode == KEY_ENTER) {
        // Prepare JSON data
        std::string json_data = prepare_json_data();
        mclog::tagInfo(getAppInfo().name, "Send data - {}", json_data);

        audio::play_random_tone();
        start_sending();
        return;
    }

    // Handle arrow keys only when FN is pressed (using raw coordinates)
    if (fn_pressed && raw_event.state) {
        // Up arrow: row=2, col=11
        if (raw_event.row == 2 && raw_event.col == 11) {
            _focus_line = FocusLine::TextInput;
            render_text_input_line();
            render_style_line();
            GetHAL().pushCanvas();
            return;
        }
        // Down arrow: row=3, col=11
        if (raw_event.row == 3 && raw_event.col == 11) {
            _focus_line = FocusLine::Style;
            render_text_input_line();
            render_style_line();
            GetHAL().pushCanvas();
            return;
        }

        // Handle Left/Right arrow keys - Switch style options when style is focused
        if (_focus_line == FocusLine::Style) {
            // Left arrow: row=3, col=10
            if (raw_event.row == 3 && raw_event.col == 10) {
                _style_index = (_style_index - 1 + _style_options.size()) % _style_options.size();
                render_style_line();
                GetHAL().pushCanvas();
                return;
            }
            // Right arrow: row=3, col=12
            if (raw_event.row == 3 && raw_event.col == 12) {
                _style_index = (_style_index + 1) % _style_options.size();
                render_style_line();
                GetHAL().pushCanvas();
                return;
            }
        }
    }

    // Handle Backspace
    if (keyEvent.keyCode == KEY_BACKSPACE && _focus_line == FocusLine::TextInput) {
        if (!_input_text.empty()) {
            _input_text.pop_back();
            render_text_input_line();
            GetHAL().pushCanvas();
        }
        return;
    }

    // Handle Space
    if (keyEvent.keyCode == KEY_SPACE && _focus_line == FocusLine::TextInput) {
        if (_input_text.length() < INPUT_BUFFER_SIZE - 1) {
            _input_text += ' ';
            render_text_input_line();
            GetHAL().pushCanvas();
        }
        return;
    }

    // Add regular characters when text input is focused
    if (_focus_line == FocusLine::TextInput && keyEvent.keyName && strlen(keyEvent.keyName) == 1 &&
        _input_text.length() < INPUT_BUFFER_SIZE - 1) {
        _input_text += keyEvent.keyName;
        render_text_input_line();
        GetHAL().pushCanvas();
    }
}

void AppStringIRToolKit::handle_arrow_keys()
{
    // Arrow keys are now handled in handle_key_event
}

void AppStringIRToolKit::handle_input()
{
    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }

    // Add more input handling here
}

void AppStringIRToolKit::start_sending()
{
    // Prepare JSON data and encode with RS
    std::string json_data = prepare_json_data();

    // Encode data with RS error correction
    if (!RSEncoder::encode(json_data, _encoded_data)) {
        mclog::tagError(getAppInfo().name, "Failed to encode data");
        show_error_message("Encode failed");
        return;
    }

    // Self-validate encoded payload before transmission
    std::string decoded_data;
    if (!RSDecoder::decode(_encoded_data, decoded_data)) {
        mclog::tagError(getAppInfo().name, "Self-check decode failed");
        show_error_message("Decode self-check failed");
        _encoded_data.clear();
        return;
    }

    if (decoded_data != json_data) {
        mclog::tagError(getAppInfo().name, "Self-check mismatch");
        show_error_message("Decode mismatch");
        _encoded_data.clear();
        return;
    }

    _screen_state       = ScreenState::Sending;
    _sending_start_time = GetHAL().millis();
    _sending_dots       = 0;
    _send_index         = 0;

    render_sending_animation();
}

void AppStringIRToolKit::process_sending()
{
    const auto now = GetHAL().millis();

    // Update animation dots every 300ms
    if ((now - _sending_start_time) / 300 > _sending_dots) {
        _sending_dots = (now - _sending_start_time) / 300;
        render_sending_animation();
    }

    // Send IR data progressively
    send_ir_data();

    // Check if all data sent
    if (_send_index >= _encoded_data.size()) {
        _screen_state       = ScreenState::SendComplete;
        _sending_start_time = now;
        render_send_complete();
    }
}

void AppStringIRToolKit::render_sending_animation()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);

    // Title
    GetHAL().canvas.setCursor(60, 30);
    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.print("Sending");

    // Animated dots
    for (int i = 0; i < (_sending_dots % 4); i++) {
        GetHAL().canvas.print(".");
    }

    // Show what's being sent
    GetHAL().canvas.setCursor(20, 50);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.printf("Text: %s", _input_text.c_str());

    GetHAL().canvas.setCursor(20, 65);
    GetHAL().canvas.printf("Style: %s", _style_options[_style_index].c_str());

    const size_t total_packets = _encoded_data.size();
    const size_t sent_packets  = (_send_index > total_packets) ? total_packets : _send_index;
    GetHAL().canvas.setCursor(20, 80);
    GetHAL().canvas.printf("Packet: %u/%u", static_cast<unsigned>(sent_packets), static_cast<unsigned>(total_packets));

    GetHAL().pushCanvas();
}

void AppStringIRToolKit::render_send_complete()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);

    // Success message
    GetHAL().canvas.setCursor(40, 40);
    GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    GetHAL().canvas.print("Send Complete!");

    // Show bytes sent
    GetHAL().canvas.setCursor(30, 60);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.printf("Sent %d bytes", _encoded_data.size());

    GetHAL().pushCanvas();
}

void AppStringIRToolKit::render_error_screen()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);

    GetHAL().canvas.setCursor(40, 40);
    GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
    GetHAL().canvas.print("Send Error");

    GetHAL().canvas.setCursor(15, 60);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.print(_error_message.c_str());

    GetHAL().pushCanvas();
}

std::string AppStringIRToolKit::prepare_json_data()
{
    // Create JSON string with text and style for StringIR receivers
    std::string json = fmt::format("{{\"text\":\"{}\",\"style\":\"{}\"}}", _input_text, _style_options[_style_index]);
    return json;
}

void AppStringIRToolKit::send_ir_data()
{
    // StringIR protocol: addr = packet sequence number (0-254), command = data byte
    // Maximum 255 bytes can be sent
    // First packet (addr=0) is sent 5 times for better reliability

    if (_send_index < _encoded_data.size()) {
        // Check if we exceed maximum length
        if (_send_index > 254) {
            mclog::tagError(getAppInfo().name, "Data too large, max 255 bytes");
            _send_index = _encoded_data.size();  // Force completion
            return;
        }

        // Send: addr = sequence number, cmd = data byte
        uint8_t addr = static_cast<uint8_t>(_send_index);  // Sequence number (0-254)
        uint8_t cmd  = _encoded_data[_send_index];         // Data byte

        // First packet: send 5 times for better reliability
        if (_send_index == 0) {
            for (int i = 0; i < 5; i++) {
                GetHAL().irSend(addr, cmd);
                mclog::tagDebug(getAppInfo().name, "Sent first packet (repeat {}): addr=0x{:02X}, cmd=0x{:02X}", i + 1,
                                addr, cmd);
            }
        } else {
            // Other packets: send once
            GetHAL().irSend(addr, cmd);
            mclog::tagDebug(getAppInfo().name, "Sent byte #{}: 0x{:02X} ({}/{})", addr, cmd, _send_index + 1,
                            _encoded_data.size());
        }

        _send_index++;
    }
}

void AppStringIRToolKit::show_error_message(const std::string& message)
{
    _error_message            = message;
    _error_display_start_time = GetHAL().millis();
    _screen_state             = ScreenState::Error;
    render_error_screen();
}
