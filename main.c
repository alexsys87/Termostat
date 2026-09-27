#include "HD44780.h"
#include "menu_system.h"
#include "flash_storage.h"
#include "main.h"

#ifdef DEBUG_BUTTONS
volatile uint8_t virtual_btn_up = 0;
volatile uint8_t virtual_btn_down = 0;
volatile uint8_t virtual_btn_enter = 0;
#endif

// ==================== Global variables ====================
thermostat_t thermo;
static HD44780 lcd;
static menu_state_t menu;

volatile uint32_t tick_count = 0;
static uint32_t ticks_per_us = 0;              // SysTick ticks in 1 us (set in systick_init)

// Non-blocking beep on PF1 (passive buzzer)
static volatile uint8_t beep_active = 0;        // 1 - sound is playing
static volatile uint16_t beep_half_periods = 0; // remaining half-periods (4 per ms at 2 kHz)

// ==================== Buttons ====================
typedef struct {
    uint16_t pin;
    uint8_t allow_repeat;   // auto-repeat while held
    uint8_t raw;            // last sampled level, 1 = pressed
    uint8_t pressed;        // debounced state
    uint8_t repeat_count;   // auto-repeat events since press (for acceleration)
    uint32_t last_change;   // time of the last raw level change
    uint32_t last_event;    // time of the last press/repeat event
} button_t;

static button_t btn_up    = {.pin = BTN_UP_PIN, .allow_repeat = 1};
static button_t btn_down  = {.pin = BTN_DOWN_PIN, .allow_repeat = 1};
static button_t btn_enter = {.pin = BTN_ENTER_PIN, .allow_repeat = 0};  // no auto-repeat: holding Enter must not jump through menus

// ==================== Parameter descriptors ====================
// Limits and steps are in scaled units: float value * 10^decimals

static const char* const LBL_OFF_ON[] = {"Off", "On"};
static const char* const LBL_NO_YES[] = {"No", "Yes"};
static const char* const LBL_REG[]    = {"ON/OFF", "PID"};
static const char* const LBL_MODE[]   = {"Heat", "Cool"};
static const char* const LBL_RELAY[]  = {"NO", "NC"};
static const char* const LBL_UNITS[]  = {"C", "F"};
static const char* const LBL_MANUAL[] = {"Auto", "Man On", "Man Off"};

#define P_FLOAT(var, lo, hi, st, dec, sfx) {&thermo.var, NULL, sfx, lo, hi, st, PARAM_FLOAT, dec, 0}
#define P_U8(var, lo, hi, st, sfx)         {&thermo.var, NULL, sfx, lo, hi, st, PARAM_U8, 0, 0}
#define P_U16(var, lo, hi, st, sfx)        {&thermo.var, NULL, sfx, lo, hi, st, PARAM_U16, 0, 0}
#define P_ENUM(var, labels)                {&thermo.var, labels, NULL, 0, COUNT_OF(labels) - 1, 1, PARAM_U8, 0, 0}
#define P_TEMP(var, st)                    P_FLOAT(var, -550, 1250, st, 1, NULL)  // DS18B20 range -55..125 C

