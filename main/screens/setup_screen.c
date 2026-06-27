#include "setup_screen.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "lvgl.h"
#include "time_service.h"
#include "vehicle_store.h"
#include "wifi_screen.h"

#define SETUP_BUTTON_HEIGHT        (44)
#define SETUP_BUTTON_SIDE_MARGIN   (18)
#define SETUP_BUTTON_BOTTOM_MARGIN (18)
#define SETUP_BUTTON_TEXT_PADDING  (18)
#define SETUP_BUTTON_GAP           (12)
#define SETUP_INPUT_WIDTH          (252)
#define SETUP_INPUT_HEIGHT         (42)
#define SETUP_SLOT_BUTTON_WIDTH    (138)
#define SETUP_SLOT_BUTTON_HEIGHT   (48)
#define SETUP_SLOT_COLS            (3)
#define SETUP_SLOT_ROWS            (5)
#define SETUP_SLOT_X_GAP           (12)
#define SETUP_SLOT_Y_GAP           (12)
#define SETUP_SLOT_START_X         (18)
#define SETUP_SLOT_START_Y         (80)
#define SETUP_EDITOR_X             (492)
#define SETUP_EDITOR_LABEL_X       (SETUP_EDITOR_X)
#define SETUP_EDITOR_INPUT_X       (SETUP_EDITOR_X)
#define SETUP_EDITOR_NAME_Y        (116)
#define SETUP_EDITOR_RFID_Y        (206)
#define SETUP_EDITOR_TITLE_Y       (80)

static lv_obj_t *s_setup_screen;
static lv_obj_t *s_previous_screen;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_keyboard_close_button;
static lv_obj_t *s_slot_buttons[VEHICLE_COUNT];
static lv_obj_t *s_slot_button_labels[VEHICLE_COUNT];
static lv_obj_t *s_selected_entry_label;
static lv_obj_t *s_name_field;
static lv_obj_t *s_rfid_field;
static size_t s_selected_index;
static bool s_syncing_fields;

static void copy_trimmed(char *destination, size_t destination_size, const char *source)
{
    if (destination == NULL || destination_size == 0) {
        return;
    }

    destination[0] = '\0';
    if (source == NULL) {
        return;
    }

    while (*source == ' ' || *source == '\t' || *source == '\r' || *source == '\n') {
        source++;
    }

    size_t length = strlen(source);
    while (length > 0) {
        char c = source[length - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }

        length--;
    }

    if (length >= destination_size) {
        length = destination_size - 1;
    }

    memcpy(destination, source, length);
    destination[length] = '\0';
}

static void refresh_slot_button(size_t index)
{
    const Vehicle *vehicle;
    char button_text[48];

    if (index >= vehicle_store_get_count() || s_slot_button_labels[index] == NULL) {
        return;
    }

    vehicle = vehicle_store_get(index);
    if (vehicle == NULL || vehicle->vehicleName[0] == '\0') {
        snprintf(button_text, sizeof(button_text), "%02u: EMPTY", (unsigned int)(index + 1u));
    } else {
        snprintf(button_text, sizeof(button_text), "%02u: %s", (unsigned int)(index + 1u), vehicle->vehicleName);
    }

    lv_label_set_text(s_slot_button_labels[index], button_text);
}

static void refresh_all_slot_buttons(void)
{
    for (size_t index = 0; index < vehicle_store_get_count(); index++) {
        refresh_slot_button(index);
    }
}

static void update_slot_selection_styles(void)
{
    for (size_t index = 0; index < vehicle_store_get_count(); index++) {
        lv_color_t bg_color = (index == s_selected_index) ? lv_color_make(110, 110, 110) : lv_color_make(60, 60, 60);
        lv_color_t border_color = (index == s_selected_index) ? lv_color_white() : lv_color_make(150, 150, 150);

        if (s_slot_buttons[index] == NULL) {
            continue;
        }

        lv_obj_set_style_bg_color(s_slot_buttons[index], bg_color, 0);
        lv_obj_set_style_border_color(s_slot_buttons[index], border_color, 0);
    }
}

static void load_selected_vehicle_into_fields(void)
{
    const Vehicle *vehicle = vehicle_store_get(s_selected_index);

    if (s_selected_entry_label == NULL || s_name_field == NULL || s_rfid_field == NULL) {
        return;
    }

    s_syncing_fields = true;
    lv_label_set_text_fmt(s_selected_entry_label, "ENTRY %u", (unsigned int)(s_selected_index + 1u));
    lv_textarea_set_text(s_name_field, (vehicle != NULL) ? vehicle->vehicleName : "");
    lv_textarea_set_text(s_rfid_field, (vehicle != NULL) ? vehicle->rfidUID : "");
    s_syncing_fields = false;
}

static void persist_selected_vehicle(void)
{
    Vehicle vehicle;

    if (s_name_field == NULL || s_rfid_field == NULL || s_syncing_fields) {
        return;
    }

    memset(&vehicle, 0, sizeof(vehicle));
    copy_trimmed(vehicle.vehicleName, sizeof(vehicle.vehicleName), lv_textarea_get_text(s_name_field));
    copy_trimmed(vehicle.rfidUID, sizeof(vehicle.rfidUID), lv_textarea_get_text(s_rfid_field));

    if (vehicle_store_set(s_selected_index, &vehicle) == ESP_OK) {
        refresh_slot_button(s_selected_index);
    }
}

static void select_vehicle(size_t index)
{
    if (index >= vehicle_store_get_count()) {
        return;
    }

    persist_selected_vehicle();
    s_selected_index = index;
    update_slot_selection_styles();
    load_selected_vehicle_into_fields();
}

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
        persist_selected_vehicle();
        hide_keyboard();
    }
}

