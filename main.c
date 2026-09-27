#include "HD44780.h"
#include "menu_system.h"
#include "flash_storage.h"
#include "main.h"

#ifdef DEBUG_BUTTONS
volatile uint8_t virtual_btn_up = 0;
volatile uint8_t virtual_btn_down = 0;
volatile uint8_t virtual_btn_enter = 0;
#endif

// ==================== Переменные для меню ====================
void * current_edit_value = NULL;
uint8_t current_edit_type = 0;
char edit_buffer[12]; // Буфер для 8x2 экрана

// ==================== Структуры и глобальные переменные ====================
thermostat_t thermo;
HD44780 lcd;
menu_state_t global_menu_state;

volatile uint32_t tick_count = 0;

// Управление неблокирующим beep на PF1 (пассивный бузер)
static volatile uint8_t beep_active = 0;       // 1 - звук воспроизводится
static volatile uint16_t beep_half_periods = 0; // оставшиеся полупериоды (2 полупериода = 1 мс при 2 кГц)

// ==================== Хелперы для STM32F030F4 (Экономия Flash-памяти) ====================

// Вспомогательная функция для перевода числа в строку (экономит память)
static void u32tostr(uint32_t val, char *dst, uint8_t is_signed) {
    char temp[11];
    uint8_t i = 0;
    uint8_t negative = 0;

    if (is_signed) {
        int32_t sval = (int32_t)val;
        if (sval < 0) {
            negative = 1;
            val = (uint32_t)(-sval);
        }
    }

    // Извлекаем цифры (они получаются в обратном порядке)
    do {
        temp[i++] = (val % 10) + '0';
        val /= 10;
    } while (val > 0);

    if (negative) {
        temp[i++] = '-';
    }

    // Переворачиваем строку в целевой буфер
    while (i > 0) {
        *dst++ = temp[--i];
    }
    *dst = '\0';
}

// Легковесная замена стандартному snprintf
int mini_snprintf(char *buffer, size_t buf_size, const char *format, ...) {
    if (buffer == NULL || buf_size == 0) return 0;
    
    va_list args;
    va_start(args, format);
    
    size_t count = 0;
    const char *p = format;
    
    while (*p != '\0' && count < buf_size - 1) {
        if (*p != '%') {
            buffer[count++] = *p++;
            continue;
        }
        
        p++; // Пропускаем символ '%'
        
        uint8_t left_align = 0;
        if (*p == '-') {
            left_align = 1;
            p++;
        }
        
        uint8_t width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        
        char tmp_buf[12];
        const char *str_val = tmp_buf;
        char c_val[2] = {0, 0};
        
        switch (*p) {
            case 'd': {
                int32_t val = va_arg(args, int32_t);
                u32tostr((uint32_t)val, tmp_buf, 1);
                break;
            }
            case 'u': {
                uint32_t val = va_arg(args, uint32_t);
                u32tostr(val, tmp_buf, 0);
                break;
            }
            case 's': {
                str_val = va_arg(args, const char *);
                if (!str_val) str_val = "(null)";
                break;
            }
            case 'c': {
                c_val[0] = (char)va_arg(args, int);
                str_val = c_val;
                break;
            }
            case '%': {
                c_val[0] = '%';
                str_val = c_val;
                break;
            }
            default: {
                // Игнорируем неизвестные спецификаторы
                p++;
                continue;
            }
        }
        
        // Считаем длину подставляемого значения
        size_t len = 0;
        while (str_val[len] != '\0') len++;
        
        // Заполнение пробелами слева (выравнивание вправо, например %5s)
        if (!left_align && width > len) {
            for (uint8_t i = 0; i < width - len && count < buf_size - 1; i++) {
                buffer[count++] = ' ';
            }
        }
        
        // Копирование самого значения
        for (size_t i = 0; i < len && count < buf_size - 1; i++) {
            buffer[count++] = str_val[i];
        }
        
        // Заполнение пробелами справа (выравнивание влево, например %-3s)
        if (left_align && width > len) {
            for (uint8_t i = 0; i < width - len && count < buf_size - 1; i++) {
                buffer[count++] = ' ';
            }
        }
        
        p++; // Пропускаем символ спецификатора (d, s, c и т.д.)
    }
    
    buffer[count] = '\0';
    va_end(args);
    
    return count;
}

// Перевод float в строку без использования %f (экономит ~8 КБ Flash)
void format_float_to_str(char * buf, size_t size, float val) {
  if (val < 0.0f) {
    int i_val = (int)(-val);
    int f_val = (int)((-val - i_val) * 10.0f + 0.5f);
    if (f_val > 9) {
      i_val++;
      f_val = 0;
    }
    if (i_val == 0) mini_snprintf(buf, size, "-0.%1d", f_val);
    else mini_snprintf(buf, size, "-%d.%1d", i_val, f_val);
  } else {
    int i_val = (int) val;
    int f_val = (int)((val - i_val) * 10.0f + 0.5f);
    if (f_val > 9) {
      i_val++;
      f_val = 0;
    }
    mini_snprintf(buf, size, "%d.%1d", i_val, f_val);
  }
}

// Универсальная настройка пинов выхода
void setup_gpio_output(GPIO_TypeDef * port, uint16_t pin_mask) {
  for (int i = 0; i < 16; i++) {
    if (pin_mask & (1 << i)) {
      port->MODER &= ~(0x3 << (i * 2));
      port->MODER |= (0x1 << (i * 2));
      port->OTYPER &= ~(1 << i);
      port->OSPEEDR |= (0x3 << (i * 2)); // High speed
    }
  }
}