// Basic
static const param_desc_t P_REG       = P_ENUM(regulator_type, LBL_REG);
static const param_desc_t P_SETPOINT  = P_TEMP(setpoint, 5);
static const param_desc_t P_HYST      = P_FLOAT(hysteresis, 1, 200, 1, 1, NULL);     // 0.1..20.0
static const param_desc_t P_CAL       = P_FLOAT(calibration, -100, 100, 1, 1, NULL); // -10.0..+10.0
static const param_desc_t P_MODE      = P_ENUM(mode, LBL_MODE);
static const param_desc_t P_RELAY     = P_ENUM(relay_logic, LBL_RELAY);
static const param_desc_t P_UNITS     = P_ENUM(temp_units, LBL_UNITS);
static const param_desc_t P_SAFETY    = P_ENUM(safety_enabled, LBL_OFF_ON);
static const param_desc_t P_TEMP_MIN  = P_TEMP(temp_min, 5);
static const param_desc_t P_TEMP_MAX  = P_TEMP(temp_max, 5);
static const param_desc_t P_MANUAL    = P_ENUM(manual_mode, LBL_MANUAL);
// PID
static const param_desc_t P_KP        = P_FLOAT(kp, 0, 9999, 1, 1, NULL);            // 0..999.9
static const param_desc_t P_KI        = P_FLOAT(ki, 0, 9999, 1, 2, NULL);            // 0..99.99
static const param_desc_t P_KD        = P_FLOAT(kd, 0, 9999, 1, 1, NULL);            // 0..999.9
static const param_desc_t P_PID_INT   = P_U16(pid_interval, 100, 30000, 100, "ms");
static const param_desc_t P_PWM_PER   = P_U16(pwm_period, 1, 600, 1, "s");
static const param_desc_t P_PID_LIM   = P_FLOAT(pid_output_limit, 0, 100, 1, 0, "%");
// Advanced
static const param_desc_t P_FILTER    = P_U8(temp_filter, 0, 10, 1, NULL);
static const param_desc_t P_RES       = P_U8(temp_resolution, 9, 12, 1, "bit");
static const param_desc_t P_RDELAY    = P_U8(relay_delay, 0, 255, 1, "s");
static const param_desc_t P_CYCLE     = P_U8(cycle_protection, 0, 60, 1, "min");
static const param_desc_t P_SCHEDULE  = P_ENUM(schedule_enabled, LBL_OFF_ON);
static const param_desc_t P_CLOCK     = {&thermo.current_hour, NULL, "h", 0, 23, 1, PARAM_U8, 0, PF_NOSAVE};
static const param_desc_t P_DAY_SP    = P_TEMP(day_setpoint, 5);
static const param_desc_t P_NIGHT_SP  = P_TEMP(night_setpoint, 5);
static const param_desc_t P_DAY_H     = P_U8(day_start_hour, 0, 23, 1, "h");
static const param_desc_t P_NIGHT_H   = P_U8(night_start_hour, 0, 23, 1, "h");
// Two-point calibration
static const param_desc_t P_CAL1_T    = P_TEMP(cal_point1_temp, 1);
static const param_desc_t P_CAL1_M    = P_TEMP(cal_point1_measured, 1);
static const param_desc_t P_CAL2_T    = P_TEMP(cal_point2_temp, 1);
static const param_desc_t P_CAL2_M    = P_TEMP(cal_point2_measured, 1);
// System
static const param_desc_t P_AUTOSAVE  = P_ENUM(auto_save, LBL_OFF_ON);
static const param_desc_t P_BEEP      = P_ENUM(beep_enabled, LBL_OFF_ON);
static const param_desc_t P_PWR       = P_ENUM(power_save, LBL_OFF_ON);
static const param_desc_t P_UPDATE    = P_U8(update_interval, 1, 60, 1, "s");
static const param_desc_t P_KEYREP    = P_ENUM(key_repeat, LBL_OFF_ON);
static const param_desc_t P_REP_DELAY = P_U16(key_repeat_delay, 100, 3000, 50, "ms");
static const param_desc_t P_REP_RATE  = P_U16(key_repeat_rate, 20, 1000, 10, "ms");
static const param_desc_t P_DEBOUNCE  = P_U16(debounce_time, 5, 200, 5, "ms");
static const param_desc_t P_TIMEOUT   = P_U8(display_timeout, 0, 255, 1, "min");
static const param_desc_t P_FACTORY   = {&thermo.confirm, LBL_NO_YES, NULL, 0, 1, 1, PARAM_U8, 0, PF_NOSAVE};
// Statistics
static const param_desc_t P_RUNTIME   = {&thermo.total_runtime, NULL, "m", 0, 0, 0, PARAM_U32, 0, PF_READONLY};
static const param_desc_t P_CYCLES    = {&thermo.relay_cycles, NULL, NULL, 0, 0, 0, PARAM_U32, 0, PF_READONLY};
static const param_desc_t P_CLEAR     = {&thermo.confirm, LBL_NO_YES, NULL, 0, 1, 1, PARAM_U8, 0, PF_NOSAVE};

// All stored parameters: validated after loading from Flash
static const param_desc_t* const stored_params[] = {
    &P_REG, &P_SETPOINT, &P_HYST, &P_CAL, &P_MODE, &P_RELAY, &P_UNITS, &P_SAFETY, &P_TEMP_MIN,
    &P_TEMP_MAX, &P_MANUAL, &P_KP, &P_KI, &P_KD, &P_PID_INT, &P_PWM_PER, &P_PID_LIM, &P_FILTER,
    &P_RES, &P_RDELAY, &P_CYCLE, &P_SCHEDULE, &P_DAY_SP, &P_NIGHT_SP, &P_DAY_H, &P_NIGHT_H,
    &P_CAL1_T, &P_CAL1_M, &P_CAL2_T, &P_CAL2_M, &P_AUTOSAVE, &P_BEEP, &P_PWR, &P_UPDATE,
    &P_KEYREP, &P_REP_DELAY, &P_REP_RATE, &P_DEBOUNCE, &P_TIMEOUT
};

// ==================== Menu structure (texts up to 7 characters) ====================

static void menu_save_action(void);

