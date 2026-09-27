#include "ui.h"
#include "thermostat.h"
#include "menu_system.h"
#include "HD44780.h"
#include "board.h"
#include <string.h>

static HD44780 lcd;
static menu_state_t menu;
static uint8_t display_on;
static uint32_t last_user_action;

// Staging values edited in the menu and applied when editing is finished
static uint8_t ui_confirm;      // Yes/No for dangerous actions
static uint8_t ui_tune;         // auto-tuning on/off

// ==================== Buttons ====================
typedef struct {
    uint8_t mask;           // BTN_xxx
    uint8_t allow_repeat;   // auto-repeat while held
    uint8_t raw;            // last sampled level, 1 = pressed
    uint8_t pressed;        // debounced state
    uint8_t repeat_count;   // auto-repeat events since press (for acceleration)
    uint32_t last_change;   // time of the last raw level change
    uint32_t last_event;    // time of the last press/repeat event
} button_t;

static button_t btn_up    = {.mask = BTN_UP, .allow_repeat = 1};
static button_t btn_down  = {.mask = BTN_DOWN, .allow_repeat = 1};
static button_t btn_enter = {.mask = BTN_ENTER, .allow_repeat = 0};  // holding Enter must not jump through menus

// ==================== Parameter descriptors ====================
// Limits and steps are in stored units (temperatures in 0.1 C)

static const char* const LBL_OFF_ON[] = {"Off", "On"};
static const char* const LBL_NO_YES[] = {"No", "Yes"};
static const char* const LBL_REG[]    = {"ON/OFF", "PID"};
static const char* const LBL_MODE[]   = {"Heat", "Cool"};
static const char* const LBL_RELAY[]  = {"NO", "NC"};
static const char* const LBL_UNITS[]  = {"C", "F"};
static const char* const LBL_MANUAL[] = {"Auto", "Man On", "Man Off"};
static const char* const LBL_SOURCE[] = {"S1", "S2", "S3", "S4", "Avg", "Min", "Max"};

#define CFG(field)                      &thermo.cfg.field
#define P_TEMP(f, st)                   {CFG(f), NULL, -550, 1250, st, PARAM_TEMP, 1, 0, UNIT_NONE}  // DS18B20 range -55..125 C
#define P_DELTA(f, lo, hi)              {CFG(f), NULL, lo, hi, 1, PARAM_TDELTA, 1, 0, UNIT_NONE}
#define P_U8(f, lo, hi, st, unit)       {CFG(f), NULL, lo, hi, st, PARAM_U8, 0, 0, unit}
#define P_U16(f, lo, hi, st, dec, unit) {CFG(f), NULL, lo, hi, st, PARAM_U16, dec, 0, unit}
#define P_ENUM(f, labels)               {CFG(f), labels, 0, COUNT_OF(labels) - 1, 1, PARAM_U8, 0, 0, UNIT_NONE}
#define P_VIEW(var, type, unit)         {&(var), NULL, 0, 0, 0, type, 0, PF_READONLY, unit}
#define P_CONFIRM                       {&ui_confirm, LBL_NO_YES, 0, 1, 1, PARAM_U8, 0, PF_NOSAVE, UNIT_NONE}

