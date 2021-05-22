/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Leo Leung <leo@steamr.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "hw.h"
#include "func_settings.h"
#include "uui.h"
#include "uui_number.h"
#include "dbg_printf.h"
#include "mini-printf.h"
#include "dps-model.h"
#include "ili9163c.h"
#include "font-full_small.h"
#include "opendps.h"
#include "gfx-gear.h"
#include "pastunits.h"
#include "opendps.h"

#define SCREEN_ID  (7)

/*
 *  The settings screen is layed out as a series of rows of labels and values
 *  that can be paged using the existing UI subsystem.
 *
 *   Label             Value
 *  -------------------------
 *  | Brightness       100% |  <- first field / uui_number
 *  | Refresh Rate    100ms |
 *  | V ADC K       13.0000 |              ... 
 *  | V ADC C       13.0000 |
 *  | V DAC K       13.0000 |
 *  | V DAC C       13.0000 |  <- sixth field / uui_number
 *  -------------------------
 *
 * When a value is set, it calls the set function callback in order to do something.
 * Values cannot be 'negative' because uui_number does not support it. Instead, the
 * set callback function will use the field color as the sign. RED values are treated
 * as negative numbers while WHITE numbers are positive.
 *
 */


// Number of fields shown per page
#define ITEMS_PER_PAGE 5

// Total number of fields that can be edited
#define ITEMS 13

// PAGES = ceil(ITEMS / ITEMS_PER_PAGE)
#define PAGES (ITEMS+(ITEMS_PER_PAGE-1))/ITEMS_PER_PAGE

// UI Row height in pixels
#define ROW_HEIGHT 17

// Field X/Y offsets. Fields are shown on the right-hand side of the screen
#define FIELD_Y_OFFSET 5
#define FIELD_X_OFFSET 128

// which page we are currently on.
static int8_t current_page = 0;
static int8_t current_item = 0;
static bool select_mode;

int8_t ui_settings = 0;

/*
 * This is the implementation of the Settings screen.
 */
static void settings_enable(bool _enable);
static void settings_tick(void);
static void settings_reset(void);

static void past_save(past_t *past);

static void activated(void);
static void deactivated(void);

static bool event(uui_t *ui, event_t event, uint8_t data);
static void field_changed(ui_number_t *item);
static void set_page(int8_t page);

static set_param_status_t set_parameter(char *name, char *value);
static set_param_status_t get_parameter(char *name, char *value, uint32_t value_len);


/*
 * Get/Set functions used as callbacks for all editable fields
 */
static int32_t get_v_adc_k(void);
static void set_v_adc_k(struct ui_number_t *);
static int32_t get_v_adc_c(void);
static void set_v_adc_c(struct ui_number_t *);
static int32_t get_v_dac_k(void);
static void set_v_dac_k(struct ui_number_t *);
static int32_t get_v_dac_c(void);
static void set_v_dac_c(struct ui_number_t *item);
static int32_t get_a_adc_k(void);
static void set_a_adc_k(struct ui_number_t *item);
static int32_t get_a_adc_c(void);
static void set_a_adc_c(struct ui_number_t *item);
static int32_t get_a_dac_k(void);
static void set_a_dac_k(struct ui_number_t *item);
static int32_t get_a_dac_c(void);
static void set_a_dac_c(struct ui_number_t *item);
static int32_t get_vin_adc_k(void);
static void set_vin_adc_k(struct ui_number_t *item);
static int32_t get_vin_adc_c(void);
static void set_vin_adc_c(struct ui_number_t *item);
static int32_t get_brightness(void);
static void set_brightness(struct ui_number_t *);
static int32_t get_refresh(void);
static void set_refresh(struct ui_number_t *item);
static int32_t get_on_locked(void);
static void set_on_locked(struct ui_number_t *item);


struct field_item {
    const char* label;
    int32_t min;
    int32_t max;
    int8_t digits;
    int8_t decimals;
    int8_t unit;
    int32_t (*get)(void);
    void (*set)(struct ui_number_t *i);
};


/*
 * Fields that can be edited
 * Ensure that ITEMS match the number of elements here
 *
 * Note: Min values cannot be negative because uui_number cannot handle negative numbers.
 *       If you want to enable the possibility for negative numbers, set the min value to 0.
 *       The sign can be toggled by setting a value to 0 in the UI (shown in RED for negative)
 *       If you do NOT want the possibility for negative numbers, set the min value to a non-zero value.
 */
