
# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)


# add_definitions(-DMC_USE_CUSTOM_CONF)
# add_definitions(-DNO_BOOT_PLAY_STUFF)

include_directories(./main/apps/utils/usb_keyboard_wrap/hid_device)
#include_directories(./components/NeoLED)


# Exclude vendored sub-repos that may sit untracked inside components/ but are
# not consumed by main/. The project ships its own forks of m5::Speaker_Class
# and m5::Mic_Class under main/hal/{speaker,mic}/, and nothing #includes
# mooncake_log or smooth_ui_toolkit. Letting IDF auto-discover them causes
# spurious compile errors when their pinned versions disagree with the
# tracked M5GFX in components/M5GFX/.
set(EXCLUDE_COMPONENTS
    M5Unified
    mooncake_log
    smooth_ui_toolkit
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cardputer)
