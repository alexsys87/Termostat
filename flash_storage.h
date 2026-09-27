#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include "stm32f0xx.h"
#include <stdint.h>

#define PARAMS_FLASH_ADDRESS 0x08003C00   // last 1 KB page of the 16 KB STM32F030F4
#define PARAMS_PAGE_SIZE     1024         // Flash page size of STM32F030x4/x6

// WARNING: the structure is stored in Flash as is. Do not change the order or types of fields,
// otherwise saved settings are reset to defaults (CRC mismatch).

typedef struct {
    // Basic
    float setpoint;
    float hysteresis;
    float calibration;

    // Modes
    uint8_t regulator_type;     // 0=ON/OFF, 1=PID
    uint8_t mode;               // 0=Heating, 1=Cooling
    uint8_t relay_logic;        // 0=NO, 1=NC
    uint8_t auto_save;          // 0=Off, 1=On
    uint8_t beep_enabled;
    uint8_t display_timeout;    // minutes
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
    float pid_output_limit;

    // Advanced
    uint8_t temp_filter;        // 0..10
    uint8_t temp_resolution;    // 9..12
    uint8_t relay_delay;        // seconds
    uint8_t cycle_protection;   // minutes

    // Schedule
    uint8_t schedule_enabled;
    float day_setpoint;
    float night_setpoint;
    uint8_t day_start_hour;
    uint8_t night_start_hour;

    // Two-point calibration
    float cal_point1_temp;
    float cal_point1_measured;
    float cal_point2_temp;
    float cal_point2_measured;

    // Display
    uint8_t display_contrast;
    uint8_t display_rotation;
    uint8_t display_metrics;

    // UART
    uint32_t uart_baudrate;
    uint8_t uart_echo;
    uint8_t uart_debug;

    // Power saving
    uint8_t power_save;
    uint8_t update_interval;    // seconds

    // Manual control and buttons
    uint8_t manual_mode;        // 0=Auto, 1=Manual On, 2=Manual Off
    uint8_t key_repeat;
    uint16_t key_repeat_delay;  // ms
    uint16_t key_repeat_rate;   // ms
    uint16_t debounce_time;     // ms

    // Statistics
    uint32_t total_runtime;     // minutes (relay ON time)
    uint32_t relay_cycles;

    uint32_t crc;
} thermostat_params_t;

void save_parameters(void);
void load_parameters(void);
void set_default_parameters(void);

#endif