struct field_item field_items[] = {
// All values are 10^4 and usess the si_decimilli unit.
//  LABEL               MIN     MAX         DIGITS,   DEC,  UNIT,          GET callback         SET callback
    {"Brightness",      10000,  1000000,    3,        0,    unit_none,     &get_brightness,     &set_brightness }, // TODO: Set this to unit_percent from dpsmode PR
    {"Refresh",         100000, 9990000,    3,        0,    unit_ms,       &get_refresh,        &set_refresh },

    // constants
    {"V ADC K",         -9999999,  9999999,  3,       4,    unit_none,     &get_v_adc_k,        &set_v_adc_k },
    {"V ADC C",         -9999999,  9999999,  3,       4,    unit_none,     &get_v_adc_c,        &set_v_adc_c },
    {"V DAC K",         -9999999,  9999999,  3,       4,    unit_none,     &get_v_dac_k,        &set_v_dac_k },
    {"V DAC C",         -9999999,  9999999,  3,       4,    unit_none,     &get_v_dac_c,        &set_v_dac_c },
    {"I ADC K",         -9999999,  9999999,  3,       4,    unit_none,     &get_a_adc_k,        &set_a_adc_k },
    {"I ADC C",         -9999999,  9999999,  3,       4,    unit_none,     &get_a_adc_c,        &set_a_adc_c },
    {"I DAC K",         -9999999,  9999999,  3,       4,    unit_none,     &get_a_dac_k,        &set_a_dac_k },
    {"I DAC C",         -9999999,  9999999,  3,       4,    unit_none,     &get_a_dac_c,        &set_a_dac_c },
    {"Vin ADC C",       -9999999,  9999999,  3,       4,    unit_none,     &get_vin_adc_k,      &set_vin_adc_k },
    {"Vin ADC K",       -9999999,  9999999,  3,       4,    unit_none,     &get_vin_adc_c,      &set_vin_adc_c },

    // lock screen when output enabled
    {"LockScr-En",      0,        10000,    1,        0,    unit_bool,     &get_on_locked,      &set_on_locked },
};

/*
 * Field UI elements
 * One field for each row
 */
ui_number_t settings_field[] = {
{
    {
        .type = ui_item_number,
        .id = 11,
        .x = FIELD_X_OFFSET,
        .y = FIELD_Y_OFFSET + (1 * ROW_HEIGHT),
        .can_focus = true,
    },
    .font_size = FONT_FULL_SMALL,
    .alignment = ui_text_right_aligned,
    .pad_dot = false,
    .color = WHITE,
    .value = 0,
    .min = 0,
    .max = 9999999,
    .si_prefix = si_decimilli,
    .num_digits = 3,
    .num_decimals = 4,
    .unit = unit_none,
    .changed = &field_changed,
},
{
    {
        .type = ui_item_number,
        .id = 12,
        .x = FIELD_X_OFFSET,
        .y = FIELD_Y_OFFSET + (2*ROW_HEIGHT),
        .can_focus = true,
    },
    .font_size = FONT_FULL_SMALL,
    .alignment = ui_text_right_aligned,
    .pad_dot = false,
    .color = WHITE,
    .value = 0,
    .min = 0,
    .max = 9999999,
    .si_prefix = si_decimilli,
    .num_digits = 3,
    .num_decimals = 4,
    .unit = unit_none,
    .changed = &field_changed,
},
{
    {
        .type = ui_item_number,
        .id = 13,
        .x = FIELD_X_OFFSET,
        .y = FIELD_Y_OFFSET + (3*ROW_HEIGHT),
        .can_focus = true,
    },
    .font_size = FONT_FULL_SMALL,
    .alignment = ui_text_right_aligned,
    .pad_dot = false,
    .color = WHITE,
    .value = 0,
    .min = 0,
    .max = 9999999,
    .si_prefix = si_decimilli,
    .num_digits = 3,
    .num_decimals = 4,
    .unit = unit_none,
    .changed = &field_changed,
},
{
    {
        .type = ui_item_number,
        .id = 14,
        .x = FIELD_X_OFFSET,
        .y = FIELD_Y_OFFSET + (4*ROW_HEIGHT),
        .can_focus = true,
    },
    .font_size = FONT_FULL_SMALL,
    .alignment = ui_text_right_aligned,
    .pad_dot = false,
    .color = WHITE,
    .value = 0,
    .min = 0,
    .max = 9999999,
    .si_prefix = si_decimilli,
    .num_digits = 3,
    .num_decimals = 4,
    .unit = unit_none,
    .changed = &field_changed,
},
{
    {
        .type = ui_item_number,
        .id = 15,
        .x = FIELD_X_OFFSET,
        .y = FIELD_Y_OFFSET + (5*ROW_HEIGHT),
        .can_focus = true,
    },
    .font_size = FONT_FULL_SMALL,
    .alignment = ui_text_right_aligned,
    .pad_dot = false,
    .color = WHITE,
    .value = 0,
    .min = 0,
    .max = 9999999,
    .si_prefix = si_decimilli,
    .num_digits = 3,
    .num_decimals = 4,
    .unit = unit_none,
    .changed = &field_changed,
}
};


