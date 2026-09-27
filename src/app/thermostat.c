#include "thermostat.h"
#include "board.h"
#include <string.h>

thermostat_t thermo;

static temp_filter_t filter;
static pid_state_t pid;

// DS18B20 measurement cycle
static uint8_t ds_pending;              // conversion in progress
static uint8_t ds_resolution;           // resolution written to the sensors, 0 = unknown
static uint32_t ds_start_time;
static uint8_t cycle_fail_count;        // cycles without any sensor answering
static uint32_t last_measure_time;
static uint8_t has_measure_time;

// Statistics saving
static uint32_t saved_runtime;
static uint32_t saved_cycles;
static uint16_t stats_minutes;

static uint32_t alarm_beep_timer;

// ==================== Helpers ====================

void beep(uint16_t duration_ms) {
    if (thermo.cfg.beep_enabled) board_beeper_start(duration_ms);
}

static int16_t clamp_temp(int32_t t) {
    if (t > 2000) return 2000;
    if (t < -1000) return -1000;
    return (int16_t)t;
}

bool thermostat_alarm_active(void) {
    return thermo.sensor_error || thermo.alarm_limit || thermo.alarm_latched;
}

// ==================== Initialization ====================

void thermostat_init(void) {
    memset(&thermo, 0, sizeof(thermo));
    if (!settings_load(&thermo.cfg)) {
        // Flash is empty, corrupted or holds an older layout
        settings_defaults(&thermo.cfg);
        settings_save(&thermo.cfg);
    }

    thermo.temp = TEMP_INVALID;
    thermo.day_min = TEMP_INVALID;
    thermo.day_max = TEMP_INVALID;
    for (uint8_t i = 0; i < MAX_SENSORS; i++) thermo.sensors[i].temp = TEMP_INVALID;
    thermo.rescan_request = 1;
    thermo.update_display = 1;

    saved_runtime = thermo.cfg.total_runtime;
    saved_cycles = thermo.cfg.relay_cycles;
}

static void relay_output(void) {
    // Physical level depends on the relay logic (NO/NC)
    board_relay_write((thermo.relay_state != 0) != (thermo.cfg.relay_logic != 0));
}

void thermostat_start(void) {
    thermo.relay_state = 0;
    thermo.relay_last_switch = tick_count;
    relay_output();
    pid_reset(&pid);
}

// ==================== Relay ====================

static void relay_set(uint8_t on) {
    if (on != thermo.relay_state) {
        thermo.relay_state = on;
        thermo.relay_last_switch = tick_count;
        if (on) {
            thermo.cfg.relay_cycles++;
        } else {
            thermo.relay_last_off = tick_count;
            thermo.relay_off_valid = 1;
        }
        thermo.update_display = 1;
    }
    // The pin is refreshed on every call: this also applies a changed NO/NC setting at once
    relay_output();
}

// ==================== Temperature sensors (non-blocking) ====================

static void sensor_error_set(void) {
    if (thermo.sensor_error) return;
    thermo.sensor_error = 1;
    thermo.temp_valid = 0;          // control_task switches the load off
    thermo.temp = TEMP_INVALID;
    thermo.alarm_muted = 0;         // new alarm event
    temp_filter_reset(&filter);
    pid_reset(&pid);
    has_measure_time = 0;
    ds_resolution = 0;              // re-apply the resolution when the sensor is back
    thermo.update_display = 1;
}

// A measurement cycle could not be started (no sensors, bus problem)
static void cycle_failed(void) {
    if (cycle_fail_count < 255) cycle_fail_count++;
    if (cycle_fail_count >= SENSOR_FAIL_LIMIT) sensor_error_set();
}

// Temperature used for regulation according to the "Src" setting
static int16_t control_temperature(void) {
    uint8_t src = thermo.cfg.sensor_source;
    if (src <= SRC_SENSOR4) {
        return (src < thermo.sensor_count) ? thermo.sensors[src].temp : TEMP_INVALID;
    }

    int32_t sum = 0;
    int16_t t_min = INT16_MAX, t_max = -INT16_MAX;
    uint8_t n = 0;
    for (uint8_t i = 0; i < thermo.sensor_count; i++) {
        int16_t t = thermo.sensors[i].temp;
        if (t == TEMP_INVALID) continue;
        sum += t;
        n++;
        if (t < t_min) t_min = t;
        if (t > t_max) t_max = t;
    }
    if (n == 0) return TEMP_INVALID;
    if (src == SRC_AVERAGE) return (int16_t)div_round(sum, n);
    return (src == SRC_MINIMUM) ? t_min : t_max;
}

