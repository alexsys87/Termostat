#include "menu_system.h"
#include <stddef.h>

void menu_init(menu_state_t* state, const menu_item_t* root_menu, uint8_t item_count) {
    state->current_menu = root_menu;
    state->menu_item_count = item_count;
    state->selected_index = 0;
    state->scroll_offset = 0;
    state->edit_mode = 0;
}

void menu_draw(menu_state_t* state, HD44780* lcd) {
    HD44780_clear(lcd);
    uint8_t line = 0;
    uint8_t max_items = 2; // Так как дисплей имеет 2 строки

    // Логика прокрутки (скроллинга) меню
    if (state->selected_index >= state->scroll_offset + max_items) {
        state->scroll_offset = state->selected_index - max_items + 1;
    }
    else if (state->selected_index < state->scroll_offset) {
        state->scroll_offset = state->selected_index;
    }

    // Отрисовка видимых пунктов
    for (uint8_t i = state->scroll_offset; i < state->menu_item_count && line < max_items; i++, line++) {
        HD44780_cursor_to(lcd, 0, line);
        
        // Отрисовка курсора через функцию символа
        if (i == state->selected_index) {
            HD44780_put_char(lcd, '>');
        } else {
            HD44780_put_char(lcd, ' ');
        }
        
        // Отрисовка текста пункта меню
        HD44780_put_str(lcd, state->current_menu[i].text);
    }
}

void menu_handle_up(menu_state_t* state) {
    if (state->selected_index > 0) {
        state->selected_index--;
    } else {
        // Зацикливание: переход с верхнего пункта на самый нижний
        state->selected_index = state->menu_item_count - 1;
    }
}

void menu_handle_down(menu_state_t* state) {
    if (state->selected_index < state->menu_item_count - 1) {
        state->selected_index++;
    } else {
        // Зацикливание: переход с нижнего пункта на самый верхний
        state->selected_index = 0;
    }
}

void menu_handle_enter(menu_state_t* state) {
    const menu_item_t* item = &state->current_menu[state->selected_index];
    
    // Если это конечный пункт (Leaf), выполняем действие
    if (item->action != NULL) {
        item->action();
    } 
    // Если это узел с подменю (Node), переходим в него
    else if (item->children != NULL && item->children_count > 0) {
        state->current_menu = item->children;
        state->menu_item_count = item->children_count;
        state->selected_index = 0;
        state->scroll_offset = 0;
        state->edit_mode = 0;
    }
}