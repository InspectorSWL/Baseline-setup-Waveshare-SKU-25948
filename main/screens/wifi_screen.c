#include "wifi_screen.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "wifi_manager.h"

#define WIFI_INPUT_WIDTH  (300)
#define WIFI_INPUT_HEIGHT (44)
#define WIFI_BUTTON_WIDTH (180)
#define WIFI_BUTTON_HEIGHT (56)
#define WIFI_SCAN_BUTTON_WIDTH (280)
#define WIFI_RESULT_PANEL_WIDTH (560)
#define WIFI_RESULT_PANEL_HEIGHT (52)

static lv_obj_t *s_wifi_screen;
static lv_obj_t *s_previous_screen;
static lv_obj_t *s_ssid_textarea;
static lv_obj_t *s_password_textarea;
static lv_obj_t *s_signal_panel;
static lv_obj_t *s_signal_label;
static lv_obj_t *s_keyboard;
static bool s_event_handlers_registered;
static bool s_connect_requested;
static bool s_waiting_for_connect_result;

static void load_saved_credentials_into_fields(void)
{
    char saved_ssid[33] = { 0 };
    char saved_password[65] = { 0 };

    if (s_ssid_textarea == NULL || s_password_textarea == NULL) {
        return;
    }

    if (!wifi_manager_get_saved_credentials(saved_ssid, sizeof(saved_ssid), saved_password, sizeof(saved_password))) {
        return;
    }

    lv_textarea_set_text(s_ssid_textarea, saved_ssid);
    lv_textarea_set_text(s_password_textarea, saved_password);
}

static void set_status(const char *text, lv_color_t color)
{
    LV_UNUSED(text);
    LV_UNUSED(color);
}

static void set_signal(const char *text, lv_color_t color)
{
    if (s_signal_label == NULL) {
        return;
    }

    lv_label_set_text(s_signal_label, text);
    lv_obj_set_style_text_color(s_signal_label, color, 0);

    if (s_signal_panel != NULL) {
        lv_obj_set_style_border_color(s_signal_panel, color, 0);
    }
}

static void hide_keyboard(void)
{
    if (s_keyboard == NULL) {
        return;
    }

    lv_keyboard_set_textarea(s_keyboard, NULL);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        hide_keyboard();
    }
}

static void textarea_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *textarea = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_keyboard_set_textarea(s_keyboard, textarea);
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static const char *disconnect_reason_text(wifi_err_reason_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
        return "Auth timeout";
    case WIFI_REASON_AUTH_FAIL:
        return "Auth failed";
    case WIFI_REASON_ASSOC_FAIL:
        return "Association failed";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "Handshake timeout";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4-way timeout";
    case WIFI_REASON_NO_AP_FOUND:
        return "AP not found";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "AP weak";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "Auth mode mismatch";
    case WIFI_REASON_CONNECTION_FAIL:
        return "Connection failed";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "Beacon timeout";
    case WIFI_REASON_ASSOC_LEAVE:
        return "Reconnecting";
    default:
        return "Disconnected";
    }
}

static void wifi_event_ui_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (!s_wifi_screen) {
        return;
    }

    if (!lvgl_port_lock(0)) {
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        set_status("", lv_palette_main(LV_PALETTE_GREEN));
        set_signal("Connected", lv_palette_main(LV_PALETTE_GREEN));
        hide_keyboard();
        s_connect_requested = false;
        s_waiting_for_connect_result = false;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected_event = event_data;
        char status_text[96];

        if (s_waiting_for_connect_result && disconnected_event != NULL
            && disconnected_event->reason == WIFI_REASON_ASSOC_LEAVE) {
            return;
        }

        if (s_connect_requested) {
            if (disconnected_event != NULL) {
                snprintf(status_text, sizeof(status_text), "%s (%d)",
                         disconnect_reason_text(disconnected_event->reason), disconnected_event->reason);
                set_status(status_text, lv_palette_main(LV_PALETTE_RED));
                set_signal(status_text, lv_palette_main(LV_PALETTE_RED));
            } else {
                set_status("Connection Failed", lv_palette_main(LV_PALETTE_RED));
                set_signal("Connection Failed", lv_palette_main(LV_PALETTE_RED));
            }
            s_connect_requested = false;
            s_waiting_for_connect_result = false;
        } else if (!wifi_manager_is_connected()) {
            set_status("Disconnected", lv_color_white());
        }
    }

    lvgl_port_unlock();
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, WIFI_BUTTON_WIDTH, WIFI_BUTTON_HEIGHT);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_bg_color(button, lv_color_make(120, 120, 120), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_make(150, 150, 150), 0);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    return button;
}

