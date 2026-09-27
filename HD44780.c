#include "HD44780.h"
#include "main.h" // Подключаем main.h для использования точных аппаратных delay_us и delay_ms

static void gpio_set(GPIO_TypeDef* port, uint16_t pin) {
    port->BSRR = pin;
}

static void gpio_reset(GPIO_TypeDef* port, uint16_t pin) {
    port->BRR = pin;
}

static void gpio_set_output(GPIO_TypeDef* port, uint16_t pin) {
    // Безопасный перебор битов (исключает бесконечный цикл)
    for(int i = 0; i < 16; i++) {
        if (pin & (1 << i)) {
            port->MODER &= ~(0x3 << (i * 2));
            port->MODER |=  (0x1 << (i * 2));
            port->OTYPER &= ~(1 << i);
            port->OSPEEDR |= (0x3 << (i * 2)); // High speed для стабильности
            port->PUPDR &= ~(0x3 << (i * 2));
        }
    }
}

static void enable_gpio_clock(GPIO_TypeDef* port) {
    if (port == GPIOA) RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    else if (port == GPIOB) RCC->AHBENR |= RCC_AHBENR_GPIOBEN;
    else if (port == GPIOF) RCC->AHBENR |= RCC_AHBENR_GPIOFEN; 
}

static void pulse_enable(HD44780* lcd) {
    gpio_set(lcd->control_port, lcd->e_pin);
    delay_us(1); // Минимально необходимая длительность импульса E
    gpio_reset(lcd->control_port, lcd->e_pin);
    delay_us(50);
}

static void send_nibble(HD44780* lcd, uint8_t data) {
    for (int i = 0; i < 4; i++) gpio_reset(lcd->data_port, lcd->data_pins[i]);
    for (int i = 0; i < 4; i++) if (data & (1 << i)) gpio_set(lcd->data_port, lcd->data_pins[i]);
    pulse_enable(lcd);
}

static void send_command(HD44780* lcd, uint8_t cmd) {
    gpio_reset(lcd->control_port, lcd->rs_pin);
    send_nibble(lcd, cmd >> 4);
    send_nibble(lcd, cmd & 0x0F);
    if (cmd == HD44780_CLEAR_DISPLAY || cmd == HD44780_RETURN_HOME) delay_ms(2);
    else delay_us(50);
}

static void send_data(HD44780* lcd, uint8_t data) {
    gpio_set(lcd->control_port, lcd->rs_pin);
    send_nibble(lcd, data >> 4);
    send_nibble(lcd, data & 0x0F);
    delay_us(50);
}

void HD44780_init(HD44780* lcd,
                 GPIO_TypeDef* data_port, uint16_t data_pins[4],
                 GPIO_TypeDef* control_port, uint16_t rs_pin, uint16_t e_pin) {
    lcd->data_port = data_port;
    lcd->control_port = control_port;
    for (int i = 0; i < 4; i++) lcd->data_pins[i] = data_pins[i];
    lcd->rs_pin = rs_pin;
    lcd->e_pin = e_pin;

    enable_gpio_clock(data_port);
    enable_gpio_clock(control_port);

    for (int i = 0; i < 4; i++) gpio_set_output(data_port, data_pins[i]);
    gpio_set_output(control_port, rs_pin);
    gpio_set_output(control_port, e_pin);

    gpio_reset(control_port, rs_pin);
    gpio_reset(control_port, e_pin);
    for (int i = 0; i < 4; i++) gpio_reset(data_port, data_pins[i]);

    // Ожидание готовности питания дисплея (минимум 40мс по даташиту)
    delay_ms(50);

    // Строгая последовательность инициализации 4-битного режима по даташиту
    send_nibble(lcd, 0x03);
    delay_ms(5);
    send_nibble(lcd, 0x03);
    delay_us(150);
    send_nibble(lcd, 0x03);
    send_nibble(lcd, 0x02);

    // Настройка параметров
    send_command(lcd, HD44780_FUNCTION_SET | HD44780_4BIT_MODE | HD44780_2LINE | HD44780_5x8DOTS);
    send_command(lcd, HD44780_DISPLAY_CONTROL | HD44780_DISPLAY_OFF);
    send_command(lcd, HD44780_CLEAR_DISPLAY);
    send_command(lcd, HD44780_ENTRY_MODE_SET | HD44780_ENTRY_LEFT);

    // Включение дисплея
    lcd->display_control = HD44780_DISPLAY_ON | HD44780_CURSOR_OFF | HD44780_BLINK_OFF;
    send_command(lcd, HD44780_DISPLAY_CONTROL | lcd->display_control);
    delay_ms(2);
}

void HD44780_clear(HD44780* lcd) {
    send_command(lcd, HD44780_CLEAR_DISPLAY);
    delay_ms(2); // Очистка требует длительного времени
}

void HD44780_home(HD44780* lcd) {
    send_command(lcd, HD44780_RETURN_HOME);
    delay_ms(2); // Возврат каретки требует длительного времени
}

void HD44780_cursor_to(HD44780* lcd, uint8_t col, uint8_t row) {
    uint8_t row_offsets[] = {HD44780_ROW0_ADDR, HD44780_ROW1_ADDR};
    if (row >= 2) row = 1;
    send_command(lcd, HD44780_SET_DDRAM_ADDR | (col + row_offsets[row]));
}

void HD44780_put_str(HD44780* lcd, const char* str) {
    while (*str) send_data(lcd, (uint8_t)*str++);
}

void HD44780_put_char(HD44780* lcd, char c) {
    send_data(lcd, (uint8_t)c);
}

void HD44780_display_on(HD44780* lcd) {
    lcd->display_control |= HD44780_DISPLAY_ON;
    send_command(lcd, HD44780_DISPLAY_CONTROL | lcd->display_control);
}

void HD44780_display_off(HD44780* lcd) {
    lcd->display_control &= ~HD44780_DISPLAY_ON;
    send_command(lcd, HD44780_DISPLAY_CONTROL | lcd->display_control);
}

void HD44780_cursor_on(HD44780* lcd) {
    lcd->display_control |= HD44780_CURSOR_ON;
    send_command(lcd, HD44780_DISPLAY_CONTROL | lcd->display_control);
}

void HD44780_cursor_off(HD44780* lcd) {
    lcd->display_control &= ~HD44780_CURSOR_ON;
    send_command(lcd, HD44780_DISPLAY_CONTROL | lcd->display_control);
}