MENU_ITEMS(basic_menu) = {
    MENU_PARAM("Reg", P_REG),
    MENU_PARAM("Setp", P_SETPOINT),
    MENU_PARAM("Hyst", P_HYST),
    MENU_PARAM("Cal", P_CAL),
    MENU_PARAM("Mode", P_MODE),
    MENU_PARAM("Relay", P_RELAY),
    MENU_PARAM("Units", P_UNITS),
    MENU_PARAM("Safe", P_SAFETY),
    MENU_PARAM("Min", P_TEMP_MIN),
    MENU_PARAM("Max", P_TEMP_MAX),
    MENU_PARAM("Manual", P_MANUAL),
    MENU_BACK("Back")
};

MENU_ITEMS(pid_menu) = {
    MENU_PARAM("Kp", P_KP),
    MENU_PARAM("Ki", P_KI),
    MENU_PARAM("Kd", P_KD),
    MENU_PARAM("P.Int", P_PID_INT),
    MENU_PARAM("P.Per", P_PWM_PER),
    MENU_PARAM("Lim", P_PID_LIM),
    MENU_BACK("Back")
};

MENU_ITEMS(advanced_menu) = {
    MENU_PARAM("Filt", P_FILTER),
    MENU_PARAM("Res", P_RES),
    MENU_PARAM("Dly", P_RDELAY),
    MENU_PARAM("Cyc", P_CYCLE),
    MENU_PARAM("Sch", P_SCHEDULE),
    MENU_PARAM("Time", P_CLOCK),
    MENU_PARAM("Day", P_DAY_SP),
    MENU_PARAM("Night", P_NIGHT_SP),
    MENU_PARAM("DSta", P_DAY_H),
    MENU_PARAM("NSta", P_NIGHT_H),
    MENU_BACK("Back")
};

MENU_ITEMS(calibration_menu) = {
    MENU_PARAM("P1T", P_CAL1_T),
    MENU_PARAM("P1M", P_CAL1_M),
    MENU_PARAM("P2T", P_CAL2_T),
    MENU_PARAM("P2M", P_CAL2_M),
    MENU_BACK("Back")
};

MENU_ITEMS(system_menu) = {
    MENU_PARAM("Auto", P_AUTOSAVE),
    MENU_ACTION("Save", menu_save_action),
    MENU_PARAM("Beep", P_BEEP),
    MENU_PARAM("Pwr", P_PWR),
    MENU_PARAM("Upd", P_UPDATE),
    MENU_PARAM("TOut", P_TIMEOUT),
    MENU_PARAM("KeyR", P_KEYREP),
    MENU_PARAM("RptD", P_REP_DELAY),
    MENU_PARAM("RptR", P_REP_RATE),
    MENU_PARAM("Deb", P_DEBOUNCE),
    MENU_PARAM("Fact", P_FACTORY),
    MENU_BACK("Back")
};

MENU_ITEMS(stats_menu) = {
    MENU_PARAM("Run", P_RUNTIME),
    MENU_PARAM("Cycl", P_CYCLES),
    MENU_PARAM("Clr", P_CLEAR),
    MENU_BACK("Back")
};

MENU_ITEMS(main_menu) = {
    MENU_NODE("Basic", basic_menu),
    MENU_NODE("PID", pid_menu),
    MENU_NODE("Adv", advanced_menu),
    MENU_NODE("Cal", calibration_menu),
    MENU_NODE("Sys", system_menu),
    MENU_NODE("Stats", stats_menu),
    MENU_BACK("Back")
};

// ==================== Hardware helpers ====================

// Generic push-pull output setup
static void setup_gpio_output(GPIO_TypeDef* port, uint16_t pin_mask) {
    for (int i = 0; i < 16; i++) {
        if (pin_mask & (1 << i)) {
            port->MODER &= ~(0x3 << (i * 2));
            port->MODER |= (0x1 << (i * 2));
            port->OTYPER &= ~(1 << i);
            port->OSPEEDR |= (0x3 << (i * 2)); // High speed
        }
    }
}

// Generic input with pull-up setup
static void setup_gpio_input_pullup(GPIO_TypeDef* port, uint16_t pin_mask) {
    for (int i = 0; i < 16; i++) {
        if (pin_mask & (1 << i)) {
            port->MODER &= ~(0x3 << (i * 2));
            port->PUPDR &= ~(0x3 << (i * 2));
            port->PUPDR |= (0x1 << (i * 2));
        }
    }
}

// Precise microsecond delay based on SysTick (required for 1-Wire on Cortex-M0).
// Works with interrupts disabled.
void delay_us(uint32_t us) {
    uint32_t ticks = us * ticks_per_us;
    uint32_t reload = SysTick->LOAD + 1;
    uint32_t start = SysTick->VAL;
    uint32_t elapsed = 0;

    while (elapsed < ticks) {
        uint32_t current = SysTick->VAL;
        // SysTick counts down and reloads from LOAD after reaching 0
        elapsed += (start >= current) ? (start - current) : (start + reload - current);
        start = current;
    }
}

