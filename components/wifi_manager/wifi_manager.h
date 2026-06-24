#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool target_found;
    bool target_match_is_alias;
    uint16_t ap_count;
    int8_t target_rssi;
    uint8_t target_channel;
    int8_t strongest_rssi;
    uint8_t strongest_channel;
    char matched_ssid[33];
    char strongest_ssid[33];
} wifi_scan_result_t;

void wifi_manager_init(void);

void wifi_manager_connect(
    const char *ssid,
    const char *password
);

bool wifi_manager_scan(const char *ssid, wifi_scan_result_t *result);

bool wifi_manager_scan_ssid(
    const char *ssid,
    int8_t *rssi,
    uint8_t *channel
);

bool wifi_manager_get_saved_credentials(
    char *ssid,
    size_t ssid_size,
    char *password,
    size_t password_size
);

bool wifi_manager_is_connected(void);