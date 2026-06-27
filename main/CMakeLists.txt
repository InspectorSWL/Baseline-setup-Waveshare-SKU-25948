idf_component_register(
    SRCS "waveshare_rgb_lcd_port.c" "lvgl_port.c" "gui.c" "main.c" "vehicle_store.c" "time_service.c" "screens/wifi_screen.c" "screens/setup_screen.c" "fonts/lv_font_montserrat_52_liters.c"
    INCLUDE_DIRS "." "screens"
    REQUIRES driver esp_event esp_http_server esp_lcd esp_lcd_touch_gt911 esp_netif esp_timer esp_wifi nvs_flash wifi_manager lwip)
