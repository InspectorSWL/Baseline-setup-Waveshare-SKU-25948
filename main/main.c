/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "gui.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "time_service.h"
#include "vehicle_store.h"
#include "wifi_manager.h"
#include "waveshare_rgb_lcd_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_main";
static httpd_handle_t s_http_server;

typedef struct {
    int id;
    gpio_num_t gpio;
    const char *label;
} web_gpio_t;

static const web_gpio_t s_web_gpios[] = {
    { 1, GPIO_NUM_11, "GPIO11" },
    { 2, GPIO_NUM_12, "GPIO12" },
    { 3, GPIO_NUM_13, "GPIO13" },
    { 4, GPIO_NUM_16, "GPIO16" },
};

static const char s_control_page[] =
    "<!DOCTYPE html>"
    "<html><head><meta charset='utf-8'><title>ESP32-S3 Dashboard</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:0;background:#0f1115;color:#f2f4f8;}"
    ".page{max-width:1080px;margin:0 auto;padding:24px;}"
    "h1{margin:0 0 8px;font-size:32px;}"
    ".sub{color:#9ba6b2;margin-bottom:22px;}"
    ".cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-bottom:18px;}"
    ".card,.panel{background:#171b22;border:1px solid #283141;border-radius:14px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.18);}"
    ".eyebrow{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:#8d98a7;margin-bottom:8px;}"
    ".value{font-size:24px;font-weight:700;}"
    ".layout{display:grid;grid-template-columns:1.1fr .9fr;gap:16px;}"
    "table{width:100%;border-collapse:collapse;font-size:14px;}"
    "th,td{padding:10px 8px;border-bottom:1px solid #283141;text-align:left;vertical-align:top;}"
    "th{color:#95a3b8;font-weight:600;}"
    "button{margin:4px 6px 0 0;padding:10px 14px;border:0;border-radius:10px;cursor:pointer;font-weight:600;}"
    ".on{background:#1d7f46;color:#fff;}.off{background:#ad2638;color:#fff;}"
    ".mono{font-family:Consolas,monospace;}"
    ".gpio-grid{display:grid;grid-template-columns:repeat(2,minmax(150px,1fr));gap:12px;}"
    "@media (max-width:820px){.layout{grid-template-columns:1fr;}.page{padding:16px;}}"
    "</style>"
    "<script>"
    "async function setGpio(pin,state){await fetch(`/gpio?pin=${pin}&state=${state}`);await refreshAll();}"
    "function setText(id,value){document.getElementById(id).textContent=value;}"
    "function formatUptime(ms){const s=Math.floor(ms/1000);const h=Math.floor(s/3600);const m=Math.floor((s%3600)/60);const sec=s%60;return `${h}h ${m}m ${sec}s`;}"
    "async function refreshStatus(){const r=await fetch('/status');const j=await r.json();setText('wifi',j.wifi);setText('ip',j.ip);setText('time',j.time);setText('uptime',formatUptime(j.uptime_ms));setText('gpio1',j.gpio1);setText('gpio2',j.gpio2);setText('gpio3',j.gpio3);setText('gpio4',j.gpio4);}"
    "async function refreshVehicles(){const r=await fetch('/vehicles');const list=await r.json();const body=document.getElementById('vehicles');body.innerHTML='';let used=0;for(const item of list){if(item.name||item.rfid){used++;}const tr=document.createElement('tr');tr.innerHTML=`<td>${item.slot}</td><td></td><td class='mono'></td>`;tr.children[1].textContent=item.name||'EMPTY';tr.children[2].textContent=item.rfid||'-';body.appendChild(tr);}setText('vehiclesCount',`${used} / ${list.length}`);}"
    "async function refreshAll(){await Promise.all([refreshStatus(),refreshVehicles()]);}"
    "setInterval(refreshAll,5000);window.addEventListener('load',refreshAll);"
    "</script>"
    "</head><body><div class='page'><h1>ESP32-S3 Dashboard</h1><div class='sub'>Native ESP-IDF HTTP server status, outputs, and saved vehicle registry.</div>"
    "<div class='cards'>"
    "<div class='card'><div class='eyebrow'>WiFi</div><div class='value' id='wifi'>Loading...</div></div>"
    "<div class='card'><div class='eyebrow'>IP Address</div><div class='value mono' id='ip'>Loading...</div></div>"
    "<div class='card'><div class='eyebrow'>Device Time</div><div class='value mono' id='time'>Loading...</div></div>"
    "<div class='card'><div class='eyebrow'>Uptime</div><div class='value' id='uptime'>Loading...</div></div>"
    "</div>"
    "<div class='layout'>"
    "<div class='panel'><div class='eyebrow'>Saved Vehicles</div><div class='sub'>Configured entries: <span id='vehiclesCount'>Loading...</span></div><table><thead><tr><th>Slot</th><th>Vehicle Name</th><th>RFID UID</th></tr></thead><tbody id='vehicles'></tbody></table></div>"
    "<div class='panel'><div class='eyebrow'>GPIO Outputs</div><div class='gpio-grid'>"
    "<div class='card'><div class='eyebrow'>GPIO11</div><div class='value' id='gpio1'>-</div><button class='on' onclick='setGpio(1,\"on\")'>ON</button><button class='off' onclick='setGpio(1,\"off\")'>OFF</button></div>"
    "<div class='card'><div class='eyebrow'>GPIO12</div><div class='value' id='gpio2'>-</div><button class='on' onclick='setGpio(2,\"on\")'>ON</button><button class='off' onclick='setGpio(2,\"off\")'>OFF</button></div>"
    "<div class='card'><div class='eyebrow'>GPIO13</div><div class='value' id='gpio3'>-</div><button class='on' onclick='setGpio(3,\"on\")'>ON</button><button class='off' onclick='setGpio(3,\"off\")'>OFF</button></div>"
    "<div class='card'><div class='eyebrow'>GPIO16</div><div class='value' id='gpio4'>-</div><button class='on' onclick='setGpio(4,\"on\")'>ON</button><button class='off' onclick='setGpio(4,\"off\")'>OFF</button></div>"
    "</div></div></div></div></body></html>";

