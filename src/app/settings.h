#ifndef SETTINGS_H
#define SETTINGS_H

// Persistent settings stored in the last Flash page.
// Temperatures are in 0.1 C, see regulator.h for the PID units.

#include <stdint.h>
#include <stdbool.h>

#define SETTINGS_MAGIC      0x5448      // "TH"
#define SETTINGS_VERSION    2           // bump when the layout below changes

// Sensor source (control temperature)
enum {
    SRC_SENSOR1 = 0,
    SRC_SENSOR2,
    SRC_SENSOR3,
    SRC_SENSOR4,
    SRC_AVERAGE,
    SRC_MINIMUM,
    SRC_MAXIMUM
};

// WARNING: the structure is written to Flash as is. It is laid out without padding;
// any change of fields requires SETTINGS_VERSION to be increased.
typedef struct {
    uint16_t magic;
    uint16_t version;

    // Temperatures, 0.1 C
    int16_t setpoint;
    int16_t hysteresis;
    int16_t calibration;        // offset added after the two-point calibration
    int16_t temp_min;           // alarm limits
    int16_t temp_max;
    int16_t alarm_hyst;         // alarm release hysteresis
    int16_t day_setpoint;
    int16_t night_setpoint;
    int16_t cal_p1_ref;         // two-point calibration: at reference ref the sensor showed meas
    int16_t cal_p1_meas;
    int16_t cal_p2_ref;
    int16_t cal_p2_meas;

    // PID
    uint16_t kp;                // 0.1 %/C
    uint16_t ki;                // 0.001 %/(C*s)
    uint16_t kd;                // %*s/C
    uint16_t pwm_period;        // s

    // Buttons, ms
    uint16_t key_repeat_delay;
    uint16_t key_repeat_rate;
    uint16_t debounce_time;

    // Modes
    uint8_t regulator_type;     // 0 = ON/OFF, 1 = PID
    uint8_t mode;               // 0 = heating, 1 = cooling
    uint8_t relay_logic;        // 0 = NO, 1 = NC
    uint8_t manual_mode;        // 0 = auto, 1 = manual on, 2 = manual off
    uint8_t temp_units;         // 0 = Celsius, 1 = Fahrenheit
    uint8_t safety_enabled;     // alarm limits enabled
    uint8_t alarm_latch;        // alarm must be acknowledged by a key press
    uint8_t alarm_sound;        // periodic beep while an alarm is active
    uint8_t pid_output_limit;   // %
    uint8_t temp_filter;        // 0 = off, 1 (strong) .. 10 (off)
    uint8_t temp_resolution;    // 9..12 bits
    uint8_t sensor_source;      // SRC_xxx
    uint8_t relay_delay;        // s, minimal time between relay switches
    uint8_t cycle_protection;   // min, minimal relay OFF time
    uint8_t schedule_enabled;
    uint8_t day_start_hour;
    uint8_t night_start_hour;
    uint8_t auto_save;
    uint8_t beep_enabled;       // key clicks and short signals
    uint8_t power_save;         // sleep between main loop iterations
    uint8_t update_interval;    // s, measurement period
    uint8_t display_timeout;    // min, 0 = never
    uint8_t key_repeat;
    uint8_t reserved[3];

    // Statistics
    uint32_t total_runtime;     // minutes with the relay ON
    uint32_t relay_cycles;      // number of relay ON switches

    uint32_t crc;               // CRC-32 of all previous bytes
} settings_t;

void settings_defaults(settings_t* s);

// Loads the latest valid record. Returns false if there is none.
bool settings_load(settings_t* s);

// Stores the record (magic, version and crc are filled in). Identical data is not written again.
void settings_save(settings_t* s);

// Updates only the statistics in the stored record (other unsaved changes stay unsaved)
void settings_save_stats(uint32_t total_runtime, uint32_t relay_cycles);

#endif // SETTINGS_H
