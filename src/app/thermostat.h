#ifndef THERMOSTAT_H
#define THERMOSTAT_H

#include <stdint.h>
#include <stdbool.h>
#include "settings.h"
#include "regulator.h"
#include "ds18b20.h"

// ==================== Configuration ====================
#define MAX_SENSORS             4       // DS18B20 on the 1-Wire bus
#define SENSOR_FAIL_LIMIT       3       // failed reads in a row before a sensor is considered lost
#define ALARM_BEEP_PERIOD_MS    5000    // alarm sound repetition
#define ALARM_BEEP_MS           300
#define STATS_SAVE_PERIOD_MIN   (24 * 60)  // statistics are stored once a day

// ==================== State ====================
typedef struct {
    uint8_t rom[DS18B20_ROM_SIZE];
    int16_t temp;               // 0.1 C after calibration, TEMP_INVALID if lost
    uint8_t fail_count;
} sensor_t;

typedef struct {
    settings_t cfg;             // persistent settings (see settings.h)

    // Measurement
    int16_t temp;               // control temperature, 0.1 C (filtered)
    uint8_t temp_valid;
    uint8_t sensor_error;       // no control temperature
    uint8_t sensor_count;
    uint8_t rescan_request;
    sensor_t sensors[MAX_SENSORS];
    int16_t day_min;            // extremes since midnight (or since the statistics were cleared)
    int16_t day_max;

    // Relay
    uint8_t relay_state;        // logical state: 1 = load ON
    uint8_t relay_off_valid;    // relay_last_off holds a real switch-off time
    uint32_t relay_last_switch;
    uint32_t relay_last_off;

    // Alarm
    uint8_t alarm_limit;        // temperature outside the limits (with release hysteresis)
    uint8_t alarm_latched;      // waits for acknowledgement
    uint8_t alarm_muted;        // alarm sound silenced by a key press

    // PID
    int32_t pid_output;         // 0.01 %
    uint32_t pwm_cycle_start;
    uint32_t pwm_on_ms;

    // Auto-tuning
    autotune_t tune;

    // Clock for the schedule (no RTC on this board: runs from the internal oscillator)
    uint16_t clock_minutes;     // 0..1439

    // Flags for the user interface
    uint8_t update_display;
    uint8_t params_changed;
} thermostat_t;

extern thermostat_t thermo;

void thermostat_init(void);             // loads settings, no hardware access
void thermostat_start(void);            // hardware is ready: sensors, relay
void thermostat_task(void);             // call from the main loop

void beep(uint16_t duration_ms);        // key click / short signal (respects beep_enabled)

bool thermostat_is_day(void);           // schedule: day period active
int16_t thermostat_active_setpoint(void);
bool thermostat_alarm_active(void);
bool thermostat_acknowledge(void);      // key press on the main screen: mute / acknowledge, true if consumed
void thermostat_mute(void);
void thermostat_rescan(void);
bool thermostat_tune_start(void);
void thermostat_tune_stop(void);
void thermostat_clear_stats(void);
void thermostat_save(void);             // store settings now

#endif // THERMOSTAT_H
