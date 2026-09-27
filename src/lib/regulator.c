#include "regulator.h"
#include <string.h>

// ==================== Helpers ====================

int32_t div_round(int32_t num, int32_t den) {
    if (den < 0) {
        num = -num;
        den = -den;
    }
    return (num >= 0) ? (num + den / 2) / den : -((-num + den / 2) / den);
}

#define PID_D_FILTER    8       // D term time constant, measurements

static int32_t clamp32(int64_t v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < -INT32_MAX) return -INT32_MAX;
    return (int32_t)v;
}

// Saturating arithmetic: only a 64-bit multiply is used (cheap on Cortex-M0), no 64-bit division
static int32_t mul_sat(int32_t a, int32_t b) {
    return clamp32((int64_t)a * b);
}

static int32_t add_sat(int32_t a, int32_t b) {
    return clamp32((int64_t)a + b);
}

static int16_t clamp16(int32_t v) {
    if (v > INT16_MAX) return INT16_MAX;
    if (v < -INT16_MAX) return -INT16_MAX;   // INT16_MIN is TEMP_INVALID
    return (int16_t)v;
}

// ==================== Temperature ====================

int16_t temp_from_ds18b20(int16_t raw) {
    return (int16_t)div_round((int32_t)raw * 10, 16);
}

int16_t temp_to_fahrenheit(int16_t c10) {
    return clamp16(div_round((int32_t)c10 * 9, 5) + 320);
}

int16_t temp_delta_to_fahrenheit(int16_t c10) {
    return clamp16(div_round((int32_t)c10 * 9, 5));
}

// Maps the measured value to the real one: T = ref1 + (M - meas1) * (ref2 - ref1) / (meas2 - meas1)
int16_t temp_calibrate(int16_t measured, int16_t ref1, int16_t meas1, int16_t ref2, int16_t meas2) {
    int32_t dm = (int32_t)meas2 - meas1;
    if (dm == 0) return measured;   // points are not set
    return clamp16(ref1 + div_round(((int32_t)measured - meas1) * ((int32_t)ref2 - ref1), dm));
}

void temp_filter_reset(temp_filter_t* f) {
    f->initialized = 0;
}

// The state keeps 8 extra fraction bits, so small changes are not lost to integer rounding
int16_t temp_filter_apply(temp_filter_t* f, int16_t value, uint8_t strength) {
    int32_t target = (int32_t)value * 256;
    if (!f->initialized || strength == 0 || strength >= 10) {
        f->state = target;      // first value initializes the filter
        f->initialized = 1;
        return value;
    }
    f->state += (target - f->state) * strength / 10;
    return clamp16(div_round(f->state, 256));
}

// ==================== PID ====================

void pid_reset(pid_state_t* pid) {
    pid->integral = 0;
    pid->d_term = 0;
    pid->has_prev = 0;
}

int32_t pid_update(pid_state_t* pid, const pid_config_t* cfg, int16_t setpoint, int16_t input, uint32_t dt_ms) {
    int32_t out_max = (int32_t)cfg->limit * PID_OUTPUT_SCALE;
    int32_t error = cfg->cooling ? (int32_t)input - setpoint : (int32_t)setpoint - input;

    // Time step in 0.1 s, limited to 10 minutes (e.g. after a long sensor outage)
    int32_t dt = (int32_t)((dt_ms + 50) / 100);
    if (dt < 1) dt = 1;
    if (dt > 6000) dt = 6000;

    // P: kp [0.1 %/C] * error [0.1 C] = 0.01 %
    int32_t p = mul_sat(cfg->kp, error);

    // I: ki [0.001 %/(C*s)] * error [0.1 C] * dt [0.1 s] = 0.00001 % = 0.001 output units.
    // Anti-windup: the integral term is kept within 0 .. out_max
    pid->integral = add_sat(pid->integral, mul_sat(mul_sat(cfg->ki, error), dt));
    if (pid->integral > out_max * 1000) pid->integral = out_max * 1000;
    if (pid->integral < 0) pid->integral = 0;

    // D on measurement (no kick when the setpoint changes):
    // kd [%*s/C] * dInput [0.1 C] / dt [0.1 s] = %, * 100 -> 0.01 %
    // The DS18B20 resolution (0.0625 C) makes the raw derivative noisy, so the D term
    // is low-pass filtered with a time constant of PID_D_FILTER measurements
    if (pid->has_prev) {
        int32_t d_input = (int32_t)input - pid->prev_input;
        if (!cfg->cooling) d_input = -d_input;   // heating: rising temperature reduces the output
        int32_t d_raw = mul_sat(mul_sat(cfg->kd, d_input), 100) / dt;
        pid->d_term += (d_raw - pid->d_term) / PID_D_FILTER;
    }
    int32_t d = pid->d_term;
    pid->prev_input = input;
    pid->has_prev = 1;

    int32_t out = add_sat(add_sat(p, pid->integral / 1000), d);
    if (out > out_max) out = out_max;
    if (out < 0) out = 0;
    return out;
}

