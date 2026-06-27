#include "time_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TIME_SERVICE_NAMESPACE            "time_service"
#define TIME_SERVICE_EPOCH_KEY            "last_epoch"
#define TIME_SERVICE_LABEL_MAX_COUNT      (4)
#define TIME_SERVICE_TEXT_LEN             (32)
#define TIME_SERVICE_PLACEHOLDER          "----/--/-- --:--:--"
#define TIME_SERVICE_SYNC_INTERVAL_MS     (24ULL * 60ULL * 60ULL * 1000ULL)
#define TIME_SERVICE_SAVE_INTERVAL_SEC    (30 * 60)
#define TIME_SERVICE_VALID_EPOCH_MIN      (1704067200LL)

static const char *TAG = "time_service";

static lv_obj_t *s_time_labels[TIME_SERVICE_LABEL_MAX_COUNT];
static lv_timer_t *s_ui_timer;
static time_t s_last_saved_epoch;
static char s_time_text[TIME_SERVICE_TEXT_LEN] = TIME_SERVICE_PLACEHOLDER;
static bool s_time_valid;
static bool s_initialized;
static bool s_sntp_started;

static bool time_service_has_valid_time(time_t now)
{
    return now >= TIME_SERVICE_VALID_EPOCH_MIN;
}

static void time_service_refresh_cached_text(void)
{
    time_t now = time(NULL);
    struct tm local_time;

    if (!time_service_has_valid_time(now) || localtime_r(&now, &local_time) == NULL) {
        strcpy(s_time_text, TIME_SERVICE_PLACEHOLDER);
        s_time_valid = false;
        return;
    }

    strftime(s_time_text, sizeof(s_time_text), "%Y-%m-%d %H:%M:%S", &local_time);
    s_time_valid = true;
}

static void time_service_update_labels(void)
{
    for (size_t index = 0; index < TIME_SERVICE_LABEL_MAX_COUNT; index++) {
        if (s_time_labels[index] == NULL) {
            continue;
        }

        lv_label_set_text(s_time_labels[index], s_time_text);
    }
}

static esp_err_t time_service_save_epoch(time_t epoch)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    if (!time_service_has_valid_time(epoch)) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = nvs_open(TIME_SERVICE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for save: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_i64(nvs_handle, TIME_SERVICE_EPOCH_KEY, (int64_t)epoch);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        s_last_saved_epoch = epoch;
    } else {
        ESP_LOGW(TAG, "Failed to persist time: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void time_service_restore_epoch(void)
{
    nvs_handle_t nvs_handle;
    int64_t stored_epoch = 0;
    esp_err_t ret = nvs_open(TIME_SERVICE_NAMESPACE, NVS_READONLY, &nvs_handle);

    if (ret != ESP_OK) {
        return;
    }

    ret = nvs_get_i64(nvs_handle, TIME_SERVICE_EPOCH_KEY, &stored_epoch);
    nvs_close(nvs_handle);
    if (ret != ESP_OK || !time_service_has_valid_time((time_t)stored_epoch)) {
        return;
    }

    struct timeval tv = {
        .tv_sec = (time_t)stored_epoch,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) == 0) {
        s_last_saved_epoch = (time_t)stored_epoch;
        ESP_LOGI(TAG, "Restored last known time from NVS");
    }
}

static void time_service_sync_notification_cb(struct timeval *tv)
{
    time_t now = (tv != NULL) ? tv->tv_sec : time(NULL);

    time_service_refresh_cached_text();
    time_service_update_labels();
    (void)time_service_save_epoch(now);
    ESP_LOGI(TAG, "Time synchronized: %s", s_time_text);
}

static void time_service_ui_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    time_service_refresh_cached_text();
    time_service_update_labels();

    if (!s_time_valid) {
        return;
    }

    time_t now = time(NULL);
    if ((now - s_last_saved_epoch) >= TIME_SERVICE_SAVE_INTERVAL_SEC) {
        (void)time_service_save_epoch(now);
    }
}

static void time_service_start_sntp(void)
{
    if (!s_sntp_started) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_set_sync_interval(TIME_SERVICE_SYNC_INTERVAL_MS);
        esp_sntp_set_time_sync_notification_cb(time_service_sync_notification_cb);
        esp_sntp_init();
        s_sntp_started = true;
        ESP_LOGI(TAG, "SNTP initialized");
        return;
    }

    esp_sntp_restart();
    ESP_LOGI(TAG, "SNTP restart requested");
}

esp_err_t time_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    time_service_restore_epoch();
    time_service_refresh_cached_text();
    s_ui_timer = lv_timer_create(time_service_ui_timer_cb, 1000, NULL);
    if (s_ui_timer == NULL) {
        return ESP_FAIL;
    }

    time_service_update_labels();
    s_initialized = true;
    return ESP_OK;
}

void time_service_sync_on_wifi_connected(void)
{
    if (!s_initialized) {
        return;
    }

    time_service_start_sntp();
}

const char *time_service_get_text(void)
{
    time_service_refresh_cached_text();
    return s_time_text;
}

bool time_service_is_valid(void)
{
    time_service_refresh_cached_text();
    return s_time_valid;
}

lv_obj_t *time_service_create_status_label(lv_obj_t *parent)
{
    lv_obj_t *label;

    if (parent == NULL) {
        return NULL;
    }

    label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -18, 14);

    for (size_t index = 0; index < TIME_SERVICE_LABEL_MAX_COUNT; index++) {
        if (s_time_labels[index] == NULL) {
            s_time_labels[index] = label;
            break;
        }
    }

    lv_label_set_text(label, s_time_text);
    return label;
}