// Универсальная настройка пинов входа с подтяжкой (Pull-Up)
void setup_gpio_input_pullup(GPIO_TypeDef * port, uint16_t pin_mask) {
  for (int i = 0; i < 16; i++) {
    if (pin_mask & (1 << i)) {
      port->MODER &= ~(0x3 << (i * 2));
      port->PUPDR &= ~(0x3 << (i * 2));
      port->PUPDR |= (0x1 << (i * 2));
    }
  }
}

// ==================== Реализация функций меню ====================

// Функции для редактирования float-параметров
void menu_setpoint_action(void) {
  current_edit_value = & thermo.setpoint;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_hysteresis_action(void) {
  current_edit_value = & thermo.hysteresis;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_calibration_action(void) {
  current_edit_value = & thermo.calibration;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_temp_min_action(void) {
  current_edit_value = & thermo.temp_min;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_temp_max_action(void) {
  current_edit_value = & thermo.temp_max;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_pid_kp_action(void) {
  current_edit_value = & thermo.kp;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_pid_ki_action(void) {
  current_edit_value = & thermo.ki;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_pid_kd_action(void) {
  current_edit_value = & thermo.kd;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_pid_output_limit_action(void) {
  current_edit_value = & thermo.pid_output_limit;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_day_setpoint_action(void) {
  current_edit_value = & thermo.day_setpoint;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_night_setpoint_action(void) {
  current_edit_value = & thermo.night_setpoint;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_cal_point1_temp_action(void) {
  current_edit_value = & thermo.cal_point1_temp;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_cal_point1_measured_action(void) {
  current_edit_value = & thermo.cal_point1_measured;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_cal_point2_temp_action(void) {
  current_edit_value = & thermo.cal_point2_temp;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_cal_point2_measured_action(void) {
  current_edit_value = & thermo.cal_point2_measured;
  current_edit_type = 0;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}

// Функции для редактирования uint8 параметров
void menu_regulator_type_action(void) {
  current_edit_value = & thermo.regulator_type;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_mode_action(void) {
  current_edit_value = & thermo.mode;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_relay_logic_action(void) {
  current_edit_value = & thermo.relay_logic;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_auto_save_action(void) {
  current_edit_value = & thermo.auto_save;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_beep_action(void) {
  current_edit_value = & thermo.beep_enabled;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_display_timeout_action(void) {
  current_edit_value = & thermo.display_timeout;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_temp_units_action(void) {
  current_edit_value = & thermo.temp_units;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_safety_enabled_action(void) {
  current_edit_value = & thermo.safety_enabled;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_temp_filter_action(void) {
  current_edit_value = & thermo.temp_filter;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_temp_resolution_action(void) {
  current_edit_value = & thermo.temp_resolution;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_relay_delay_action(void) {
  current_edit_value = & thermo.relay_delay;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_cycle_protection_action(void) {
  current_edit_value = & thermo.cycle_protection;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_schedule_enabled_action(void) {
  current_edit_value = & thermo.schedule_enabled;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_day_start_action(void) {
  current_edit_value = & thermo.day_start_hour;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_night_start_action(void) {
  current_edit_value = & thermo.night_start_hour;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_display_contrast_action(void) {
  current_edit_value = & thermo.display_contrast;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_display_rotation_action(void) {
  current_edit_value = & thermo.display_rotation;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_display_metrics_action(void) {
  current_edit_value = & thermo.display_metrics;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_power_save_action(void) {
  current_edit_value = & thermo.power_save;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_update_interval_action(void) {
  current_edit_value = & thermo.update_interval;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_manual_mode_action(void) {
  current_edit_value = & thermo.manual_mode;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_key_repeat_action(void) {
  current_edit_value = & thermo.key_repeat;
  current_edit_type = 1;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}

// Функции для редактирования uint16 параметров
void menu_pid_interval_action(void) {
  current_edit_value = & thermo.pid_interval;
  current_edit_type = 2;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_pwm_period_action(void) {
  current_edit_value = & thermo.pwm_period;
  current_edit_type = 2;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_key_repeat_delay_action(void) {
  current_edit_value = & thermo.key_repeat_delay;
  current_edit_type = 2;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_key_repeat_rate_action(void) {
  current_edit_value = & thermo.key_repeat_rate;
  current_edit_type = 2;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_debounce_time_action(void) {
  current_edit_value = & thermo.debounce_time;
  current_edit_type = 2;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}

// Функции для отображения статистики (uint32)
void menu_show_runtime_action(void) {
  current_edit_value = & thermo.total_runtime;
  current_edit_type = 3;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}
void menu_show_cycles_action(void) {
  current_edit_value = & thermo.relay_cycles;
  current_edit_type = 3;
  global_menu_state.edit_mode = 1;
  update_menu_display();
}

// Функции действий (без редактирования)
void menu_factory_reset_action(void) {
  set_default_parameters();
  save_parameters();
  thermo.update_display = 1;
  global_menu_state.current_menu = NULL;
  beep(1000);
}
void menu_clear_stats_action(void) {
  thermo.total_runtime = 0;
  thermo.relay_cycles = 0;
  thermo.params_changed = 1;
  save_if_changed();
  beep(200);
}
void menu_back_action(void) {
  global_menu_state.current_menu = NULL;
  thermo.update_display = 1;
  save_if_changed();
}

// ==================== Определение структуры меню ====================

MENU_ITEMS(basic_menu) = {
  MENU_LEAF(101, 1, "Reg", menu_regulator_type_action),
  MENU_LEAF(102, 1, "Setp", menu_setpoint_action),
  MENU_LEAF(103, 1, "Hyst", menu_hysteresis_action),
  MENU_LEAF(104, 1, "Cal", menu_calibration_action),
  MENU_LEAF(105, 1, "Mode", menu_mode_action),
  MENU_LEAF(106, 1, "Relay", menu_relay_logic_action),
  MENU_LEAF(107, 1, "Units", menu_temp_units_action),
  MENU_LEAF(108, 1, "Safe", menu_safety_enabled_action),
  MENU_LEAF(109, 1, "Min", menu_temp_min_action),
  MENU_LEAF(110, 1, "Max", menu_temp_max_action),
  MENU_LEAF(111, 1, "Manual", menu_manual_mode_action),
  MENU_LEAF(112, 1, "Back", menu_back_action)
};

MENU_ITEMS(pid_menu) = {
  MENU_LEAF(201, 2, "Kp", menu_pid_kp_action),
  MENU_LEAF(202, 2, "Ki", menu_pid_ki_action),
  MENU_LEAF(203, 2, "Kd", menu_pid_kd_action),
  MENU_LEAF(204, 2, "P.Int", menu_pid_interval_action),
  MENU_LEAF(205, 2, "P.Per", menu_pwm_period_action),
  MENU_LEAF(206, 2, "Lim", menu_pid_output_limit_action),
  MENU_LEAF(207, 2, "Back", menu_back_action)
};

MENU_ITEMS(advanced_menu) = {
  MENU_LEAF(301, 3, "Filt", menu_temp_filter_action),
  MENU_LEAF(302, 3, "Res", menu_temp_resolution_action),
  MENU_LEAF(303, 3, "Dly", menu_relay_delay_action),
  MENU_LEAF(304, 3, "Cyc", menu_cycle_protection_action),
  MENU_LEAF(305, 3, "Sch", menu_schedule_enabled_action),
  MENU_LEAF(306, 3, "Day", menu_day_setpoint_action),
  MENU_LEAF(307, 3, "Night", menu_night_setpoint_action),
  MENU_LEAF(308, 3, "DSta", menu_day_start_action),
  MENU_LEAF(309, 3, "NSta", menu_night_start_action),
  MENU_LEAF(310, 3, "Back", menu_back_action)
};

MENU_ITEMS(calibration_menu) = {
  MENU_LEAF(401, 4, "P1T", menu_cal_point1_temp_action),
  MENU_LEAF(402, 4, "P1M", menu_cal_point1_measured_action),
  MENU_LEAF(403, 4, "P2T", menu_cal_point2_temp_action),
  MENU_LEAF(404, 4, "P2M", menu_cal_point2_measured_action),
  MENU_LEAF(405, 4, "Back", menu_back_action)
};

MENU_ITEMS(display_menu) = {
  MENU_LEAF(501, 5, "Cont", menu_display_contrast_action),
  MENU_LEAF(502, 5, "Rot", menu_display_rotation_action),
  MENU_LEAF(503, 5, "Metr", menu_display_metrics_action),
  MENU_LEAF(504, 5, "TOut", menu_display_timeout_action),
  MENU_LEAF(505, 5, "Back", menu_back_action)
};

MENU_ITEMS(system_menu) = {
  MENU_LEAF(601, 6, "Auto", menu_auto_save_action),
  MENU_LEAF(602, 6, "Beep", menu_beep_action),
  MENU_LEAF(603, 6, "Pwr", menu_power_save_action),
  MENU_LEAF(604, 6, "Upd", menu_update_interval_action),
  MENU_LEAF(605, 6, "KeyR", menu_key_repeat_action),
  MENU_LEAF(606, 6, "RptD", menu_key_repeat_delay_action),
  MENU_LEAF(607, 6, "RptR", menu_key_repeat_rate_action),
  MENU_LEAF(608, 6, "Deb", menu_debounce_time_action),
  MENU_LEAF(609, 6, "Fact", menu_factory_reset_action),
  MENU_LEAF(610, 6, "Back", menu_back_action)
};

MENU_ITEMS(stats_menu) = {
  MENU_LEAF(701, 7, "Run", menu_show_runtime_action),
  MENU_LEAF(702, 7, "Cycl", menu_show_cycles_action),
  MENU_LEAF(703, 7, "Clr", menu_clear_stats_action),
  MENU_LEAF(704, 7, "Back", menu_back_action)
};

MENU_ITEMS(main_menu) = {
  MENU_NODE(1, 0, "Basic", basic_menu),
  MENU_NODE(2, 0, "PID", pid_menu),
  MENU_NODE(3, 0, "Adv", advanced_menu),
  MENU_NODE(4, 0, "Cal", calibration_menu),
  MENU_NODE(5, 0, "Disp", display_menu),
  MENU_NODE(6, 0, "Sys", system_menu),
  MENU_NODE(7, 0, "Stats", stats_menu),
  MENU_LEAF(8, 0, "Back", menu_back_action)
};

// ==================== Аппаратные функции и задержки ====================

// Точная микросекундная задержка на базе SysTick (Важно для 1-Wire на Cortex-M0)
void delay_us(uint32_t us) {
  uint32_t ticks = us * (SystemCoreClock / 1000000);
  uint32_t start = SysTick->VAL;
  uint32_t load = SysTick->LOAD;
  uint32_t elapsed = 0;

  while (elapsed < ticks) {
    uint32_t current = SysTick->VAL;
    if (start >= current) elapsed += (start - current);
    else elapsed += (start + load - current);
    start = current;
  }
}

void delay_ms(uint32_t ms) {
  while (ms--) delay_us(1000);
}

/**
 * @brief  Подача звукового сигнала на пассивный бузер
 * @param  duration_ms: длительность сигнала в миллисекундах
 * @note   Частота 2 кГц (период 500 мкс). Функция блокирующая.
 */
/*
void beep(uint16_t duration_ms) {
    if (!thermo.beep_enabled) return;

    uint32_t cycles = duration_ms * 2;   // 2 периода за 1 мс
    for (uint32_t i = 0; i < cycles; i++) {
        BEEPER_PORT->BSRR = BEEPER_PIN;  // HIGH
        delay_us(250);
        BEEPER_PORT->BRR  = BEEPER_PIN;  // LOW
        delay_us(250);
    }
    BEEPER_PORT->BRR = BEEPER_PIN;       // гарантированно выключить
}
*/

/**
 * @brief Запуск воспроизведения звука на пассивном бузере (неблокирующий)
 * @param duration_ms Длительность звука в миллисекундах
 */
void beep(uint16_t duration_ms) {
    if (!thermo.beep_enabled) return;
    if (beep_active) return;   // Уже звучит — игнорируем

    // Количество полупериодов: при 2 кГц один полупериод = 250 мкс,
    // за 1 мс проходит 4 полупериода. Упростим: duration_ms * 4.
    beep_half_periods = duration_ms * 4;
    beep_active = 1;

    // Сбрасываем счётчик таймера и включаем прерывание
    TIM14->CNT = 0;
    TIM14->CR1 |= TIM_CR1_CEN;   // Убеждаемся, что таймер запущен
}

void system_clock_init(void)
{
  // 1. Включаем HSI (если вдруг выключен) и ждем готовности
  RCC->CR |= RCC_CR_HSION;
  while (!(RCC->CR & RCC_CR_HSIRDY));

  // 2. Настройка Flash: ОБЯЗАТЕЛЬНО ДО переключения на 48 МГц
  // Для 48 МГц на STM32F0 нужен 1 Cycle Latency (LATENCY = 0x01)
  FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;

  // 3. Если PLL уже включен — переключаемся на HSI и выключаем PLL для настройки
  if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL) {
    RCC->CFGR &= ~RCC_CFGR_SW;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);
  }
  RCC->CR &= ~RCC_CR_PLLON;
  while (RCC->CR & RCC_CR_PLLRDY);

  // 4. Настройка PLL: HSI/2 * 12 = 48 МГц
  // Убеждаемся, что PREDIV = /1 (для F030 это важно)
  RCC->CFGR2 &= ~RCC_CFGR2_PREDIV1; 
  
  // Выбираем источник PLL (HSI/2) и множитель x12
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_PLLMULL) | RCC_CFGR_PLLMULL12;

  // 5. Включаем PLL и ждем
  RCC->CR |= RCC_CR_PLLON;
  while (!(RCC->CR & RCC_CR_PLLRDY));

  // 6. Переключаем систему на PLL
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
  
  SystemCoreClockUpdate();
}

void gpio_init(void) {
  // Включаем тактирование портов
  RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOFEN;

  // Настраиваем выходы через универсальную функцию
  setup_gpio_output(RELAY_PORT, RELAY_PIN);
  setup_gpio_output(BEEPER_PORT, BEEPER_PIN);

  // Реле и бузер выкл по умолчанию
  RELAY_PORT->BRR = RELAY_PIN;
  BEEPER_PORT->BRR = BEEPER_PIN;

  // Настраиваем кнопки как входы с подтяжкой
  setup_gpio_input_pullup(BTN_PORT, BTN_UP_PIN | BTN_DOWN_PIN | BTN_ENTER_PIN);
}

void systick_init(void) {
  SysTick_Config(SystemCoreClock / 1000); // Прерывание каждую 1 мс
}

/**
 * @brief Инициализация TIM14 для генерации прерываний 2 кГц (управление пассивным бузером на PF1)
 * @note  Частота = 48 МГц / (PSC+1) / (ARR+1) = 48e6 / 48 / 500 = 2000 Гц
 */
void beep_timer_init(void) {
    // Включаем тактирование TIM14 и GPIOF
    RCC->APB1ENR |= RCC_APB1ENR_TIM14EN;
    RCC->AHBENR |= RCC_AHBENR_GPIOFEN;

    // PF1 настраиваем как push-pull выход (уже должно быть из gpio_init, но на всякий случай)
    GPIOF->MODER &= ~GPIO_MODER_MODER1;
    GPIOF->MODER |= GPIO_MODER_MODER1_0;   // Output
    GPIOF->OTYPER &= ~GPIO_OTYPER_OT_1;    // Push-pull
    GPIOF->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR1; // High speed

    // Настройка TIM14: PSC = 47 (делитель 48), ARR = 499 (период 500 мкс)
    TIM14->PSC = 47;      // 48 МГц / 48 = 1 МГц (счётчик тикает каждую 1 мкс)
    TIM14->ARR = 499;     // 500 тиков = 500 мкс → частота 2 кГц
    TIM14->CR1 = TIM_CR1_CEN;  // Запускаем таймер (счёт идёт, но прерывание пока отключено)

    // Разрешаем прерывание по обновлению (UEV)
    TIM14->DIER |= TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM14_IRQn);
}

void TIM14_IRQHandler(void) {
    if (TIM14->SR & TIM_SR_UIF) {
        TIM14->SR &= ~TIM_SR_UIF;   // Сбрасываем флаг

        if (beep_active) {
            // Переключаем PF1
            GPIOF->ODR ^= GPIO_ODR_1;

            // Уменьшаем счётчик полупериодов
            if (--beep_half_periods == 0) {
                // Время вышло — выключаем звук
                beep_active = 0;
                GPIOF->BRR = GPIO_BRR_BR_1;   // PF1 = 0 (гарантированно LOW)
                TIM14->CR1 &= ~TIM_CR1_CEN;   // Останавливаем таймер (экономия энергии)
            }
        } else {
            // Если beep не активен, таймер не должен генерировать прерывания,
            // но на всякий случай выключаем его
            TIM14->CR1 &= ~TIM_CR1_CEN;
        }
    }
}

// ==================== Логика Термостата ====================

float apply_temp_filter(float new_temp) {
  if (thermo.temp_filter == 0) return new_temp;
  float alpha = thermo.temp_filter / 10.0f;
  thermo.filtered_temp = alpha * new_temp + (1.0f - alpha) * thermo.filtered_temp;
  return thermo.filtered_temp;
}

void apply_calibration(float * temp) {
  if (thermo.cal_point1_temp == thermo.cal_point2_temp) return;
  float slope = (thermo.cal_point2_measured - thermo.cal_point1_measured) /
    (thermo.cal_point2_temp - thermo.cal_point1_temp);
  * temp = thermo.cal_point1_measured + slope * ( * temp - thermo.cal_point1_temp);
}

void update_onoff_thermostat(void) {
    uint8_t new_state = thermo.relay_state;

    // Вычисляем желаемое состояние реле в зависимости от режима (нагрев/охлаждение)
    if (thermo.mode == 0) { // Нагрев
        if (thermo.current_temp < (thermo.setpoint - thermo.hysteresis))
            new_state = 1;
        else if (thermo.current_temp >= thermo.setpoint)
            new_state = 0;
    } else { // Охлаждение
        if (thermo.current_temp > (thermo.setpoint + thermo.hysteresis))
            new_state = 1;
        else if (thermo.current_temp <= thermo.setpoint)
            new_state = 0;
    }

    // Если состояние изменилось, проверяем задержку между переключениями
    if (new_state != thermo.relay_state) {
        // Защита от слишком частого переключения (relay_delay в секундах)
        if (thermo.relay_delay > 0 && 
            (tick_count - thermo.relay_last_switch) < (uint32_t)thermo.relay_delay * 1000) {
            return; // Ещё не прошло relay_delay секунд – игнорируем смену состояния
        }

        // Обновляем время последнего переключения
        thermo.relay_last_switch = tick_count;

        // Меняем состояние реле
        thermo.relay_state = new_state;
        if (thermo.relay_logic == 0) { // Normally Open (NO)
            if (thermo.relay_state) RELAY_PORT->BSRR = RELAY_PIN;
            else RELAY_PORT->BRR = RELAY_PIN;
        } else { // Normally Closed (NC)
            if (thermo.relay_state) RELAY_PORT->BRR = RELAY_PIN;
            else RELAY_PORT->BSRR = RELAY_PIN;
        }

        // Увеличиваем счётчик циклов и запрашиваем обновление дисплея
        thermo.relay_cycles++;
        thermo.update_display = 1;

        // Короткий звуковой сигнал (для пассивного бузера – сгенерированный тон)
        beep(50);
    }
}

float compute_pid(float setpoint, float current) {
  // Правильный расчет ошибки для нагрева и охлаждения
  float error = (thermo.mode == 0) ? (setpoint - current) : (current - setpoint);

  float prop = thermo.kp * error;
  thermo.integral += error * thermo.ki;

  if (thermo.integral > thermo.pid_output_limit) thermo.integral = thermo.pid_output_limit;
  if (thermo.integral < -thermo.pid_output_limit) thermo.integral = -thermo.pid_output_limit;

  float deriv = thermo.kd * (error - thermo.previous_error);
  thermo.previous_error = error;

  float out = prop + thermo.integral + deriv;
  if (out > thermo.pid_output_limit) out = thermo.pid_output_limit;
  if (out < 0.0f) out = 0.0f;
  return out;
}

void update_pid_thermostat(void) {
  if (tick_count - thermo.last_pid_time >= thermo.pid_interval) {
    thermo.last_pid_time = tick_count;
    float out = compute_pid(thermo.setpoint, thermo.current_temp);
    thermo.pwm_on_time = (uint16_t)(out * thermo.pwm_period / 100.0f);
    thermo.pwm_cycle_start = tick_count;
    thermo.update_display = 1;
  }

  uint32_t cycle_time = (tick_count - thermo.pwm_cycle_start) / 1000;
  uint8_t new_state = (cycle_time < thermo.pwm_on_time) ? 1 : 0;

  if (cycle_time >= thermo.pwm_period) {
    thermo.pwm_cycle_start = tick_count;
    new_state = (thermo.pwm_on_time > 0) ? 1 : 0;
  }

  if (new_state != thermo.relay_state) {
    thermo.relay_state = new_state;
    if (thermo.relay_logic == 0) {
      if (thermo.relay_state) RELAY_PORT -> BSRR = RELAY_PIN;
      else RELAY_PORT -> BRR = RELAY_PIN;
    } else {
      if (thermo.relay_state) RELAY_PORT -> BRR = RELAY_PIN;
      else RELAY_PORT -> BSRR = RELAY_PIN;
    }
    thermo.relay_cycles++;
    thermo.update_display = 1;
    //beep(50);
  }
}

void handle_manual_mode(void) {
  if (thermo.manual_mode == 0) return;
  uint8_t desired = (thermo.manual_mode == 1) ? 1 : 0;
  if (desired != thermo.relay_state) {
    thermo.relay_state = desired;
    if (thermo.relay_logic == 0) {
      if (thermo.relay_state) RELAY_PORT -> BSRR = RELAY_PIN;
      else RELAY_PORT -> BRR = RELAY_PIN;
    } else {
      if (thermo.relay_state) RELAY_PORT -> BRR = RELAY_PIN;
      else RELAY_PORT -> BSRR = RELAY_PIN;
    }
    thermo.update_display = 1;
    beep(50);
  }
}

void handle_schedule(void) {
  if (!thermo.schedule_enabled) return;
  thermo.current_hour = (thermo.uptime_minutes / 60) % 24;
  if (thermo.current_hour >= thermo.day_start_hour && thermo.current_hour < thermo.night_start_hour)
    thermo.setpoint = thermo.day_setpoint;
  else
    thermo.setpoint = thermo.night_setpoint;
}

bool check_safety_limits(void) {
  if (!thermo.safety_enabled) return false;
  if (thermo.current_temp < thermo.temp_min || thermo.current_temp > thermo.temp_max) {
    if (thermo.relay_state && thermo.manual_mode == 0) {
      thermo.relay_state = 0;
      if (thermo.relay_logic == 0) RELAY_PORT -> BRR = RELAY_PIN;
      else RELAY_PORT -> BSRR = RELAY_PIN;
      thermo.update_display = 1;
    }
    return true; // Лимиты нарушены!
  }
  return false; // Все ок
}

// ==================== Неблокирующее чтение DS18B20 ====================
void thermostat_start_conversion(void) {
  if (thermo.ds_pending) return;
  ds18b20_start_conversion();
  thermo.ds_pending = 1;
}

void thermostat_process(void) {
  if (!thermo.ds_pending) return;
  if (!ds18b20_is_conversion_done()) return;

  float raw = ds18b20_read_result();
  if (raw <= -273.0f) return; // Игнор ошибки датчика
  
  thermo.ds_pending = 0;

  apply_calibration(&raw);
  thermo.current_temp = apply_temp_filter(raw + thermo.calibration);

  // Если сработала защита, термостат блокируется!
  if (check_safety_limits()) {
      // Ничего не делаем, реле уже выключено функцией защиты
  } 
  
  handle_schedule();

  if (thermo.manual_mode != 0) {
    handle_manual_mode();
  } else {
    if (thermo.regulator_type == 0) update_onoff_thermostat();
    else update_pid_thermostat();
  }
  thermo.update_display = 1;
}

// ==================== Отрисовка Дисплея (8x2 Строго) ====================

void convert_temperature(float * temp) {
  if (thermo.temp_units) * temp = * temp * 9.0f / 5.0f + 32.0f;
}

void update_main_display(void) {
  char buf[12];
  char temp_str[8];
  float disp_temp = thermo.current_temp, disp_set = thermo.setpoint;

  // Конвертируем значения (в C или F)
  convert_temperature(&disp_temp);
  convert_temperature(&disp_set);

  HD44780_clear(&lcd);

  // --- СТРОКА 1: Текущая температура ---
  // Используем функцию для перевода float в строку "XX.X"
  format_float_to_str(temp_str, sizeof(temp_str), disp_temp);
  
  HD44780_cursor_to(&lcd, 0, 0);
  mini_snprintf(buf, sizeof(buf), "%5s %c", temp_str, thermo.temp_units ? 'F' : 'C');
  HD44780_put_str(&lcd, buf);

  // --- СТРОКА 2: Уставка или статус ---
  HD44780_cursor_to(&lcd, 0, 1);
  
  if (thermo.manual_mode == 0) {
    if (thermo.regulator_type == 0) {
      // Режим обычного термостата (ON/OFF)
      format_float_to_str(temp_str, sizeof(temp_str), disp_set);
      // %4s дополнит строку уставки пробелами, %-3s выровняет статус влево
      mini_snprintf(buf, sizeof(buf), "%4s %-3s", temp_str, thermo.relay_state ? "ON" : "OFF");
      HD44780_put_str(&lcd, buf);
    } else {
      // Режим ПИД-регулятора
      uint8_t power = (thermo.pwm_period > 0) ? ((thermo.pwm_on_time * 100) / thermo.pwm_period) : 0;
      mini_snprintf(buf, sizeof(buf), "PWR:%3d%%", power);
      HD44780_put_str(&lcd, buf);
    }
  } else {
    // Ручной режим (просто выводим готовую строку)
    HD44780_put_str(&lcd, thermo.manual_mode == 1 ? " MAN ON " : " MAN OFF");
  }

  thermo.update_display = 0;
}

void update_menu_display(void) {
  if (global_menu_state.edit_mode && current_edit_value) {
    HD44780_clear(&lcd);
    HD44780_cursor_to(&lcd, 0, 0);
    const menu_item_t * item = &global_menu_state.current_menu[global_menu_state.selected_index];
    HD44780_put_str(&lcd, item -> text);

    HD44780_cursor_to(&lcd, 0, 1);

    if (current_edit_type == 0) { // float
      format_float_to_str(edit_buffer, sizeof(edit_buffer), *(float * ) current_edit_value);
      HD44780_put_str(&lcd, edit_buffer);
    } else if (current_edit_type == 1) { // uint8
      uint8_t value = * (uint8_t * ) current_edit_value;
      if (current_edit_value == & thermo.manual_mode) {
        HD44780_put_str(&lcd, (value == 0) ? "Auto" : ((value == 1) ? "Man On" : "Man Off"));
      } else if (current_edit_value == & thermo.relay_logic) {
        HD44780_put_str(&lcd, value ? "NC" : "NO");
      } else if (current_edit_value == & thermo.temp_units) {
        HD44780_put_str(&lcd, value ? "F" : "C");
      } else if (current_edit_value == & thermo.mode) {
        HD44780_put_str(&lcd, value ? "Cool" : "Heat");
      } else if (current_edit_value == & thermo.regulator_type) {
        HD44780_put_str(&lcd, value ? "PID" : "ON/OFF");
      } else if (current_edit_value == & thermo.key_repeat || current_edit_value == & thermo.auto_save ||
        current_edit_value == & thermo.beep_enabled || current_edit_value == & thermo.safety_enabled ||
        current_edit_value == & thermo.schedule_enabled || current_edit_value == & thermo.power_save) {
        HD44780_put_str(&lcd, value ? "On" : "Off");
      } else {
        mini_snprintf(edit_buffer, sizeof(edit_buffer), "%d", value);
        HD44780_put_str(&lcd, edit_buffer);
      }
    } else if (current_edit_type == 2) { // uint16
      uint16_t value = * (uint16_t * ) current_edit_value;
      if (current_edit_value == & thermo.pwm_period) {
        mini_snprintf(edit_buffer, sizeof(edit_buffer), "%ds", value);
      } else if (current_edit_value == & thermo.key_repeat_delay || current_edit_value == & thermo.debounce_time) {
        mini_snprintf(edit_buffer, sizeof(edit_buffer), "%dms", value);
      } else {
        mini_snprintf(edit_buffer, sizeof(edit_buffer), "%d", value);
      }
      HD44780_put_str(&lcd, edit_buffer);
    } else if (current_edit_type == 3) { // uint32
      mini_snprintf(edit_buffer, sizeof(edit_buffer), "%u", *(uint32_t * ) current_edit_value);
      HD44780_put_str(&lcd, edit_buffer);
    }
  } else {
    menu_draw(&global_menu_state, &lcd);
  }
}

void handle_display_timeout(void) {
  if (thermo.display_timeout && thermo.display_on &&
    tick_count - thermo.last_user_action > (uint32_t) thermo.display_timeout * 60000) {
    thermo.display_on = 0;
    HD44780_display_off(&lcd);
  }
}

// ==================== Статистика и Сохранение ====================
void update_statistics(void) {
  static uint32_t last = 0;
  if (tick_count - last >= 60000) {
    last = tick_count;
    if (thermo.relay_state) thermo.total_runtime++;
    thermo.uptime_minutes++;
  }
}

void save_if_changed(void) {
  if (thermo.params_changed && thermo.auto_save) {
    thermo.params_changed = 0;
    save_parameters();
  }
}

// ==================== Опрос Кнопок и Лимиты параметров ====================

// Безопасное редактирование значений (Ограничения)
void modify_edit_value(int8_t dir) {
  if (!current_edit_value) return;

  if (current_edit_type == 0) { // float
    float * v = (float * ) current_edit_value;
    * v += dir * 0.5f;
    if ( * v > 999.0f) * v = 999.0f;
    if ( * v < -99.0f) * v = -99.0f;
  } else if (current_edit_type == 1) { // uint8
    uint8_t * v = (uint8_t * ) current_edit_value;
    int16_t temp = * v + dir;

    if (current_edit_value == & thermo.manual_mode) {
      if (temp > 2) temp = (dir > 0) ? 2 : 0;
      if (temp < 0) temp = 0;
    } else if (
      current_edit_value == & thermo.mode || current_edit_value == & thermo.regulator_type ||
      current_edit_value == & thermo.relay_logic || current_edit_value == & thermo.temp_units ||
      current_edit_value == & thermo.auto_save || current_edit_value == & thermo.beep_enabled ||
      current_edit_value == & thermo.safety_enabled || current_edit_value == & thermo.schedule_enabled ||
      current_edit_value == & thermo.power_save || current_edit_value == & thermo.display_metrics ||
      current_edit_value == & thermo.display_rotation || current_edit_value == & thermo.key_repeat
    ) {
      if (temp > 1) temp = (dir > 0) ? 1 : 0;
      if (temp < 0) temp = 0;
    } else if (current_edit_value == & thermo.day_start_hour || current_edit_value == & thermo.night_start_hour) {
      if (temp > 23) temp = (dir > 0) ? 23 : 0;
      if (temp < 0) temp = 0;
    } else {
      if (temp > 255) temp = 255;
      if (temp < 0) temp = 0;
    }
    * v = (uint8_t) temp;
  } else if (current_edit_type == 2) { // uint16
    uint16_t * v = (uint16_t * ) current_edit_value;
    int32_t temp = * v + dir;

    if (current_edit_value == & thermo.pwm_period) {
      if (temp < 1) temp = 1; // Нельзя делить на ноль!
    }
    if (temp > 65535) temp = 65535;
    if (temp < 0) temp = 0;

    * v = (uint16_t) temp;
  }
}

uint8_t is_button_pressed(uint16_t pin, uint32_t * last_press, uint32_t * last_repeat,
  uint8_t * state, uint8_t * debounced) {
  #ifdef DEBUG_BUTTONS
  uint8_t raw;
  if (pin == BTN_UP_PIN)
    raw = virtual_btn_up;
  else if (pin == BTN_DOWN_PIN)
    raw = virtual_btn_down;
  else if (pin == BTN_ENTER_PIN)
    raw = virtual_btn_enter;
  else
    raw = 0;
  #else
  uint8_t raw = ((BTN_PORT -> IDR & pin) == 0) ? 1 : 0;
  #endif

  // 1. Если физическое состояние пина изменилось (пошел дребезг или нажатие)
  // Обновляем состояние и сбрасываем таймер
  if (raw != * state) {
    * last_press = tick_count;
    * state = raw;
  }

  // 2. Проверяем, оставался ли сигнал стабильным нужное время
  if ((tick_count - * last_press) >= thermo.debounce_time) {

    // Если стабильное состояние отличается от того, что мы уже зафиксировали
    if (raw != * debounced) {
      * debounced = raw; // Сохраняем новое уверенное состояние

      // Если это уверенное нажатие (а не отпускание)
      if ( * debounced == 1) {
        * last_repeat = tick_count; // Готовим таймер для автоповтора
        return 1; // Выдаем одиночный импульс нажатия
      }
    }
  }

  // 3. Обработка долгого удержания (автоповтор)
  if ( * debounced == 1 && thermo.key_repeat) {
    // Отсчитываем начальную задержку удержания от момента стабилизации (*last_press)
    if ((tick_count - * last_press) > thermo.key_repeat_delay) {

      // Генерируем повторные срабатывания с нужным интервалом
      if ((tick_count - * last_repeat) > thermo.key_repeat_rate) {
        * last_repeat = tick_count;
        return 1;
      }
    }
  }

  return 0;
}

void button_up_action(void) {
  thermo.last_user_action = tick_count;
  if (!thermo.display_on) {
    thermo.display_on = 1;
    HD44780_display_on(&lcd);
    return;
  }

  if (global_menu_state.current_menu == NULL) {
    if (thermo.manual_mode == 0) {
      thermo.setpoint += 0.5f;
      thermo.params_changed = 1;
      thermo.update_display = 1;
    }
  } else {
    if (global_menu_state.edit_mode && current_edit_value) {
      modify_edit_value(1);
      thermo.params_changed = 1;
      update_menu_display();
    } else if (!global_menu_state.edit_mode) {
      menu_handle_up(&global_menu_state);
      update_menu_display();
    }
  }
  beep(30);
}

void button_down_action(void) {
  thermo.last_user_action = tick_count;
  if (!thermo.display_on) {
    thermo.display_on = 1;
    HD44780_display_on(&lcd);
    return;
  }

  if (global_menu_state.current_menu == NULL) {
    if (thermo.manual_mode == 0) {
      thermo.setpoint -= 0.5f;
      thermo.params_changed = 1;
      thermo.update_display = 1;
    }
  } else {
    if (global_menu_state.edit_mode && current_edit_value) {
      modify_edit_value(-1);
      thermo.params_changed = 1;
      update_menu_display();
    } else if (!global_menu_state.edit_mode) {
      menu_handle_down(&global_menu_state);
      update_menu_display();
    }
  }
  beep(30);
}

void button_enter_action(void) {
  thermo.last_user_action = tick_count;
  if (!thermo.display_on) {
    thermo.display_on = 1;
    HD44780_display_on(&lcd);
    return;
  }

  if (global_menu_state.current_menu == NULL) {
    global_menu_state.current_menu = main_menu;
    global_menu_state.menu_item_count = COUNT_OF(main_menu);
    global_menu_state.selected_index = 0;
    global_menu_state.scroll_offset = 0;
    global_menu_state.edit_mode = 0;
    update_menu_display();
  } else {
    if (global_menu_state.edit_mode) {
      global_menu_state.edit_mode = 0;
      current_edit_value = NULL;
      update_menu_display();
    } else {
      menu_handle_enter(&global_menu_state);
    }
  }
  beep(50);
}

void handle_buttons_with_debounce(void) {
  if (is_button_pressed(BTN_UP_PIN, & thermo.btn_up_last_press, & thermo.btn_up_last_repeat, &
      thermo.btn_up_state, & thermo.btn_up_debounced)) {
    button_up_action();
  }

  if (is_button_pressed(BTN_DOWN_PIN, & thermo.btn_down_last_press, & thermo.btn_down_last_repeat, &
      thermo.btn_down_state, & thermo.btn_down_debounced)) {
    button_down_action();
  }

  if (is_button_pressed(BTN_ENTER_PIN, & thermo.btn_enter_last_press, & thermo.btn_enter_last_repeat, &
      thermo.btn_enter_state, & thermo.btn_enter_debounced)) {
    button_enter_action();
  }
}

// ==================== Прерывание системного таймера ====================
void SysTick_Handler(void) {
  tick_count++;
}

// ==================== Главный цикл (main) ====================
int main(void) {
  system_clock_init();

  // SysTick ДОЛЖЕН быть инициализирован ДО портов, чтобы заработали задержки 1-Wire датчика
  systick_init();
  gpio_init();
  beep_timer_init();

  load_parameters();

  thermo.relay_state = 0;
  thermo.update_display = 1;
  thermo.params_changed = 0;
  thermo.last_user_action = tick_count;
  thermo.display_on = 1;
  thermo.filtered_temp = thermo.setpoint;
  thermo.relay_last_switch = tick_count;
  thermo.integral = 0;
  thermo.previous_error = 0;
  thermo.last_pid_time = tick_count;
  thermo.pwm_cycle_start = tick_count;
  thermo.pwm_on_time = 0;
  thermo.uptime_minutes = 0;
  thermo.ds_pending = 0;

  // Инициализация дисплея
  uint16_t data_pins[4] = {
    LCD_D4_PIN,
    LCD_D5_PIN,
    LCD_D6_PIN,
    LCD_D7_PIN
  };
  HD44780_init( & lcd, GPIOA, data_pins, GPIOA, LCD_RS_PIN, LCD_E_PIN);

  menu_init( & global_menu_state, main_menu, COUNT_OF(main_menu));
  global_menu_state.current_menu = NULL;

  ds18b20_init();
  set_ds18b20_resolution(thermo.temp_resolution);

  beep(200);

  // Таймеры задач
  static uint32_t temp_timer = 0, disp_timer = 0, save_timer = 0, btn_timer = 0;

  while (1) {
    // Задача 1: Запуск датчика температуры
    if (tick_count - temp_timer >= (uint32_t) thermo.update_interval * 1000) {
      temp_timer = tick_count;
      thermostat_start_conversion();
    }

    // Задача 2: Опрос датчика
    thermostat_process();

    // Задача 3: Отрисовка дисплея (каждые 500мс)
    if (thermo.update_display && thermo.display_on && (tick_count - disp_timer >= 500)) {
      disp_timer = tick_count;
      if (global_menu_state.current_menu == NULL) update_main_display();
      else update_menu_display();
    }

    // Задача 4: Опрос кнопок не блокируя цикл (каждые 5мс)
    if (tick_count - btn_timer >= 5) {
      btn_timer = tick_count;
      handle_buttons_with_debounce();
    }

    // Задача 5: Фоновые процессы
    update_statistics();
    handle_display_timeout();

    // Задача 6: Отложенное сохранение настроек во Flash (раз в 10 секунд при изменениях)
    if (thermo.params_changed && thermo.auto_save && (tick_count - save_timer >= 10000)) {
      save_timer = tick_count;
      save_if_changed();
    }
  }
}