/*
 * Main screen UI
 */
ui_screen_t settings_screen = {
    .id = SCREEN_ID,
    .name = "settings",
    .icon_data = (uint8_t *) gfx_gear,
    .icon_data_len = sizeof(gfx_gear),
    .icon_width = GFX_GEAR_WIDTH,
    .icon_height = GFX_GEAR_HEIGHT,
    .event = &event,
    .activated = &activated,
    .deactivated = &deactivated,
    .enable = &settings_enable,
    .past_save = &past_save,
    .past_restore = NULL,
    .tick = &settings_tick,
    .num_items = ITEMS_PER_PAGE,
    .items = {
        (ui_item_t*) &settings_field[0], 
        (ui_item_t*) &settings_field[1], 
        (ui_item_t*) &settings_field[2], 
        (ui_item_t*) &settings_field[3], 
        (ui_item_t*) &settings_field[4]
    },
    .set_parameter = &set_parameter,
    .get_parameter = &get_parameter,
    .parameters = {
        {
            .name = {'\0'} /** Terminator */
        },
    },
};


/**
 * @brief      Set function parameter
 *
 * @param[in]  name   name of parameter
 * @param[in]  value  value of parameter as a string - always in SI units
 *
 * @retval     set_param_status_t status code
 */
static set_param_status_t set_parameter(char *name, char *value) {
    (void)name;
    (void)value;

    return ps_unknown_name;
}

/**
 * @brief      Get function parameter
 *
 * @param[in]  name       name of parameter
 * @param[in]  value      value of parameter as a string - always in SI units
 * @param[in]  value_len  length of value buffer
 *
 * @retval     set_param_status_t status code
 */
static set_param_status_t get_parameter(char *name, char *value, uint32_t value_len) {
    (void)name;
    (void)value;
    (void)value_len;

    return ps_unknown_name;
}


/*
 * Get / Set callback functions
 */
static int32_t get_v_adc_k() {
    return v_adc_k_coef * 10000;
}
static void set_v_adc_k(ui_number_t *item) {
    v_adc_k_coef = item->value / 10000.0f;
}
static int32_t get_v_adc_c() {
    return v_adc_c_coef * 10000;
}
static void set_v_adc_c(ui_number_t *item) {
    v_adc_c_coef = item->value / 10000.0f;
}


static int32_t get_v_dac_k() {
    return v_dac_k_coef * 10000;
}
static void set_v_dac_k(ui_number_t *item) {
    v_dac_k_coef = item->value / 10000.0f;
}
static int32_t get_v_dac_c() {
    return v_dac_c_coef * 10000;
}
static void set_v_dac_c(ui_number_t *item) {
    v_dac_c_coef = item->value / 10000.0f;
}



static int32_t get_a_adc_k() {
    return a_adc_k_coef * 10000;
}
static void set_a_adc_k(ui_number_t *item) {
    a_adc_k_coef = item->value / 10000.0f;
}
static int32_t get_a_adc_c() {
    return a_adc_c_coef * 10000;
}
static void set_a_adc_c(ui_number_t *item) {
    a_adc_c_coef = item->value / 10000.0f;
}


static int32_t get_a_dac_k() {
    return a_dac_k_coef * 10000;
}
static void set_a_dac_k(ui_number_t *item) {
    a_dac_k_coef = item->value / 10000.0f;
}
static int32_t get_a_dac_c() {
    return a_dac_c_coef * 10000;
}
static void set_a_dac_c(ui_number_t *item) {
    a_dac_c_coef = item->value / 10000.0f;
}


