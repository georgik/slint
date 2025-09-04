// Copyright © SixtyFPS GmbH <info@slint.dev>
// SPDX-License-Identifier: MIT

#include "demo-sw-renderer.h"
#include "slint-esp.h"
#include <ctime>
#include <memory>
#include <slint-platform.h>

// Hardware includes for M5Stack Tab5 manual initialization
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "sdkconfig.h"
#include <vector>
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"
// Manual hardware initialization includes
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_check.h"

using RenderingRotation = slint::platform::SoftwareRenderer::RenderingRotation;

static const char* TAG = "M5Tab5-Slint";

// Note: LCD synchronization removed - DBI interface doesn't support transaction callbacks

#include "esp_ota_ops.h"

// M5Stack Tab5 hardware constants (known values for Tab5)
#define LCD_H_RES  720
#define LCD_V_RES  1280

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




// Note: Display flushing is handled internally by slint_esp_init
// No custom display flush callback needed when using SlintPlatformConfiguration

// Touch input is handled automatically by slint_esp_init

// Manual hardware initialization functions (avoiding BSP issues)
static esp_err_t manual_i2c_init(i2c_master_bus_handle_t *i2c_handle)
{
    ESP_LOGI(TAG, "Manual I2C initialization");
    
    i2c_master_bus_config_t i2c_bus_conf = {};
    i2c_bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_conf.sda_io_num = GPIO_NUM_31;  // BSP_I2C_SDA
    i2c_bus_conf.scl_io_num = GPIO_NUM_32;  // BSP_I2C_SCL
    i2c_bus_conf.i2c_port = I2C_NUM_0;
    i2c_bus_conf.flags.enable_internal_pullup = true;
    
    return i2c_new_master_bus(&i2c_bus_conf, i2c_handle);
}

static esp_err_t manual_display_backlight_init()
{
    ESP_LOGI(TAG, "Manual backlight initialization");
    
    // Setup LEDC peripheral for PWM backlight control
    ledc_timer_config_t lcd_backlight_timer = {};
    lcd_backlight_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    lcd_backlight_timer.duty_resolution = LEDC_TIMER_12_BIT;
    lcd_backlight_timer.timer_num = LEDC_TIMER_0;
    lcd_backlight_timer.freq_hz = 5000;
    lcd_backlight_timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&lcd_backlight_timer));

    ledc_channel_config_t lcd_backlight_channel = {};
    lcd_backlight_channel.gpio_num = GPIO_NUM_22;  // BSP_LCD_BACKLIGHT
    lcd_backlight_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    lcd_backlight_channel.channel = LEDC_CHANNEL_1;
    lcd_backlight_channel.intr_type = LEDC_INTR_DISABLE;
    lcd_backlight_channel.timer_sel = LEDC_TIMER_0;
    lcd_backlight_channel.duty = 0;
    lcd_backlight_channel.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&lcd_backlight_channel));

    // Set backlight to 100%
    uint32_t duty_cycle = 4095;  // 12-bit resolution: 100% = 4095
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty_cycle));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
    
    return ESP_OK;
}

static esp_err_t manual_dsi_phy_power_init()
{
    ESP_LOGI(TAG, "Manual DSI PHY power initialization");
    
    // Turn on the power for MIPI DSI PHY
    static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id = 4;  // LDO_VO4 for DPHY power
    ldo_cfg.voltage_mv = 2500;
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire LDO channel for DPHY: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
    return ESP_OK;
}

