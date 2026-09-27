#include "flash_storage.h"
#include "main.h"   // access to the thermo variable
#include <string.h>

// The page is divided into equal slots. Every save writes the record into the next free slot,
// the page is erased only when all slots are used. This reduces Flash wear (STM32F030 guarantees
// only ~1000 erase cycles) by PARAMS_SLOT_COUNT times.
// The last record with a valid CRC is the current one.
#define PARAMS_SLOT_SIZE   (sizeof(thermostat_params_t))
#define PARAMS_SLOT_COUNT  (PARAMS_PAGE_SIZE / PARAMS_SLOT_SIZE)
#define PARAMS_WORD_COUNT  (PARAMS_SLOT_SIZE / 4)

// Record size must be a multiple of 4 (written by words) - guaranteed by the uint32_t crc field
typedef char params_size_check[(PARAMS_SLOT_SIZE % 4 == 0 && PARAMS_SLOT_COUNT >= 1) ? 1 : -1];

static uint32_t slot_address(uint32_t slot) {
    return PARAMS_FLASH_ADDRESS + slot * PARAMS_SLOT_SIZE;
}

static void flash_unlock(void) {
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xCDEF89AB;
    }
}

static void flash_lock(void) {
    FLASH->CR |= FLASH_CR_LOCK;
}

static void flash_erase_page(uint32_t address) {
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPERR; // Clear stale flags
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = address;
    FLASH->CR |= FLASH_CR_STRT;
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->CR &= ~FLASH_CR_PER;
    FLASH->SR = FLASH_SR_EOP;
}