static int32_t get_vin_adc_k() {
    return vin_adc_k_coef * 10000;
}
static void set_vin_adc_k(ui_number_t *item) {
    vin_adc_k_coef = item->value / 10000.0f;
}
static int32_t get_vin_adc_c() {
    return vin_adc_c_coef * 10000;
}
static void set_vin_adc_c(ui_number_t *item) {
    vin_adc_c_coef = item->value / 10000.0f;
}


static int32_t get_brightness() {
    return (hw_get_backlight() / 1.28f) * 10000;
}
static void set_brightness(ui_number_t *item) {
    hw_set_backlight((item->value / 10000.0f) * 1.28f);
}

static int32_t get_refresh() {
    return opendps_screen_update_ms * 10000;
}
static void set_refresh(ui_number_t *item) {
    opendps_screen_update_ms = item->value / 10000;

    // ensure sane values
    if (opendps_screen_update_ms > 10000) {
        opendps_screen_update_ms = 10000;
    }
    if (opendps_screen_update_ms < 10) {
        opendps_screen_update_ms = 10;
    }
}

static int32_t get_on_locked() {
    if (ui_settings | SCREEN_LOCKED_WHEN_ON) {
        return 10000;
    }
    return 0;
}

static void set_on_locked(struct ui_number_t *item) {
    if (item->value == 0) {
        ui_settings = ui_settings & ~SCREEN_LOCKED_WHEN_ON;
    } else {
        ui_settings = ui_settings | SCREEN_LOCKED_WHEN_ON;
    }
}



/**
 * @brief  event handler: We only care about the SET, M1 and M2 buttons
 *         used to keep track of where we are in the menu
 */
static bool event(uui_t *ui, event_t event, uint8_t data) {
    (void)ui;

    switch (event) {
        case event_button_m1:
        case event_button_m2:

            // go up
            if ( event == event_button_m1 ) {
                ui_screen_t *screen = ui->screens[ui->cur_screen];

                // not first item, move up one
                if (current_item > 0) {
                    current_item--;
                    if ( ! select_mode) screen->cur_item = current_item;
                    return false;
                }

                // we are left with current_item == 0.
                
                if (current_page == 0) {
                    // do nothing for first page/first item
                    return true;
                }

                // current_item == 0, and current_page != 0
                set_page(current_page - 1);
                current_item = ITEMS_PER_PAGE - 1;
                if ( ! select_mode) screen->cur_item = current_item;
                return false;

            // go down
            } else {
                ui_screen_t *screen = ui->screens[ui->cur_screen];

                // not last item, go down
                if (current_item < ITEMS_PER_PAGE - 1) {
                    // do nothing if we are on the last item already
                    if ((current_page * ITEMS_PER_PAGE) + current_item >= ITEMS - 1)
                        return true;

                    // otherwise, go down one item
                    current_item++;
                    if ( ! select_mode) screen->cur_item = current_item;
                    return false;
                }

                // we are left with current_item == last item

                if (current_page == PAGES - 1) {
                    // last page. do nothing
                    return true;
                }

                // last item, but not last page
                set_page(current_page + 1);
                current_item = 0;
                if ( ! select_mode) screen->cur_item = current_item;
                return false;
            }

        case event_button_sel:
            // long SET press will reest all values
            if (data == press_long) {
                settings_reset();
                // update page to update values
                set_page(current_page);
                return true;
            }

            select_mode = ! select_mode;

            return false;

        default:
            return false;
    }

    return false;
}

// generic field change function called by all fields
// the current item is used to determine the actual field being changed
static void field_changed(struct ui_number_t *item) {
    int8_t page_offset = current_page * ITEMS_PER_PAGE;

    if (page_offset + current_item >= ITEMS) {
        // nope
        return;
    }

    // call the appropriate set function
    field_items[page_offset + current_item].set(item);
}

/**
 * @brief set_page will change the current settings page
 *        to the given page number.
 */
