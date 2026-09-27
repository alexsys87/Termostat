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
    PARAM_FLOAT = 0,   // float, edited as a scaled integer (see decimals)
    PARAM_U8,          // uint8_t
    PARAM_U16,         // uint16_t
    PARAM_U32          // uint32_t (read-only counters)
};

// Parameter flags
#define PF_READONLY  0x01  // value can only be viewed
#define PF_NOSAVE    0x02  // value is not stored in Flash (changing it does not trigger a save)

typedef struct {
    void* value;                // pointer to the variable
    const char* const* labels;  // names of enumerated values (index = value - min), NULL for numbers
    const char* suffix;         // unit shown after the number, NULL if none
    int16_t min;                // limits and step in scaled units (float: value * 10^decimals)
    int16_t max;
    uint16_t step;
    uint8_t type;               // PARAM_xxx
    uint8_t decimals;           // PARAM_FLOAT only: digits after the decimal point (0..3)
    uint8_t flags;              // PF_xxx
} param_desc_t;

int32_t param_get(const param_desc_t* p);
void param_set(const param_desc_t* p, int32_t value);
bool param_step(const param_desc_t* p, int8_t dir, uint8_t accel);
void param_clamp(const param_desc_t* p);
void param_format(const param_desc_t* p, char* buf);

// ==================== Text helpers (no printf, saves Flash) ====================
char* fmt_uint(char* dst, uint32_t value);
char* fmt_fixed(char* dst, int32_t scaled, uint8_t decimals);
char* str_copy(char* dst, const char* src);

// ==================== Menu ====================
typedef void (*menu_action_t)(void);

typedef struct menu_item_t {
    const char* text;
    menu_action_t action;              // leaf: action on Enter
    const param_desc_t* param;         // leaf: editable/viewable parameter
    const struct menu_item_t* children;// node: submenu
    uint8_t children_count;
} menu_item_t;                         // all of action/param/children NULL = "Back" item

#define MENU_ITEMS(name) static const menu_item_t name[]
#define MENU_NODE(text, child_array) {text, NULL, NULL, child_array, (uint8_t)COUNT_OF(child_array)}
#define MENU_ACTION(text, action)    {text, action, NULL, NULL, 0}
#define MENU_PARAM(text, param)      {text, NULL, &(param), NULL, 0}
#define MENU_BACK(text)              {text, NULL, NULL, NULL, 0}

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

void menu_open(menu_state_t* state, const menu_item_t* root_menu, uint8_t item_count);
void menu_close(menu_state_t* state);
bool menu_is_open(const menu_state_t* state);
const param_desc_t* menu_edit_param(const menu_state_t* state);
void menu_draw(menu_state_t* state, HD44780* lcd);
menu_event_t menu_handle_up(menu_state_t* state, uint8_t accel);
menu_event_t menu_handle_down(menu_state_t* state, uint8_t accel);
menu_event_t menu_handle_enter(menu_state_t* state);

#endif // MENU_SYSTEM_H