// Basic
static const param_desc_t P_REG       = P_ENUM(regulator_type, LBL_REG);
static const param_desc_t P_SETPOINT  = P_TEMP(setpoint, 5);
static const param_desc_t P_HYST      = P_DELTA(hysteresis, 1, 200);         // 0.1..20.0
static const param_desc_t P_MODE      = P_ENUM(mode, LBL_MODE);
static const param_desc_t P_RELAY     = P_ENUM(relay_logic, LBL_RELAY);
static const param_desc_t P_RDELAY    = P_U8(relay_delay, 0, 255, 1, UNIT_S);
static const param_desc_t P_CYCLE     = P_U8(cycle_protection, 0, 60, 1, UNIT_MIN);
static const param_desc_t P_UNITS     = P_ENUM(temp_units, LBL_UNITS);
static const param_desc_t P_MANUAL    = P_ENUM(manual_mode, LBL_MANUAL);
// Alarm
static const param_desc_t P_SAFETY    = P_ENUM(safety_enabled, LBL_OFF_ON);
static const param_desc_t P_TEMP_MIN  = P_TEMP(temp_min, 5);
static const param_desc_t P_TEMP_MAX  = P_TEMP(temp_max, 5);
static const param_desc_t P_ALARM_HYS = P_DELTA(alarm_hyst, 0, 100);         // 0..10.0
static const param_desc_t P_LATCH     = P_ENUM(alarm_latch, LBL_OFF_ON);
static const param_desc_t P_SOUND     = P_ENUM(alarm_sound, LBL_OFF_ON);
// PID
static const param_desc_t P_KP        = P_U16(kp, 0, 9999, 1, 1, UNIT_NONE);      // 0..999.9 %/C
static const param_desc_t P_KI        = P_U16(ki, 0, 30000, 1, 3, UNIT_NONE);     // 0..30.000 %/(C*s)
static const param_desc_t P_KD        = P_U16(kd, 0, 30000, 1, 0, UNIT_NONE);     // 0..30000 %*s/C
static const param_desc_t P_PWM_PER   = P_U16(pwm_period, 1, 600, 1, 0, UNIT_S);
static const param_desc_t P_PID_LIM   = P_U8(pid_output_limit, 1, 100, 1, UNIT_PERCENT);
static const param_desc_t P_TUNE      = {&ui_tune, LBL_OFF_ON, 0, 1, 1, PARAM_U8, 0, PF_NOSAVE, UNIT_NONE};
// Sensor
static const param_desc_t P_SOURCE    = P_ENUM(sensor_source, LBL_SOURCE);
static const param_desc_t P_FOUND     = P_VIEW(thermo.sensor_count, PARAM_U8, UNIT_NONE);
static const param_desc_t P_FILTER    = P_U8(temp_filter, 0, 10, 1, UNIT_NONE);
static const param_desc_t P_RES       = P_U8(temp_resolution, 9, 12, 1, UNIT_BIT);
static const param_desc_t P_UPDATE    = P_U8(update_interval, 1, 60, 1, UNIT_S);
// Calibration
static const param_desc_t P_OFFSET    = P_DELTA(calibration, -100, 100);     // -10.0..+10.0
static const param_desc_t P_CAL1_R    = P_TEMP(cal_p1_ref, 1);
static const param_desc_t P_CAL1_M    = P_TEMP(cal_p1_meas, 1);
static const param_desc_t P_CAL2_R    = P_TEMP(cal_p2_ref, 1);
static const param_desc_t P_CAL2_M    = P_TEMP(cal_p2_meas, 1);
// System
static const param_desc_t P_AUTOSAVE  = P_ENUM(auto_save, LBL_OFF_ON);
static const param_desc_t P_BEEP      = P_ENUM(beep_enabled, LBL_OFF_ON);
static const param_desc_t P_PWR       = P_ENUM(power_save, LBL_OFF_ON);
static const param_desc_t P_TIMEOUT   = P_U8(display_timeout, 0, 255, 1, UNIT_MIN);
static const param_desc_t P_KEYREP    = P_ENUM(key_repeat, LBL_OFF_ON);
static const param_desc_t P_REP_DELAY = P_U16(key_repeat_delay, 100, 3000, 50, 0, UNIT_MS);
static const param_desc_t P_REP_RATE  = P_U16(key_repeat_rate, 20, 1000, 10, 0, UNIT_MS);
static const param_desc_t P_DEBOUNCE  = P_U16(debounce_time, 5, 200, 5, 0, UNIT_MS);
static const param_desc_t P_FACTORY   = P_CONFIRM;
// Statistics
static const param_desc_t P_T1        = P_VIEW(thermo.sensors[0].temp, PARAM_TEMP, UNIT_NONE);
static const param_desc_t P_T2        = P_VIEW(thermo.sensors[1].temp, PARAM_TEMP, UNIT_NONE);
static const param_desc_t P_T3        = P_VIEW(thermo.sensors[2].temp, PARAM_TEMP, UNIT_NONE);
static const param_desc_t P_T4        = P_VIEW(thermo.sensors[3].temp, PARAM_TEMP, UNIT_NONE);
static const param_desc_t P_DAY_MIN   = P_VIEW(thermo.day_min, PARAM_TEMP, UNIT_NONE);
static const param_desc_t P_DAY_MAX   = P_VIEW(thermo.day_max, PARAM_TEMP, UNIT_NONE);
static const param_desc_t P_RUNTIME   = P_VIEW(thermo.cfg.total_runtime, PARAM_U32, UNIT_M);
static const param_desc_t P_CYCLES    = P_VIEW(thermo.cfg.relay_cycles, PARAM_U32, UNIT_NONE);
static const param_desc_t P_CLEAR     = P_CONFIRM;