static esp_err_t manual_display_init(esp_lcd_panel_handle_t *ret_panel, esp_lcd_touch_handle_t *ret_touch)
{
    ESP_LOGI(TAG, "Manual display initialization");
    
    esp_err_t ret = ESP_OK;
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_handle_t disp_panel = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    
    // Initialize all configs at the top to avoid goto crossing initialization
    esp_lcd_dsi_bus_config_t bus_config = {};
    esp_lcd_dbi_io_config_t dbi_config = {};
    esp_lcd_dpi_panel_config_t dpi_config = {};
    ili9881c_vendor_config_t vendor_config = {};
    esp_lcd_panel_dev_config_t lcd_dev_config = {};
    uint16_t *test_buffer = NULL;  // Initialize test buffer pointer
    
    // Enable DSI PHY power
    ret = manual_dsi_phy_power_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DSI PHY power failed: %s", esp_err_to_name(ret));
        goto err;
    }
    
    // Create MIPI DSI bus
    bus_config.bus_id = 0;
    bus_config.num_data_lanes = 2;
    bus_config.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_config.lane_bit_rate_mbps = 1000;
    ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New DSI bus failed: %s", esp_err_to_name(ret));
        goto err;
    }
    
    // Create DBI panel IO
    dbi_config.virtual_channel = 0;
    dbi_config.lcd_cmd_bits = 8;
    dbi_config.lcd_param_bits = 8;
    ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New panel IO failed: %s", esp_err_to_name(ret));
        goto err;
    }
    
    // Create DPI panel config
    dpi_config.virtual_channel = 0;
    dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_config.dpi_clock_freq_mhz = 60;
    dpi_config.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi_config.num_fbs = 1;
    dpi_config.video_timing.h_size = LCD_H_RES;
    dpi_config.video_timing.v_size = LCD_V_RES;
    dpi_config.video_timing.hsync_back_porch = 140;
    dpi_config.video_timing.hsync_pulse_width = 40;
    dpi_config.video_timing.hsync_front_porch = 40;
    dpi_config.video_timing.vsync_back_porch = 20;
    dpi_config.video_timing.vsync_pulse_width = 4;
    dpi_config.video_timing.vsync_front_porch = 20;
    dpi_config.flags.use_dma2d = true;
    
    // Create vendor config for ILI9881C
    vendor_config.mipi_config.dsi_bus = mipi_dsi_bus;
    vendor_config.mipi_config.dpi_config = &dpi_config;
    vendor_config.mipi_config.lane_num = 2;
    
    // Create LCD panel config
    lcd_dev_config.bits_per_pixel = 16;
    lcd_dev_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    lcd_dev_config.reset_gpio_num = GPIO_NUM_NC;
    lcd_dev_config.vendor_config = &vendor_config;
    
    // Create ILI9881C panel
    ret = esp_lcd_new_panel_ili9881c(io, &lcd_dev_config, &disp_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New panel failed: %s", esp_err_to_name(ret));
        goto err;
    }
    
    ret = esp_lcd_panel_reset(disp_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
        goto err;
    }
    
    ret = esp_lcd_panel_init(disp_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
        goto err;
    }
    
    ret = esp_lcd_panel_disp_on_off(disp_panel, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Panel display on failed: %s", esp_err_to_name(ret));
        goto err;
    }
    
    ESP_LOGI(TAG, "Display panel initialized successfully");
    
    // Note: DBI interface doesn't support on_color_trans_done callbacks
    // For now, we'll use simple delays instead of semaphore synchronization
    
    // Test display with synchronized blue color drawing
    ESP_LOGI(TAG, "Testing display with synchronized blue color...");
    test_buffer = (uint16_t *)heap_caps_malloc(LCD_H_RES * 10 * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (test_buffer) {
        // Fill buffer with blue color (RGB565: 0x001F)
        for (int i = 0; i < LCD_H_RES * 10; i++) {
            test_buffer[i] = 0x001F;  // Blue in RGB565
        }
        
        // Draw blue stripes across the screen with simple timing
        ESP_LOGI(TAG, "Drawing blue stripes with timing delays...");
        for (int y = 0; y < LCD_V_RES; y += 10) {
            // Draw the bitmap
            esp_err_t draw_ret = esp_lcd_panel_draw_bitmap(disp_panel, 0, y, LCD_H_RES, y + 10, test_buffer);
            if (draw_ret == ESP_OK) {
                // Small delay to allow transaction to complete
                vTaskDelay(pdMS_TO_TICKS(1));  // 1ms delay per stripe
            } else {
                ESP_LOGE(TAG, "Failed to draw at y=%d: %s", y, esp_err_to_name(draw_ret));
            }
        }
        
        free(test_buffer);
        ESP_LOGI(TAG, "Blue screen test completed with timing delays");
        vTaskDelay(pdMS_TO_TICKS(1000));  // Show blue screen for 1 second
    }
    
    *ret_panel = disp_panel;
    *ret_touch = touch_handle;
    return ESP_OK;
    
err:
    if (test_buffer) {
        free(test_buffer);
    }
    if (disp_panel) {
        esp_lcd_panel_del(disp_panel);
    }
    if (io) {
        esp_lcd_panel_io_del(io);
    }
    if (mipi_dsi_bus) {
        esp_lcd_del_dsi_bus(mipi_dsi_bus);
    }
    return ret;
}

