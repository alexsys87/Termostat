#include "main.h"

void ds18b20_init(void) {
    // DS18B20 на PB1 – настраиваем жестко как выход Открытый Сток (Open-Drain)
    // Благодаря этому нам больше не нужно тратить такты на переключение MODER туда-сюда.
    DS18B20_PORT->MODER &= ~(0x3 << (DS18B20_PIN_NUM * 2));
    DS18B20_PORT->MODER |=  (0x1 << (DS18B20_PIN_NUM * 2)); // Выход
    DS18B20_PORT->OTYPER |= DS18B20_PIN;                    // Open-Drain
    DS18B20_PORT->PUPDR &= ~(0x3 << (DS18B20_PIN_NUM * 2));
    DS18B20_PORT->PUPDR |=  (0x1 << (DS18B20_PIN_NUM * 2)); // Pull-Up включен
    // Установка High Speed для более четких фронтов (необязательно, но полезно)
    //DS18B20_PORT->OSPEEDR |= (0x3 << (DS18B20_PIN_NUM * 2)); 
    
    // По умолчанию отпускаем линию (высокий уровень за счет подтяжки)
    DS18B20_PORT->BSRR = DS18B20_PIN; 
}

// Функция сброса, возвращает true если датчик присутствует
bool ds18b20_reset(void) {
    uint32_t timeout;

    __disable_irq(); // Критически важно для стабильности таймингов!
    DS18B20_PORT->BRR = DS18B20_PIN;  // Притягиваем линию к GND
    delay_us(480);
    DS18B20_PORT->BSRR = DS18B20_PIN; // Отпускаем линию
    delay_us(60);
    __enable_irq();

    // Ждем, когда датчик притянет линию (Presence Pulse)
    timeout = 500;
    while ((DS18B20_PORT->IDR & DS18B20_PIN) && timeout--) delay_us(2);
    if (timeout == 0) return false;

    // Ждем, когда датчик отпустит линию
    timeout = 500;
    while (!(DS18B20_PORT->IDR & DS18B20_PIN) && timeout--) delay_us(2);
    if (timeout == 0) return false;
    
    delay_us(300); // Дожидаемся окончания тайм-слота
    return true;
}

void ds18b20_write_bit(uint8_t bit) {
    __disable_irq(); 
    DS18B20_PORT->BRR = DS18B20_PIN; // Притягиваем линию
    if (bit) {
        delay_us(5);                      // Для '1' отпускаем почти сразу
        DS18B20_PORT->BSRR = DS18B20_PIN; 
        delay_us(60);
    } else {
        delay_us(60);                     // Для '0' держим линию долго
        DS18B20_PORT->BSRR = DS18B20_PIN; 
        delay_us(5);
    }
    __enable_irq();
}

void ds18b20_write_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        ds18b20_write_bit(data & 0x01);
        data >>= 1;
    }
}

uint8_t ds18b20_read_bit(void) {
    uint8_t bit = 0;
    
    __disable_irq();
    DS18B20_PORT->BRR = DS18B20_PIN;  // Стартуем чтение
    delay_us(2);
    DS18B20_PORT->BSRR = DS18B20_PIN; // Отпускаем, даем датчику управлять
    delay_us(10);
    
    if (DS18B20_PORT->IDR & DS18B20_PIN) {
        bit = 1; // Если датчик не притянул линию, значит передает '1'
    }
    delay_us(50); // Ждем конца тайм-слота чтения
    __enable_irq();
    
    return bit;
}

uint8_t ds18b20_read_byte(void) {
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        data >>= 1;
        if (ds18b20_read_bit()) {
            data |= 0x80;
        }
    }
    return data;
}

// Запуск преобразования (неблокирующий)
bool ds18b20_start_conversion(void) {
    if (!ds18b20_reset()) return false;
    ds18b20_write_byte(0xCC);  // Skip ROM
    ds18b20_write_byte(0x44);  // Convert T
    thermo.ds_pending = 1;
    thermo.ds_start_time = tick_count;
    return true;
}

// Проверка завершения преобразования (прошло 750 мс для 12 бит)
bool ds18b20_is_conversion_done(void) {
    if (!thermo.ds_pending) return false;
    if (tick_count - thermo.ds_start_time >= 750) return true;
    return false;
}

// Считывание результата
float ds18b20_read_result(void) {
    if (!thermo.ds_pending) return -273.15f;

    uint8_t temp_l, temp_h;
    if (!ds18b20_reset()) {
        thermo.ds_pending = 0;
        return -273.15f; // Если датчик пропал в процессе конверсии
    }
    ds18b20_write_byte(0xCC);   // Skip ROM
    ds18b20_write_byte(0xBE);   // Read Scratchpad
    
    temp_l = ds18b20_read_byte();
    temp_h = ds18b20_read_byte();
    
    // Сбросим термостат, чтобы прервать чтение остальных байт Scratchpad
    ds18b20_reset();
    
    thermo.ds_pending = 0;
    
    int16_t raw_temp = (temp_h << 8) | temp_l;
    
    // Защита от ошибочного чтения при обрыве провода или включении (85°C)
    if (raw_temp == 0x0550 || raw_temp == (int16_t)0xFFFF || raw_temp == 0x0000) {
        // Вернем предыдущее значение или ошибку
        return thermo.current_temp; 
    }
    
    return raw_temp / 16.0f;
}

// Установка разрешения (9–12 бит)
void set_ds18b20_resolution(uint8_t res) {
    if (res < 9 || res > 12) return;
    if (!ds18b20_reset()) return;
    ds18b20_write_byte(0xCC);
    ds18b20_write_byte(0x4E); // Write Scratchpad
    ds18b20_write_byte(0x00); // TH Register
    ds18b20_write_byte(0x00); // TL Register
    ds18b20_write_byte((res - 9) << 5 | 0x1F); // Configuration Register
    if (!ds18b20_reset()) return;
    ds18b20_write_byte(0xCC);
    ds18b20_write_byte(0x48); // Copy Scratchpad to EEPROM
    delay_ms(15);             // Даем время на запись в EEPROM
}