#include "wifi_manager.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_manager";
static const char *NVS_NAMESPACE = "wifi_manager";
static const char *NVS_KEY_SSID = "ssid";
static const char *NVS_KEY_PASSWORD = "password";
static bool s_wifi_initialized;
static bool s_wifi_connected;
static char s_saved_ssid[33];
static char s_saved_password[65];
static char s_pending_ssid[33];
static char s_pending_password[65];

static void trim_copy(char *destination, size_t destination_size, const char *source)
{
    if (destination == NULL || destination_size == 0) {
        return;
    }

    destination[0] = '\0';

    if (source == NULL) {
        return;
    }

    while (*source != '\0' && isspace((unsigned char)*source)) {
        source++;
    }

    size_t length = strlen(source);
    while (length > 0 && isspace((unsigned char)source[length - 1])) {
        length--;
    }

    if (length >= destination_size) {
        length = destination_size - 1;
    }

    memcpy(destination, source, length);
    destination[length] = '\0';
}

static void normalize_ssid_for_compare(char *destination, size_t destination_size, const char *source)
{
    char trimmed[33] = { 0 };

    trim_copy(trimmed, sizeof(trimmed), source);

    size_t length = strlen(trimmed);
    while (length > 0 && isspace((unsigned char)trimmed[length - 1])) {
        trimmed[--length] = '\0';
    }

    if (length > 0 && (trimmed[length - 1] == ']' || trimmed[length - 1] == ')')) {
        char closing = trimmed[length - 1];
        char opening = closing == ']' ? '[' : '(';
        size_t suffix_start = length - 1;

        while (suffix_start > 0 && trimmed[suffix_start - 1] != opening) {
            suffix_start--;
        }

        if (suffix_start > 0 && trimmed[suffix_start - 1] == opening) {
            char suffix[16] = { 0 };
            size_t suffix_length = 0;

            for (size_t index = suffix_start; index < length - 1 && suffix_length < sizeof(suffix) - 1; index++) {
                if (isalnum((unsigned char)trimmed[index])) {
                    suffix[suffix_length++] = (char)tolower((unsigned char)trimmed[index]);
                }
            }
            suffix[suffix_length] = '\0';

            if (strcmp(suffix, "2g") == 0 || strcmp(suffix, "24g") == 0
                || strcmp(suffix, "2ghz") == 0 || strcmp(suffix, "24ghz") == 0) {
                length = suffix_start - 1;
                while (length > 0 && isspace((unsigned char)trimmed[length - 1])) {
                    length--;
                }
                trimmed[length] = '\0';
            }
        }
    }

    trim_copy(destination, destination_size, trimmed);

    for (size_t index = 0; destination[index] != '\0'; index++) {
        destination[index] = (char)tolower((unsigned char)destination[index]);
    }
}

static bool ssid_matches_target(const char *visible_ssid, const char *requested_ssid, bool *is_alias)
{
    char normalized_visible[33] = { 0 };
    char normalized_requested[33] = { 0 };
    char trimmed_visible[33] = { 0 };
    char trimmed_requested[33] = { 0 };

    trim_copy(trimmed_visible, sizeof(trimmed_visible), visible_ssid);
    trim_copy(trimmed_requested, sizeof(trimmed_requested), requested_ssid);
    normalize_ssid_for_compare(normalized_visible, sizeof(normalized_visible), visible_ssid);
    normalize_ssid_for_compare(normalized_requested, sizeof(normalized_requested), requested_ssid);

    if (normalized_visible[0] == '\0' || normalized_requested[0] == '\0') {
        return false;
    }

    if (strcmp(normalized_visible, normalized_requested) != 0) {
        return false;
    }

    if (is_alias != NULL) {
        *is_alias = strcmp(trimmed_visible, trimmed_requested) != 0;
    }

    return true;
}

static void copy_string(char *destination, size_t destination_size, const char *source)
{
    if (destination == NULL || destination_size == 0) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    strncpy(destination, source, destination_size - 1);
    destination[destination_size - 1] = '\0';
}

static void load_saved_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        s_saved_ssid[0] = '\0';
        s_saved_password[0] = '\0';
        return;
    }

    size_t ssid_size = sizeof(s_saved_ssid);
    size_t password_size = sizeof(s_saved_password);

    if (nvs_get_str(nvs_handle, NVS_KEY_SSID, s_saved_ssid, &ssid_size) != ESP_OK) {
        s_saved_ssid[0] = '\0';
    }

    if (nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, s_saved_password, &password_size) != ESP_OK) {
        s_saved_password[0] = '\0';
    }

    nvs_close(nvs_handle);
}

static void save_pending_credentials(void)
{
    if (s_pending_ssid[0] == '\0') {
        return;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open NVS for WiFi credentials: 0x%x", ret);
        return;
    }

    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, NVS_KEY_SSID, s_pending_ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, s_pending_password));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);

    copy_string(s_saved_ssid, sizeof(s_saved_ssid), s_pending_ssid);
    copy_string(s_saved_password, sizeof(s_saved_password), s_pending_password);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            s_wifi_connected = false;
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            s_wifi_connected = false;
            ESP_LOGI(TAG, "WiFi disconnected");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        save_pending_credentials();
        ESP_LOGI(TAG, "WiFi connected");
    }
}

