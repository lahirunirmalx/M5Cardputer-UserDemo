/*
 * SPDX-FileCopyrightText: 2024 M5Cardputer dev-main contributors
 * SPDX-FileContributor: ported to CardputerADV
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <string>

class AppCalculator : public mooncake::AppAbility {
public:
    AppCalculator();
    ~AppCalculator();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void draw();
    void draw_grid();
    void apply_op();
    void do_equals();
    void clear_state();

    void on_digit(char c);
    void on_op(char op);
    void on_sign();
    void on_backspace();
    void on_clear();

    void on_key(int keyCode, const char* keyName);

    int _key_slot_id     = -1;
    std::string _display_str;
    std::string _input_str;
    double _stored_val   = 0;
    char _pending_op     = 0;
    bool _error          = false;
    bool _result_shown   = false;
};
