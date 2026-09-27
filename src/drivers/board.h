#ifndef BOARD_H
#define BOARD_H

// Board support: pin assignment, clock, timing, GPIO, beeper and watchdog.
// Everything that touches the MCU pins lives here, the application only calls these functions.

#include "stm32f0xx.h"
#include <stdint.h>
#include <stdbool.h>

// ==================== Pin masks ====================
#define PIN_0    ((uint16_t)0x0001)
#define PIN_1    ((uint16_t)0x0002)
#define PIN_2    ((uint16_t)0x0004)
#define PIN_3    ((uint16_t)0x0008)
#define PIN_4    ((uint16_t)0x0010)
#define PIN_5    ((uint16_t)0x0020)
#define PIN_6    ((uint16_t)0x0040)
#define PIN_7    ((uint16_t)0x0080)
#define PIN_9    ((uint16_t)0x0200)
// PA13 (SWDIO) and PA14 (SWCLK) are reserved for the debugger

// ==================== Pin assignment (STM32F030F4P6, TSSOP-20) ====================
// The PCB is fixed: do not change this assignment
#define DS18B20_PORT    GPIOB
#define DS18B20_PIN     PIN_1        // PB1, 1-Wire bus
#define DS18B20_PIN_NUM 1

#define RELAY_PORT      GPIOF
#define RELAY_PIN       PIN_0        // PF0
#define BEEPER_PORT     GPIOF
#define BEEPER_PIN      PIN_1        // PF1, passive buzzer

#define BTN_PORT        GPIOA
#define BTN_UP_PIN      PIN_2        // PA2
#define BTN_DOWN_PIN    PIN_3        // PA3
#define BTN_ENTER_PIN   PIN_9        // PA9

#define LCD_PORT        GPIOA
#define LCD_RS_PIN      PIN_7        // PA7
#define LCD_E_PIN       PIN_5        // PA5
#define LCD_D4_PIN      PIN_0        // PA0
#define LCD_D5_PIN      PIN_1        // PA1
#define LCD_D6_PIN      PIN_6        // PA6
#define LCD_D7_PIN      PIN_4        // PA4

// Button bits returned by board_buttons_read()
#define BTN_UP          0x01
#define BTN_DOWN        0x02
#define BTN_ENTER       0x04

// Uncomment to drive the buttons from the debugger (write virtual_buttons)
//#define DEBUG_BUTTONS
#ifdef DEBUG_BUTTONS
extern volatile uint8_t virtual_buttons;
#endif

// Independent watchdog (~2 s). It is frozen while the debugger halts the core.
#ifndef USE_WATCHDOG
#define USE_WATCHDOG    1
#endif

// ==================== Time ====================
extern volatile uint32_t tick_count;   // milliseconds since start-up

void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

// ==================== Initialization ====================
void board_clock_init(void);
void board_systick_init(void);
void board_gpio_init(bool relay_idle_high);
void board_beeper_init(void);

// ==================== Watchdog ====================
void board_watchdog_init(void);
void board_watchdog_feed(void);

// ==================== I/O ====================
void board_relay_write(bool high);
uint8_t board_buttons_read(void);      // BTN_xxx bits of pressed buttons
void board_beeper_start(uint16_t duration_ms);
void board_sleep(void);                // sleep until the next interrupt

#endif // BOARD_H