static void read_sensors(void) {
    const settings_t* c = &thermo.cfg;
    for (uint8_t i = 0; i < thermo.sensor_count; i++) {
        sensor_t* s = &thermo.sensors[i];
        int16_t raw;
        if (ds18b20_read_raw(s->rom, &raw)) {
            int16_t t = temp_calibrate(temp_from_ds18b20(raw), c->cal_p1_ref, c->cal_p1_meas,
                                       c->cal_p2_ref, c->cal_p2_meas);
            s->temp = clamp_temp((int32_t)t + c->calibration);
            s->fail_count = 0;
        } else if (++s->fail_count >= SENSOR_FAIL_LIMIT) {
            s->fail_count = SENSOR_FAIL_LIMIT;
            s->temp = TEMP_INVALID;     // keep the last value until the limit is reached
        }
    }
}

static bool all_sensors_lost(void) {
    for (uint8_t i = 0; i < thermo.sensor_count; i++) {
        if (thermo.sensors[i].temp != TEMP_INVALID || thermo.sensors[i].fail_count < SENSOR_FAIL_LIMIT) return false;
    }
    return true;
}

static void scan_sensors(void) {
    uint8_t roms[MAX_SENSORS][DS18B20_ROM_SIZE];
    uint8_t count = ds18b20_search(roms, MAX_SENSORS);

    for (uint8_t i = 0; i < count; i++) {
        sensor_t* s = &thermo.sensors[i];
        if (i >= thermo.sensor_count || memcmp(s->rom, roms[i], DS18B20_ROM_SIZE) != 0) {
            memcpy(s->rom, roms[i], DS18B20_ROM_SIZE);
            s->temp = TEMP_INVALID;     // another sensor in this position
        }
        s->fail_count = 0;
    }
    for (uint8_t i = count; i < MAX_SENSORS; i++) thermo.sensors[i].temp = TEMP_INVALID;
    thermo.sensor_count = count;
    ds_resolution = 0;                  // new sensors need the resolution
    thermo.update_display = 1;
}

// Runs the measurement cycle. Returns true when a new control temperature is available,
// *dt_ms is the time since the previous one.
static bool sensor_task(uint32_t* dt_ms) {
    static uint32_t start_timer;
    static uint8_t started = 0;

    if (ds_pending) {
        if (tick_count - ds_start_time < ds18b20_conversion_time_ms(ds_resolution)) return false;
        ds_pending = 0;

        read_sensors();
        int16_t t = control_temperature();
        if (t == TEMP_INVALID) {
            sensor_error_set();
            return false;
        }

        cycle_fail_count = 0;
        thermo.sensor_error = 0;
        thermo.temp = temp_filter_apply(&filter, t, thermo.cfg.temp_filter);
        thermo.temp_valid = 1;
        if (thermo.day_min == TEMP_INVALID || thermo.temp < thermo.day_min) thermo.day_min = thermo.temp;
        if (thermo.day_max == TEMP_INVALID || thermo.temp > thermo.day_max) thermo.day_max = thermo.temp;
        thermo.update_display = 1;

        *dt_ms = has_measure_time ? tick_count - last_measure_time : (uint32_t)thermo.cfg.update_interval * 1000;
        last_measure_time = tick_count;
        has_measure_time = 1;
        return true;
    }

    if (started && tick_count - start_timer < (uint32_t)thermo.cfg.update_interval * 1000) return false;
    started = 1;
    start_timer = tick_count;

    // Search the bus at start-up, on request and when every sensor is lost (reconnected cable)
    if (thermo.rescan_request || thermo.sensor_count == 0 || all_sensors_lost()) {
        thermo.rescan_request = 0;
        scan_sensors();
    }
    if (thermo.sensor_count == 0) {
        cycle_failed();
        return false;
    }

    // Apply a new resolution (start-up, menu change, sensor reconnection)
    if (ds_resolution != thermo.cfg.temp_resolution) {
        if (!ds18b20_set_resolution(thermo.cfg.temp_resolution)) {
            cycle_failed();
            return false;
        }
        ds_resolution = thermo.cfg.temp_resolution;
    }

    if (ds18b20_start_conversion()) {
        ds_pending = 1;
        ds_start_time = tick_count;
    } else {
        cycle_failed();
    }
    return false;
}

void thermostat_rescan(void) {
    thermo.rescan_request = 1;
}

// ==================== Alarm ====================

