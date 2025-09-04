// Copyright © SixtyFPS GmbH <info@slint.dev>
// SPDX-License-Identifier: MIT

// M5Stack Tab5 with Slint UI using BSP
#include <ctime>
#include <memory>

// Slint includes
#include "slint-esp.h"
#include <slint-platform.h>

// Include the generated minimal app header
#include "minimal.h"

// M5Stack Tab5 BSP includes
#include "bsp/m5stack_tab5.h"
#include "bsp/display.h"
#include "bsp/touch.h"

// Basic ESP-IDF includes
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "sdkconfig.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"
#include "esp_lcd_panel_ops.h"

using RenderingRotation = slint::platform::SoftwareRenderer::RenderingRotation;

static const char* TAG = "M5Tab5-Slint";

// Note: LCD synchronization removed - DBI interface doesn't support transaction callbacks

#include "esp_ota_ops.h"

// M5Stack Tab5 hardware constants from BSP
#define LCD_H_RES  BSP_LCD_H_RES  // 720
#define LCD_V_RES  BSP_LCD_V_RES  // 1280

// Note: BSP manages display and touch handles
// No need for global panel handle as BSP manages it

void reset_to_factory_app()
{
    const esp_partition_t *factory_partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory_partition != NULL) {
        if (esp_ota_set_boot_partition(factory_partition) == ESP_OK) {
            printf("Set boot partition to factory, restarting now.\\n");
        } else {
            printf("Failed to set boot partition to factory.\\n");
        }
    } else {
        printf("Factory partition not found.\\n");
    }
    fflush(stdout);
}

RenderingRotation read_rotation()
{
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        return RenderingRotation::NoRotation;
    }

    auto handle = nvs::open_nvs_handle("slint", NVS_READONLY, &err);
    if (err != ESP_OK) {
        return RenderingRotation::NoRotation;
    }

    uint32_t rotation = 0;
    err = handle->get_item("rotation", rotation);
    if (err != ESP_OK) {
        return RenderingRotation::NoRotation;
    }
    return static_cast<RenderingRotation>(rotation);
}

// BSP-based display initialization using M5Stack Tab5 BSP
static esp_err_t bsp_display_init(esp_lcd_panel_handle_t *ret_panel, esp_lcd_touch_handle_t *ret_touch)
{
    ESP_LOGI(TAG, "Initializing M5Stack Tab5 display using BSP...");
    
    esp_err_t ret = ESP_OK;
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    
    // Initialize I2C bus (required for touch)
    ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP I2C init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "BSP I2C initialized successfully");
    
    // Initialize display using BSP
    bsp_display_config_t display_config = { .dummy = 0 };
    ret = bsp_display_new(&display_config, &panel, &io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP display init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "BSP display initialized successfully");
    
    // Turn on the display
    ret = esp_lcd_panel_disp_on_off(panel, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display turn on failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize display brightness
    ret = bsp_display_brightness_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display brightness init failed: %s", esp_err_to_name(ret));
        // Continue - not critical
    } else {
        // Set brightness to 80%
        bsp_display_brightness_set(80);
        ESP_LOGI(TAG, "Display brightness set to 80%");
    }
    
    // Initialize touch (optional)
    if (ret_touch) {
        bsp_touch_config_t touch_config = { .dummy = NULL };
        ret = bsp_touch_new(&touch_config, &touch);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "BSP touch init failed: %s", esp_err_to_name(ret));
            // Continue without touch - not critical for display test
            touch = NULL;
        } else {
            ESP_LOGI(TAG, "BSP touch initialized successfully");
        }
    }
    
    *ret_panel = panel;
    if (ret_touch) {
        *ret_touch = touch;
    }
    
    ESP_LOGI(TAG, "M5Stack Tab5 BSP display initialization complete");
    return ESP_OK;
}

// Initialize Slint platform with BSP handles
static esp_err_t init_slint_with_bsp(esp_lcd_panel_handle_t display_panel, esp_lcd_touch_handle_t touch_handle)
{
    ESP_LOGI(TAG, "Initializing Slint platform with BSP handles...");
    
    // Configure Slint platform with BSP-initialized hardware handles
    SlintPlatformConfiguration<slint::platform::Rgb565Pixel> slint_config;
    slint_config.size = slint::PhysicalSize(slint::Size<uint32_t>{ LCD_H_RES, LCD_V_RES });  // 720x1280 portrait
    slint_config.panel_handle = display_panel;  // Use BSP display panel handle
    slint_config.touch_handle = touch_handle;   // Use BSP touch handle if available
    slint_config.rotation = read_rotation();    // Read rotation from NVS or use default
    slint_config.byte_swap = false;
    
    ESP_LOGI(TAG, "Slint config: size=%dx%d, panel=%p, touch=%p", 
             LCD_H_RES, LCD_V_RES, display_panel, touch_handle);
    
    // Ensure memory is properly aligned before initializing Slint
    // Force PSRAM allocation alignment
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL);
    
    // Initialize Slint with the BSP-configured platform
    slint_esp_init(slint_config);
    ESP_LOGI(TAG, "Slint platform initialized successfully with BSP!");
    
    return ESP_OK;
}

// Touch initialization function removed - not needed for display test

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting M5Stack Tab5 Blue Screen Test");
    ESP_LOGI(TAG, "Using M5Stack Tab5 BSP for hardware initialization");

    // BSP display initialization
    esp_lcd_panel_handle_t display_panel = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;
    esp_err_t ret = bsp_display_init(&display_panel, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BSP display initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "BSP display initialized successfully");
    
    // Give hardware time to stabilize
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "M5Stack Tab5 BSP initialization complete");
    ESP_LOGI(TAG, "Display resolution: %dx%d", LCD_H_RES, LCD_V_RES);
    
    // Check memory status before Slint initialization
    ESP_LOGI(TAG, "Free heap: %u bytes, largest free block: %u bytes", 
             (unsigned int)esp_get_free_heap_size(), (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Free PSRAM: %u bytes", (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    // Initialize Slint with BSP handles
    esp_err_t slint_ret = init_slint_with_bsp(display_panel, touch_handle);
    if (slint_ret != ESP_OK) {
        ESP_LOGE(TAG, "Slint initialization with BSP failed: %s", esp_err_to_name(slint_ret));
        return;
    }
    
    ESP_LOGI(TAG, "Checking memory after Slint initialization...");
    ESP_LOGI(TAG, "Free heap: %u bytes, largest free block: %u bytes", 
             (unsigned int)esp_get_free_heap_size(), (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Free PSRAM: %u bytes", (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    ESP_LOGI(TAG, "Creating minimal Slint UI with BSP...");
    
    // Create minimal UI without text/fonts to test basic rendering
    auto demo = MinimalApp::create();
    ESP_LOGI(TAG, "Minimal Slint UI created successfully");
    
    ESP_LOGI(TAG, "Starting Slint UI on M5Stack Tab5 with BSP!");
    
    // Final memory check before running
    ESP_LOGI(TAG, "Final memory check - Free heap: %u bytes, Free PSRAM: %u bytes", 
             (unsigned int)esp_get_free_heap_size(), (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    // Show and run the demo
    demo->show();
    ESP_LOGI(TAG, "Slint UI window shown, entering event loop...");
    demo->run();
    
    ESP_LOGE(TAG, "Slint demo exited unexpectedly");
}