static void flash_write_block(uint32_t address, const uint32_t* data, uint32_t words) {
    // STM32F0 programs Flash by half-words (16 bit)
    const uint16_t* half = (const uint16_t*)data;
    while (FLASH->SR & FLASH_SR_BSY);
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPERR;
    FLASH->CR |= FLASH_CR_PG;
    for (uint32_t i = 0; i < words * 2; i++) {
        *(__IO uint16_t*)(address + i * 2) = half[i];
        while (FLASH->SR & FLASH_SR_BSY);
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->SR = FLASH_SR_EOP;
}

static bool slot_is_erased(uint32_t slot) {
    const uint32_t* data = (const uint32_t*)slot_address(slot);
    for (uint32_t i = 0; i < PARAMS_WORD_COUNT; i++) {
        if (data[i] != 0xFFFFFFFF) return false;
    }
    return true;
}

static uint32_t calculate_crc(const thermostat_params_t* params) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* data = (const uint8_t*)params;
    // CRC covers all bytes before the crc field
    uint32_t size = offsetof(thermostat_params_t, crc);

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

// Finds the last valid record. Returns the slot number or -1 if there is none.
static int32_t find_last_record(thermostat_params_t* out) {
    int32_t last = -1;
    for (uint32_t slot = 0; slot < PARAMS_SLOT_COUNT; slot++) {
        const thermostat_params_t* rec = (const thermostat_params_t*)slot_address(slot);
        if (rec->crc == calculate_crc(rec)) {
            last = (int32_t)slot;
        }
    }
    if (last >= 0) {
        memcpy(out, (const void*)slot_address((uint32_t)last), PARAMS_SLOT_SIZE);
    }
    return last;
}

static void params_from_thermo(thermostat_params_t* p) {
    memset(p, 0, sizeof(thermostat_params_t)); // Clear padding bytes (important for CRC and comparison)

    p->setpoint = thermo.setpoint;
    p->hysteresis = thermo.hysteresis;
    p->calibration = thermo.calibration;
    p->regulator_type = thermo.regulator_type;
    p->mode = thermo.mode;
    p->relay_logic = thermo.relay_logic;
    p->auto_save = thermo.auto_save;
    p->beep_enabled = thermo.beep_enabled;
    p->display_timeout = thermo.display_timeout;
    p->temp_units = thermo.temp_units;
    p->temp_min = thermo.temp_min;
    p->temp_max = thermo.temp_max;
    p->safety_enabled = thermo.safety_enabled;
    p->kp = thermo.kp;
    p->ki = thermo.ki;
    p->kd = thermo.kd;
    p->pid_interval = thermo.pid_interval;
    p->pwm_period = thermo.pwm_period;
    p->pid_output_limit = thermo.pid_output_limit;
    p->temp_filter = thermo.temp_filter;
    p->temp_resolution = thermo.temp_resolution;
    p->relay_delay = thermo.relay_delay;
    p->cycle_protection = thermo.cycle_protection;
    p->schedule_enabled = thermo.schedule_enabled;
    p->day_setpoint = thermo.day_setpoint;
    p->night_setpoint = thermo.night_setpoint;
    p->day_start_hour = thermo.day_start_hour;
    p->night_start_hour = thermo.night_start_hour;
    p->cal_point1_temp = thermo.cal_point1_temp;
    p->cal_point1_measured = thermo.cal_point1_measured;
    p->cal_point2_temp = thermo.cal_point2_temp;
    p->cal_point2_measured = thermo.cal_point2_measured;
    p->display_contrast = thermo.display_contrast;
    p->display_rotation = thermo.display_rotation;
    p->display_metrics = thermo.display_metrics;
    p->power_save = thermo.power_save;
    p->update_interval = thermo.update_interval;
    p->manual_mode = thermo.manual_mode;
    p->key_repeat = thermo.key_repeat;
    p->key_repeat_delay = thermo.key_repeat_delay;
    p->key_repeat_rate = thermo.key_repeat_rate;
    p->debounce_time = thermo.debounce_time;
    p->total_runtime = thermo.total_runtime;
    p->relay_cycles = thermo.relay_cycles;
}

static void params_to_thermo(const thermostat_params_t* p) {
    thermo.setpoint = p->setpoint;
    thermo.hysteresis = p->hysteresis;
    thermo.calibration = p->calibration;
    thermo.regulator_type = p->regulator_type;
    thermo.mode = p->mode;
    thermo.relay_logic = p->relay_logic;
    thermo.auto_save = p->auto_save;
    thermo.beep_enabled = p->beep_enabled;
    thermo.display_timeout = p->display_timeout;
    thermo.temp_units = p->temp_units;
    thermo.temp_min = p->temp_min;
    thermo.temp_max = p->temp_max;
    thermo.safety_enabled = p->safety_enabled;
    thermo.kp = p->kp;
    thermo.ki = p->ki;
    thermo.kd = p->kd;
    thermo.pid_interval = p->pid_interval;
    thermo.pwm_period = p->pwm_period;
    thermo.pid_output_limit = p->pid_output_limit;
    thermo.temp_filter = p->temp_filter;
    thermo.temp_resolution = p->temp_resolution;
    thermo.relay_delay = p->relay_delay;
    thermo.cycle_protection = p->cycle_protection;
    thermo.schedule_enabled = p->schedule_enabled;
    thermo.day_setpoint = p->day_setpoint;
    thermo.night_setpoint = p->night_setpoint;
    thermo.day_start_hour = p->day_start_hour;
    thermo.night_start_hour = p->night_start_hour;
    thermo.cal_point1_temp = p->cal_point1_temp;
    thermo.cal_point1_measured = p->cal_point1_measured;
    thermo.cal_point2_temp = p->cal_point2_temp;
    thermo.cal_point2_measured = p->cal_point2_measured;
    thermo.display_contrast = p->display_contrast;
    thermo.display_rotation = p->display_rotation;
    thermo.display_metrics = p->display_metrics;
    thermo.power_save = p->power_save;
    thermo.update_interval = p->update_interval;
    thermo.manual_mode = p->manual_mode;
    thermo.key_repeat = p->key_repeat;
    thermo.key_repeat_delay = p->key_repeat_delay;
    thermo.key_repeat_rate = p->key_repeat_rate;
    thermo.debounce_time = p->debounce_time;
    thermo.total_runtime = p->total_runtime;
    thermo.relay_cycles = p->relay_cycles;
}

void save_parameters(void) {
    thermostat_params_t params;
    thermostat_params_t last;

    params_from_thermo(&params);
    params.crc = calculate_crc(&params);

    // Data equals the last record: do not touch Flash
    int32_t last_slot = find_last_record(&last);
    if (last_slot >= 0 && memcmp(&last, &params, sizeof(params)) == 0) return;

    // Next free slot after the last record
    uint32_t slot = (last_slot < 0) ? 0 : (uint32_t)last_slot + 1;
    while (slot < PARAMS_SLOT_COUNT && !slot_is_erased(slot)) slot++;

    // Critical section: no interrupts while Flash is being modified
    __disable_irq();
    flash_unlock();
    if (slot >= PARAMS_SLOT_COUNT) {
        flash_erase_page(PARAMS_FLASH_ADDRESS);
        slot = 0;
    }
    flash_write_block(slot_address(slot), (const uint32_t*)&params, PARAMS_WORD_COUNT);

    // Verify: on error erase the page and write the record into the first slot
    if (memcmp((const void*)slot_address(slot), &params, sizeof(params)) != 0) {
        flash_erase_page(PARAMS_FLASH_ADDRESS);
        flash_write_block(slot_address(0), (const uint32_t*)&params, PARAMS_WORD_COUNT);
    }
    flash_lock();
    __enable_irq();
}

void load_parameters(void) {
    thermostat_params_t params;

    if (find_last_record(&params) >= 0) {
        params_to_thermo(&params);
    } else {
        // Flash is empty or corrupted (first start)
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
