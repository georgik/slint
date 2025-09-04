# Copyright © SixtyFPS GmbH <info@slint.dev>
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.14)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(SLINT_ESP_LOCAL_EXAMPLE ON)
set(SLINT_FEATURE_SDF_FONTS ON)
# Include Slint ESP-IDF bindings and M5Stack Tab5 BSP
set(EXTRA_COMPONENT_DIRS ../../../api/cpp/esp-idf/ /Volumes/ssdt5/Users/georgik/projects/esp-bsp-m5stack/bsp/m5stack_tab5/)
set(SLINT_STYLE "cosmic-light" CACHE STRING "Style for demo" FORCE)

add_compile_options(-fdiagnostics-color=always)

project(home-automation LANGUAGES CXX)
