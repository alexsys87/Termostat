#ifndef MAIN_H
#define MAIN_H

#include "stm32f0xx.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

// ==================== Pin definitions ====================
#define PIN_A0   ((uint16_t)0x0001)
#define PIN_A1   ((uint16_t)0x0002)
#define PIN_A2   ((uint16_t)0x0004)
#define PIN_A3   ((uint16_t)0x0008)
#define PIN_A4   ((uint16_t)0x0010)
#define PIN_A5   ((uint16_t)0x0020)
#define PIN_A6   ((uint16_t)0x0040)
#define PIN_A7   ((uint16_t)0x0080)
#define PIN_A8   ((uint16_t)0x0100)
#define PIN_A9   ((uint16_t)0x0200)
#define PIN_A10  ((uint16_t)0x0400)
#define PIN_A13  ((uint16_t)0x2000) // SWDIO (do not use!)
#define PIN_A14  ((uint16_t)0x4000) // SWCLK (do not use!)

#define PIN_B1   ((uint16_t)0x0002) // The only PB pin of STM32F030F4

#define PIN_F0   ((uint16_t)0x0001)
#define PIN_F1   ((uint16_t)0x0002)

// ==================== Pin assignment (20-pin TSSOP) ====================
// Sensor and outputs
#define DS18B20_PIN     PIN_B1   // PB1
#define DS18B20_PORT    GPIOB
#define RELAY_PIN       PIN_F0   // PF0
#define RELAY_PORT      GPIOF
#define BEEPER_PIN      PIN_F1   // PF1
#define BEEPER_PORT     GPIOF

// Buttons (active low, internal pull-ups)
#define BTN_UP_PIN      PIN_A2   // PA2
#define BTN_DOWN_PIN    PIN_A3   // PA3
#define BTN_ENTER_PIN   PIN_A9   // PA9
#define BTN_PORT        GPIOA

// Display
#define LCD_RS_PIN      PIN_A7   // PA7
#define LCD_E_PIN       PIN_A5   // PA5
#define LCD_D4_PIN      PIN_A0   // PA0
#define LCD_D5_PIN      PIN_A1   // PA1
#define LCD_D6_PIN      PIN_A6   // PA6
#define LCD_D7_PIN      PIN_A4   // PA4

// Pin number for register macros (PB1 = 1)
#define DS18B20_PIN_NUM 1   

// Uncomment to drive buttons from the debugger (virtual_btn_* variables)
//#define DEBUG_BUTTONS

// Independent watchdog (~2 s). It is frozen while the debugger halts the core.
#define USE_WATCHDOG            1

// ==================== Timing constants ====================
#define SENSOR_FAIL_LIMIT       3       // failed reads in a row before the sensor error
#define MENU_TIMEOUT_MS         30000   // leave the menu after inactivity
#define SAVE_DELAY_MS           10000   // auto-save N ms after the last key press
#define DISPLAY_REFRESH_MS      100     // minimal display redraw interval
#define BUTTON_POLL_MS          5       // button polling period
#define KEY_ACCEL_REPEATS       10      // after N auto-repeats the edit step is multiplied by 10

// ==================== Thermostat state ====================
typedef struct {
    // ---- Parameters (stored in Flash, see flash_storage.c) ----
    // Basic
    float setpoint;
    float hysteresis;
    float calibration;

    // Operating modes
    uint8_t regulator_type;     // 0=ON/OFF, 1=PID
    uint8_t mode;               // 0=Heating, 1=Cooling
    uint8_t relay_logic;        // 0=NO, 1=NC
    uint8_t auto_save;
    uint8_t beep_enabled;
    uint8_t display_timeout;    // minutes, 0 = never
    uint8_t temp_units;         // 0=Celsius, 1=Fahrenheit

    // Safety
    float temp_min;
    float temp_max;
    uint8_t safety_enabled;

    // PID
    float kp;
    float ki;
    float kd;
    uint16_t pid_interval;      // ms
    uint16_t pwm_period;        // seconds
    float pid_output_limit;     // %

    // Advanced
    uint8_t temp_filter;        // 0 = off, 1 (strong) .. 10 (off)
    uint8_t temp_resolution;    // 9..12 bits
    uint8_t relay_delay;        // seconds between relay switches
    uint8_t cycle_protection;   // minimal relay OFF time, minutes

    // Schedule
    uint8_t schedule_enabled;
    float day_setpoint;
    float night_setpoint;
    uint8_t day_start_hour;
    uint8_t night_start_hour;

    // Two-point calibration: at the reference temperature T the sensor showed M
    float cal_point1_temp;
    float cal_point1_measured;
    float cal_point2_temp;
    float cal_point2_measured;

    // Display (reserved, not used)
    uint8_t display_contrast;
    uint8_t display_rotation;
    uint8_t display_metrics;

    // Power saving
    uint8_t power_save;         // sleep (WFI) between main loop iterations
    uint8_t update_interval;    // seconds between temperature measurements

    // Manual control and buttons
    uint8_t manual_mode;        // 0=Auto, 1=Manual On, 2=Manual Off
    uint8_t key_repeat;
    uint16_t key_repeat_delay;  // ms
    uint16_t key_repeat_rate;   // ms
    uint16_t debounce_time;     // ms

    // Statistics
    uint32_t total_runtime;     // minutes with relay ON
    uint32_t relay_cycles;      // number of relay ON switches

    // ---- Runtime variables (not stored) ----
    float current_temp;         // C, after calibration and filter
    uint8_t temp_valid;         // current_temp contains a real measurement
    uint8_t sensor_error;       // sensor lost (SENSOR_FAIL_LIMIT failures in a row)
    uint8_t sensor_fail_count;
    uint8_t safety_tripped;     // temperature is outside temp_min..temp_max

    uint8_t relay_state;        // logical state: 1 = load ON
    uint8_t relay_off_valid;    // relay_last_off holds a real switch-off time
    uint32_t relay_last_switch;
    uint32_t relay_last_off;

    uint8_t update_display;
    uint8_t params_changed;
    uint8_t display_on;
    uint32_t last_user_action;

    uint16_t clock_minutes;     // time of day for the schedule, 0..1439
    uint8_t current_hour;       // clock_minutes / 60 (editable in the menu)
    uint8_t confirm;            // Yes/No answer for dangerous menu actions

    // PID
    float integral;
    float previous_error;
    float pid_output;           // 0..100 %
    uint32_t last_pid_time;
    uint32_t pwm_cycle_start;
    uint32_t pwm_on_ms;

    // DS18B20
    uint8_t ds_pending;         // conversion in progress
    uint8_t ds_resolution;      // resolution currently written to the sensor, 0 = unknown
    uint32_t ds_start_time;
} thermostat_t;

extern thermostat_t thermo;
extern volatile uint32_t tick_count;

// ==================== Function prototypes ====================
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

// DS18B20
void ds18b20_init(void);
bool ds18b20_start_conversion(void);
bool ds18b20_read_raw(int16_t* raw);
bool ds18b20_set_resolution(uint8_t resolution);
uint16_t ds18b20_conversion_time_ms(uint8_t resolution);

#endif // MAIN_H
