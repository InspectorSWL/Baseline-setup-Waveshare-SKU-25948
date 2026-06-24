/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "wifi_manager.h"
#include "waveshare_rgb_lcd_port.h"
#include <stdio.h>

static const char *TAG = "app_main";

void app_main()
{
    printf("[STEP] app_main entered\n");
    fflush(stdout);
    ESP_LOGI(TAG, "FW_MARKER: HELLO_WORLD_2026_06_21");

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

    ret = lvgl_port_init(waveshare_rgb_lcd_get_panel_handle(), waveshare_rgb_lcd_get_touch_handle());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL init failed: 0x%x", ret);
        printf("[STEP] LVGL init failed: 0x%x\n", ret);
        fflush(stdout);
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    wifi_manager_init();

    ESP_LOGI(TAG, "Create LVGL button screen");
    if (lvgl_port_lock(-1)) {
        gui_create();
        lvgl_port_unlock();
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
