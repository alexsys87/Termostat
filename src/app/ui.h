#ifndef UI_H
#define UI_H

// User interface: 8x2 LCD, three buttons and the menu

#define MENU_TIMEOUT_MS         30000   // leave the menu after inactivity
#define SAVE_DELAY_MS           10000   // auto-save N ms after the last key press
#define DISPLAY_REFRESH_MS      100     // minimal display redraw interval
#define BUTTON_POLL_MS          5       // button polling period

void ui_sanitize_settings(void);        // clamp loaded settings into the allowed ranges
void ui_init(void);                     // LCD initialization
void ui_task(void);                     // call from the main loop

#endif // UI_H
