#include "menu_system.h"

static const uint16_t pow10_table[] = {1, 10, 100, 1000};

// ==================== Text helpers ====================

// Writes an unsigned number, returns pointer to the terminating zero
char* fmt_uint(char* dst, uint32_t value) {
    char tmp[10];
    uint8_t n = 0;
    do {
        tmp[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n) *dst++ = tmp[--n];
    *dst = '\0';
    return dst;
}

// Writes a fixed-point number: scaled = value * 10^decimals (e.g. 253, 1 -> "25.3")
char* fmt_fixed(char* dst, int32_t scaled, uint8_t decimals) {
    uint32_t abs_value;
    if (scaled < 0) {
        *dst++ = '-';
        abs_value = (uint32_t)(-scaled);
    } else {
        abs_value = (uint32_t)scaled;
    }
    if (decimals > 3) decimals = 3;
    uint16_t div = pow10_table[decimals];
    dst = fmt_uint(dst, abs_value / div);
    if (decimals) {
        uint32_t frac = abs_value % div;
        *dst++ = '.';
        while (decimals--) {
            div /= 10;
            *dst++ = (char)('0' + (frac / div) % 10);
        }
        *dst = '\0';
    }
    return dst;
}

char* str_copy(char* dst, const char* src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
    return dst;
}

// ==================== Parameters ====================

// Returns the value in scaled units (float: value * 10^decimals)
int32_t param_get(const param_desc_t* p) {
    switch (p->type) {
        case PARAM_FLOAT: {
            float f = *(const float*)p->value;
            if (f != f) return p->min;                // NaN (e.g. erased Flash)
            f *= pow10_table[p->decimals];
            if (f > 1000000.0f) return 1000000;       // keep the conversion defined
            if (f < -1000000.0f) return -1000000;
            return (int32_t)(f >= 0.0f ? f + 0.5f : f - 0.5f);
        }
        case PARAM_U8:  return *(const uint8_t*)p->value;
        case PARAM_U16: return *(const uint16_t*)p->value;
        default:        return (int32_t)*(const uint32_t*)p->value;
    }
}

void param_set(const param_desc_t* p, int32_t value) {
    if (p->type == PARAM_U32) return; // counters are read-only
    if (value < p->min) value = p->min;
    if (value > p->max) value = p->max;
    switch (p->type) {
        case PARAM_FLOAT: *(float*)p->value = (float)value / pow10_table[p->decimals]; break;
        case PARAM_U8:    *(uint8_t*)p->value = (uint8_t)value; break;
        default:          *(uint16_t*)p->value = (uint16_t)value; break;
    }
}

// Changes the value by one step (accel multiplies the step while a key is held).
// Enumerated values wrap around, numbers are clamped. Returns true if the value changed.
bool param_step(const param_desc_t* p, int8_t dir, uint8_t accel) {
    if ((p->flags & PF_READONLY) || p->type == PARAM_U32) return false;

    int32_t old_value = param_get(p);
    int32_t value = old_value;

    if (p->labels) {
        value += dir;
        if (value > p->max) value = p->min;
        if (value < p->min) value = p->max;
    } else {
        value += (int32_t)dir * p->step * accel;
        if (value > p->max) value = p->max;
        if (value < p->min) value = p->min;
    }
    if (value == old_value) return false;
    param_set(p, value);
    return true;
}

// Forces the value into its allowed range (used after loading from Flash)
void param_clamp(const param_desc_t* p) {
    if (p->type == PARAM_U32) return;
    int32_t value = param_get(p);
    if (value < p->min || value > p->max || (p->type == PARAM_FLOAT && *(float*)p->value != *(float*)p->value)) {
        param_set(p, value);
    }
}

// Formats the value for display. buf must hold at least 16 characters.
void param_format(const param_desc_t* p, char* buf) {
    if (p->type == PARAM_U32) {
        buf = fmt_uint(buf, *(const uint32_t*)p->value);
    } else {
        int32_t value = param_get(p);
        if (p->labels) {
            if (value < p->min || value > p->max) value = p->min;
            str_copy(buf, p->labels[value - p->min]);
            return;
        }
        buf = fmt_fixed(buf, value, (p->type == PARAM_FLOAT) ? p->decimals : 0);
    }
    if (p->suffix) str_copy(buf, p->suffix);
}

// ==================== Menu ====================

void menu_open(menu_state_t* state, const menu_item_t* root_menu, uint8_t item_count) {
    state->current_menu = root_menu;
    state->menu_item_count = item_count;
    state->parent_menu = NULL;
    state->parent_item_count = 0;
    state->parent_index = 0;
    state->selected_index = 0;
    state->scroll_offset = 0;
    state->edit_mode = 0;
}

void menu_close(menu_state_t* state) {
    state->current_menu = NULL;
    state->parent_menu = NULL;
    state->edit_mode = 0;
}

bool menu_is_open(const menu_state_t* state) {
    return state->current_menu != NULL;
}

const param_desc_t* menu_edit_param(const menu_state_t* state) {
    if (state->current_menu == NULL || !state->edit_mode) return NULL;
    return state->current_menu[state->selected_index].param;
}

void menu_draw(menu_state_t* state, HD44780* lcd) {
    char line[16];

    if (state->current_menu == NULL) return;

    const menu_item_t* selected = &state->current_menu[state->selected_index];

    // Edit mode: name on the first line, value on the second one
    if (state->edit_mode && selected->param) {
        HD44780_print_line(lcd, 0, selected->text);
        param_format(selected->param, line);
        HD44780_print_line(lcd, 1, line);
        return;
    }

    // Keep the selected item visible
    if (state->selected_index >= state->scroll_offset + LCD_ROWS) {
        state->scroll_offset = state->selected_index - LCD_ROWS + 1;
    } else if (state->selected_index < state->scroll_offset) {
        state->scroll_offset = state->selected_index;
    }

    for (uint8_t row = 0; row < LCD_ROWS; row++) {
        uint8_t i = state->scroll_offset + row;
        line[0] = '\0';
        if (i < state->menu_item_count) {
            line[0] = (i == state->selected_index) ? '>' : ' ';
            // Menu texts are short constants, LCD_COLS - 1 characters are shown
            const char* text = state->current_menu[i].text;
            uint8_t n = 1;
            while (*text && n < LCD_COLS) line[n++] = *text++;
            line[n] = '\0';
        }
        HD44780_print_line(lcd, row, line);
    }
}

menu_event_t menu_handle_up(menu_state_t* state, uint8_t accel) {
    if (state->current_menu == NULL) return MENU_EVT_NONE;

    if (state->edit_mode) {
        const param_desc_t* p = state->current_menu[state->selected_index].param;
        return (p && param_step(p, 1, accel)) ? MENU_EVT_CHANGED : MENU_EVT_NONE;
    }

    // Wrap around: from the first item to the last one
    state->selected_index = (state->selected_index > 0) ? state->selected_index - 1
                                                        : state->menu_item_count - 1;
    return MENU_EVT_NONE;
}

menu_event_t menu_handle_down(menu_state_t* state, uint8_t accel) {
    if (state->current_menu == NULL) return MENU_EVT_NONE;

    if (state->edit_mode) {
        const param_desc_t* p = state->current_menu[state->selected_index].param;
        return (p && param_step(p, -1, accel)) ? MENU_EVT_CHANGED : MENU_EVT_NONE;
    }

    // Wrap around: from the last item to the first one
    state->selected_index = (state->selected_index + 1 < state->menu_item_count) ? state->selected_index + 1 : 0;
    return MENU_EVT_NONE;
}

menu_event_t menu_handle_enter(menu_state_t* state) {
    if (state->current_menu == NULL) return MENU_EVT_NONE;

    const menu_item_t* item = &state->current_menu[state->selected_index];

    if (state->edit_mode) {
        state->edit_mode = 0;
        return MENU_EVT_EDIT_END;
    }

    if (item->param) {
        state->edit_mode = 1;
        return MENU_EVT_EDIT_BEGIN;
    }

    if (item->action) {
        item->action();
        return MENU_EVT_NONE;
    }

    if (item->children && item->children_count) {
        // Enter the submenu, remember where we came from
        state->parent_menu = state->current_menu;
        state->parent_item_count = state->menu_item_count;
        state->parent_index = state->selected_index;
        state->current_menu = item->children;
        state->menu_item_count = item->children_count;
        state->selected_index = 0;
        state->scroll_offset = 0;
        return MENU_EVT_NONE;
    }

    // "Back" item: return to the parent menu or close the menu on the top level
    if (state->parent_menu) {
        state->current_menu = state->parent_menu;
        state->menu_item_count = state->parent_item_count;
        state->selected_index = state->parent_index;
        state->scroll_offset = state->parent_index;
        state->parent_menu = NULL;
        return MENU_EVT_NONE;
    }

    menu_close(state);
    return MENU_EVT_EXIT;
}