static void set_page(int8_t page) {
    current_page = page;

    int8_t page_offset = current_page * ITEMS_PER_PAGE;

    // draw each field with its corresponding label
    for (uint8_t i = 0; i < ITEMS_PER_PAGE; i++) {
        // if greater than total number of items, clear the area
        if (page_offset + i >= ITEMS) {
            break;
        }
       
        // update field value using value from the get function
        int32_t value = field_items[page_offset + i].get();
        settings_field[i].value = value;
        settings_field[i].min = field_items[page_offset + i].min;
        settings_field[i].max = field_items[page_offset + i].max;
        settings_field[i].num_digits = field_items[page_offset + i].digits;
        settings_field[i].num_decimals = field_items[page_offset + i].decimals;
        settings_field[i].unit = field_items[page_offset + i].unit;
        // cur_digit may be wrongg after changing digits and decimals, updating it once more:
        settings_field[i].cur_digit = field_items[page_offset + i].digits + field_items[page_offset + i].decimals - 1;
    }

    tft_clear();
}


/**
 * @brief settings_tick will render the UI
 */
static void settings_tick(void) {
    int8_t page_offset = current_page * ITEMS_PER_PAGE;

    // draw each field with its corresponding label
    for (uint8_t i = 0; i < ITEMS_PER_PAGE; i++) {
        // if greater than total number of items, clear the area
        if (page_offset + i >= ITEMS) {
            tft_fill(0 /* x */, FIELD_Y_OFFSET + ((1 + i) * ROW_HEIGHT) /* y, +1 because first row is the header */,
                TFT_WIDTH, ROW_HEIGHT,
                BLACK);
        } else {
            tft_puts(FONT_FULL_SMALL, 
                    field_items[page_offset + i].label,
                    0 /* x, 1st px for select bar */,  FIELD_Y_OFFSET + ((1 + i) * ROW_HEIGHT) + FONT_FULL_SMALL_MAX_GLYPH_HEIGHT  /* y */ ,
                    TFT_WIDTH/2, FONT_FULL_SMALL_MAX_GLYPH_HEIGHT,
                    current_item == i ? ORANGE : WHITE, false);

            // vertical separator |, for each entry
            ili9163c_draw_vline(TFT_WIDTH/2 + 3, FIELD_Y_OFFSET + FONT_FULL_SMALL_MAX_GLYPH_HEIGHT + (i * ROW_HEIGHT), 
                    ROW_HEIGHT,
                    WHITE);

            settings_field[i].ui.draw(&settings_field[i].ui);
        }
    }

    // print the header
    tft_puts(FONT_FULL_SMALL, "OpenDPS Settings", 1 /*x*/, FONT_FULL_SMALL_MAX_GLYPH_HEIGHT /*y*/,
            FONT_FULL_SMALL_MAX_GLYPH_WIDTH * 16, FONT_FULL_SMALL_MAX_GLYPH_HEIGHT,
            GREEN, false);

    // header separator _____
    ili9163c_draw_hline(0,  FIELD_Y_OFFSET + FONT_FULL_SMALL_MAX_GLYPH_HEIGHT, TFT_WIDTH, WHITE);

}


/**
 * @brief resets the settings value.
 */
static void settings_reset() {
    a_adc_k_coef = A_ADC_K;
    a_adc_c_coef = A_ADC_C;
    a_dac_k_coef = A_DAC_K;
    a_dac_c_coef = A_DAC_C;
    v_adc_k_coef = V_ADC_K;
    v_adc_c_coef = V_ADC_C;
    v_dac_k_coef = V_DAC_K;
    v_dac_c_coef = V_DAC_C;
    vin_adc_k_coef = VIN_ADC_K;
    vin_adc_c_coef = VIN_ADC_C;

    // clear calibration settings from past
    opendps_clear_calibration();

    // reset interval and brightness settings
    opendps_screen_update_ms = 250;
    hw_set_backlight(128);
}


/**
 * @brief ensure power constants are saved
 */
static void past_save(past_t *past) {
    pwrctl_past_save(past);
}


/**
 * @brief on activation, reset to first page
 */
static void activated() {
    // screen activation will cause cur_item to be reset
    // Ensure current item matches the UI system
    // Move back to previous page on init.
    set_page(current_page);
    current_item = 0;
}

/**
 * @brief on deactivation, clear the screen
 */
static void deactivated() {
    tft_clear();
}

static void settings_enable(bool enabled) {
    (void)enabled;
}

/**
 * @brief      Initialise the CV module and add its screen to the UI
 *
 * @param      ui    The user interface
 */
void func_settings_init(uui_t *ui) {
    for (uint8_t i = 0; i < ITEMS_PER_PAGE; i++) {
        number_init(&settings_field[i]); 
    }

    // initialize page and selected items
    set_page(0);
    current_item = 0;

    uui_add_screen(ui, &settings_screen);
}


