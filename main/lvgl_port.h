#ifndef LVGL_PORT_H_
#define LVGL_PORT_H_

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LVGL_PORT_H_RES (800)
#define LVGL_PORT_V_RES (480)
#define LVGL_PORT_TICK_PERIOD_MS (2)
#define LVGL_PORT_TASK_MAX_DELAY_MS (500)
#define LVGL_PORT_TASK_MIN_DELAY_MS (10)
#define LVGL_PORT_TASK_STACK_SIZE (6 * 1024)
#define LVGL_PORT_TASK_PRIORITY (2)
#define LVGL_PORT_BUFFER_HEIGHT (40)

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel_handle, esp_lcd_touch_handle_t touch_handle);
bool lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif

#endif