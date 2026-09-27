#ifndef MENU_SYSTEM_H
#define MENU_SYSTEM_H

#include "stm32f0xx.h"
#include "HD44780.h"
#include <stddef.h>

// Классический и безопасный макрос вычисления размера массива
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

#define MENU_ITEMS(name) static const menu_item_t name[]
#define MENU_LEAF(id, parent_id, text, action) {(uint8_t)(id), (uint8_t)(parent_id), text, action, NULL, 0}
#define MENU_NODE(id, parent_id, text, child_array) {(uint8_t)(id), (uint8_t)(parent_id), text, NULL, child_array, COUNT_OF(child_array)}

typedef void (*menu_action_t)(void);

typedef struct menu_item_t {
    uint8_t id;
    uint8_t parent_id;
    const char* text;
    menu_action_t action;
    const struct menu_item_t* children;
    uint8_t children_count;
} menu_item_t;

typedef struct {
    const menu_item_t* current_menu;
    uint8_t menu_item_count;
    uint8_t selected_index;
    uint8_t scroll_offset;
    uint8_t edit_mode;
} menu_state_t;

void menu_init(menu_state_t* state, const menu_item_t* root_menu, uint8_t item_count);
void menu_draw(menu_state_t* state, HD44780* lcd);
void menu_handle_up(menu_state_t* state);
void menu_handle_down(menu_state_t* state);
void menu_handle_enter(menu_state_t* state);

#endif // MENU_SYSTEM_H