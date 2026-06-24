#include "gui.h"

#include <stdio.h>

#include "lvgl.h"
#include "lvgl_port.h"
#include "screens/setup_screen.h"
#include "screens/wifi_screen.h"

#define GUI_BUTTON_WIDTH  (250)
#define GUI_BUTTON_HEIGHT (100)
#define GUI_BUTTON_RADIUS (8)
#define GUI_BUTTON_COLS   (2)
#define GUI_BUTTON_ROWS   (4)

typedef enum {
    GUI_ACTION_ON1,
    GUI_ACTION_ON2,
    GUI_ACTION_ON3,
    GUI_ACTION_ON4,
    GUI_ACTION_ON5,
    GUI_ACTION_ON6,
    GUI_ACTION_WIFI,
    GUI_ACTION_SETUP,
} gui_action_t;

typedef struct {
    const char *label;
    gui_action_t action;
} gui_button_config_t;

static lv_obj_t *s_output_button_labels[GUI_OUTPUT_BUTTON_COUNT];

static void button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const gui_button_config_t *button_config = (const gui_button_config_t *)lv_event_get_user_data(e);
    if (button_config != NULL) {
        if (button_config->action == GUI_ACTION_WIFI) {
            printf("Button WIFI pressed\n");
            wifi_screen_show();
            return;
        }

        if (button_config->action == GUI_ACTION_SETUP) {
            printf("Button SETUP pressed\n");
            setup_screen_show();
            return;
        }

        printf("Button %s pressed\n", button_config->label);
    }
}

static lv_obj_t *create_button(lv_obj_t *parent, const gui_button_config_t *button_config, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, GUI_BUTTON_WIDTH, GUI_BUTTON_HEIGHT);
    lv_obj_set_pos(button, x, y);
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

    if (button_config->action >= GUI_ACTION_ON1 && button_config->action <= GUI_ACTION_ON6) {
        s_output_button_labels[button_config->action] = label;
    }

    return button;
}

void gui_set_output_label(unsigned int index, const char *text)
{
    if (index >= GUI_OUTPUT_BUTTON_COUNT || s_output_button_labels[index] == NULL || text == NULL) {
        return;
    }

    lv_label_set_text(s_output_button_labels[index], text);
    lv_obj_center(s_output_button_labels[index]);
}

void gui_create(void)
{
    static const gui_button_config_t button_configs[GUI_BUTTON_ROWS][GUI_BUTTON_COLS] = {
        {{"ON1", GUI_ACTION_ON1}, {"ON2", GUI_ACTION_ON2}},
        {{"ON3", GUI_ACTION_ON3}, {"ON4", GUI_ACTION_ON4}},
        {{"ON5", GUI_ACTION_ON5}, {"ON6", GUI_ACTION_ON6}},
        {{"WIFI", GUI_ACTION_WIFI}, {"SETUP", GUI_ACTION_SETUP}},
    };

    const lv_coord_t horizontal_gap =
        (LVGL_PORT_H_RES - (GUI_BUTTON_COLS * GUI_BUTTON_WIDTH)) / (GUI_BUTTON_COLS + 1);
    const lv_coord_t vertical_gap =
        (LVGL_PORT_V_RES - (GUI_BUTTON_ROWS * GUI_BUTTON_HEIGHT)) / (GUI_BUTTON_ROWS + 1);

    lv_obj_t *screen = lv_scr_act();
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    for (int row = 0; row < GUI_BUTTON_ROWS; row++) {
        for (int col = 0; col < GUI_BUTTON_COLS; col++) {
            const lv_coord_t x = horizontal_gap + (col * (GUI_BUTTON_WIDTH + horizontal_gap));
            const lv_coord_t y = vertical_gap + (row * (GUI_BUTTON_HEIGHT + vertical_gap));

            create_button(screen, &button_configs[row][col], x, y);
        }
    }

    wifi_screen_create();
    setup_screen_create();
}