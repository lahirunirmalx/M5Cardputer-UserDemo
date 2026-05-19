/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <hal/hal.h>
#include <string>
#include <vector>

/**
 * @brief StringIR creation and sharing toolkit application.
 */
class AppStringIRToolKit : public mooncake::AppAbility {
public:
    AppStringIRToolKit();
    ~AppStringIRToolKit();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    uint32_t _cursor_update_time                                                = 0;
    uint32_t _sending_start_time                                                = 0;
    enum class ScreenState { Main, Sending, SendComplete, Error } _screen_state = ScreenState::Main;
    enum class FocusLine { TextInput, Style } _focus_line                       = FocusLine::TextInput;
    int _sending_dots                                                           = 0;

    // Style options
    std::vector<std::string> _style_options = {"Normal", "Fade", "Stream"};
    int _style_index                        = 0;

    // Text input
    std::string _input_text;
    bool _cursor_state     = false;
    int _key_event_slot_id = -1;

    // Encoded data for sending
    std::vector<uint8_t> _encoded_data;
    size_t _send_index = 0;
    std::string _error_message;
    uint32_t _error_display_start_time = 0;

    static constexpr uint32_t CURSOR_BLINK_PERIOD = 500;
    static constexpr size_t INPUT_BUFFER_SIZE     = 64;

    void render_main_interface();
    void render_text_input_line();
    void render_style_line();
    void render_help_line();
    void render_sending_animation();
    void render_send_complete();
    void render_error_screen();
    void handle_input();
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void handle_arrow_keys();
    void update_cursor();
    void start_sending();
    void process_sending();
    void send_ir_data();
    std::string prepare_json_data();
    void show_error_message(const std::string& message);
};
