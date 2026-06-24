idf_component_register(
    SRCS "waveshare_rgb_lcd_port.c" "lvgl_port.c" "gui.c" "main.c" "screens/wifi_screen.c" "screens/setup_screen.c"
    INCLUDE_DIRS "." "screens"
    REQUIRES driver esp_lcd esp_lcd_touch_gt911 wifi_manager)
