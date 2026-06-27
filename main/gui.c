#include "gui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "lvgl_port.h"
#include "screens/setup_screen.h"
#include "screens/wifi_screen.h"
#include "time_service.h"

LV_FONT_DECLARE(lv_font_montserrat_52_liters);

#define GUI_NAV_BUTTON_HEIGHT   (44)
#define GUI_BUTTON_RADIUS       (8)
#define GUI_BUTTON_SIDE_MARGIN  (18)
#define GUI_BUTTON_BOTTOM_MARGIN (18)
#define GUI_BUTTON_TEXT_PADDING (18)
#define GUI_MAX_USER_TEXT_LEN   (96)
#define GUI_MAX_VOLUME_TEXT_LEN (16)

typedef enum {
    GUI_ACTION_SETUP,
} gui_action_t;

typedef struct {
    const char *label;
    gui_action_t action;
} gui_button_config_t;

static lv_obj_t *s_current_user_label;
static lv_obj_t *s_volume_label;
static char s_current_user_text[GUI_MAX_USER_TEXT_LEN] = "Current User: Waiting For RFID";
static char s_volume_text[GUI_MAX_VOLUME_TEXT_LEN] = "0.0 L";

static float clamp_volume_liters(float liters)
{
    if (liters < 0.0f) {
        return 0.0f;
    }

    if (liters > 1000.0f) {
        return 1000.0f;
    }

    return liters;
}

static void refresh_current_user_label(void)
{
    if (s_current_user_label == NULL) {
        return;
    }

    lv_label_set_text(s_current_user_label, s_current_user_text);
}

static void refresh_volume_label(void)
{
    if (s_volume_label == NULL) {
        return;
    }

    lv_label_set_text(s_volume_label, s_volume_text);
}

static void button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const gui_button_config_t *button_config = (const gui_button_config_t *)lv_event_get_user_data(e);
    if (button_config != NULL) {
        if (button_config->action == GUI_ACTION_SETUP) {
            printf("Button SETUP pressed\n");
            setup_screen_show();
            return;
        }

        printf("Button %s pressed\n", button_config->label);
    }
}

static lv_obj_t *create_button(lv_obj_t *parent, const gui_button_config_t *button_config)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_height(button, GUI_NAV_BUTTON_HEIGHT);
    lv_obj_set_width(button, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(button, GUI_BUTTON_TEXT_PADDING, 0);
    lv_obj_set_style_pad_right(button, GUI_BUTTON_TEXT_PADDING, 0);
    lv_obj_set_style_radius(button, GUI_BUTTON_RADIUS, 0);
    lv_obj_set_style_bg_color(button, lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_bg_color(button, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_border_color(button, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_white(), 0);
    lv_obj_set_style_text_color(button, lv_color_black(), LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, (void *)button_config);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, button_config->label);
    lv_obj_center(label);

    return button;
}

static void create_home_layout(lv_obj_t *screen)
{
    static const gui_button_config_t setup_button_config = {"Setup", GUI_ACTION_SETUP};

    lv_obj_t *top_label = lv_label_create(screen);
    s_current_user_label = top_label;
    lv_obj_set_width(top_label, LV_PCT(100));
    lv_obj_set_style_text_align(top_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(top_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(top_label, &lv_font_montserrat_24, 0);
    lv_obj_align(top_label, LV_ALIGN_TOP_MID, 0, 24);
    refresh_current_user_label();

    (void)time_service_create_status_label(screen);

    lv_obj_t *volume_label = lv_label_create(screen);
    s_volume_label = volume_label;
    lv_obj_set_style_text_color(volume_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(volume_label, &lv_font_montserrat_52_liters, 0);
    lv_obj_align(volume_label, LV_ALIGN_CENTER, 0, -20);
    refresh_volume_label();

    lv_obj_t *setup_button = create_button(screen, &setup_button_config);
    lv_obj_align(setup_button, LV_ALIGN_BOTTOM_RIGHT, -GUI_BUTTON_SIDE_MARGIN, -GUI_BUTTON_BOTTOM_MARGIN);
}

void gui_set_current_user(const char *vehicle_name)
{
    const char *display_name = vehicle_name;

    if (display_name == NULL || display_name[0] == '\0') {
        display_name = "Waiting For RFID";
    }

    snprintf(s_current_user_text, sizeof(s_current_user_text), "Current User: %s", display_name);
    refresh_current_user_label();
}

void gui_set_volume_liters(float liters)
{
    snprintf(s_volume_text, sizeof(s_volume_text), "%.1f L", clamp_volume_liters(liters));
    refresh_volume_label();
}

void gui_create(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    memset(s_current_user_text, 0, sizeof(s_current_user_text));
    memset(s_volume_text, 0, sizeof(s_volume_text));
    gui_set_current_user(NULL);
    gui_set_volume_liters(0.0f);

    create_home_layout(screen);

    wifi_screen_create();
}