void delay_ms(uint32_t ms) {
    while (ms--) delay_us(1000);
}

static void system_clock_init(void) {
    // 1. Make sure HSI is running
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY));

    // 2. Flash latency MUST be set before switching to 48 MHz (1 wait state + prefetch)
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;

    // 3. If PLL is already used, switch to HSI and stop the PLL to reconfigure it
    if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) {
        RCC->CFGR &= ~RCC_CFGR_SW;
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);
    }
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY);

    // 4. PLL: HSI/2 * 12 = 48 MHz
    RCC->CFGR2 &= ~RCC_CFGR2_PREDIV1;
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLMULL | RCC_CFGR_PLLSRC)) | RCC_CFGR_PLLSRC_HSI_DIV2 | RCC_CFGR_PLLMULL12;

    // 5. Start the PLL
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    // 6. Use PLL as the system clock
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    SystemCoreClockUpdate();
}

static void systick_init(void) {
    ticks_per_us = SystemCoreClock / 1000000;
    SysTick_Config(SystemCoreClock / 1000); // 1 ms interrupt
}

static void relay_output(uint8_t on);

static void gpio_init(void) {
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOFEN;

    // Set the "relay off" level BEFORE the pin becomes an output:
    // with NC logic "off" is the high level, a low glitch would switch the load on
    relay_output(0);
    BEEPER_PORT->BRR = BEEPER_PIN;
    setup_gpio_output(RELAY_PORT, RELAY_PIN);
    setup_gpio_output(BEEPER_PORT, BEEPER_PIN);

    setup_gpio_input_pullup(BTN_PORT, BTN_UP_PIN | BTN_DOWN_PIN | BTN_ENTER_PIN);
}

static void watchdog_init(void) {
#if USE_WATCHDOG
    // Freeze the watchdog while the core is halted by the debugger
    RCC->APB2ENR |= RCC_APB2ENR_DBGMCUEN;
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    // LSI ~40 kHz / 32 = 1250 Hz, 2500 counts = ~2 s
    IWDG->KR = 0xCCCC;          // start
    IWDG->KR = 0x5555;          // unlock PR/RLR
    IWDG->PR = 3;               // prescaler /32
    IWDG->RLR = 2500;
    while (IWDG->SR);           // wait until the registers are updated
    IWDG->KR = 0xAAAA;          // reload
#endif
}

static inline void watchdog_feed(void) {
#if USE_WATCHDOG
    IWDG->KR = 0xAAAA;
#endif
}

// ==================== Beeper ====================

/**
 * @brief TIM14 generates 4 kHz update interrupts, each toggles PF1 -> 2 kHz tone
 * @note  48 MHz / (PSC+1) / (ARR+1) = 48e6 / 48 / 250 = 4000 Hz
 */
static void beep_timer_init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_TIM14EN;

    TIM14->PSC = 47;      // 1 MHz counter clock
    TIM14->ARR = 249;     // 250 us = one half-period of the 2 kHz tone
    TIM14->DIER |= TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM14_IRQn);
    // The timer is started only by beep()
}

/**
 * @brief Starts a non-blocking beep on the passive buzzer
 * @param duration_ms Duration in milliseconds
 */
static void beep(uint16_t duration_ms) {
    if (!thermo.beep_enabled) return;
    if (beep_active) return;   // already playing

    beep_half_periods = (duration_ms > 16000) ? 64000 : duration_ms * 4; // 4 half-periods per ms
    beep_active = 1;

    TIM14->CNT = 0;
    TIM14->CR1 |= TIM_CR1_CEN;
}

void TIM14_IRQHandler(void) {
    if (TIM14->SR & TIM_SR_UIF) {
        TIM14->SR = (uint16_t)~TIM_SR_UIF;   // rc_w0: write 0 only to UIF

        if (beep_active && beep_half_periods) {
            GPIOF->ODR ^= GPIO_ODR_1;
            if (--beep_half_periods == 0) {
                beep_active = 0;
                BEEPER_PORT->BRR = BEEPER_PIN;   // leave the buzzer pin low
                TIM14->CR1 &= ~TIM_CR1_CEN;      // stop the timer
            }
        } else {
            beep_active = 0;
            TIM14->CR1 &= ~TIM_CR1_CEN;
        }
    }
}

// ==================== Relay ====================

// Writes the physical pin level according to the relay logic (NO/NC)
static void relay_output(uint8_t on) {
    if ((on != 0) != (thermo.relay_logic != 0)) RELAY_PORT->BSRR = RELAY_PIN;
    else RELAY_PORT->BRR = RELAY_PIN;
}

