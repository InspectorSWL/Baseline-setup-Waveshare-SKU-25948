#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t time_service_init(void);

void time_service_sync_on_wifi_connected(void);

const char *time_service_get_text(void);

bool time_service_is_valid(void);

lv_obj_t *time_service_create_status_label(lv_obj_t *parent);