void wifi_manager_init(void)
{
    if (s_wifi_initialized) {
        return;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    load_saved_credentials();

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_initialized = true;
    s_wifi_connected = false;

    if (s_saved_ssid[0] != '\0') {
        wifi_manager_connect(s_saved_ssid, s_saved_password);
    }
}

void wifi_manager_connect(const char *ssid, const char *password)
{
    if (!s_wifi_initialized) {
        wifi_manager_init();
    }

    wifi_config_t wifi_config = { 0 };
    char entered_ssid[sizeof(wifi_config.sta.ssid)] = { 0 };
    char trimmed_ssid[sizeof(wifi_config.sta.ssid)] = { 0 };
    char trimmed_password[sizeof(wifi_config.sta.password)] = { 0 };
    wifi_scan_result_t scan_result;

    trim_copy(entered_ssid, sizeof(entered_ssid), ssid);
    trim_copy(trimmed_ssid, sizeof(trimmed_ssid), ssid);
    trim_copy(trimmed_password, sizeof(trimmed_password), password);

    if (trimmed_password[0] == '\0' && s_saved_password[0] != '\0') {
        bool matches_saved_ssid = false;

        if (s_saved_ssid[0] != '\0') {
            matches_saved_ssid = ssid_matches_target(s_saved_ssid, trimmed_ssid, NULL)
                || ssid_matches_target(trimmed_ssid, s_saved_ssid, NULL);
        }

        if (matches_saved_ssid) {
            copy_string(trimmed_password, sizeof(trimmed_password), s_saved_password);
        }
    }

    if (wifi_manager_scan(trimmed_ssid, &scan_result) && scan_result.target_found && scan_result.matched_ssid[0] != '\0') {
        strncpy(trimmed_ssid, scan_result.matched_ssid, sizeof(trimmed_ssid) - 1);
        trimmed_ssid[sizeof(trimmed_ssid) - 1] = '\0';
    }

    strncpy((char *)wifi_config.sta.ssid, trimmed_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, trimmed_password, sizeof(wifi_config.sta.password) - 1);
    copy_string(s_pending_ssid, sizeof(s_pending_ssid), entered_ssid);
    copy_string(s_pending_password, sizeof(s_pending_password), trimmed_password);

    s_wifi_connected = false;

    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
        ESP_LOGW(TAG, "esp_wifi_disconnect returned 0x%x", ret);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());
}

bool wifi_manager_scan(const char *ssid, wifi_scan_result_t *result)
{
    if (!s_wifi_initialized) {
        wifi_manager_init();
    }

    if (result == NULL) {
        return false;
    }

    memset(result, 0, sizeof(*result));
    result->target_rssi = INT8_MIN;
    result->strongest_rssi = INT8_MIN;

    char trimmed_ssid[33] = { 0 };
    trim_copy(trimmed_ssid, sizeof(trimmed_ssid), ssid);
    if (trimmed_ssid[0] == '\0') {
        return false;
    }

    wifi_scan_config_t scan_config = { 0 };
    scan_config.show_hidden = true;

    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_scan_start failed: 0x%x", ret);
        return false;
    }

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    result->ap_count = ap_count;
    if (ap_count == 0) {
        return true;
    }

    wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (ap_records == NULL) {
        ESP_LOGW(TAG, "Unable to allocate AP scan buffer for %u records", ap_count);
        return false;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));
    result->ap_count = ap_count;

    for (uint16_t index = 0; index < ap_count; index++) {
        const char *current_ssid = (const char *)ap_records[index].ssid;
        bool is_alias = false;

        if (ap_records[index].rssi > result->strongest_rssi) {
            result->strongest_rssi = ap_records[index].rssi;
            result->strongest_channel = ap_records[index].primary;
            strncpy(result->strongest_ssid, current_ssid, sizeof(result->strongest_ssid) - 1);
            result->strongest_ssid[sizeof(result->strongest_ssid) - 1] = '\0';
        }

        if (!ssid_matches_target(current_ssid, trimmed_ssid, &is_alias)) {
            continue;
        }

        if (!result->target_found || ap_records[index].rssi > result->target_rssi) {
            result->target_found = true;
            result->target_match_is_alias = is_alias;
            result->target_rssi = ap_records[index].rssi;
            result->target_channel = ap_records[index].primary;
            strncpy(result->matched_ssid, current_ssid, sizeof(result->matched_ssid) - 1);
            result->matched_ssid[sizeof(result->matched_ssid) - 1] = '\0';
        }
    }

    free(ap_records);
    return true;
}

bool wifi_manager_scan_ssid(const char *ssid, int8_t *rssi, uint8_t *channel)
{
    wifi_scan_result_t result;

    if (!wifi_manager_scan(ssid, &result)) {
        return false;
    }

    if (!result.target_found) {
        return false;
    }

    if (rssi != NULL) {
        *rssi = result.target_rssi;
    }

    if (channel != NULL) {
        *channel = result.target_channel;
    }

    return true;
}

bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}

bool wifi_manager_get_saved_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    if (!s_wifi_initialized) {
        wifi_manager_init();
    }

    if (s_saved_ssid[0] == '\0') {
        if (ssid != NULL && ssid_size > 0) {
            ssid[0] = '\0';
        }
        if (password != NULL && password_size > 0) {
            password[0] = '\0';
        }
        return false;
    }

    if (ssid != NULL && ssid_size > 0) {
        copy_string(ssid, ssid_size, s_saved_ssid);
    }

    if (password != NULL && password_size > 0) {
        copy_string(password, password_size, s_saved_password);
    }

    return true;
}