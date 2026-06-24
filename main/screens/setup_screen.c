#include "setup_screen.h"

#include <stddef.h>

#include "gui.h"
#include "lvgl.h"

#define SETUP_BUTTON_WIDTH  (180)
#define SETUP_BUTTON_HEIGHT (56)
#define SETUP_INPUT_WIDTH   (220)
#define SETUP_INPUT_HEIGHT  (42)
#define SETUP_FIELD_COLS    (2)
#define SETUP_FIELD_ROWS    (3)
#define SETUP_FIELD_X_GAP   (396)
#define SETUP_FIELD_Y_GAP   (64)
#define SETUP_FIELD_LABEL_X (24)
#define SETUP_FIELD_INPUT_X (56)
#define SETUP_FIELD_START_Y (84)

static lv_obj_t *s_setup_screen;
static lv_obj_t *s_previous_screen;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_keyboard_close_button;
static lv_obj_t *s_name_fields[GUI_OUTPUT_BUTTON_COUNT];

static void hide_keyboard(void)
{
    if (s_keyboard == NULL) {
        return;
    }

    lv_keyboard_set_textarea(s_keyboard, NULL);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);

    if (s_keyboard_close_button != NULL) {
        lv_obj_add_flag(s_keyboard_close_button, LV_OBJ_FLAG_HIDDEN);
    }
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        hide_keyboard();
    }
}

static void name_field_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *textarea = lv_event_get_target(e);

    for (size_t index = 0; index < GUI_OUTPUT_BUTTON_COUNT; index++) {
        if (textarea == s_name_fields[index] &&
            (code == LV_EVENT_VALUE_CHANGED || code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED)) {
            gui_set_output_label((unsigned int)index, lv_textarea_get_text(textarea));
            break;
        }
    }

    if ((code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) && s_keyboard != NULL) {
        lv_keyboard_set_textarea(s_keyboard, textarea);
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);

        if (s_keyboard_close_button != NULL) {
            lv_obj_clear_flag(s_keyboard_close_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void keyboard_close_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        hide_keyboard();
    }
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, SETUP_BUTTON_WIDTH, SETUP_BUTTON_HEIGHT);
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

static void back_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        setup_screen_hide();
    }
}

static void create_number_list(lv_obj_t *parent)
{
    static const char *field_labels[] = {
        "ON1",
        "ON2",
        "ON3",
        "ON4",
        "ON5",
        "ON6",
    };

    for (size_t index = 0; index < GUI_OUTPUT_BUTTON_COUNT; index++) {
        const lv_coord_t col = (lv_coord_t)(index / SETUP_FIELD_ROWS);
        const lv_coord_t row = (lv_coord_t)(index % SETUP_FIELD_ROWS);
        const lv_coord_t label_x = SETUP_FIELD_LABEL_X + (col * SETUP_FIELD_X_GAP);
        const lv_coord_t input_x = SETUP_FIELD_INPUT_X + (col * SETUP_FIELD_X_GAP);
        const lv_coord_t y = SETUP_FIELD_START_Y + (row * SETUP_FIELD_Y_GAP);

        lv_obj_t *number_label = lv_label_create(parent);
        lv_label_set_text_fmt(number_label, "%u", (unsigned int)(index + 1));
        lv_obj_set_style_text_color(number_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(number_label, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(number_label, label_x, y);

        s_name_fields[index] = lv_textarea_create(parent);
        lv_obj_set_size(s_name_fields[index], SETUP_INPUT_WIDTH, SETUP_INPUT_HEIGHT);
        lv_obj_set_pos(s_name_fields[index], input_x, y - 10);
        lv_textarea_set_one_line(s_name_fields[index], true);
        lv_textarea_set_max_length(s_name_fields[index], 10);
        lv_textarea_set_text(s_name_fields[index], field_labels[index]);
        lv_obj_set_style_bg_color(s_name_fields[index], lv_color_make(45, 45, 45), 0);
        lv_obj_set_style_bg_opa(s_name_fields[index], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_name_fields[index], 1, 0);
        lv_obj_set_style_border_color(s_name_fields[index], lv_color_make(150, 150, 150), 0);
        lv_obj_set_style_text_color(s_name_fields[index], lv_color_white(), 0);
        lv_obj_add_event_cb(s_name_fields[index], name_field_event_cb, LV_EVENT_ALL, NULL);
    }
}

void setup_screen_create(void)
{
    if (s_setup_screen != NULL) {
        return;
    }

    s_setup_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_setup_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_setup_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_setup_screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(s_setup_screen);
    lv_label_set_text(title, "SETUP");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    create_number_list(s_setup_screen);

    lv_obj_t *back_button = create_action_button(s_setup_screen, "BACK");
    lv_obj_align(back_button, LV_ALIGN_TOP_MID, 0, 312);
    lv_obj_add_event_cb(back_button, back_button_event_cb, LV_EVENT_CLICKED, NULL);

    s_keyboard_close_button = create_action_button(s_setup_screen, "CLOSE");
    lv_obj_set_size(s_keyboard_close_button, 140, 44);
    lv_obj_align(s_keyboard_close_button, LV_ALIGN_BOTTOM_RIGHT, -18, -176);
    lv_obj_add_flag(s_keyboard_close_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard_close_button, keyboard_close_button_event_cb, LV_EVENT_CLICKED, NULL);

    s_keyboard = lv_keyboard_create(s_setup_screen);
    lv_obj_set_size(s_keyboard, 800, 170);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_btnmatrix_set_btn_ctrl_all(s_keyboard, LV_BTNMATRIX_CTRL_NO_REPEAT);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);
}

void setup_screen_show(void)
{
    if (s_setup_screen == NULL) {
        setup_screen_create();
    }

    s_previous_screen = lv_scr_act();
    lv_scr_load(s_setup_screen);
}

void setup_screen_hide(void)
{
    hide_keyboard();

    if (s_previous_screen != NULL) {
        lv_scr_load(s_previous_screen);
    }
}