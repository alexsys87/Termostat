#include "flash_storage.h"
#include "main.h"   // для доступа к переменной thermo
#include <string.h>

void flash_unlock(void) {
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xCDEF89AB;
    }
}

void flash_lock(void) {
    FLASH->CR |= FLASH_CR_LOCK;
}

void flash_erase_page(uint32_t address) {
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = address;
    FLASH->CR |= FLASH_CR_STRT;
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR &= ~FLASH_CR_PER;
    FLASH->SR = FLASH_SR_EOP; // Сброс флага окончания операции
}

void flash_write_word(uint32_t address, uint32_t data) {
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR |= FLASH_CR_PG;
    
    // 1. Пишем младшие 16 бит (Специфика STM32F0)
    *(__IO uint16_t*)address = (uint16_t)data;
    while (FLASH->SR & FLASH_SR_BSY); 
    
    // 2. Пишем старшие 16 бит
    *(__IO uint16_t*)(address + 2) = (uint16_t)(data >> 16);
    while (FLASH->SR & FLASH_SR_BSY); 
    
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->SR = FLASH_SR_EOP; 
}

uint32_t flash_read_word(uint32_t address) {
    return *(__IO uint32_t*)address;
}

uint32_t calculate_crc(thermostat_params_t* params) {
    uint32_t crc = 0xFFFFFFFF;
    uint8_t* data = (uint8_t*)params;
    // CRC считается для всех байт до самого поля crc
    uint32_t size = sizeof(thermostat_params_t) - sizeof(uint32_t); 
    
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (uint32_t j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc = crc >> 1;
        }
    }
    return ~crc;
}

void save_parameters(void) {
    thermostat_params_t params;
    memset(&params, 0, sizeof(thermostat_params_t)); // Очистка мусора и padding байтов
    
    // Заполняем структуру
    params.setpoint = thermo.setpoint;
    params.hysteresis = thermo.hysteresis;
    params.calibration = thermo.calibration;
    params.regulator_type = thermo.regulator_type;
    params.mode = thermo.mode;
    params.relay_logic = thermo.relay_logic;
    params.auto_save = thermo.auto_save;
    params.beep_enabled = thermo.beep_enabled;
    params.display_timeout = thermo.display_timeout;
    params.temp_units = thermo.temp_units;
    params.temp_min = thermo.temp_min;
    params.temp_max = thermo.temp_max;
    params.safety_enabled = thermo.safety_enabled;
    params.kp = thermo.kp;
    params.ki = thermo.ki;
    params.kd = thermo.kd;
    params.pid_interval = thermo.pid_interval;
    params.pwm_period = thermo.pwm_period;
    params.pid_output_limit = thermo.pid_output_limit;
    params.temp_filter = thermo.temp_filter;
    params.temp_resolution = thermo.temp_resolution;
    params.relay_delay = thermo.relay_delay;
    params.cycle_protection = thermo.cycle_protection;
    params.schedule_enabled = thermo.schedule_enabled;
    params.day_setpoint = thermo.day_setpoint;
    params.night_setpoint = thermo.night_setpoint;
    params.day_start_hour = thermo.day_start_hour;
    params.night_start_hour = thermo.night_start_hour;
    params.cal_point1_temp = thermo.cal_point1_temp;
    params.cal_point1_measured = thermo.cal_point1_measured;
    params.cal_point2_temp = thermo.cal_point2_temp;
    params.cal_point2_measured = thermo.cal_point2_measured;
    params.display_contrast = thermo.display_contrast;
    params.display_rotation = thermo.display_rotation;
    params.display_metrics = thermo.display_metrics;
    params.power_save = thermo.power_save;
    params.update_interval = thermo.update_interval;
    params.manual_mode = thermo.manual_mode;
    params.key_repeat = thermo.key_repeat;
    params.key_repeat_delay = thermo.key_repeat_delay;
    params.key_repeat_rate = thermo.key_repeat_rate;
    params.debounce_time = thermo.debounce_time;
    params.total_runtime = thermo.total_runtime;
    params.relay_cycles = thermo.relay_cycles;

    params.crc = calculate_crc(&params);

    // Безопасное количество слов (округление вверх, если размер не кратен 4)
    uint32_t words_to_write = (sizeof(thermostat_params_t) + 3) / 4;
    uint32_t* data_ptr = (uint32_t*)&params;

    // Критическая секция: запрещаем прерывания на всё время работы с Flash
    __disable_irq(); 
    
    flash_unlock();
    flash_erase_page(PARAMS_FLASH_ADDRESS);
    
    for (uint32_t i = 0; i < words_to_write; i++) {
        flash_write_word(PARAMS_FLASH_ADDRESS + i * 4, data_ptr[i]);
    }
    
    flash_lock();
    
    __enable_irq(); // Разрешаем прерывания
}

