#ifndef MENU_SYSTEM_H
#define MENU_SYSTEM_H

#include "stm32f0xx.h"
#include "HD44780.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Classic and safe array size macro
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

// ==================== Editable parameters ====================
enum {
    PARAM_U8 = 0,      // uint8_t
    PARAM_U16,         // uint16_t, fixed point with 'decimals'
    PARAM_I16,         // int16_t, fixed point with 'decimals'
    PARAM_U32,         // uint32_t (read-only counters)
    PARAM_TEMP,        // int16_t temperature, 0.1 C, shown in the selected units
    PARAM_TDELTA       // int16_t temperature difference, 0.1 C, shown in the selected units
};

// Parameter flags
#define PF_READONLY  0x01  // value can only be viewed
#define PF_NOSAVE    0x02  // value is not stored in Flash (changing it does not trigger a save)

// Units shown after a number
enum {
    UNIT_NONE = 0,
    UNIT_S,
    UNIT_MS,
    UNIT_MIN,
    UNIT_H,
    UNIT_PERCENT,
    UNIT_BIT,
    UNIT_M
};

typedef struct {
    void* value;                // pointer to the variable
    const char* const* labels;  // names of enumerated values (index = value - min), NULL for numbers
    int16_t min;                // limits and step in stored units
    int16_t max;
    uint16_t step;
    uint8_t type;               // PARAM_xxx
    uint8_t decimals;           // PARAM_U16/PARAM_I16: digits after the decimal point (0..3)
    uint8_t flags;              // PF_xxx
    uint8_t unit;               // UNIT_xxx
} param_desc_t;

// Show temperatures in Fahrenheit (values are always stored in Celsius)
extern bool param_fahrenheit;

int32_t param_get(const param_desc_t* p);
void param_set(const param_desc_t* p, int32_t value);
bool param_step(const param_desc_t* p, int8_t dir, uint16_t accel);
void param_clamp(const param_desc_t* p);
void param_format(const param_desc_t* p, char* buf);
char* fmt_temp(char* dst, int16_t c10, bool delta);

// ==================== Text helpers (no printf, saves Flash) ====================
char* fmt_uint(char* dst, uint32_t value);
char* fmt_fixed(char* dst, int32_t scaled, uint8_t decimals);
char* str_copy(char* dst, const char* src);

// ==================== Menu ====================
typedef void (*menu_action_t)(void);

enum {
    MENU_KIND_BACK = 0,     // return to the parent menu / close the menu
    MENU_KIND_NODE,         // submenu
    MENU_KIND_ACTION,       // function called on Enter
    MENU_KIND_PARAM         // editable/viewable parameter
};

typedef struct menu_item_t {
    const char* text;
    union {
        const struct menu_item_t* children;
        menu_action_t action;
        const param_desc_t* param;
    } target;
    uint8_t kind;           // MENU_KIND_xxx
    uint8_t count;          // number of submenu items
} menu_item_t;

#define MENU_ITEMS(name) static const menu_item_t name[]
#define MENU_NODE(text, child_array) {text, {.children = child_array}, MENU_KIND_NODE, (uint8_t)COUNT_OF(child_array)}
#define MENU_ACTION(text, fn)        {text, {.action = fn}, MENU_KIND_ACTION, 0}
#define MENU_PARAM(text, p)          {text, {.param = &(p)}, MENU_KIND_PARAM, 0}
#define MENU_BACK(text)              {text, {.children = NULL}, MENU_KIND_BACK, 0}

typedef struct {
    const menu_item_t* current_menu;   // NULL = menu is closed
    const menu_item_t* parent_menu;    // NULL on the top level
    uint8_t menu_item_count;
    uint8_t parent_item_count;
    uint8_t parent_index;
    uint8_t selected_index;
    uint8_t scroll_offset;
    uint8_t edit_mode;
} menu_state_t;

typedef enum {
    MENU_EVT_NONE = 0,
    MENU_EVT_CHANGED,      // parameter value was changed
    MENU_EVT_EDIT_BEGIN,   // parameter editing started
    MENU_EVT_EDIT_END,     // parameter editing finished
    MENU_EVT_EXIT          // "Back" on the top level: menu closed
} menu_event_t;

// Forces every editable parameter of the menu tree into its range (after loading settings)
void menu_clamp_params(const menu_item_t* items, uint8_t count);

void menu_open(menu_state_t* state, const menu_item_t* root_menu, uint8_t item_count);
void menu_close(menu_state_t* state);
bool menu_is_open(const menu_state_t* state);
const param_desc_t* menu_edit_param(const menu_state_t* state);
void menu_draw(menu_state_t* state, HD44780* lcd);
menu_event_t menu_handle_up(menu_state_t* state, uint16_t accel);
menu_event_t menu_handle_down(menu_state_t* state, uint16_t accel);
menu_event_t menu_handle_enter(menu_state_t* state);

#endif // MENU_SYSTEM_H
