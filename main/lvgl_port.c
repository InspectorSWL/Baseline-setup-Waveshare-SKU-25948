#include "lvgl_port.h"

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "lv_port";
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static TaskHandle_t s_lvgl_task_handle = NULL;
static bool s_touch_was_pressed = false;
static uint16_t s_last_touch_x = 0;
static uint16_t s_last_touch_y = 0;
static int64_t s_touch_release_candidate_us = 0;

#define TOUCH_RELEASE_DEBOUNCE_US (40000)

static void flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;

    esp_lcd_panel_draw_bitmap(panel_handle,
                              area->x1,
                              area->y1,
                              area->x2 + 1,
                              area->y2 + 1,
                              color_map);

    lv_disp_flush_ready(drv);
}

static lv_disp_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;

    const int buffer_pixels = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT;
    lv_color_t *buf1 = heap_caps_malloc((size_t)buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1) {
        buf1 = heap_caps_malloc((size_t)buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA);
    }
    if (!buf1) {
        return NULL;
    }

    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, buffer_pixels);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LVGL_PORT_H_RES;
    disp_drv.ver_res = LVGL_PORT_V_RES;
    disp_drv.flush_cb = flush_callback;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = panel_handle;

    return lv_disp_drv_register(&disp_drv);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch_handle = (esp_lcd_touch_handle_t)indev_drv->user_data;

    if (touch_handle == NULL) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t touch_x = 0;
    uint16_t touch_y = 0;
    uint8_t touch_count = 0;
    esp_lcd_touch_point_data_t touch_points[1] = {0};

    esp_lcd_touch_read_data(touch_handle);
    if (esp_lcd_touch_get_data(touch_handle, touch_points, &touch_count, 1) == ESP_OK && touch_count > 0) {
        touch_x = touch_points[0].x;
        touch_y = touch_points[0].y;
        data->point.x = touch_x;
        data->point.y = touch_y;
        data->state = LV_INDEV_STATE_PRESSED;
        s_last_touch_x = touch_x;
        s_last_touch_y = touch_y;
        s_touch_release_candidate_us = 0;
        if (!s_touch_was_pressed) {
            ESP_LOGI(TAG, "Touch detected at (%u, %u), count=%u", touch_x, touch_y, touch_count);
        }
        s_touch_was_pressed = true;
        return;
    }

    if (s_touch_was_pressed) {
        const int64_t now_us = esp_timer_get_time();
        if (s_touch_release_candidate_us == 0) {
            s_touch_release_candidate_us = now_us;
        }

        if ((now_us - s_touch_release_candidate_us) < TOUCH_RELEASE_DEBOUNCE_US) {
            data->point.x = s_last_touch_x;
            data->point.y = s_last_touch_y;
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }

        ESP_LOGI(TAG, "Touch released");
    }
    s_touch_was_pressed = false;
    s_touch_release_candidate_us = 0;
    data->state = LV_INDEV_STATE_RELEASED;
}

static lv_indev_t *indev_init(esp_lcd_touch_handle_t touch_handle)
{
    static lv_indev_drv_t indev_drv;

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    indev_drv.user_data = touch_handle;

    return lv_indev_drv_register(&indev_drv);
}

static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static esp_err_t tick_init(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = tick_increment,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t timer_handle = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle));
    return esp_timer_start_periodic(timer_handle, LVGL_PORT_TICK_PERIOD_MS * 1000);
}

static void lvgl_port_task(void *arg)
{
    while (true) {
        uint32_t delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        if (lvgl_port_lock(-1)) {
            delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS) {
            delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        } else if (delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS) {
            delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel_handle, esp_lcd_touch_handle_t touch_handle)
{
    if (!panel_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    lv_init();
    ESP_ERROR_CHECK(tick_init());

    lv_disp_t *disp = display_init(panel_handle);
    if (!disp) {
        return ESP_ERR_NO_MEM;
    }

    if (touch_handle != NULL && indev_init(touch_handle) == NULL) {
        return ESP_FAIL;
    }
    if (touch_handle != NULL) {
        ESP_LOGI(TAG, "LVGL touch input registered");
    } else {
        ESP_LOGW(TAG, "LVGL started without touch input handle");
    }

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mutex) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_ret = xTaskCreatePinnedToCore(lvgl_port_task,
                                                  "lvgl",
                                                  LVGL_PORT_TASK_STACK_SIZE,
                                                  NULL,
                                                  LVGL_PORT_TASK_PRIORITY,
                                                  &s_lvgl_task_handle,
                                                  tskNO_AFFINITY);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool lvgl_port_lock(int timeout_ms)
{
    if (!s_lvgl_mutex) {
        return false;
    }

    TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lvgl_mutex, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    if (s_lvgl_mutex) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}