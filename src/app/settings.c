#include "settings.h"
#include "flash.h"
#include "crc.h"
#include <stddef.h>
#include <string.h>

// Last 1 KB page of the 16 KB STM32F030F4 (reserved in Termostat.icf)
#ifndef SETTINGS_FLASH_ADDRESS
#define SETTINGS_FLASH_ADDRESS  0x08003C00UL
#endif

// The page is divided into equal slots. Every save writes the record into the next free slot,
// the page is erased only when all slots are used. This reduces Flash wear (STM32F030 guarantees
// only ~1000 erase cycles) by SLOT_COUNT times. The last record with a valid CRC is the current one.
#define SLOT_SIZE   (sizeof(settings_t))
#define SLOT_COUNT  (FLASH_PAGE_SIZE / SLOT_SIZE)

// Compile-time checks: no padding (fixed layout) and whole words
typedef char settings_size_check[(SLOT_SIZE == 80 && SLOT_SIZE % 4 == 0) ? 1 : -1];

static uintptr_t slot_address(uint32_t slot) {
    return (uintptr_t)SETTINGS_FLASH_ADDRESS + slot * SLOT_SIZE;
}

static uint32_t record_crc(const settings_t* s) {
    return crc32(s, offsetof(settings_t, crc));
}

static bool record_valid(const settings_t* s) {
    return s->magic == SETTINGS_MAGIC && s->version == SETTINGS_VERSION && s->crc == record_crc(s);
}

static bool slot_erased(uint32_t slot) {
    const uint32_t* data = (const uint32_t*)slot_address(slot);
    for (uint32_t i = 0; i < SLOT_SIZE / 4; i++) {
        if (data[i] != 0xFFFFFFFF) return false;
    }
    return true;
}

// Returns the slot of the latest valid record or -1
static int32_t find_last_record(void) {
    int32_t last = -1;
    for (uint32_t slot = 0; slot < SLOT_COUNT; slot++) {
        if (record_valid((const settings_t*)slot_address(slot))) last = (int32_t)slot;
    }
    return last;
}

bool settings_load(settings_t* s) {
    int32_t slot = find_last_record();
    if (slot < 0) return false;
    memcpy(s, (const void*)slot_address((uint32_t)slot), SLOT_SIZE);
    return true;
}

void settings_save(settings_t* s) {
    s->magic = SETTINGS_MAGIC;
    s->version = SETTINGS_VERSION;
    memset(s->reserved, 0, sizeof(s->reserved));
    s->crc = record_crc(s);

    int32_t last = find_last_record();
    if (last >= 0 && memcmp((const void*)slot_address((uint32_t)last), s, SLOT_SIZE) == 0) return;

    // Next free slot after the latest record. Slots with torn writes or programming errors
    // are skipped; the page is erased (at most once) only when no free slot is left, so the
    // latest valid record is never destroyed before the new one is written successfully.
    uint32_t slot = (last < 0) ? 0 : (uint32_t)last + 1;
    bool erased = false;
    for (;;) {
        while (slot < SLOT_COUNT && !slot_erased(slot)) slot++;
        if (slot >= SLOT_COUNT) {
            if (erased) return;             // Flash is worn out: give up
            flash_erase_page(SETTINGS_FLASH_ADDRESS);
            erased = true;
            slot = 0;
        }
        flash_program(slot_address(slot), s, SLOT_SIZE);
        if (memcmp((const void*)slot_address(slot), s, SLOT_SIZE) == 0) return;   // verified
        slot++;
    }
}

void settings_save_stats(uint32_t total_runtime, uint32_t relay_cycles) {
    settings_t s;
    if (!settings_load(&s)) return;
    s.total_runtime = total_runtime;
    s.relay_cycles = relay_cycles;
    settings_save(&s);
}

void settings_defaults(settings_t* s) {
    memset(s, 0, sizeof(*s));

    s->setpoint = 250;              // 25.0 C
    s->hysteresis = 10;             // 1.0 C
    s->calibration = 0;
    s->temp_min = 0;                // 0.0 C
    s->temp_max = 400;              // 40.0 C
    s->alarm_hyst = 10;             // 1.0 C
    s->day_setpoint = 220;
    s->night_setpoint = 180;
    s->cal_p1_ref = 0;              // identity calibration
    s->cal_p1_meas = 0;
    s->cal_p2_ref = 1000;
    s->cal_p2_meas = 1000;

    s->kp = 100;                    // 10.0 %/C
    s->ki = 20;                     // 0.020 %/(C*s), Ti = 500 s
    s->kd = 0;
    s->pwm_period = 10;

    s->key_repeat_delay = 500;
    s->key_repeat_rate = 200;
    s->debounce_time = 50;

    s->regulator_type = 0;          // ON/OFF
    s->mode = 0;                    // heating
    s->relay_logic = 0;             // NO
    s->manual_mode = 0;
    s->temp_units = 0;
    s->safety_enabled = 1;
    s->alarm_latch = 0;
    s->alarm_sound = 1;
    s->pid_output_limit = 100;
    s->temp_filter = 3;
    s->temp_resolution = 12;
    s->sensor_source = SRC_SENSOR1;
    s->relay_delay = 2;
    s->cycle_protection = 3;
    s->schedule_enabled = 0;
    s->day_start_hour = 7;
    s->night_start_hour = 23;
    s->auto_save = 1;
    s->beep_enabled = 1;
    s->power_save = 0;
    s->update_interval = 2;
    s->display_timeout = 10;
    s->key_repeat = 1;
}