static void relay_set(uint8_t on) {
    if (on != thermo.relay_state) {
        thermo.relay_state = on;
        thermo.relay_last_switch = tick_count;
        if (on) {
            thermo.relay_cycles++;
        } else {
            thermo.relay_last_off = tick_count;
            thermo.relay_off_valid = 1;
        }
        thermo.update_display = 1;
    }
    // The pin is refreshed on every call: this also applies a changed NO/NC setting at once
    relay_output(thermo.relay_state);
}

// ==================== Thermostat logic ====================

// Setpoint that is active now (day/night one when the schedule is enabled)
static const param_desc_t* active_setpoint_param(void) {
    if (!thermo.schedule_enabled) return &P_SETPOINT;

    uint8_t hour = thermo.clock_minutes / 60;
    uint8_t day_h = thermo.day_start_hour, night_h = thermo.night_start_hour;
    bool is_day = (day_h <= night_h) ? (hour >= day_h && hour < night_h)
                                     : (hour >= day_h || hour < night_h); // day period crosses midnight
    return is_day ? &P_DAY_SP : &P_NIGHT_SP;
}

static float active_setpoint(void) {
    return *(const float*)active_setpoint_param()->value;
}

static float apply_temp_filter(float new_temp) {
    // First valid measurement initializes the filter (no slow start from a wrong value)
    if (thermo.temp_filter == 0 || thermo.temp_filter >= 10 || !thermo.temp_valid) return new_temp;
    float alpha = thermo.temp_filter / 10.0f;
    return alpha * new_temp + (1.0f - alpha) * thermo.current_temp;
}

// Two-point calibration: at the reference temperature T the sensor showed M.
// Maps the measured value to the real one: T = T1 + (M - M1) * (T2 - T1) / (M2 - M1)
static float apply_calibration(float measured) {
    float dm = thermo.cal_point2_measured - thermo.cal_point1_measured;
    if (dm < 0.1f && dm > -0.1f) return measured; // points are not set or invalid
    return thermo.cal_point1_temp +
           (measured - thermo.cal_point1_measured) * (thermo.cal_point2_temp - thermo.cal_point1_temp) / dm;
}

static void pid_reset(void) {
    thermo.integral = 0;
    thermo.previous_error = 0;
    thermo.pid_output = 0;
    thermo.pwm_on_ms = 0;
    // Compute PID and start a new PWM period on the very next call
    thermo.last_pid_time = tick_count - thermo.pid_interval;
    thermo.pwm_cycle_start = tick_count - (uint32_t)thermo.pwm_period * 1000;
}

static float compute_pid(float setpoint, float current) {
    // Error sign depends on the mode: heating wants T to rise, cooling wants it to fall
    float error = (thermo.mode == 0) ? (setpoint - current) : (current - setpoint);
    float limit = thermo.pid_output_limit;

    thermo.integral += error * thermo.ki;
    // Anti-windup
    if (thermo.integral > limit) thermo.integral = limit;
    if (thermo.integral < -limit) thermo.integral = -limit;

    float deriv = thermo.kd * (error - thermo.previous_error);
    thermo.previous_error = error;

    float out = thermo.kp * error + thermo.integral + deriv;
    if (out > limit) out = limit;
    if (out < 0.0f) out = 0.0f;
    return out;
}

// ON/OFF regulator with hysteresis, anti-chatter delay and compressor protection
static uint8_t onoff_regulator(void) {
    float sp = active_setpoint();
    float t = thermo.current_temp;
    uint8_t want = thermo.relay_state;

    if (thermo.mode == 0) { // Heating
        if (t < sp - thermo.hysteresis) want = 1;
        else if (t >= sp) want = 0;
    } else {                // Cooling
        if (t > sp + thermo.hysteresis) want = 1;
        else if (t <= sp) want = 0;
    }

    if (want == thermo.relay_state) return want;

    // Minimal time between any two switches
    if (thermo.relay_delay &&
        tick_count - thermo.relay_last_switch < (uint32_t)thermo.relay_delay * 1000) {
        return thermo.relay_state;
    }
    // Minimal OFF time before switching on again (compressor protection)
    if (want && thermo.cycle_protection && thermo.relay_off_valid &&
        tick_count - thermo.relay_last_off < (uint32_t)thermo.cycle_protection * 60000) {
        return thermo.relay_state;
    }
    return want;
}

