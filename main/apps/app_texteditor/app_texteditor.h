/*
 * SPDX-FileCopyrightText: 2024 Anderson Antunes
 * SPDX-FileContributor: ported to CardputerADV from M5Cardputer dev-main
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <string>

class AppTextEditor : public mooncake::AppAbility {
public:
    AppTextEditor();
    ~AppTextEditor();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class DialogAction { None, DeleteFile };

    void load_buffer_from_disk();
    void render_dialog(const std::string& message, bool can_skip);
    void on_key_event(int keyCode, bool isModifier, const char* keyName);
    void erase_last_char_on_canvas();
    void blink_cursor();
    void append_buffer_to_disk();

    int _key_slot_id                       = -1;
    int _lines                             = 1;
    std::string _text_buffer               = "";
    uint32_t _cursor_blink_period_ms       = 500;
    uint32_t _cursor_blink_last_ms         = 0;
    bool _cursor_visible                   = false;
    bool _waiting_user_input               = false;
    bool _dialog_skippable                 = false;
    bool _sd_mounted                       = false;
    DialogAction _dialog_action            = DialogAction::None;
};
