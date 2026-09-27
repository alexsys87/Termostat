#ifndef HD44780_H
#define HD44780_H

#include "stm32f0xx.h"
#include <stdint.h>

typedef struct {
    GPIO_TypeDef* data_port;
    GPIO_TypeDef* control_port;
    uint16_t data_pins[4];
    uint16_t rs_pin;
    uint16_t e_pin;
    uint8_t display_control;
} HD44780;

// Commands
#define HD44780_CLEAR_DISPLAY   0x01
#define HD44780_RETURN_HOME     0x02
#define HD44780_ENTRY_MODE_SET  0x04
#define HD44780_DISPLAY_CONTROL 0x08
#define HD44780_FUNCTION_SET    0x20
#define HD44780_SET_DDRAM_ADDR  0x80

// Flags
#define HD44780_ENTRY_LEFT      0x02
#define HD44780_DISPLAY_ON      0x04
#define HD44780_DISPLAY_OFF     0x00
#define HD44780_CURSOR_ON       0x02
#define HD44780_CURSOR_OFF      0x00
#define HD44780_BLINK_ON        0x01
#define HD44780_BLINK_OFF       0x00
#define HD44780_4BIT_MODE       0x00
#define HD44780_2LINE           0x08
#define HD44780_5x8DOTS         0x00

#define HD44780_ROW0_ADDR       0x00
#define HD44780_ROW1_ADDR       0x40

// Display geometry (8x2 module)
#define LCD_COLS                8
#define LCD_ROWS                2

void HD44780_init(HD44780* lcd,
                 GPIO_TypeDef* data_port, uint16_t data_pins[4],
                 GPIO_TypeDef* control_port, uint16_t rs_pin, uint16_t e_pin);
void HD44780_clear(HD44780* lcd);
void HD44780_home(HD44780* lcd);
void HD44780_cursor_to(HD44780* lcd, uint8_t col, uint8_t row);
void HD44780_put_str(HD44780* lcd, const char* str);
void HD44780_put_char(HD44780* lcd, char c);
void HD44780_print_line(HD44780* lcd, uint8_t row, const char* str);
void HD44780_display_on(HD44780* lcd);
void HD44780_display_off(HD44780* lcd);
void HD44780_cursor_on(HD44780* lcd);
void HD44780_cursor_off(HD44780* lcd);

#endif // HD44780_H