void load_parameters(void) {
    thermostat_params_t params;
    uint32_t* data_ptr = (uint32_t*)&params;
    
    uint32_t words_to_read = (sizeof(thermostat_params_t) + 3) / 4;

    for (uint32_t i = 0; i < words_to_read; i++) {
        data_ptr[i] = flash_read_word(PARAMS_FLASH_ADDRESS + i * 4);
    }

    if (params.crc == calculate_crc(&params)) {
        // Загружаем данные
        thermo.setpoint = params.setpoint;
        thermo.hysteresis = params.hysteresis;
        thermo.calibration = params.calibration;
        thermo.regulator_type = params.regulator_type;
        thermo.mode = params.mode;
        thermo.relay_logic = params.relay_logic;
        thermo.auto_save = params.auto_save;
        thermo.beep_enabled = params.beep_enabled;
        thermo.display_timeout = params.display_timeout;
        thermo.temp_units = params.temp_units;
        thermo.temp_min = params.temp_min;
        thermo.temp_max = params.temp_max;
        thermo.safety_enabled = params.safety_enabled;
        thermo.kp = params.kp;
        thermo.ki = params.ki;
        thermo.kd = params.kd;
        thermo.pid_interval = params.pid_interval;
        thermo.pwm_period = params.pwm_period;
        thermo.pid_output_limit = params.pid_output_limit;
        thermo.temp_filter = params.temp_filter;
        thermo.temp_resolution = params.temp_resolution;
        thermo.relay_delay = params.relay_delay;
        thermo.cycle_protection = params.cycle_protection;
        thermo.schedule_enabled = params.schedule_enabled;
        thermo.day_setpoint = params.day_setpoint;
        thermo.night_setpoint = params.night_setpoint;
        thermo.day_start_hour = params.day_start_hour;
        thermo.night_start_hour = params.night_start_hour;
        thermo.cal_point1_temp = params.cal_point1_temp;
        thermo.cal_point1_measured = params.cal_point1_measured;
        thermo.cal_point2_temp = params.cal_point2_temp;
        thermo.cal_point2_measured = params.cal_point2_measured;
        thermo.display_contrast = params.display_contrast;
        thermo.display_rotation = params.display_rotation;
        thermo.display_metrics = params.display_metrics;
        thermo.power_save = params.power_save;
        thermo.update_interval = params.update_interval;
        thermo.manual_mode = params.manual_mode;
        thermo.key_repeat = params.key_repeat;
        thermo.key_repeat_delay = params.key_repeat_delay;
        thermo.key_repeat_rate = params.key_repeat_rate;
        thermo.debounce_time = params.debounce_time;
        thermo.total_runtime = params.total_runtime;
        thermo.relay_cycles = params.relay_cycles;
    } else {
        // Если флеш пустая или CRC не совпадает (первый запуск)
        set_default_parameters();
        save_parameters();
    }
}

void set_default_parameters(void) {
    thermo.setpoint = 25.0f;
    thermo.hysteresis = 1.0f;
    thermo.calibration = 0.0f;
    thermo.regulator_type = 0;         // ON/OFF
    thermo.mode = 0;                   // Heating
    thermo.relay_logic = 0;            // NO
    thermo.auto_save = 1;
    thermo.beep_enabled = 1;
    thermo.display_timeout = 10;
    thermo.temp_units = 0;
    thermo.temp_min = 0.0f;
    thermo.temp_max = 40.0f;
    thermo.safety_enabled = 1;
    thermo.kp = 2.0f;
    thermo.ki = 0.1f;
    thermo.kd = 0.5f;
    thermo.pid_interval = 1000;
    thermo.pwm_period = 10;
    thermo.pid_output_limit = 100.0f;
    thermo.temp_filter = 3;
    thermo.temp_resolution = 12;
    thermo.relay_delay = 2;
    thermo.cycle_protection = 3;
    thermo.schedule_enabled = 0;
    thermo.day_setpoint = 22.0f;
    thermo.night_setpoint = 18.0f;
    thermo.day_start_hour = 7;
    thermo.night_start_hour = 23;
    thermo.cal_point1_temp = 0.0f;
    thermo.cal_point1_measured = 0.0f;
    thermo.cal_point2_temp = 100.0f;
    thermo.cal_point2_measured = 100.0f;
    thermo.display_contrast = 50;
    thermo.display_rotation = 0;
    thermo.display_metrics = 1;
    thermo.power_save = 0;
    thermo.update_interval = 2;
    thermo.manual_mode = 0;
    thermo.key_repeat = 1;
    thermo.key_repeat_delay = 500;
    thermo.key_repeat_rate = 200;
    thermo.debounce_time = 50;
    thermo.total_runtime = 0;
    thermo.relay_cycles = 0;
}