// PID regulator with slow PWM (time-proportional) relay output
static uint8_t pid_regulator(void) {
    if (tick_count - thermo.last_pid_time >= thermo.pid_interval) {
        thermo.last_pid_time = tick_count;
        float out = compute_pid(active_setpoint(), thermo.current_temp);
        if ((uint8_t)(out + 0.5f) != (uint8_t)(thermo.pid_output + 0.5f)) thermo.update_display = 1;
        thermo.pid_output = out;
    }

    // The ON time is latched at the start of each PWM period, so PID updates
    // in the middle of a period do not make the relay chatter
    uint32_t period_ms = (uint32_t)thermo.pwm_period * 1000;
    uint32_t elapsed = tick_count - thermo.pwm_cycle_start;
    if (elapsed >= period_ms) {
        thermo.pwm_cycle_start = tick_count;
        elapsed = 0;
        thermo.pwm_on_ms = (uint32_t)(thermo.pid_output * (float)period_ms / 100.0f);
    }
    return elapsed < thermo.pwm_on_ms;
}

// Decides the relay state, called on every main loop iteration
static void control_task(void) {
    static uint8_t last_regulator = 0xFF, last_mode = 0xFF;
    uint8_t want;

    // Restart PID when the regulator or the mode changes
    if (thermo.regulator_type != last_regulator || thermo.mode != last_mode) {
        last_regulator = thermo.regulator_type;
        last_mode = thermo.mode;
        pid_reset();
    }

    uint8_t tripped = thermo.safety_enabled && thermo.temp_valid &&
                      (thermo.current_temp < thermo.temp_min || thermo.current_temp > thermo.temp_max);
    if (tripped != thermo.safety_tripped) {
        thermo.safety_tripped = tripped;
        thermo.update_display = 1;
        if (tripped) beep(500);
    }

    if (thermo.manual_mode == 1) {
        want = 1;
    } else if (thermo.manual_mode == 2) {
        want = 0;
    } else if (!thermo.temp_valid || thermo.safety_tripped) {
        want = 0; // fail-safe: no temperature or limits violated -> load off
    } else if (thermo.regulator_type == 0) {
        want = onoff_regulator();
    } else {
        want = pid_regulator();
    }

    // Short beep on switching (not in PID mode where the relay switches every PWM period)
    if (want != thermo.relay_state && (thermo.regulator_type == 0 || thermo.manual_mode != 0)) {
        beep(50);
    }
    relay_set(want);
}

// ==================== Temperature sensor (non-blocking) ====================

static void sensor_failed(void) {
    if (thermo.sensor_fail_count < 255) thermo.sensor_fail_count++;
    if (thermo.sensor_fail_count >= SENSOR_FAIL_LIMIT && !thermo.sensor_error) {
        thermo.sensor_error = 1;
        thermo.temp_valid = 0;     // control_task switches the load off
        thermo.ds_resolution = 0;  // re-apply the resolution when the sensor is back
        thermo.update_display = 1;
        beep(500);
    }
}

static void sensor_task(void) {
    static uint32_t start_timer;
    static uint8_t started = 0;

    if (thermo.ds_pending) {
        if (tick_count - thermo.ds_start_time < ds18b20_conversion_time_ms(thermo.ds_resolution)) return;
        thermo.ds_pending = 0;

        int16_t raw;
        if (!ds18b20_read_raw(&raw)) {
            sensor_failed();
            return;
        }

        float t = apply_calibration(raw / 16.0f) + thermo.calibration;
        thermo.current_temp = apply_temp_filter(t);
        thermo.temp_valid = 1;
        thermo.sensor_error = 0;
        thermo.sensor_fail_count = 0;
        thermo.update_display = 1;
        return;
    }

    if (started && tick_count - start_timer < (uint32_t)thermo.update_interval * 1000) return;
    started = 1;
    start_timer = tick_count;

    // Apply a new resolution (after start-up, menu change or sensor reconnection)
    if (thermo.ds_resolution != thermo.temp_resolution) {
        if (!ds18b20_set_resolution(thermo.temp_resolution)) {
            sensor_failed();
            return;
        }
        thermo.ds_resolution = thermo.temp_resolution;
    }

    if (ds18b20_start_conversion()) {
        thermo.ds_pending = 1;
        thermo.ds_start_time = tick_count;
    } else {
        sensor_failed();
    }
}

// ==================== Display (8x2) ====================

// Formats temperature in the selected units with one decimal
static char* fmt_temp(char* dst, float celsius) {
    if (thermo.temp_units) celsius = celsius * 9.0f / 5.0f + 32.0f;
    float scaled = celsius * 10.0f;
    return fmt_fixed(dst, (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f), 1);
}

// Right-aligns src in a field of the given width
static char* put_right(char* dst, const char* src, uint8_t width) {
    uint8_t len = (uint8_t)strlen(src);
    while (len < width--) *dst++ = ' ';
    return str_copy(dst, src);
}

