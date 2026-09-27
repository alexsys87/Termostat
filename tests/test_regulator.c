#include "test.h"
#include "regulator.h"

// ==================== Simulated heater ====================
// First-order process with dead time: dT/dt = (gain * u - (T - ambient)) / tau

#define SIM_STEP_MS     100
#define SIM_DELAY       300     // 30 s dead time in SIM_STEP_MS steps

typedef struct {
    double temp;
    double ambient;
    double gain;        // C of rise at 100 % power
    double tau;         // s
    double delay_line[SIM_DELAY];
    int delay_pos;
} plant_t;

static double plant_gain = 60.0;
static double plant_tau = 600.0;

static void plant_init(plant_t* p, double ambient) {
    memset(p, 0, sizeof(*p));
    p->temp = ambient;
    p->ambient = ambient;
    p->gain = plant_gain;
    p->tau = plant_tau;
}

static void plant_step(plant_t* p, double power) {  // power 0..1
    double delayed = p->delay_line[p->delay_pos];
    p->delay_line[p->delay_pos] = power;
    p->delay_pos = (p->delay_pos + 1) % SIM_DELAY;
    p->temp += (p->gain * delayed - (p->temp - p->ambient)) / p->tau * (SIM_STEP_MS / 1000.0);
}

static int16_t plant_measure(const plant_t* p) {
    // Quantized like a 12-bit DS18B20
    int16_t raw = (int16_t)(p->temp * 16.0 + (p->temp >= 0 ? 0.5 : -0.5));
    return temp_from_ds18b20(raw);
}

// Runs the PID with a relay PWM, returns the maximal temperature and the final one
static void run_pid(const pid_config_t* cfg, int16_t setpoint, uint32_t duration_s,
                    double* max_temp, double* final_temp, double* final_error_max) {
    plant_t plant;
    pid_state_t pid;
    int32_t output = 0;
    uint32_t pwm_on_ms = 0;
    const uint32_t pwm_period_ms = 10000;

    plant_init(&plant, 20.0);
    pid_reset(&pid);
    *max_temp = plant.temp;
    *final_error_max = 0;

    for (uint32_t t = 0; t < duration_s * 1000; t += SIM_STEP_MS) {
        if (t % 2000 == 0) output = pid_update(&pid, cfg, setpoint, plant_measure(&plant), 2000);
        if (t % pwm_period_ms == 0) pwm_on_ms = (uint32_t)output * 10 / 10;  // period 10 s
        int on = (t % pwm_period_ms) < pwm_on_ms;
        plant_step(&plant, on ? 1.0 : 0.0);
        if (plant.temp > *max_temp) *max_temp = plant.temp;
        // Error during the last hour
        if (t > (duration_s - 3600) * 1000) {
            double e = plant.temp - setpoint / 10.0;
            if (e < 0) e = -e;
            if (e > *final_error_max) *final_error_max = e;
        }
    }
    *final_temp = plant.temp;
}

static void test_conversions(void) {
    CHECK_EQ(div_round(5, 2), 3);
    CHECK_EQ(div_round(-5, 2), -3);
    CHECK_EQ(div_round(7, -2), -4);

    CHECK_EQ(temp_from_ds18b20(0x0191), 251);     // +25.0625 C
    CHECK_EQ(temp_from_ds18b20(0x0000), 0);       // 0 C is a valid value
    CHECK_EQ(temp_from_ds18b20((int16_t)0xFFF8), -5);    // -0.5 C
    CHECK_EQ(temp_from_ds18b20((int16_t)0xFF5E), -101);  // -10.125 C
    CHECK_EQ(temp_from_ds18b20((int16_t)0xFC90), -550);  // -55 C
    CHECK_EQ(temp_from_ds18b20(0x07D0), 1250);    // +125 C

    CHECK_EQ(temp_to_fahrenheit(0), 320);
    CHECK_EQ(temp_to_fahrenheit(1000), 2120);
    CHECK_EQ(temp_to_fahrenheit(-400), -400);
    CHECK_EQ(temp_delta_to_fahrenheit(10), 18);
}

static void test_calibration(void) {
    // Identity
    CHECK_EQ(temp_calibrate(253, 0, 0, 1000, 1000), 253);
    // Equal points: no correction
    CHECK_EQ(temp_calibrate(253, 0, 50, 1000, 50), 253);
    // Sensor shows 1.0 C at 0 C (ice) and 99.0 C at 100 C (boiling water)
    CHECK_EQ(temp_calibrate(10, 0, 10, 1000, 990), 0);
    CHECK_EQ(temp_calibrate(990, 0, 10, 1000, 990), 1000);
    CHECK_EQ(temp_calibrate(500, 0, 10, 1000, 990), 500);   // (50 - 1) * 100 / 98 = 50.0
}

static void test_filter(void) {
    temp_filter_t f;
    temp_filter_reset(&f);

    // First value initializes the filter
    CHECK_EQ(temp_filter_apply(&f, 200, 3), 200);
    // Converges to a step without a dead band caused by integer rounding
    int16_t v = 0;
    for (int i = 0; i < 100; i++) v = temp_filter_apply(&f, 205, 1);
    CHECK_EQ(v, 205);
    // Strength 0 and 10 disable the filter
    CHECK_EQ(temp_filter_apply(&f, 300, 0), 300);
    CHECK_EQ(temp_filter_apply(&f, 100, 10), 100);
}

