#include "vehicle_store.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "vehicle_store";
static const char *NVS_NAMESPACE = "vehicles";
static const char *NVS_KEY_BLOB = "records";

static bool s_initialized;
static Vehicle s_vehicles[VEHICLE_COUNT];

static void normalize_vehicle(Vehicle *vehicle)
{
    if (vehicle == NULL) {
        return;
    }

    vehicle->vehicleName[VEHICLE_NAME_MAX_LEN] = '\0';
    vehicle->rfidUID[VEHICLE_RFID_UID_MAX_LEN] = '\0';
}

static esp_err_t load_vehicles_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        memset(s_vehicles, 0, sizeof(s_vehicles));
        return ESP_OK;
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open vehicle store for reading: 0x%x", ret);
        memset(s_vehicles, 0, sizeof(s_vehicles));
        return ret;
    }

    size_t blob_size = sizeof(s_vehicles);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_BLOB, s_vehicles, &blob_size);
    nvs_close(nvs_handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        memset(s_vehicles, 0, sizeof(s_vehicles));
        return ESP_OK;
    }

    if (ret != ESP_OK || blob_size != sizeof(s_vehicles)) {
        ESP_LOGW(TAG, "Vehicle store contents invalid, resetting: ret=0x%x size=%u", ret, (unsigned int)blob_size);
        memset(s_vehicles, 0, sizeof(s_vehicles));
        return ret == ESP_OK ? ESP_FAIL : ret;
    }

    for (size_t index = 0; index < VEHICLE_COUNT; index++) {
        normalize_vehicle(&s_vehicles[index]);
    }

    return ESP_OK;
}

static esp_err_t save_vehicles_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open vehicle store for writing: 0x%x", ret);
        return ret;
    }

    ret = nvs_set_blob(nvs_handle, NVS_KEY_BLOB, s_vehicles, sizeof(s_vehicles));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t vehicle_store_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    memset(s_vehicles, 0, sizeof(s_vehicles));
    esp_err_t ret = load_vehicles_from_nvs();
    if (ret != ESP_OK && ret != ESP_FAIL) {
        return ret;
    }

    s_initialized = true;
    return ESP_OK;
}

size_t vehicle_store_get_count(void)
{
    return VEHICLE_COUNT;
}

const Vehicle *vehicle_store_get(size_t index)
{
    if (index >= VEHICLE_COUNT) {
        return NULL;
    }

    if (!s_initialized) {
        if (vehicle_store_init() != ESP_OK) {
            return NULL;
        }
    }

    return &s_vehicles[index];
}

esp_err_t vehicle_store_set(size_t index, const Vehicle *vehicle)
{
    if (index >= VEHICLE_COUNT || vehicle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        esp_err_t ret = vehicle_store_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    s_vehicles[index] = *vehicle;
    normalize_vehicle(&s_vehicles[index]);

    return save_vehicles_to_nvs();
}