static void update_main_display(void) {
    char line[16];
    char num[12];
    char* p;

    // --- Line 1: current temperature ---
    if (thermo.sensor_error) {
        str_copy(line, "Sens.Err");
    } else if (!thermo.temp_valid) {
        str_copy(line, " --.- ");
    } else {
        fmt_temp(num, thermo.current_temp);
        p = put_right(line, num, 5);
        *p++ = ' ';
        *p++ = thermo.temp_units ? 'F' : 'C';
        *p = '\0';
    }
    HD44780_print_line(&lcd, 0, line);

    // --- Line 2: setpoint and state ---
    if (thermo.manual_mode) {
        str_copy(line, thermo.manual_mode == 1 ? " MAN ON" : " MAN OFF");
    } else if (thermo.safety_tripped) {
        str_copy(line, "LIMIT!");
    } else if (thermo.regulator_type == 0) {
        // "25.0 OFF"; the space is dropped for 5-character values ("-10.5OFF")
        char* end = fmt_temp(num, active_setpoint());
        p = put_right(line, num, 4);
        if (end - num <= 4) *p++ = ' ';
        str_copy(p, thermo.relay_state ? "ON" : "OFF");
    } else {
        p = str_copy(line, "PWR:");
        fmt_uint(num, (uint32_t)(thermo.pid_output + 0.5f));
        p = put_right(p, num, 3);
        str_copy(p, "%");
    }
    HD44780_print_line(&lcd, 1, line);
}

static void display_refresh(void) {
    if (!thermo.display_on) return;
    if (menu_is_open(&menu)) menu_draw(&menu, &lcd);
    else update_main_display();
    thermo.update_display = 0;
}

// ==================== Settings storage ====================

static void save_if_changed(void) {
    if (thermo.params_changed && thermo.auto_save) {
        thermo.params_changed = 0;
        save_parameters();
    }
}

static void sanitize_parameters(void) {
    for (uint8_t i = 0; i < COUNT_OF(stored_params); i++) {
        param_clamp(stored_params[i]);
    }
}

// Manual save (works also when auto-save is off)
static void menu_save_action(void) {
    thermo.params_changed = 0;
    save_parameters();
    beep(200);
}

// Called when parameter editing is finished
static void on_param_edited(const param_desc_t* p) {
    if (p == &P_CLOCK) {
        thermo.clock_minutes = (uint16_t)thermo.current_hour * 60;
    } else if (p == &P_FACTORY && thermo.confirm) {
        set_default_parameters();
        save_parameters();
        thermo.params_changed = 0;
        menu_close(&menu);
        beep(1000);
    } else if (p == &P_CLEAR && thermo.confirm) {
        thermo.total_runtime = 0;
        thermo.relay_cycles = 0;
        thermo.params_changed = 1;
        save_if_changed();
        beep(200);
    }
    thermo.confirm = 0;
}

// ==================== Statistics and clock ====================

static void statistics_task(void) {
    static uint32_t minute_timer = 0;
    if (tick_count - minute_timer < 60000) return;
    minute_timer += 60000; // no accumulated drift

    if (thermo.relay_state) thermo.total_runtime++;

    thermo.clock_minutes++;
    if (thermo.clock_minutes >= 24 * 60) thermo.clock_minutes = 0;
    // Do not overwrite the hour while the user is editing it
    if (menu_edit_param(&menu) != &P_CLOCK) {
        thermo.current_hour = thermo.clock_minutes / 60;
    }
    if (thermo.schedule_enabled) thermo.update_display = 1;
}

// ==================== Buttons ====================

static uint8_t button_read(uint16_t pin) {
#ifdef DEBUG_BUTTONS
    if (pin == BTN_UP_PIN) return virtual_btn_up;
    if (pin == BTN_DOWN_PIN) return virtual_btn_down;
    if (pin == BTN_ENTER_PIN) return virtual_btn_enter;
    return 0;
#else
    return (BTN_PORT->IDR & pin) ? 0 : 1; // active low
#endif
}

// Returns 1 on a debounced press and on every auto-repeat event
static uint8_t button_poll(button_t* b) {
    uint8_t raw = button_read(b->pin);
    uint32_t now = tick_count;

    // Level changed (press, release or bounce): restart the debounce timer
    if (raw != b->raw) {
        b->raw = raw;
        b->last_change = now;
        return 0;
    }
    if (now - b->last_change < thermo.debounce_time) return 0;

    // Stable new state
    if (raw != b->pressed) {
        b->pressed = raw;
        b->repeat_count = 0;
        if (raw) {
            b->last_event = now;
            return 1;
        }
        return 0;
    }

    // Auto-repeat while held
    if (raw && b->allow_repeat && thermo.key_repeat &&
        now - b->last_change >= thermo.key_repeat_delay &&
        now - b->last_event >= thermo.key_repeat_rate) {
        b->last_event = now;
        if (b->repeat_count < 255) b->repeat_count++;
        return 1;
    }
    return 0;
}