static esp_err_t append_json_escaped(httpd_req_t *request, const char *text)
{
    char chunk[8];

    if (text == NULL) {
        return ESP_OK;
    }

    while (*text != '\0') {
        switch (*text) {
        case '\\':
            if (httpd_resp_send_chunk(request, "\\\\", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
                return ESP_FAIL;
            }
            break;
        case '"':
            if (httpd_resp_send_chunk(request, "\\\"", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
                return ESP_FAIL;
            }
            break;
        case '\n':
            if (httpd_resp_send_chunk(request, "\\n", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
                return ESP_FAIL;
            }
            break;
        case '\r':
            if (httpd_resp_send_chunk(request, "\\r", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
                return ESP_FAIL;
            }
            break;
        case '\t':
            if (httpd_resp_send_chunk(request, "\\t", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
                return ESP_FAIL;
            }
            break;
        default:
            chunk[0] = *text;
            chunk[1] = '\0';
            if (httpd_resp_send_chunk(request, chunk, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
                return ESP_FAIL;
            }
            break;
        }

        text++;
    }

    return ESP_OK;
}

static const web_gpio_t *find_web_gpio(int id)
{
    for (size_t index = 0; index < sizeof(s_web_gpios) / sizeof(s_web_gpios[0]); index++) {
        if (s_web_gpios[index].id == id) {
            return &s_web_gpios[index];
        }
    }

    return NULL;
}

static void init_web_gpios(void)
{
    for (size_t index = 0; index < sizeof(s_web_gpios) / sizeof(s_web_gpios[0]); index++) {
        gpio_config_t gpio_config_info = {
            .pin_bit_mask = 1ULL << s_web_gpios[index].gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_ERROR_CHECK(gpio_config(&gpio_config_info));
        ESP_ERROR_CHECK(gpio_set_level(s_web_gpios[index].gpio, 0));
    }
}

static esp_err_t send_status_json(httpd_req_t *request)
{
    char ip_address[16] = { 0 };
    char response[320];
    int64_t uptime_ms = esp_timer_get_time() / 1000;
    const char *time_text = time_service_get_text();

    if (!wifi_manager_get_ip_address(ip_address, sizeof(ip_address))) {
        snprintf(ip_address, sizeof(ip_address), "0.0.0.0");
    }

    snprintf(response,
             sizeof(response),
             "{\"wifi\":\"%s\",\"ip\":\"%s\",\"time\":\"%s\",\"time_valid\":%s,\"uptime_ms\":%lld,\"gpio1\":\"%s\",\"gpio2\":\"%s\",\"gpio3\":\"%s\",\"gpio4\":\"%s\"}",
             wifi_manager_is_connected() ? "connected" : "disconnected",
             ip_address,
             time_text,
             time_service_is_valid() ? "true" : "false",
             uptime_ms,
             gpio_get_level(s_web_gpios[0].gpio) ? "ON" : "OFF",
             gpio_get_level(s_web_gpios[1].gpio) ? "ON" : "OFF",
             gpio_get_level(s_web_gpios[2].gpio) ? "ON" : "OFF",
             gpio_get_level(s_web_gpios[3].gpio) ? "ON" : "OFF");

    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_get_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html");
    return httpd_resp_send(request, s_control_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get_handler(httpd_req_t *request)
{
    return send_status_json(request);
}

static esp_err_t vehicles_get_handler(httpd_req_t *request)
{
    esp_err_t ret;

    httpd_resp_set_type(request, "application/json");
    ret = httpd_resp_send_chunk(request, "[", HTTPD_RESP_USE_STRLEN);
    if (ret != ESP_OK) {
        return ret;
    }

    for (size_t index = 0; index < vehicle_store_get_count(); index++) {
        const Vehicle *vehicle = vehicle_store_get(index);
        char prefix[40];

        snprintf(prefix, sizeof(prefix), "%s{\"slot\":%u,\"name\":\"",
                 (index == 0) ? "" : ",",
                 (unsigned int)(index + 1u));
        if (httpd_resp_send_chunk(request, prefix, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            return ESP_FAIL;
        }
        if (append_json_escaped(request, (vehicle != NULL) ? vehicle->vehicleName : "") != ESP_OK) {
            return ESP_FAIL;
        }
        if (httpd_resp_send_chunk(request, "\",\"rfid\":\"", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            return ESP_FAIL;
        }
        if (append_json_escaped(request, (vehicle != NULL) ? vehicle->rfidUID : "") != ESP_OK) {
            return ESP_FAIL;
        }
        if (httpd_resp_send_chunk(request, "\"}", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    if (httpd_resp_send_chunk(request, "]", HTTPD_RESP_USE_STRLEN) != ESP_OK) {
        return ESP_FAIL;
    }

    return httpd_resp_send_chunk(request, NULL, 0);
}

static esp_err_t gpio_get_handler(httpd_req_t *request)
{
    char query[64];
    char pin_param[8] = { 0 };
    char state_param[8] = { 0 };
    const web_gpio_t *web_gpio;
    int pin_id;
    int level;

    if (httpd_req_get_url_query_len(request) <= 0 || httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "missing query string");
    }

    if (httpd_query_key_value(query, "pin", pin_param, sizeof(pin_param)) != ESP_OK
        || httpd_query_key_value(query, "state", state_param, sizeof(state_param)) != ESP_OK) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "pin and state are required");
    }

    pin_id = atoi(pin_param);
    web_gpio = find_web_gpio(pin_id);
    if (web_gpio == NULL) {
        httpd_resp_set_status(request, "404 Not Found");
        return httpd_resp_sendstr(request, "unknown pin");
    }

    if (strcmp(state_param, "on") == 0) {
        level = 1;
    } else if (strcmp(state_param, "off") == 0) {
        level = 0;
    } else {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "state must be on or off");
    }

    ESP_ERROR_CHECK(gpio_set_level(web_gpio->gpio, level));
    ESP_LOGI(TAG, "%s set to %s", web_gpio->label, level ? "ON" : "OFF");

    return send_status_json(request);
}

static void start_http_server(void)
{
    if (s_http_server != NULL) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t vehicles_uri = {
        .uri = "/vehicles",
        .method = HTTP_GET,
        .handler = vehicles_get_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t gpio_uri = {
        .uri = "/gpio",
        .method = HTTP_GET,
        .handler = gpio_get_handler,
        .user_ctx = NULL,
    };

    if (httpd_start(&s_http_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        s_http_server = NULL;
        return;
    }

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http_server, &root_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http_server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http_server, &vehicles_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_http_server, &gpio_uri));
    ESP_LOGI(TAG, "HTTP server started");
}

static void stop_http_server(void)
{
    if (s_http_server == NULL) {
        return;
    }

    httpd_stop(s_http_server);
    s_http_server = NULL;
    ESP_LOGI(TAG, "HTTP server stopped");
}

static void network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    LV_UNUSED(arg);
    LV_UNUSED(event_data);

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        time_service_sync_on_wifi_connected();
        start_http_server();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        stop_http_server();
    }
}

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

    init_web_gpios();
    wifi_manager_init();
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, network_event_handler, NULL));

    if (wifi_manager_is_connected()) {
        start_http_server();
    }

    ESP_LOGI(TAG, "Create LVGL button screen");
    if (lvgl_port_lock(-1)) {
        ESP_ERROR_CHECK(time_service_init());
        gui_create();
        lvgl_port_unlock();
    }

    if (wifi_manager_is_connected()) {
        time_service_sync_on_wifi_connected();
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
