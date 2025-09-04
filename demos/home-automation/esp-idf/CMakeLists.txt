# Copyright © SixtyFPS GmbH <info@slint.dev>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.14)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# M5Stack Tab5 with Slint integration
set(SLINT_ESP_LOCAL_EXAMPLE ON)
set(SLINT_FEATURE_SDF_FONTS ON)
# Include Slint ESP-IDF bindings and M5Stack Tab5 BSP
set(EXTRA_COMPONENT_DIRS ../../../api/cpp/esp-idf/ /Volumes/ssdt5/Users/georgik/projects/esp-bsp-m5stack/bsp/m5stack_tab5/)
set(SLINT_STYLE "cosmic-light" CACHE STRING "Style for demo" FORCE)

add_compile_options(-fdiagnostics-color=always)

# Fix atomic operations stringop-overflow warnings in GCC 15.1.0 (ESP-IDF toolchain)
# These are false positives in Slint's atomic reference counting
add_compile_options(
    -Wno-stringop-overflow
    -Wno-array-bounds
    -fno-strict-aliasing
)

project(home-automation LANGUAGES CXX)