// ==================== Menu structure (texts up to 7 characters) ====================

static void menu_save_action(void);
static void menu_scan_action(void);

MENU_ITEMS(basic_menu) = {
    MENU_PARAM("Reg", P_REG),
    MENU_PARAM("Setp", P_SETPOINT),
    MENU_PARAM("Hyst", P_HYST),
    MENU_PARAM("Mode", P_MODE),
    MENU_PARAM("Relay", P_RELAY),
    MENU_PARAM("Dly", P_RDELAY),
    MENU_PARAM("Cyc", P_CYCLE),
    MENU_PARAM("Units", P_UNITS),
    MENU_PARAM("Manual", P_MANUAL),
    MENU_BACK("Back")
};

MENU_ITEMS(alarm_menu) = {
    MENU_PARAM("Safe", P_SAFETY),
    MENU_PARAM("Min", P_TEMP_MIN),
    MENU_PARAM("Max", P_TEMP_MAX),
    MENU_PARAM("AHys", P_ALARM_HYS),
    MENU_PARAM("Latch", P_LATCH),
    MENU_PARAM("Sound", P_SOUND),
    MENU_BACK("Back")
};

MENU_ITEMS(pid_menu) = {
    MENU_PARAM("Kp", P_KP),
    MENU_PARAM("Ki", P_KI),
    MENU_PARAM("Kd", P_KD),
    MENU_PARAM("P.Per", P_PWM_PER),
    MENU_PARAM("Lim", P_PID_LIM),
    MENU_PARAM("Tune", P_TUNE),
    MENU_BACK("Back")
};

MENU_ITEMS(sensor_menu) = {
    MENU_PARAM("Src", P_SOURCE),
    MENU_PARAM("Found", P_FOUND),
    MENU_ACTION("Scan", menu_scan_action),
    MENU_PARAM("Filt", P_FILTER),
    MENU_PARAM("Res", P_RES),
    MENU_PARAM("Upd", P_UPDATE),
    MENU_BACK("Back")
};

MENU_ITEMS(calibration_menu) = {
    MENU_PARAM("Offs", P_OFFSET),
    MENU_PARAM("P1T", P_CAL1_R),
    MENU_PARAM("P1M", P_CAL1_M),
    MENU_PARAM("P2T", P_CAL2_R),
    MENU_PARAM("P2M", P_CAL2_M),
    MENU_BACK("Back")
};

MENU_ITEMS(system_menu) = {
    MENU_PARAM("Auto", P_AUTOSAVE),
    MENU_ACTION("Save", menu_save_action),
    MENU_PARAM("Beep", P_BEEP),
    MENU_PARAM("Pwr", P_PWR),
    MENU_PARAM("TOut", P_TIMEOUT),
    MENU_PARAM("KeyR", P_KEYREP),
    MENU_PARAM("RptD", P_REP_DELAY),
    MENU_PARAM("RptR", P_REP_RATE),
    MENU_PARAM("Deb", P_DEBOUNCE),
    MENU_PARAM("Fact", P_FACTORY),
    MENU_BACK("Back")
};

MENU_ITEMS(stats_menu) = {
    MENU_PARAM("T1", P_T1),
    MENU_PARAM("T2", P_T2),
    MENU_PARAM("T3", P_T3),
    MENU_PARAM("T4", P_T4),
    MENU_PARAM("TMin", P_DAY_MIN),
    MENU_PARAM("TMax", P_DAY_MAX),
    MENU_PARAM("Run", P_RUNTIME),
    MENU_PARAM("Cycl", P_CYCLES),
    MENU_PARAM("Clr", P_CLEAR),
    MENU_BACK("Back")
};

MENU_ITEMS(main_menu) = {
    MENU_NODE("Basic", basic_menu),
    MENU_NODE("Alarm", alarm_menu),
    MENU_NODE("PID", pid_menu),
    MENU_NODE("Sensor", sensor_menu),
    MENU_NODE("Cal", calibration_menu),
    MENU_NODE("Sys", system_menu),
    MENU_NODE("Stats", stats_menu),
    MENU_BACK("Back")
};

// ==================== Display (8x2) ====================