static lv_color_t signal_color_from_rssi(int8_t rssi)
{
    if (rssi >= -60) {
        return lv_palette_main(LV_PALETTE_GREEN);
    }

    if (rssi >= -75) {
        return lv_palette_main(LV_PALETTE_YELLOW);
    }

    return lv_palette_main(LV_PALETTE_RED);
}

static void scan_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const char *ssid = lv_textarea_get_text(s_ssid_textarea);
    wifi_scan_result_t scan_result;
    char signal_text[160];

    if (ssid == NULL || ssid[0] == '\0') {
        set_status("Enter SSID", lv_palette_main(LV_PALETTE_RED));
        set_signal("Enter SSID first", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    hide_keyboard();
    set_status("Scanning...", lv_color_white());
    set_signal("Scanning...", lv_color_white());

    if (!wifi_manager_scan(ssid, &scan_result)) {
        set_signal("Scan failed", lv_palette_main(LV_PALETTE_RED));
        set_status("Scan error", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    if (scan_result.target_found) {
        if (scan_result.target_match_is_alias) {
            snprintf(signal_text, sizeof(signal_text), "Detected via alias: %s | %d dBm Ch %u | APs: %u",
                     scan_result.matched_ssid, scan_result.target_rssi,
                     scan_result.target_channel, scan_result.ap_count);
        } else {
            snprintf(signal_text, sizeof(signal_text), "Detected: %d dBm Ch %u | APs seen: %u",
                     scan_result.target_rssi, scan_result.target_channel, scan_result.ap_count);
        }
        set_signal(signal_text, signal_color_from_rssi(scan_result.target_rssi));
        set_status("SSID detected", lv_color_white());
        return;
    }

    if (scan_result.ap_count == 0) {
        set_signal("No 2.4 GHz networks detected", lv_palette_main(LV_PALETTE_RED));
        set_status("No APs visible", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    snprintf(signal_text, sizeof(signal_text), "Target not seen. APs: %u | Strongest: %s %d dBm Ch %u",
             scan_result.ap_count,
             scan_result.strongest_ssid[0] != '\0' ? scan_result.strongest_ssid : "<hidden>",
             scan_result.strongest_rssi,
             scan_result.strongest_channel);
    set_signal(signal_text, lv_palette_main(LV_PALETTE_RED));
    set_status("SSID not visible", lv_palette_main(LV_PALETTE_RED));
}

static void connect_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const char *ssid = lv_textarea_get_text(s_ssid_textarea);
    const char *password = lv_textarea_get_text(s_password_textarea);

    if (ssid == NULL || ssid[0] == '\0') {
        set_status("Enter SSID", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    set_status("", lv_color_white());
    set_signal("Connecting...", lv_color_white());
    s_connect_requested = true;
    s_waiting_for_connect_result = true;
    hide_keyboard();
    wifi_manager_connect(ssid, password);
}

static void back_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        wifi_screen_hide();
    }
}

void wifi_screen_create(void)
{
    if (s_wifi_screen != NULL) {
        return;
    }

    s_wifi_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_wifi_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_wifi_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_wifi_screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(s_wifi_screen);
    lv_label_set_text(title, "WIFI SETUP");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *band_note = lv_label_create(s_wifi_screen);
    lv_label_set_text(band_note, "ESP32-S3 WiFi scans 2.4 GHz only");
    lv_obj_set_style_text_color(band_note, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(band_note, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *ssid_label = lv_label_create(s_wifi_screen);
    lv_label_set_text(ssid_label, "SSID:");
    lv_obj_set_style_text_color(ssid_label, lv_color_white(), 0);
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 250, 90);

    s_ssid_textarea = lv_textarea_create(s_wifi_screen);
    lv_obj_set_size(s_ssid_textarea, WIFI_INPUT_WIDTH, WIFI_INPUT_HEIGHT);
    lv_obj_align(s_ssid_textarea, LV_ALIGN_TOP_MID, 0, 116);
    lv_textarea_set_placeholder_text(s_ssid_textarea, "Enter SSID");
    lv_textarea_set_one_line(s_ssid_textarea, true);
    lv_obj_set_style_bg_color(s_ssid_textarea, lv_color_make(45, 45, 45), 0);
    lv_obj_set_style_text_color(s_ssid_textarea, lv_color_white(), 0);
    lv_obj_add_event_cb(s_ssid_textarea, textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *password_label = lv_label_create(s_wifi_screen);
    lv_label_set_text(password_label, "PASSWORD:");
    lv_obj_set_style_text_color(password_label, lv_color_white(), 0);
    lv_obj_align(password_label, LV_ALIGN_TOP_LEFT, 250, 176);

    s_password_textarea = lv_textarea_create(s_wifi_screen);
    lv_obj_set_size(s_password_textarea, WIFI_INPUT_WIDTH, WIFI_INPUT_HEIGHT);
    lv_obj_align(s_password_textarea, LV_ALIGN_TOP_MID, 0, 202);
    lv_textarea_set_placeholder_text(s_password_textarea, "Enter password");
    lv_textarea_set_password_mode(s_password_textarea, true);
    lv_textarea_set_one_line(s_password_textarea, true);
    lv_obj_set_style_bg_color(s_password_textarea, lv_color_make(45, 45, 45), 0);
    lv_obj_set_style_text_color(s_password_textarea, lv_color_white(), 0);
    lv_obj_add_event_cb(s_password_textarea, textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *scan_button = create_action_button(s_wifi_screen, "SCAN THIS SSID");
    lv_obj_set_size(scan_button, WIFI_SCAN_BUTTON_WIDTH, WIFI_BUTTON_HEIGHT);
    lv_obj_set_style_bg_color(scan_button, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_color(scan_button, lv_palette_darken(LV_PALETTE_BLUE, 1), LV_STATE_PRESSED);
    lv_obj_align(scan_button, LV_ALIGN_TOP_MID, 0, 270);
    lv_obj_add_event_cb(scan_button, scan_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *connect_button = create_action_button(s_wifi_screen, "CONNECT");
    lv_obj_align(connect_button, LV_ALIGN_TOP_MID, -110, 338);
    lv_obj_add_event_cb(connect_button, connect_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_button = create_action_button(s_wifi_screen, "BACK");
    lv_obj_align(back_button, LV_ALIGN_TOP_MID, 110, 338);
    lv_obj_add_event_cb(back_button, back_button_event_cb, LV_EVENT_CLICKED, NULL);

    s_signal_panel = lv_obj_create(s_wifi_screen);
    lv_obj_set_size(s_signal_panel, WIFI_RESULT_PANEL_WIDTH, WIFI_RESULT_PANEL_HEIGHT);
    lv_obj_align(s_signal_panel, LV_ALIGN_TOP_MID, 0, 404);
    lv_obj_clear_flag(s_signal_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_signal_panel, lv_color_make(22, 22, 22), 0);
    lv_obj_set_style_bg_opa(s_signal_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_signal_panel, 8, 0);
    lv_obj_set_style_border_width(s_signal_panel, 2, 0);
    lv_obj_set_style_border_color(s_signal_panel, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_signal_panel, 6, 0);

    s_signal_label = lv_label_create(s_signal_panel);
    lv_obj_set_width(s_signal_label, WIFI_RESULT_PANEL_WIDTH - 20);
    lv_label_set_long_mode(s_signal_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_signal_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_signal_label);
    set_signal("Scan result will appear here", lv_color_white());

    s_keyboard = lv_keyboard_create(s_wifi_screen);
    lv_obj_set_size(s_keyboard, 800, 170);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_btnmatrix_set_btn_ctrl_all(s_keyboard, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);

    if (!s_event_handlers_registered) {
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_ui_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_ui_handler, NULL));
        s_event_handlers_registered = true;
    }
}

void wifi_screen_show(void)
{
    if (s_wifi_screen == NULL) {
        wifi_screen_create();
    }

    s_previous_screen = lv_scr_act();
    s_connect_requested = false;
    s_waiting_for_connect_result = false;
    hide_keyboard();
    load_saved_credentials_into_fields();

    if (wifi_manager_is_connected()) {
        set_status("", lv_palette_main(LV_PALETTE_GREEN));
        set_signal("Connected", lv_palette_main(LV_PALETTE_GREEN));
    } else {
        set_status("Disconnected", lv_color_white());
        set_signal("Saved credentials loaded", lv_color_white());
    }

    lv_scr_load(s_wifi_screen);
}

void wifi_screen_hide(void)
{
    hide_keyboard();

    if (s_previous_screen != NULL) {
        lv_scr_load(s_previous_screen);
    }
}