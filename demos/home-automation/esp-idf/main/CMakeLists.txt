# Copyright © SixtyFPS GmbH <info@slint.dev>
# SPDX-License-Identifier: MIT


idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES slint esp_lcd esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_hw_support m5stack_tab5
    PRIV_REQUIRES nvs_flash app_update

)

# Use minimal UI with text to test font rendering with BSP
slint_target_sources(${COMPONENT_LIB} ../ui/minimal.slint)
target_link_options(${COMPONENT_LIB} PUBLIC -Wl,--allow-multiple-definition)
# Enable SDF font embedding for text rendering test
set_target_properties(${COMPONENT_LIB} PROPERTIES SLINT_EMBED_RESOURCES "embed-for-software-renderer-with-sdf")
set_target_properties(${COMPONENT_LIB} PROPERTIES SLINT_SCALE_FACTOR "1")

# Fix atomic operations stringop-overflow false positives in GCC 15.1.0
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -Wno-stringop-overflow
    -Wno-array-bounds
    -fno-strict-aliasing
)