// Temperature number in the selected units, one decimal, without the unit letter
static char* fmt_temp_number(char* dst, int16_t c10) {
    if (thermo.cfg.temp_units) c10 = temp_to_fahrenheit(c10);
    return fmt_fixed(dst, c10, 1);
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

    // --- Line 1: control temperature, unit and relay indicator ---
    if (thermo.sensor_error) {
        str_copy(line, "Sens.Err");
    } else if (!thermo.temp_valid) {
        str_copy(line, " --.-");
    } else {
        fmt_temp_number(num, thermo.temp);
        p = put_right(line, num, 5);
        *p++ = ' ';
        *p++ = thermo.cfg.temp_units ? 'F' : 'C';
        *p++ = thermo.relay_state ? '*' : ' ';
        *p = '\0';
    }
    HD44780_print_line(&lcd, 0, line);

    // --- Line 2: state ---
    if (thermo.tune.state == TUNE_FAILED) {
        str_copy(line, "TUNE ERR");
    } else if (thermo.tune.state == TUNE_RUNNING) {
        p = str_copy(line, "TUNE ");
        p = fmt_uint(p, thermo.tune.measured);
        *p++ = '/';
        fmt_uint(p, TUNE_MEASURED_CYCLES);
    } else if (thermo.cfg.manual_mode) {
        str_copy(line, thermo.cfg.manual_mode == 1 ? " MAN ON" : " MAN OFF");
    } else if (thermo.alarm_limit) {
        str_copy(line, "LIMIT!");
    } else if (thermo.alarm_latched) {
        str_copy(line, "LIM ACK?");
    } else if (thermo.cfg.regulator_type == 0) {
        // "25.0 OFF"; the space is dropped for 5-character values ("-10.5OFF")
        char* end = fmt_temp_number(num, thermo.cfg.setpoint);
        p = put_right(line, num, 4);
        if (end - num <= 4) *p++ = ' ';
        str_copy(p, thermo.relay_state ? "ON" : "OFF");
    } else {
        p = str_copy(line, "PWR:");
        fmt_uint(num, (uint32_t)(thermo.pid_output + PID_OUTPUT_SCALE / 2) / PID_OUTPUT_SCALE);
        p = put_right(p, num, 3);
        str_copy(p, "%");
    }
    HD44780_print_line(&lcd, 1, line);
}

static void display_refresh(void) {
    if (!display_on) return;
    param_fahrenheit = thermo.cfg.temp_units != 0;
    if (menu_is_open(&menu)) menu_draw(&menu, &lcd);
    else update_main_display();
    thermo.update_display = 0;
}

// ==================== Menu actions ====================

static void save_if_changed(void) {
    if (thermo.params_changed && thermo.cfg.auto_save) thermostat_save();
}

// Manual save (works also when auto-save is off)
static void menu_save_action(void) {
    thermostat_save();
    beep(200);
}

static void menu_scan_action(void) {
    thermostat_rescan();
    beep(100);
}

// Prepares staging values when editing starts
static void on_edit_begin(void) {
    ui_confirm = 0;
    ui_tune = (thermo.tune.state == TUNE_RUNNING);
}

// Applies staging values when editing is finished (by_timeout: never confirm dangerous actions)
static void on_edit_end(const param_desc_t* p, bool by_timeout) {
    if (by_timeout) {
        // nothing
    } else if (p == &P_FACTORY && ui_confirm) {
        settings_defaults(&thermo.cfg);
        thermostat_save();
        menu_close(&menu);
        beep(1000);
    } else if (p == &P_CLEAR && ui_confirm) {
        thermostat_clear_stats();
        beep(200);
    } else if (p == &P_TUNE) {
        if (ui_tune && thermo.tune.state != TUNE_RUNNING) {
            if (thermostat_tune_start()) {
                menu_close(&menu);          // show the progress on the main screen
            } else {
                board_beeper_start(500);    // no temperature, manual mode or alarm
            }
        } else if (!ui_tune) {
            thermostat_tune_stop();
        }
    }
}

// ==================== Buttons ====================

// Returns 1 on a debounced press and on every auto-repeat event
static uint8_t button_poll(button_t* b, uint8_t buttons) {
    uint8_t raw = (buttons & b->mask) ? 1 : 0;
    uint32_t now = tick_count;
    const settings_t* c = &thermo.cfg;

    // Level changed (press, release or bounce): restart the debounce timer
    if (raw != b->raw) {
        b->raw = raw;
        b->last_change = now;
        return 0;
    }
    if (now - b->last_change < c->debounce_time) return 0;

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
    if (raw && b->allow_repeat && c->key_repeat &&
        now - b->last_change >= c->key_repeat_delay &&
        now - b->last_event >= c->key_repeat_rate) {
        b->last_event = now;
        if (b->repeat_count < 255) b->repeat_count++;
        return 1;
    }
    return 0;
}