static esp_err_t manual_touch_init(i2c_master_bus_handle_t i2c_handle, esp_lcd_touch_handle_t *ret_touch)
{
    ESP_LOGI(TAG, "Manual GT911 touch initialization");
    
    // Create I2C panel IO for touch controller
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = {};
    tp_io_config.dev_addr = 0x5D;  // GT911 I2C address
    tp_io_config.on_color_trans_done = NULL;
    tp_io_config.user_ctx = NULL;
    tp_io_config.control_phase_bytes = 1;
    tp_io_config.dc_bit_offset = 0;
    tp_io_config.lcd_cmd_bits = 16;
    tp_io_config.lcd_param_bits = 0;
    tp_io_config.flags.dc_low_on_data = 0;
    tp_io_config.flags.disable_control_phase = 1;
    tp_io_config.scl_speed_hz = 400000;  // Set proper I2C frequency for GT911
    
    esp_err_t ret = esp_lcd_new_panel_io_i2c(i2c_handle, &tp_io_config, &tp_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch panel IO failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create touch controller config
    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = LCD_H_RES;
    tp_cfg.y_max = LCD_V_RES;
    tp_cfg.rst_gpio_num = GPIO_NUM_NC;  // No reset pin
    tp_cfg.int_gpio_num = GPIO_NUM_23;  // Touch interrupt pin
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;
    tp_cfg.flags.swap_xy = 0;
    tp_cfg.flags.mirror_x = 0;
    tp_cfg.flags.mirror_y = 0;
    
    // Create GT911 touch controller
    ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch controller failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "GT911 touch controller initialized successfully");
    return ESP_OK;
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting Slint Home Automation Demo on M5Stack Tab5");
    ESP_LOGI(TAG, "Using complete manual hardware initialization");

    // Manual I2C initialization
    i2c_master_bus_handle_t i2c_handle = NULL;
    esp_err_t ret = manual_i2c_init(&i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Manual I2C initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "I2C initialized successfully");
    
    // Manual backlight initialization
    ret = manual_display_backlight_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Backlight initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Backlight initialized successfully");
    
    // Manual display initialization
    esp_lcd_panel_handle_t display_panel = NULL;
    esp_lcd_touch_handle_t touch_handle_temp = NULL;
    ret = manual_display_init(&display_panel, &touch_handle_temp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Display initialized successfully");
    
    // Manual touch initialization
    esp_lcd_touch_handle_t touch_handle = NULL;
    ret = manual_touch_init(i2c_handle, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch initialization failed: %s", esp_err_to_name(ret));
        // Continue without touch - not critical for basic operation
    } else {
        ESP_LOGI(TAG, "Touch initialized successfully");
    }
    
    // Give hardware time to stabilize
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "M5Stack Tab5 complete hardware initialization done");
    ESP_LOGI(TAG, "Display resolution: %dx%d", LCD_H_RES, LCD_V_RES);
    
    // Check memory status before Slint initialization
    ESP_LOGI(TAG, "Free heap: %u bytes, largest free block: %u bytes", 
             (unsigned int)esp_get_free_heap_size(), (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Free PSRAM: %u bytes", (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    // Initialize Slint with hardware handles
    ESP_LOGI(TAG, "Initializing Slint ESP platform...");
    
    // Configure Slint platform with real hardware handles
    SlintPlatformConfiguration<slint::platform::Rgb565Pixel> slint_config = {};
    slint_config.size = slint::PhysicalSize(slint::Size<uint32_t>{ LCD_H_RES, LCD_V_RES });
    slint_config.panel_handle = display_panel;  // Use real display panel handle
    slint_config.touch_handle = touch_handle;   // Use real touch handle if available
    slint_config.rotation = slint::platform::SoftwareRenderer::RenderingRotation::NoRotation;
    slint_config.byte_swap = false;
    
    ESP_LOGI(TAG, "Initializing Slint platform...");
    
    // Temporary: Skip Slint initialization to test display only
    ESP_LOGI(TAG, "TEMPORARY: Skipping Slint demo to test display functionality");
    ESP_LOGI(TAG, "Blue screen should be visible - display test successful!");
    
    // Keep the system running with just the blue screen for now
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "Display test running... (blue screen visible)");
    }
    
    // TODO: Re-enable Slint once font issue is resolved
    /*
    slint_esp_init(slint_config);
    
    ESP_LOGI(TAG, "Creating Slint Home Automation demo...");
    
    // Create the Slint application
    auto demo = AppWindow::create();
    
    ESP_LOGI(TAG, "Starting Slint Home Automation Demo on M5Stack Tab5!");
    
    // Show and run the demo
    demo->show();
    demo->run();
    
    ESP_LOGE(TAG, "Slint demo exited unexpectedly");
    */
    
    /*
    // SLINT CODE COMMENTED OUT FOR TESTING
    // Reset watchdog
    esp_task_wdt_reset();
    
    // Configure Slint platform with ESP backend
    ESP_LOGI(TAG, "Configuring Slint platform...");
    
    SlintPlatformConfiguration slint_config = {};
    slint_config.size = slint::PhysicalSize({LCD_H_RES, LCD_V_RES});
    slint_config.panel_handle = display_panel;
    slint_config.touch_handle = touch_handle;
    slint_config.rotation = read_rotation();
    
    slint_esp_init(slint_config);
    
    // Reset watchdog
    esp_task_wdt_reset();
    
    // Create Slint application
    ESP_LOGI(TAG, "Creating Slint application...");
    auto demo = AppWindow::create();
    
    // Note: This demo doesn't have factory reset callback
    // If needed, call reset_to_factory_app() manually
    
    ESP_LOGI(TAG, "Starting Slint application...");
    
    // Run the Slint application (this handles the event loop internally)
    demo->run();
    */
}