// ==================== Relay auto-tuning ====================

void autotune_start(autotune_t* at, uint32_t now) {
    memset(at, 0, sizeof(*at));
    at->state = TUNE_RUNNING;
    at->start_time = now;
}

// Ziegler-Nichols PID rules from the ultimate gain Ku and period Tu:
// Kp = 0.6 Ku, Ti = Tu / 2, Td = Tu / 8
static void autotune_finish(autotune_t* at, uint8_t limit) {
    uint32_t swing = at->swing_sum / at->measured;     // peak-to-peak, 0.1 C
    uint32_t period = at->period_sum / at->measured;   // ms

    if (swing < 2 || period < 1000 || limit == 0) {
        at->state = TUNE_FAILED;
        return;
    }

    // Relay amplitude d = limit / 2 [%], oscillation amplitude a = swing / 2 [0.1 C]:
    // Ku = 4 d / (pi a) = 40 * limit / (pi * swing) [%/C], Kp = 0.6 Ku
    uint32_t kp_milli = 24000000UL * limit / (3142UL * swing);   // 0.001 %/C
    if (kp_milli > 999900) kp_milli = 999900;
    if (kp_milli < 100) kp_milli = 100;

    // Ki = Kp / Ti = 2 Kp / Tu -> in 0.001 %/(C*s)
    uint32_t ki = 2000UL * kp_milli / period;
    if (ki > 30000) ki = 30000;
    if (ki < 1) ki = 1;

    // Kd = Kp * Td = Kp * Tu / 8 [%*s/C]
    uint32_t kd = (kp_milli / 100) * (period / 1000) / 80;
    if (kd > 30000) kd = 30000;

    at->kp = (uint16_t)((kp_milli + 50) / 100);
    at->ki = (uint16_t)ki;
    at->kd = (uint16_t)kd;
    at->state = TUNE_DONE;
}

uint8_t autotune_update(autotune_t* at, int16_t temp, int16_t setpoint, uint8_t cooling,
                        uint8_t limit, uint32_t now) {
    if (at->state != TUNE_RUNNING) return 0;

    if (now - at->start_time > TUNE_TIMEOUT_MS) {
        at->state = TUNE_FAILED;
        at->output = 0;
        return 0;
    }

    // Positive error: output is needed (below the setpoint when heating)
    int32_t error = cooling ? (int32_t)temp - setpoint : (int32_t)setpoint - temp;

    if (temp > at->t_max) at->t_max = temp;
    if (temp < at->t_min) at->t_min = temp;

    if (!at->output && error > TUNE_HYSTERESIS) {
        // Switching on starts a new oscillation period
        at->output = 1;
        if (at->has_period) {
            if (at->warmup_done) {
                at->period_sum += now - at->period_start;
                at->swing_sum += (uint32_t)(at->t_max - at->t_min);
                at->measured++;
                if (at->measured >= TUNE_MEASURED_CYCLES) {
                    at->output = 0;
                    autotune_finish(at, limit);
                    return 0;
                }
            }
            at->warmup_done = 1;   // the first period is a transient and is not measured
        }
        at->has_period = 1;
        at->period_start = now;
        at->t_max = temp;
        at->t_min = temp;
    } else if (at->output && error < -TUNE_HYSTERESIS) {
        at->output = 0;
    }
    return at->output;
}
