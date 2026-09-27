#ifndef REGULATOR_H
#define REGULATOR_H

// Hardware independent control math in fixed point (no floating point library needed).
//
// Units used across the firmware:
//   temperature        int16_t, 0.1 C          (253 = 25.3 C)
//   PID output         int32_t, 0.01 %         (10000 = 100 %)
//   Kp                 uint16_t, 0.1 %/C       (100 = 10.0 % per degree)
//   Ki                 uint16_t, 0.001 %/(C*s) (20 = 0.020 % per degree per second)
//   Kd                 uint16_t, 1 %*s/C

#include <stdint.h>
#include <stdbool.h>

#define TEMP_INVALID            INT16_MIN   // "no value" marker
#define PID_OUTPUT_SCALE        100         // output units per 1 %

// ==================== Temperature ====================

int32_t div_round(int32_t num, int32_t den);            // rounded division, den != 0

int16_t temp_from_ds18b20(int16_t raw);                 // 1/16 C -> 0.1 C
int16_t temp_to_fahrenheit(int16_t c10);                // absolute temperature, 0.1 C -> 0.1 F
int16_t temp_delta_to_fahrenheit(int16_t c10);          // temperature difference

// Two-point calibration: at the reference temperature ref_x the sensor showed meas_x.
// Returns the corrected temperature (unchanged if the points are equal).
int16_t temp_calibrate(int16_t measured, int16_t ref1, int16_t meas1, int16_t ref2, int16_t meas2);

// Exponential filter. strength: 0 = off, 1 (strongest) .. 9, 10 = off
typedef struct {
    int32_t state;      // filtered value * 256
    uint8_t initialized;
} temp_filter_t;

void temp_filter_reset(temp_filter_t* f);
int16_t temp_filter_apply(temp_filter_t* f, int16_t value, uint8_t strength);

// ==================== PID ====================

typedef struct {
    int32_t integral;       // accumulated I term, 0.001 * PID output units
    int32_t d_term;         // low-pass filtered D term, 0.01 %
    int16_t prev_input;     // previous measurement (derivative on measurement)
    uint8_t has_prev;
} pid_state_t;

typedef struct {
    uint16_t kp;            // 0.1 %/C
    uint16_t ki;            // 0.001 %/(C*s)
    uint16_t kd;            // %*s/C
    uint8_t limit;          // maximal output, %
    uint8_t cooling;        // 0 = heating, 1 = cooling
} pid_config_t;

void pid_reset(pid_state_t* pid);

// Computes the output (0 .. limit * PID_OUTPUT_SCALE) for a new measurement taken dt_ms after the previous one
int32_t pid_update(pid_state_t* pid, const pid_config_t* cfg, int16_t setpoint, int16_t input, uint32_t dt_ms);

// ==================== Relay auto-tuning (Astrom-Hagglund) ====================

#define TUNE_HYSTERESIS         3           // 0.3 C band around the setpoint
#define TUNE_MEASURED_CYCLES    4           // oscillation periods used for the result
#define TUNE_TIMEOUT_MS         (8UL * 3600UL * 1000UL)

enum {
    TUNE_IDLE = 0,
    TUNE_RUNNING,
    TUNE_DONE,
    TUNE_FAILED
};

typedef struct {
    uint8_t state;          // TUNE_xxx
    uint8_t output;         // relay command
    uint8_t has_period;     // a period start was recorded
    uint8_t warmup_done;    // the first (transient) period was skipped
    uint8_t measured;       // measured periods
    int16_t t_max;          // extremes of the current period
    int16_t t_min;
    uint32_t start_time;
    uint32_t period_start;
    uint32_t period_sum;    // ms
    uint32_t swing_sum;     // peak-to-peak, 0.1 C
    // Result (valid in TUNE_DONE)
    uint16_t kp;
    uint16_t ki;
    uint16_t kd;
} autotune_t;

void autotune_start(autotune_t* at, uint32_t now);

// Processes a new measurement, returns the relay command (1 = on).
// Relay switches between 0 and limit %, so limit is used for the gain calculation.
uint8_t autotune_update(autotune_t* at, int16_t temp, int16_t setpoint, uint8_t cooling,
                        uint8_t limit, uint32_t now);

#endif // REGULATOR_H
