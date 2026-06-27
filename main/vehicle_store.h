#pragma once

#include <stddef.h>

#include "esp_err.h"

#define VEHICLE_COUNT (15u)
#define VEHICLE_NAME_MAX_LEN (24u)
#define VEHICLE_RFID_UID_MAX_LEN (32u)

typedef struct {
    char vehicleName[VEHICLE_NAME_MAX_LEN + 1u];
    char rfidUID[VEHICLE_RFID_UID_MAX_LEN + 1u];
} Vehicle;

esp_err_t vehicle_store_init(void);
size_t vehicle_store_get_count(void);
const Vehicle *vehicle_store_get(size_t index);
esp_err_t vehicle_store_set(size_t index, const Vehicle *vehicle);