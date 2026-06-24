/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"

#include <stdio.h>
#include <string.h>

#include "esp_lcd_io_i2c.h"
#include "esp_lcd_touch_gt911.h"

static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static i2c_master_dev_handle_t s_ch422g_mode_dev = NULL;
static i2c_master_dev_handle_t s_ch422g_data_dev = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static char s_touch_status[96] = "TP: init not started";
static const char *TAG = "example";

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_SPACING 1
#define FONT_SCALE 8

typedef struct {
    char ch;
    uint8_t rows[FONT_HEIGHT];
} glyph_t;

static const glyph_t s_font[] = {
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { 'd', { 0x01, 0x01, 0x07, 0x09, 0x09, 0x09, 0x07 } },
    { 'e', { 0x00, 0x00, 0x06, 0x09, 0x0F, 0x08, 0x07 } },
    { 'h', { 0x08, 0x08, 0x0E, 0x09, 0x09, 0x09, 0x09 } },
    { 'l', { 0x06, 0x02, 0x02, 0x02, 0x02, 0x02, 0x07 } },
    { 'o', { 0x00, 0x00, 0x06, 0x09, 0x09, 0x09, 0x06 } },
    { 'r', { 0x00, 0x00, 0x0B, 0x0C, 0x08, 0x08, 0x08 } },
    { 'w', { 0x00, 0x00, 0x09, 0x09, 0x09, 0x0F, 0x06 } },
};

static const glyph_t *find_glyph(char ch)
{
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); ++i) {
        if (s_font[i].ch == ch) {
            return &s_font[i];
        }
    }
    return &s_font[0];
}

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

static esp_err_t touch_reset_gpio_init(void)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = GPIO_INPUT_PIN_SEL,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        gpio_set_level(GPIO_INPUT_IO_4, 1);
    }
    return ret;
}

static esp_err_t waveshare_esp32_s3_touch_reset(void)
{
    uint8_t write_buf = 0x01;
    ESP_LOGI(TAG, "Touch reset: configure CH422G output mode");
    ESP_ERROR_CHECK(i2c_master_transmit(s_ch422g_mode_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS));

    write_buf = 0x2C;
    ESP_LOGI(TAG, "Touch reset: drive expander to 0x2C");
    ESP_ERROR_CHECK(i2c_master_transmit(s_ch422g_data_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS));
    esp_rom_delay_us(100 * 1000);

    ESP_LOGI(TAG, "Touch reset: pull GPIO4 low");
    gpio_set_level(GPIO_INPUT_IO_4, 0);
    esp_rom_delay_us(100 * 1000);

    write_buf = 0x2E;
    ESP_LOGI(TAG, "Touch reset: drive expander to 0x2E");
    ESP_ERROR_CHECK(i2c_master_transmit(s_ch422g_data_dev, &write_buf, 1, I2C_MASTER_TIMEOUT_MS));
    esp_rom_delay_us(200 * 1000);

    ESP_LOGI(TAG, "Touch reset sequence complete");

    return ESP_OK;
}