// Step multiplier while a key is held
static uint16_t button_accel(const button_t* b) {
    if (b->repeat_count > 40) return 100;
    if (b->repeat_count > 10) return 10;
    return 1;
}

// Common part of every key press. Returns true if the key press is consumed:
// it only woke the display or acknowledged an alarm.
static bool key_prelude(void) {
    last_user_action = tick_count;
    if (!display_on) {
        display_on = 1;
        HD44780_display_on(&lcd);
        display_refresh();
        return true;
    }
    if (menu_is_open(&menu)) {
        thermostat_mute();
        return false;
    }
    if (thermostat_acknowledge()) {
        display_refresh();
        return true;
    }
    return false;
}

static void button_updown_action(int8_t dir, uint16_t accel) {
    if (key_prelude()) return;

    if (!menu_is_open(&menu)) {
        // Main screen: change the setpoint (not in manual mode and not while tuning)
        if (thermo.cfg.manual_mode == 0 && thermo.tune.state != TUNE_RUNNING) {
            if (param_step(&P_SETPOINT, dir, 1)) thermo.params_changed = 1;
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
    if (key_prelude()) return;

    if (!menu_is_open(&menu)) {
        menu_open(&menu, main_menu, COUNT_OF(main_menu));
    } else {
        const param_desc_t* editing = menu_edit_param(&menu);
        menu_event_t evt = menu_handle_enter(&menu);
        if (evt == MENU_EVT_EDIT_BEGIN) {
            on_edit_begin();
        } else if (evt == MENU_EVT_EDIT_END) {
            on_edit_end(editing, false);
        } else if (evt == MENU_EVT_EXIT) {
            save_if_changed();
        }
    }
    display_refresh();
    beep(50);
}

static void buttons_task(void) {
    uint8_t buttons = board_buttons_read();

    if (button_poll(&btn_up, buttons)) button_updown_action(1, button_accel(&btn_up));
    if (button_poll(&btn_down, buttons)) button_updown_action(-1, button_accel(&btn_down));
    if (button_poll(&btn_enter, buttons)) button_enter_action();
}

// Menu and display inactivity timeouts
static void timeout_task(void) {
    uint32_t idle = tick_count - last_user_action;

    if (menu_is_open(&menu) && idle > MENU_TIMEOUT_MS) {
        const param_desc_t* p = menu_edit_param(&menu);
        if (p) on_edit_end(p, true);
        menu_close(&menu);
        save_if_changed();
        thermo.update_display = 1;
    }

    if (thermo.cfg.display_timeout && display_on && idle > (uint32_t)thermo.cfg.display_timeout * 60000) {
        display_on = 0;
        HD44780_display_off(&lcd);
    }
}

// ==================== Public ====================

void ui_sanitize_settings(void) {
    menu_clamp_params(main_menu, COUNT_OF(main_menu));
}

void ui_init(void) {
    uint16_t data_pins[4] = {LCD_D4_PIN, LCD_D5_PIN, LCD_D6_PIN, LCD_D7_PIN};
    HD44780_init(&lcd, LCD_PORT, data_pins, LCD_PORT, LCD_RS_PIN, LCD_E_PIN);
    menu_close(&menu);
    display_on = 1;
    last_user_action = tick_count;
    thermo.update_display = 1;
}

void ui_task(void) {
    static uint32_t btn_timer = 0, disp_timer = 0;

    if (tick_count - btn_timer >= BUTTON_POLL_MS) {
        btn_timer = tick_count;
        buttons_task();
    }

    timeout_task();

    // The display is redrawn only when something changed
    if (thermo.update_display && display_on && tick_count - disp_timer >= DISPLAY_REFRESH_MS) {
        disp_timer = tick_count;
        display_refresh();
    }

    // Delayed auto-save: SAVE_DELAY_MS after the last key press and outside the menu
    if (thermo.params_changed && thermo.cfg.auto_save && !menu_is_open(&menu) &&
        tick_count - last_user_action >= SAVE_DELAY_MS) {
        save_if_changed();
    }
}
