# Copyright © SixtyFPS GmbH <info@slint.dev>
# SPDX-License-Identifier: MIT


idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES slint esp_lcd esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_hw_support m5stack_tab5
    PRIV_REQUIRES nvs_flash app_update esp_lcd_touch_gt911 esp_lcd_ili9881c

)

slint_target_sources(${COMPONENT_LIB} ../../ui/demo-sw-renderer.slint COMPILATION_UNITS 20)
target_link_options(${COMPONENT_LIB} PUBLIC -Wl,--allow-multiple-definition)
set_target_properties(${COMPONENT_LIB} PROPERTIES SLINT_EMBED_RESOURCES "embed-for-software-renderer-with-sdf")