static void alarm_update(void) {
    const settings_t* c = &thermo.cfg;
    uint8_t limit = thermo.alarm_limit;

    if (!c->safety_enabled || !thermo.temp_valid) {
        limit = 0;                      // sensor faults are handled as sensor_error
    } else if (!limit) {
        limit = thermo.temp < c->temp_min || thermo.temp > c->temp_max;
    } else {
        // Release only when the temperature is back inside the limits by alarm_hyst
        limit = thermo.temp < c->temp_min + c->alarm_hyst || thermo.temp > c->temp_max - c->alarm_hyst;
    }

    if (limit && !thermo.alarm_limit) {
        thermo.alarm_muted = 0;         // new alarm event
        if (c->alarm_latch) thermo.alarm_latched = 1;
    }
    if (!c->safety_enabled || !c->alarm_latch) thermo.alarm_latched = 0;
    if (limit != thermo.alarm_limit) {
        thermo.alarm_limit = limit;
        thermo.update_display = 1;
    }
}

static void alarm_sound_task(void) {
    if (!thermostat_alarm_active() || !thermo.cfg.alarm_sound || thermo.alarm_muted) {
        alarm_beep_timer = tick_count - ALARM_BEEP_PERIOD_MS;   // beep at once when an alarm starts
        return;
    }
    if (tick_count - alarm_beep_timer >= ALARM_BEEP_PERIOD_MS) {
        alarm_beep_timer = tick_count;
        board_beeper_start(ALARM_BEEP_MS);  // independent of beep_enabled
    }
}

void thermostat_mute(void) {
    if (thermostat_alarm_active()) thermo.alarm_muted = 1;
}

bool thermostat_acknowledge(void) {
    bool consumed = false;

    if (thermostat_alarm_active() && thermo.cfg.alarm_sound && !thermo.alarm_muted) {
        thermo.alarm_muted = 1;
        consumed = true;
    }
    if (thermo.alarm_latched && !thermo.alarm_limit && thermo.temp_valid) {
        thermo.alarm_latched = 0;
        consumed = true;
    }
    if (thermo.tune.state == TUNE_FAILED) {
        thermo.tune.state = TUNE_IDLE;
        consumed = true;
    }
    if (consumed) thermo.update_display = 1;
    return consumed;
}

// ==================== Regulation ====================

static void pid_restart(void) {
    pid_reset(&pid);
    thermo.pid_output = 0;
    thermo.pwm_on_ms = 0;
    // Start a new PWM period on the next call
    thermo.pwm_cycle_start = tick_count - (uint32_t)thermo.cfg.pwm_period * 1000;
}

// ON/OFF regulator with hysteresis, anti-chatter delay and compressor protection
static uint8_t onoff_regulator(void) {
    const settings_t* c = &thermo.cfg;
    int16_t sp = c->setpoint;
    int16_t t = thermo.temp;
    uint8_t want = thermo.relay_state;

    if (c->mode == 0) { // Heating
        if (t < sp - c->hysteresis) want = 1;
        else if (t >= sp) want = 0;
    } else {            // Cooling
        if (t > sp + c->hysteresis) want = 1;
        else if (t <= sp) want = 0;
    }

    if (want == thermo.relay_state) return want;

    // Minimal time between any two switches
    if (c->relay_delay && tick_count - thermo.relay_last_switch < (uint32_t)c->relay_delay * 1000) {
        return thermo.relay_state;
    }
    // Minimal OFF time before switching on again (compressor protection)
    if (want && c->cycle_protection && thermo.relay_off_valid &&
        tick_count - thermo.relay_last_off < (uint32_t)c->cycle_protection * 60000) {
        return thermo.relay_state;
    }
    return want;
}

// PID regulator with slow PWM (time-proportional) relay output
static uint8_t pid_regulator(bool new_measurement, uint32_t dt_ms) {
    const settings_t* c = &thermo.cfg;

    if (new_measurement) {
        pid_config_t pc = {c->kp, c->ki, c->kd, c->pid_output_limit, c->mode};
        int32_t out = pid_update(&pid, &pc, c->setpoint, thermo.temp, dt_ms);
        if (out / PID_OUTPUT_SCALE != thermo.pid_output / PID_OUTPUT_SCALE) thermo.update_display = 1;
        thermo.pid_output = out;
    }

    // The ON time is latched at the start of each PWM period, so PID updates
    // in the middle of a period do not make the relay chatter
    uint32_t period_ms = (uint32_t)c->pwm_period * 1000;
    uint32_t elapsed = tick_count - thermo.pwm_cycle_start;
    if (elapsed >= period_ms) {
        thermo.pwm_cycle_start = tick_count;
        elapsed = 0;
        // 0.01 % * s -> ms
        thermo.pwm_on_ms = (uint32_t)thermo.pid_output * c->pwm_period / 10;
    }
    return elapsed < thermo.pwm_on_ms;
}

bool thermostat_tune_start(void) {
    if (!thermo.temp_valid || thermo.cfg.manual_mode || thermostat_alarm_active()) return false;
    autotune_start(&thermo.tune, tick_count);
    thermo.update_display = 1;
    return true;
}