static void test_pid_basics(void) {
    pid_state_t pid;
    pid_config_t cfg = {.kp = 100, .ki = 0, .kd = 0, .limit = 100, .cooling = 0};

    // P only: 10 %/C * 2.0 C = 20 %
    pid_reset(&pid);
    CHECK_EQ(pid_update(&pid, &cfg, 250, 230, 2000), 2000);
    // Output is limited
    CHECK_EQ(pid_update(&pid, &cfg, 250, 0, 2000), 10000);
    cfg.limit = 60;
    CHECK_EQ(pid_update(&pid, &cfg, 250, 0, 2000), 6000);
    // Above the setpoint: no negative output
    CHECK_EQ(pid_update(&pid, &cfg, 250, 300, 2000), 0);
    // Cooling reverses the error
    cfg.cooling = 1;
    cfg.limit = 100;
    CHECK_EQ(pid_update(&pid, &cfg, 250, 270, 2000), 2000);

    // I: 0.1 %/(C*s) * 1.0 C * 10 s = 1 %
    cfg = (pid_config_t){.kp = 0, .ki = 100, .kd = 0, .limit = 100, .cooling = 0};
    pid_reset(&pid);
    CHECK_EQ(pid_update(&pid, &cfg, 250, 240, 10000), 100);
    CHECK_EQ(pid_update(&pid, &cfg, 250, 240, 10000), 200);
    // Anti-windup: the integral does not grow above the limit
    for (int i = 0; i < 1000; i++) pid_update(&pid, &cfg, 250, 0, 60000);
    CHECK_EQ(pid_update(&pid, &cfg, 250, 250, 1000), 10000);
    // 0.1 %/(C*s) * -1.0 C * 100 s = -10 %
    CHECK_EQ(pid_update(&pid, &cfg, 250, 260, 100000), 9000);

    // D on measurement: rising temperature reduces the output, setpoint jumps do not kick
    cfg = (pid_config_t){.kp = 0, .ki = 0, .kd = 100, .limit = 100, .cooling = 0};
    pid_reset(&pid);
    CHECK_EQ(pid_update(&pid, &cfg, 250, 200, 2000), 0);     // no previous value
    // Falling 0.5 C/s * 100 %*s/C = 50 %, the filter passes 1/8 of a step at once
    CHECK_EQ(pid_update(&pid, &cfg, 500, 190, 2000), 5000 / 8);
    for (int i = 0; i < 100; i++) pid_update(&pid, &cfg, 500, (int16_t)(180 - i * 10), 2000);
    int32_t d_steady = pid_update(&pid, &cfg, 500, -820, 2000);   // steady fall: full D term
    CHECK(d_steady >= 4990 && d_steady <= 5000);
    CHECK_EQ(pid_update(&pid, &cfg, 250, 200, 2000), 0);     // rising: negative D clamps to 0
}

static void test_autotune_and_control(double gain, double tau) {
    plant_t plant;
    autotune_t at;
    const int16_t setpoint = 500;   // 50.0 C
    uint8_t relay = 0;
    uint32_t t;

    plant_gain = gain;
    plant_tau = tau;
    printf("  plant: gain %.0f C, tau %.0f s, dead time %d s\n", gain, tau, SIM_DELAY * SIM_STEP_MS / 1000);
    plant_init(&plant, 20.0);
    autotune_start(&at, 0);

    for (t = 0; t < TUNE_TIMEOUT_MS && at.state == TUNE_RUNNING; t += SIM_STEP_MS) {
        if (t % 2000 == 0) relay = autotune_update(&at, plant_measure(&plant), setpoint, 0, 100, t);
        plant_step(&plant, relay ? 1.0 : 0.0);
    }

    CHECK_EQ(at.state, TUNE_DONE);
    printf("  autotune: %u s, Kp=%u.%u Ki=%u.%03u Kd=%u\n", (unsigned)(t / 1000),
           at.kp / 10, at.kp % 10, at.ki / 1000, at.ki % 1000, at.kd);
    CHECK(at.kp > 0);
    CHECK(at.ki > 0);

    // Closed loop with the tuned gains reaches the setpoint and stays there
    pid_config_t cfg = {.kp = at.kp, .ki = at.ki, .kd = at.kd, .limit = 100, .cooling = 0};
    double max_temp, final_temp, final_error;
    run_pid(&cfg, setpoint, 4 * 3600, &max_temp, &final_temp, &final_error);
    printf("  closed loop: max %.2f C, final %.2f C, error in last hour %.2f C\n", max_temp, final_temp, final_error);
    CHECK(final_error < 0.5);
    CHECK(max_temp < 60.0);

    // Too small oscillation: the tuning fails instead of dividing by zero
    autotune_start(&at, 0);
    for (t = 0; t < TUNE_TIMEOUT_MS + 10000 && at.state == TUNE_RUNNING; t += 2000) {
        autotune_update(&at, setpoint, setpoint, 0, 100, t);   // temperature never leaves the band
    }
    CHECK_EQ(at.state, TUNE_FAILED);
}

int main(void) {
    test_conversions();
    test_calibration();
    test_filter();
    test_pid_basics();
    test_autotune_and_control(60.0, 600.0);
    test_autotune_and_control(40.0, 1800.0);
    return TEST_REPORT("regulator");
}