static esp_err_t touch_init(void)
{
    if (s_touch_handle != NULL) {
        ESP_LOGI(TAG, "Touch already initialized");
        snprintf(s_touch_status, sizeof(s_touch_status), "TP: already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Touch init: configure reset GPIO");
    ESP_ERROR_CHECK(touch_reset_gpio_init());
    ESP_LOGI(TAG, "Touch init: reset controller");
    ESP_ERROR_CHECK(waveshare_esp32_s3_touch_reset());

    const esp_err_t probe_14 = i2c_master_probe(s_i2c_bus_handle, 0x14, I2C_MASTER_TIMEOUT_MS);
    const esp_err_t probe_5d = i2c_master_probe(s_i2c_bus_handle, 0x5D, I2C_MASTER_TIMEOUT_MS);
    ESP_LOGI(TAG, "Touch probe results: 0x14=%s 0x5D=%s",
             probe_14 == ESP_OK ? "OK" : "MISS",
             probe_5d == ESP_OK ? "OK" : "MISS");
    snprintf(s_touch_status,
             sizeof(s_touch_status),
             "TP probe 14:%s 5D:%s",
             probe_14 == ESP_OK ? "OK" : "--",
             probe_5d == ESP_OK ? "OK" : "--");

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_LOGI(TAG, "Touch init: create GT911 I2C panel IO");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus_handle, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_io_gt911_config_t tp_gt911_config = {
        .dev_addr = tp_io_config.dev_addr,
    };
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = &tp_gt911_config,
    };

    ESP_LOGI(TAG, "Touch init: create GT911 touch handle");
    esp_err_t ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &s_touch_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Touch init succeeded");
        snprintf(s_touch_status, sizeof(s_touch_status), "TP init OK addr 0x%02lX", (unsigned long)tp_io_config.dev_addr);
    } else {
        ESP_LOGE(TAG, "Touch init failed: 0x%x", ret);
        snprintf(s_touch_status, sizeof(s_touch_status), "TP init fail 0x%x", ret);
    }
    return ret;
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

    ESP_LOGI(TAG, "Initialize GT911 touch controller");
    ESP_ERROR_CHECK(touch_init());

    return ESP_OK; // Return success 
}

esp_lcd_panel_handle_t waveshare_rgb_lcd_get_panel_handle(void)
{
    return s_panel_handle;
}

esp_lcd_touch_handle_t waveshare_rgb_lcd_get_touch_handle(void)
{
    return s_touch_handle;
}

const char *waveshare_rgb_lcd_get_touch_status(void)
{
    return s_touch_status;
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

esp_err_t waveshare_rgb_lcd_draw_text(int x, int y, const char *text, uint16_t fg_color565, uint16_t bg_color565)
{
    if (!s_panel_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!text) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t text_len = strlen(text);
    if (text_len == 0) {
        return ESP_OK;
    }

    const int glyph_advance = (FONT_WIDTH + FONT_SPACING) * FONT_SCALE;
    const int draw_width = (int)(text_len * glyph_advance);
    const int draw_height = FONT_HEIGHT * FONT_SCALE;

    if (x < 0 || y < 0 || x + draw_width > EXAMPLE_LCD_H_RES || y + draw_height > EXAMPLE_LCD_V_RES) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t *buffer = heap_caps_malloc((size_t)draw_width * draw_height * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buffer) {
        buffer = heap_caps_malloc((size_t)draw_width * draw_height * sizeof(uint16_t), MALLOC_CAP_DMA);
    }
    if (!buffer) {
        return ESP_ERR_NO_MEM;
    }

    for (int row = 0; row < draw_height; ++row) {
        for (int col = 0; col < draw_width; ++col) {
            buffer[row * draw_width + col] = bg_color565;
        }
    }

    for (size_t index = 0; index < text_len; ++index) {
        const glyph_t *glyph = find_glyph(text[index]);
        const int glyph_x = (int)index * glyph_advance;
        for (int row = 0; row < FONT_HEIGHT; ++row) {
            for (int col = 0; col < FONT_WIDTH; ++col) {
                if (glyph->rows[row] & (1U << (FONT_WIDTH - 1 - col))) {
                    const int pixel_x = glyph_x + col * FONT_SCALE;
                    const int pixel_y = row * FONT_SCALE;
                    for (int scale_y = 0; scale_y < FONT_SCALE; ++scale_y) {
                        for (int scale_x = 0; scale_x < FONT_SCALE; ++scale_x) {
                            buffer[(pixel_y + scale_y) * draw_width + pixel_x + scale_x] = fg_color565;
                        }
                    }
                }
            }
        }
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel_handle, x, y, x + draw_width, y + draw_height, buffer);
    free(buffer);
    return ret;
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