void thermostat_tune_stop(void) {
    if (thermo.tune.state == TUNE_RUNNING) thermo.tune.state = TUNE_IDLE;
    thermo.update_display = 1;
}

static uint8_t tune_regulator(bool new_measurement) {
    settings_t* c = &thermo.cfg;

    if (new_measurement) {
        uint8_t cycles = thermo.tune.measured;
        autotune_update(&thermo.tune, thermo.temp, c->setpoint, c->mode,
                        c->pid_output_limit, tick_count);
        if (cycles != thermo.tune.measured) thermo.update_display = 1;

        if (thermo.tune.state == TUNE_DONE) {
            // Apply the result and switch to the PID regulator
            c->kp = thermo.tune.kp;
            c->ki = thermo.tune.ki;
            c->kd = thermo.tune.kd;
            c->regulator_type = 1;
            thermo.tune.state = TUNE_IDLE;
            thermo.params_changed = 1;
            thermo.update_display = 1;
            beep(1000);
            return 0;
        }
        if (thermo.tune.state == TUNE_FAILED) {
            thermo.update_display = 1;
            board_beeper_start(1000);
        }
    }
    return (thermo.tune.state == TUNE_RUNNING) ? thermo.tune.output : 0;
}

// Decides the relay state, called on every main loop iteration
static void control_task(bool new_measurement, uint32_t dt_ms) {
    static uint8_t last_regulator = 0xFF, last_mode = 0xFF;
    const settings_t* c = &thermo.cfg;
    uint8_t want;

    // Restart PID when the regulator or the mode changes
    if (c->regulator_type != last_regulator || c->mode != last_mode) {
        last_regulator = c->regulator_type;
        last_mode = c->mode;
        pid_restart();
    }

    alarm_update();
    bool blocked = !thermo.temp_valid || thermo.alarm_limit || thermo.alarm_latched;

    // Tuning is aborted by manual control and by any alarm
    if (thermo.tune.state == TUNE_RUNNING && (c->manual_mode || blocked)) {
        thermo.tune.state = TUNE_FAILED;
        thermo.update_display = 1;
    }

    if (c->manual_mode == 1) {
        want = 1;
    } else if (c->manual_mode == 2) {
        want = 0;
    } else if (blocked) {
        want = 0;   // fail-safe: no temperature or limits violated -> load off
    } else if (thermo.tune.state == TUNE_RUNNING) {
        want = tune_regulator(new_measurement);
    } else if (c->regulator_type == 0) {
        want = onoff_regulator();
    } else {
        want = pid_regulator(new_measurement, dt_ms);
    }

    // Short beep on switching (not in PID/tuning where the relay switches regularly)
    if (want != thermo.relay_state && (c->manual_mode || (c->regulator_type == 0 && thermo.tune.state != TUNE_RUNNING))) {
        beep(50);
    }
    relay_set(want);
}

// ==================== Statistics, clock, storage ====================

void thermostat_save(void) {
    settings_save(&thermo.cfg);
    saved_runtime = thermo.cfg.total_runtime;
    saved_cycles = thermo.cfg.relay_cycles;
    stats_minutes = 0;
    thermo.params_changed = 0;
}

void thermostat_clear_stats(void) {
    thermo.cfg.total_runtime = 0;
    thermo.cfg.relay_cycles = 0;
    thermo.day_min = thermo.temp;
    thermo.day_max = thermo.temp;
    settings_save_stats(0, 0);
    saved_runtime = 0;
    saved_cycles = 0;
}

static void minute_task(void) {
    static uint32_t minute_timer = 0;
    if (tick_count - minute_timer < 60000) return;
    minute_timer += 60000; // no accumulated drift

    if (thermo.relay_state) thermo.cfg.total_runtime++;

    // Daily extremes are restarted every 24 hours of operation (there is no real-time clock)
    if (++thermo.day_minutes >= 24 * 60) {
        thermo.day_minutes = 0;
        thermo.day_min = thermo.temp;
        thermo.day_max = thermo.temp;
    }

    // Statistics are written once a day (only the counters, not unsaved settings)
    if (++stats_minutes >= STATS_SAVE_PERIOD_MIN) {
        stats_minutes = 0;
        if (thermo.cfg.total_runtime != saved_runtime || thermo.cfg.relay_cycles != saved_cycles) {
            settings_save_stats(thermo.cfg.total_runtime, thermo.cfg.relay_cycles);
            saved_runtime = thermo.cfg.total_runtime;
            saved_cycles = thermo.cfg.relay_cycles;
        }
    }
}

void thermostat_task(void) {
    uint32_t dt_ms = 0;
    bool new_measurement = sensor_task(&dt_ms);
    control_task(new_measurement, dt_ms);
    alarm_sound_task();
    minute_task();
}