// Returns true if the key press was used only to wake up the display
static bool wake_display(void) {
    thermo.last_user_action = tick_count;
    if (thermo.display_on) return false;
    thermo.display_on = 1;
    HD44780_display_on(&lcd);
    thermo.update_display = 1;
    return true;
}

static void button_updown_action(int8_t dir, uint8_t accel) {
    if (wake_display()) return;

    if (!menu_is_open(&menu)) {
        // Main screen: change the active setpoint
        if (thermo.manual_mode == 0 && param_step(active_setpoint_param(), dir, 1)) {
            thermo.params_changed = 1;
        }
    } else {
        menu_event_t evt = (dir > 0) ? menu_handle_up(&menu, accel) : menu_handle_down(&menu, accel);
        const param_desc_t* p = menu_edit_param(&menu);
        if (evt == MENU_EVT_CHANGED && p && !(p->flags & PF_NOSAVE)) {
            thermo.params_changed = 1;
        }
    }
    display_refresh();
    beep(30);
}

static void button_enter_action(void) {
    if (wake_display()) return;

    if (!menu_is_open(&menu)) {
        menu_open(&menu, main_menu, COUNT_OF(main_menu));
    } else {
        const param_desc_t* p = menu_edit_param(&menu);
        menu_event_t evt = menu_handle_enter(&menu);
        if (evt == MENU_EVT_EDIT_END) {
            on_param_edited(p);
        } else if (evt == MENU_EVT_EXIT) {
            save_if_changed();
        }
    }
    display_refresh();
    beep(50);
}

static void buttons_task(void) {
    if (button_poll(&btn_up)) {
        button_updown_action(1, (btn_up.repeat_count > KEY_ACCEL_REPEATS) ? 10 : 1);
    }
    if (button_poll(&btn_down)) {
        button_updown_action(-1, (btn_down.repeat_count > KEY_ACCEL_REPEATS) ? 10 : 1);
    }
    if (button_poll(&btn_enter)) {
        button_enter_action();
    }
}

// Menu and display inactivity timeouts
static void ui_timeout_task(void) {
    uint32_t idle = tick_count - thermo.last_user_action;

    if (menu_is_open(&menu) && idle > MENU_TIMEOUT_MS) {
        const param_desc_t* p = menu_edit_param(&menu);
        if (p) on_param_edited(p == &P_FACTORY || p == &P_CLEAR ? NULL : p); // never confirm by timeout
        menu_close(&menu);
        save_if_changed();
        thermo.update_display = 1;
    }

    if (thermo.display_timeout && thermo.display_on &&
        idle > (uint32_t)thermo.display_timeout * 60000) {
        thermo.display_on = 0;
        HD44780_display_off(&lcd);
    }
}

// ==================== System timer interrupt ====================
void SysTick_Handler(void) {
    tick_count++;
}

// ==================== Main loop ====================
int main(void) {
    system_clock_init();

    // SysTick MUST be initialized first: delay_us() used by the LCD and 1-Wire depends on it
    systick_init();
    watchdog_init();

    // Parameters are loaded before GPIO: the relay logic (NO/NC) defines the safe pin level
    load_parameters();
    sanitize_parameters();

    gpio_init();
    beep_timer_init();

    thermo.relay_state = 0;
    thermo.update_display = 1;
    thermo.params_changed = 0;
    thermo.last_user_action = tick_count;
    thermo.display_on = 1;
    thermo.relay_last_switch = tick_count;
    thermo.current_hour = thermo.clock_minutes / 60;

    uint16_t data_pins[4] = {LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN};
    HD44780_init(&lcd, GPIOA, data_pins, GPIOA, LCD_RS_PIN, LCD_E_PIN);
    menu_close(&menu);

    ds18b20_init();

    beep(200);

    uint32_t btn_timer = 0, disp_timer = 0;

    while (1) {
        watchdog_feed();

        // Temperature measurement and relay control
        sensor_task();
        control_task();

        // Buttons are polled every BUTTON_POLL_MS
        if (tick_count - btn_timer >= BUTTON_POLL_MS) {
            btn_timer = tick_count;
            buttons_task();
        }

        statistics_task();
        ui_timeout_task();

        // Display is redrawn only when something changed
        if (thermo.update_display && thermo.display_on && tick_count - disp_timer >= DISPLAY_REFRESH_MS) {
            disp_timer = tick_count;
            display_refresh();
        }

        // Delayed auto-save: SAVE_DELAY_MS after the last key press and outside the menu
        if (thermo.params_changed && thermo.auto_save && !menu_is_open(&menu) &&
            tick_count - thermo.last_user_action >= SAVE_DELAY_MS) {
            save_if_changed();
        }

        // Sleep until the next interrupt (SysTick wakes the core every 1 ms)
        if (thermo.power_save) __WFI();
    }
}
