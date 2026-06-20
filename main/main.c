/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "waveshare_rgb_lcd_port.h"
#include <stdio.h>

void app_main()
{
    printf("[STEP] app_main entered\n");
    fflush(stdout);
    ESP_LOGI(TAG, "FW_MARKER: SOLID_COLOR_TEST_2026_06_19");

    printf("[STEP] starting RGB init\n");
    fflush(stdout);
    esp_err_t ret = waveshare_esp32_s3_rgb_lcd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGB init failed: 0x%x", ret);
        printf("[STEP] RGB init failed: 0x%x\n", ret);
        fflush(stdout);
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    printf("[STEP] RGB init done\n");
    fflush(stdout);
    wavesahre_rgb_lcd_bl_on();  //Turn on the screen backlight 
    // wavesahre_rgb_lcd_bl_off(); //Turn off the screen backlight 
    
    ESP_LOGI(TAG, "Display RAW panel solid color test");

    const uint16_t test_colors[] = {
        0xFFFF, // white
        0xF800, // red
        0x07E0, // green
        0x001F, // blue
    };

    while (true) {
        for (size_t i = 0; i < sizeof(test_colors) / sizeof(test_colors[0]); ++i) {
            ret = waveshare_rgb_lcd_fill_color(test_colors[i]);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Fill color failed at index %u: 0x%x", (unsigned int)i, ret);
                printf("[STEP] fill failed at index %u: 0x%x\n", (unsigned int)i, ret);
                fflush(stdout);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}