static void slot_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    uintptr_t slot_index = (uintptr_t)lv_event_get_user_data(e);
    select_vehicle((size_t)slot_index);
}

static void field_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *textarea = lv_event_get_target(e);

    if ((code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED) && !s_syncing_fields) {
        persist_selected_vehicle();
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
        persist_selected_vehicle();
        hide_keyboard();
    }
}

static void save_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        persist_selected_vehicle();
        hide_keyboard();
    }
}

static lv_obj_t *create_action_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_height(button, SETUP_BUTTON_HEIGHT);
    lv_obj_set_width(button, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(button, SETUP_BUTTON_TEXT_PADDING, 0);
    lv_obj_set_style_pad_right(button, SETUP_BUTTON_TEXT_PADDING, 0);
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

static lv_obj_t *create_slot_button(lv_obj_t *parent, size_t index)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, SETUP_SLOT_BUTTON_WIDTH, SETUP_SLOT_BUTTON_HEIGHT);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_make(60, 60, 60), 0);
    lv_obj_set_style_bg_color(button, lv_color_make(120, 120, 120), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_pad_all(button, 8, 0);
    lv_obj_add_event_cb(button, slot_button_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_width(label, SETUP_SLOT_BUTTON_WIDTH - 16);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, "");
    lv_obj_center(label);

    s_slot_buttons[index] = button;
    s_slot_button_labels[index] = label;
    refresh_slot_button(index);

    return button;
}

static void create_editor_field(lv_obj_t *parent, const char *label_text, lv_coord_t y, lv_obj_t **textarea, uint32_t max_length)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label, SETUP_EDITOR_LABEL_X, y);

    *textarea = lv_textarea_create(parent);
    lv_obj_set_size(*textarea, SETUP_INPUT_WIDTH, SETUP_INPUT_HEIGHT);
    lv_obj_set_pos(*textarea, SETUP_EDITOR_INPUT_X, y + 26);
    lv_textarea_set_one_line(*textarea, true);
    lv_textarea_set_max_length(*textarea, max_length);
    lv_obj_set_style_bg_color(*textarea, lv_color_make(45, 45, 45), 0);
    lv_obj_set_style_bg_opa(*textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(*textarea, 1, 0);
    lv_obj_set_style_border_color(*textarea, lv_color_make(150, 150, 150), 0);
    lv_obj_set_style_text_color(*textarea, lv_color_white(), 0);
    lv_obj_add_event_cb(*textarea, field_event_cb, LV_EVENT_ALL, NULL);
}

static void back_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        persist_selected_vehicle();
        setup_screen_hide();
    }
}

static void wifi_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        wifi_screen_show();
    }
}

static void create_vehicle_editor(lv_obj_t *parent)
{
    for (size_t index = 0; index < vehicle_store_get_count(); index++) {
        const lv_coord_t col = (lv_coord_t)(index % SETUP_SLOT_COLS);
        const lv_coord_t row = (lv_coord_t)(index / SETUP_SLOT_COLS);
        const lv_coord_t x = SETUP_SLOT_START_X + (col * (SETUP_SLOT_BUTTON_WIDTH + SETUP_SLOT_X_GAP));
        const lv_coord_t y = SETUP_SLOT_START_Y + (row * (SETUP_SLOT_BUTTON_HEIGHT + SETUP_SLOT_Y_GAP));
        lv_obj_t *button = create_slot_button(parent, index);
        lv_obj_set_pos(button, x, y);
    }

    s_selected_entry_label = lv_label_create(parent);
    lv_label_set_text(s_selected_entry_label, "ENTRY 1");
    lv_obj_set_style_text_color(s_selected_entry_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_selected_entry_label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(s_selected_entry_label, SETUP_EDITOR_X, SETUP_EDITOR_TITLE_Y);

    create_editor_field(parent, "Vehicle Name", SETUP_EDITOR_NAME_Y, &s_name_field, VEHICLE_NAME_MAX_LEN);
    create_editor_field(parent, "RFID UID", SETUP_EDITOR_RFID_Y, &s_rfid_field, VEHICLE_RFID_UID_MAX_LEN);

    lv_obj_t *save_button = create_action_button(parent, "SAVE");
    lv_obj_set_size(save_button, 112, SETUP_BUTTON_HEIGHT);
    lv_obj_align(save_button, LV_ALIGN_BOTTOM_LEFT, SETUP_EDITOR_X, -SETUP_BUTTON_BOTTOM_MARGIN);
    lv_obj_add_event_cb(save_button, save_button_event_cb, LV_EVENT_CLICKED, NULL);

    refresh_all_slot_buttons();
    select_vehicle(0);
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

    (void)vehicle_store_init();

    lv_obj_t *title = lv_label_create(s_setup_screen);
    lv_label_set_text(title, "SETUP");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    (void)time_service_create_status_label(s_setup_screen);

    create_vehicle_editor(s_setup_screen);

    lv_obj_t *back_button = create_action_button(s_setup_screen, "BACK");
    lv_obj_align(back_button, LV_ALIGN_BOTTOM_RIGHT, -SETUP_BUTTON_SIDE_MARGIN, -SETUP_BUTTON_BOTTOM_MARGIN);
    lv_obj_add_event_cb(back_button, back_button_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wifi_button = create_action_button(s_setup_screen, "WIFI");
    lv_obj_align_to(wifi_button, back_button, LV_ALIGN_OUT_LEFT_MID, -SETUP_BUTTON_GAP, 0);
    lv_obj_add_event_cb(wifi_button, wifi_button_event_cb, LV_EVENT_CLICKED, NULL);

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