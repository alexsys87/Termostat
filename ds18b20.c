#include "main.h"

// ==================== DS18B20 commands ====================
#define DS18B20_CMD_SKIP_ROM        0xCC
#define DS18B20_CMD_CONVERT_T       0x44
#define DS18B20_CMD_READ_SCRATCH    0xBE
#define DS18B20_CMD_WRITE_SCRATCH   0x4E

// Raw value the sensor reports after power-up when no conversion was done (+85.0 C)
#define DS18B20_POWER_ON_VALUE      0x0550

void ds18b20_init(void) {
    // The line is configured once as an open-drain output with pull-up.
    // Writing 1 releases the line, writing 0 pulls it low, IDR always reflects the real level,
    // so there is no need to switch MODER back and forth.
    DS18B20_PORT->MODER &= ~(0x3 << (DS18B20_PIN_NUM * 2));
    DS18B20_PORT->MODER |=  (0x1 << (DS18B20_PIN_NUM * 2)); // Output
    DS18B20_PORT->OTYPER |= DS18B20_PIN;                    // Open-drain
    DS18B20_PORT->PUPDR &= ~(0x3 << (DS18B20_PIN_NUM * 2));
    DS18B20_PORT->PUPDR |=  (0x1 << (DS18B20_PIN_NUM * 2)); // Internal pull-up (an external 4.7k is still required)

    // Release the line (idle high)
    DS18B20_PORT->BSRR = DS18B20_PIN;
}

// Reset pulse, returns true if a sensor answered with a presence pulse
static bool ds18b20_reset(void) {
    bool presence;

    __disable_irq(); // Timing critical
    DS18B20_PORT->BRR = DS18B20_PIN;   // Pull the line low
    delay_us(480);
    DS18B20_PORT->BSRR = DS18B20_PIN;  // Release the line
    delay_us(70);                      // Presence pulse starts 15..60 us after release and lasts 60..240 us
    presence = (DS18B20_PORT->IDR & DS18B20_PIN) == 0;
    __enable_irq();

    delay_us(410); // Complete the 480 us receive time slot

    // After the presence pulse the line must be high again, otherwise it is shorted to GND
    return presence && (DS18B20_PORT->IDR & DS18B20_PIN);
}

static void ds18b20_write_bit(uint8_t bit) {
    __disable_irq();
    DS18B20_PORT->BRR = DS18B20_PIN;
    if (bit) {
        delay_us(5);                   // '1': release almost immediately
        DS18B20_PORT->BSRR = DS18B20_PIN;
        delay_us(60);
    } else {
        delay_us(60);                  // '0': keep the line low for the whole slot
        DS18B20_PORT->BSRR = DS18B20_PIN;
        delay_us(5);
    }
    __enable_irq();
}

static void ds18b20_write_byte(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        ds18b20_write_bit(data & 0x01);
        data >>= 1;
    }
}

static uint8_t ds18b20_read_bit(void) {
    uint8_t bit = 0;

    __disable_irq();
    DS18B20_PORT->BRR = DS18B20_PIN;   // Start the read slot
    delay_us(2);
    DS18B20_PORT->BSRR = DS18B20_PIN;  // Release, the sensor drives the line now
    delay_us(10);                      // Sample before 15 us from the slot start
    if (DS18B20_PORT->IDR & DS18B20_PIN) {
        bit = 1;
    }
    __enable_irq();

    delay_us(50); // Wait for the end of the slot
    return bit;
}

static uint8_t ds18b20_read_byte(void) {
    uint8_t data = 0;
    for (uint8_t i = 0; i < 8; i++) {
        data >>= 1;
        if (ds18b20_read_bit()) {
            data |= 0x80;
        }
    }
    return data;
}

// Dallas/Maxim CRC-8 (polynomial x^8 + x^5 + x^4 + 1)
static uint8_t ds18b20_crc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
        uint8_t byte = *data++;
        for (uint8_t i = 0; i < 8; i++) {
            uint8_t mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            byte >>= 1;
        }
    }
    return crc;
}

// Starts a temperature conversion (non-blocking)
bool ds18b20_start_conversion(void) {
    if (!ds18b20_reset()) return false;
    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(DS18B20_CMD_CONVERT_T);
    return true;
}

// Maximum conversion time for the given resolution (93.75 / 187.5 / 375 / 750 ms)
uint16_t ds18b20_conversion_time_ms(uint8_t resolution) {
    if (resolution < 9 || resolution > 12) resolution = 12;
    return (uint16_t)(750 >> (12 - resolution)) + 1;
}

// Reads the scratchpad and validates it. Raw value is in 1/16 C units.
bool ds18b20_read_raw(int16_t* raw) {
    uint8_t scratchpad[9];

    if (!ds18b20_reset()) return false;
    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(DS18B20_CMD_READ_SCRATCH);
    for (uint8_t i = 0; i < sizeof(scratchpad); i++) {
        scratchpad[i] = ds18b20_read_byte();
    }

    // CRC protects against broken wires and noise (all-0xFF means nobody answered)
    if (ds18b20_crc8(scratchpad, 8) != scratchpad[8]) return false;

    int16_t value = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);

    // Power-on value: the sensor was reset during the conversion
    if (value == DS18B20_POWER_ON_VALUE) return false;

    *raw = value;
    return true;
}

// Sets resolution 9..12 bits. Only the scratchpad is written (no EEPROM wear):
// the value is re-applied after every power-up anyway.
bool ds18b20_set_resolution(uint8_t resolution) {
    if (resolution < 9 || resolution > 12) return false;
    if (!ds18b20_reset()) return false;
    ds18b20_write_byte(DS18B20_CMD_SKIP_ROM);
    ds18b20_write_byte(DS18B20_CMD_WRITE_SCRATCH);
    ds18b20_write_byte(0x00);                                  // TH register
    ds18b20_write_byte(0x00);                                  // TL register
    ds18b20_write_byte((uint8_t)(((resolution - 9) << 5) | 0x1F)); // Configuration register
    return true;
}
