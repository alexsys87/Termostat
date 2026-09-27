#ifndef MAIN_H
#define MAIN_H

#include "stm32f0xx.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

// ==================== Определения пинов ====================
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
#define PIN_A13  ((uint16_t)0x2000) // SWDIO (Не использовать!)
#define PIN_A14  ((uint16_t)0x4000) // SWCLK (Не использовать!)

#define PIN_B1   ((uint16_t)0x0002) // Единственный пин PB на STM32F030F4

#define PIN_F0   ((uint16_t)0x0001)
#define PIN_F1   ((uint16_t)0x0002)

// ==================== Назначение пинов (Строго под 20-pin) ====================
// Датчик и исполнительные устройства
#define DS18B20_PIN     PIN_B1   // PB1
#define DS18B20_PORT    GPIOB
#define RELAY_PIN       PIN_F0   // PF0
#define RELAY_PORT      GPIOF
#define BEEPER_PIN      PIN_F1   // PF1
#define BEEPER_PORT     GPIOF

// Кнопки перенесены на свободные PA2, PA3, PA9
#define BTN_UP_PIN      PIN_A2   // PA2
#define BTN_DOWN_PIN    PIN_A3   // PA3
#define BTN_ENTER_PIN   PIN_A9   // PA9
#define BTN_PORT        GPIOA

// Дисплей 
#define LCD_RS_PIN      PIN_A7   // PA7
#define LCD_E_PIN       PIN_A5   // PA5
#define LCD_D4_PIN      PIN_A0   // PA0
#define LCD_D5_PIN      PIN_A1   // PA1
#define LCD_D6_PIN      PIN_A6   // PA6
#define LCD_D7_PIN      PIN_A4   // PA4

// Номер пина для макросов (PB1 = 1)
#define DS18B20_PIN_NUM 1   

// Раскомментируйте для включения режима отладки кнопок
//#define DEBUG_BUTTONS

// ==================== Структура термостата ====================
typedef struct {
    // Основные параметры
    float current_temp;
    float setpoint;
    float hysteresis;
    float calibration;
    uint8_t relay_state;
    uint8_t update_display;
    uint8_t params_changed;

    // Режимы работы
    uint8_t regulator_type;
    uint8_t mode;
    uint8_t relay_logic;
    uint8_t auto_save;
    uint8_t beep_enabled;
    uint8_t display_timeout;
    uint8_t temp_units;

    // Безопасность
    float temp_min;
    float temp_max;
    uint8_t safety_enabled;

    // PID
    float kp;
    float ki;
    float kd;
    uint16_t pid_interval;
    uint16_t pwm_period;
    float pid_output_limit;
    uint8_t pid_auto_tune;

    // Дополнительные
    uint8_t temp_filter;
    uint8_t temp_resolution;
    uint8_t relay_delay;
    uint8_t cycle_protection;

    // Расписание
    uint8_t schedule_enabled;
    float day_setpoint;
    float night_setpoint;
    uint8_t day_start_hour;
    uint8_t night_start_hour;

    // Калибровка
    float cal_point1_temp;
    float cal_point1_measured;
    float cal_point2_temp;
    float cal_point2_measured;

    // Дисплей
    uint8_t display_contrast;
    uint8_t display_rotation;
    uint8_t display_metrics;

    // Энергосбережение
    uint8_t power_save;
    uint8_t update_interval;

    // Ручное управление и кнопки
    uint8_t manual_mode;
    uint8_t key_repeat;
    uint16_t key_repeat_delay;
    uint16_t key_repeat_rate;
    uint16_t debounce_time;

    // Статистика
    uint32_t total_runtime;
    uint32_t relay_cycles;

    // Внутренние переменные
    uint32_t last_user_action;
    uint8_t display_on;
    uint32_t relay_last_switch;
    uint32_t stats_timer;
    float filtered_temp;
    uint8_t current_hour;

    // PID переменные
    float integral;
    float previous_error;
    uint32_t last_pid_time;
    uint32_t pwm_cycle_start;
    uint16_t pwm_on_time;

    // Состояния кнопок
    uint32_t btn_up_last_press;
    uint32_t btn_down_last_press;
    uint32_t btn_enter_last_press;
    uint32_t btn_up_last_repeat;
    uint32_t btn_down_last_repeat;
    uint32_t btn_enter_last_repeat;
    uint8_t btn_up_state;
    uint8_t btn_down_state;
    uint8_t btn_enter_state;
    uint8_t btn_up_debounced;
    uint8_t btn_down_debounced;
    uint8_t btn_enter_debounced;

    uint32_t uptime_minutes;   
    
    // DS18B20 
    uint8_t ds_pending;        
    uint32_t ds_start_time;    
    float ds_last_temp;        
} thermostat_t;

extern thermostat_t thermo;
extern volatile uint32_t tick_count;

// ==================== Прототипы функций ====================
void system_clock_init(void);
void gpio_init(void);
void systick_init(void);
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

// Прототипы DS18B20
void ds18b20_init(void);
void set_ds18b20_resolution(uint8_t res);
bool ds18b20_start_conversion(void);
bool ds18b20_is_conversion_done(void);
float ds18b20_read_result(void);

// Остальные функции
float apply_temp_filter(float new_temp);
void update_thermostat(void);
void update_onoff_thermostat(void);
void update_pid_thermostat(void);
float compute_pid(float setpoint, float current);
void handle_schedule(void);
void update_main_display(void);
void handle_buttons_with_debounce(void);
void beep(uint16_t duration_ms);
void convert_temperature(float* temp);
void handle_display_timeout(void);
void update_statistics(void);
void apply_calibration(float* temp);
bool check_safety_limits(void);
void save_if_changed(void);
void update_menu_display(void);

#endif // MAIN_H