/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"

static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static i2c_master_dev_handle_t s_ch422g_mode_dev = NULL;
static i2c_master_dev_handle_t s_ch422g_data_dev = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;

/**
 * @brief I2C master initialization
 */
static esp_err_t i2c_master_init(void)
{
    if (s_i2c_bus_handle) {
        return ESP_OK;
    }

    i2c_master_bus_config_t i2c_conf = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_conf, &s_i2c_bus_handle));

    const i2c_device_config_t ch422g_mode_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x24,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus_handle, &ch422g_mode_cfg, &s_ch422g_mode_dev));

    const i2c_device_config_t ch422g_data_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x38,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus_handle, &ch422g_data_cfg, &s_ch422g_data_dev));

    return ESP_OK;
}

// Reset LCD through CH422G expander: LCD_RST low(10ms) then high(100ms)
static esp_err_t waveshare_esp32_s3_lcd_reset(void)
{
    uint8_t write_buf = 0x01;
    ESP_ERROR_CHECK(i2c_master_transmit(s_ch422g_mode_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS));

    // Keep TP_RST/BL high and pulse LCD_RST low
    write_buf = 0x16;
    ESP_ERROR_CHECK(i2c_master_transmit(s_ch422g_data_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS));
    esp_rom_delay_us(10 * 1000);

    write_buf = 0x1E;
    ESP_ERROR_CHECK(i2c_master_transmit(s_ch422g_data_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS));
    esp_rom_delay_us(100 * 1000);

    return ESP_OK;
}

// Initialize RGB LCD
esp_err_t waveshare_esp32_s3_rgb_lcd_init()
{
    ESP_LOGI(TAG, "Initialize CH422G I2C bus");
    i2c_master_init();

    ESP_LOGI(TAG, "Reset LCD via CH422G");
    waveshare_esp32_s3_lcd_reset();
    ESP_LOGI(TAG, "Baseline mode: touch/LVGL disabled; CH422G reset path kept active");

    ESP_LOGI(TAG, "Install RGB LCD panel driver"); // Log the start of the RGB LCD panel driver installation
    esp_lcd_panel_handle_t panel_handle = NULL; // Declare a handle for the LCD panel
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT, // Set the clock source for the panel
        .timings =  {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ, // Pixel clock frequency
            .h_res = EXAMPLE_LCD_H_RES, // Horizontal resolution
            .v_res = EXAMPLE_LCD_V_RES, // Vertical resolution
            .hsync_pulse_width = 4, // Horizontal sync pulse width
            .hsync_back_porch = 8, // Horizontal back porch
            .hsync_front_porch = 8, // Horizontal front porch
            .vsync_pulse_width = 4, // Vertical sync pulse width
            .vsync_back_porch = 8, // Vertical back porch
            .vsync_front_porch = 8, // Vertical front porch
            .flags = {
                .pclk_active_neg = 1, // Active low pixel clock
            },
        },
        .data_width = EXAMPLE_RGB_DATA_WIDTH, // Data width for RGB
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = EXAMPLE_LCD_NUM_FRAME_BUFFERS, // Number of frame buffers
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE, // Bounce buffer size in pixels
        .dma_burst_size = 64,
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC, // GPIO number for horizontal sync
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC, // GPIO number for vertical sync
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE, // GPIO number for data enable
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK, // GPIO number for pixel clock
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP, // GPIO number for display
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA0,
            EXAMPLE_LCD_IO_RGB_DATA1,
            EXAMPLE_LCD_IO_RGB_DATA2,
            EXAMPLE_LCD_IO_RGB_DATA3,
            EXAMPLE_LCD_IO_RGB_DATA4,
            EXAMPLE_LCD_IO_RGB_DATA5,
            EXAMPLE_LCD_IO_RGB_DATA6,
            EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8,
            EXAMPLE_LCD_IO_RGB_DATA9,
            EXAMPLE_LCD_IO_RGB_DATA10,
            EXAMPLE_LCD_IO_RGB_DATA11,
            EXAMPLE_LCD_IO_RGB_DATA12,
            EXAMPLE_LCD_IO_RGB_DATA13,
            EXAMPLE_LCD_IO_RGB_DATA14,
            EXAMPLE_LCD_IO_RGB_DATA15,
        },
        .flags = {
            .fb_in_psram = 1, // Use PSRAM for framebuffer
        },
    };

    // Create a new RGB panel with the specified configuration
    esp_err_t ret = esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RGB panel: 0x%x", ret);
        return ret;
    }
    ESP_LOGI(TAG, "RGB panel created successfully");

    ESP_LOGI(TAG, "Initialize RGB LCD panel"); // Log the initialization of the RGB LCD panel
    ret = esp_lcd_panel_init(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init RGB panel: 0x%x", ret);
        return ret;
    }
    ESP_LOGI(TAG, "RGB panel init succeeded");
    s_panel_handle = panel_handle;
    ESP_LOGW(TAG, "Raw RGB baseline enabled");

    return ESP_OK; // Return success 
}

esp_err_t waveshare_rgb_lcd_fill_color(uint16_t color565)
{
    if (!s_panel_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    const int chunk_lines = 40;
    const size_t chunk_pixels = EXAMPLE_LCD_H_RES * chunk_lines;
    uint16_t *line_buf = heap_caps_malloc(chunk_pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!line_buf) {
        line_buf = heap_caps_malloc(chunk_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    }
    if (!line_buf) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < chunk_pixels; i++) {
        line_buf[i] = color565;
    }

    for (int y = 0; y < EXAMPLE_LCD_V_RES; y += chunk_lines) {
        int y_end = y + chunk_lines;
        if (y_end > EXAMPLE_LCD_V_RES) {
            y_end = EXAMPLE_LCD_V_RES;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel_handle, 0, y, EXAMPLE_LCD_H_RES, y_end, line_buf));
    }

    free(line_buf);
    return ESP_OK;
}

/******************************* Turn on the screen backlight **************************************/
esp_err_t wavesahre_rgb_lcd_bl_on()
{
    if (!s_ch422g_mode_dev || !s_ch422g_data_dev) {
        ESP_LOGW(TAG, "Backlight enable skipped because CH422G is not initialized");
        return ESP_OK;
    }

    //Configure CH422G to output mode 
    uint8_t write_buf = 0x01;
    esp_err_t ret = i2c_master_transmit(s_ch422g_mode_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    //Pull the backlight pin high to light the screen backlight 
    write_buf = 0x1E;
    ret = i2c_master_transmit(s_ch422g_data_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    return ESP_OK;
}

/******************************* Turn off the screen backlight **************************************/
esp_err_t wavesahre_rgb_lcd_bl_off()
{
    if (!s_ch422g_mode_dev || !s_ch422g_data_dev) {
        ESP_LOGW(TAG, "Backlight disable skipped because CH422G is not initialized");
        return ESP_OK;
    }

    //Configure CH422G to output mode 
    uint8_t write_buf = 0x01;
    esp_err_t ret = i2c_master_transmit(s_ch422g_mode_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    //Turn off the screen backlight by pulling the backlight pin low 
    write_buf = 0x1A;
    ret = i2c_master_transmit(s_ch422g_data_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    return ESP_OK;
}
