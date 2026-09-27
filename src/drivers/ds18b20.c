#include "ds18b20.h"
#include "board.h"
#include "crc.h"
#include <stddef.h>

// ==================== 1-Wire / DS18B20 commands ====================
#define OW_CMD_SEARCH_ROM           0xF0
#define OW_CMD_MATCH_ROM            0x55
#define OW_CMD_SKIP_ROM             0xCC
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

// ==================== 1-Wire low level ====================

// Reset pulse, returns true if at least one device answered with a presence pulse
static bool ow_reset(void) {
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

static void ow_write_bit(uint8_t bit) {
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

static void ow_write_byte(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        ow_write_bit(data & 0x01);
        data >>= 1;
    }
}

static uint8_t ow_read_bit(void) {
    uint8_t bit = 0;

    __disable_irq();
    DS18B20_PORT->BRR = DS18B20_PIN;   // Start the read slot
    delay_us(2);
    DS18B20_PORT->BSRR = DS18B20_PIN;  // Release, the device drives the line now
    delay_us(10);                      // Sample before 15 us from the slot start
    if (DS18B20_PORT->IDR & DS18B20_PIN) {
        bit = 1;
    }
    __enable_irq();

    delay_us(50); // Wait for the end of the slot
    return bit;
}

static uint8_t ow_read_byte(void) {
    uint8_t data = 0;
    for (uint8_t i = 0; i < 8; i++) {
        data >>= 1;
        if (ow_read_bit()) {
            data |= 0x80;
        }
    }
    return data;
}

// Reset + addressing of one device (rom == NULL: all devices)
static bool ow_select(const uint8_t* rom) {
    if (!ow_reset()) return false;
    if (rom == NULL) {
        ow_write_byte(OW_CMD_SKIP_ROM);
    } else {
        ow_write_byte(OW_CMD_MATCH_ROM);
        for (uint8_t i = 0; i < DS18B20_ROM_SIZE; i++) ow_write_byte(rom[i]);
    }
    return true;
}

// ==================== DS18B20 ====================

// ROM search (Maxim application note 187)
uint8_t ds18b20_search(uint8_t roms[][DS18B20_ROM_SIZE], uint8_t max_count) {
    uint8_t rom[DS18B20_ROM_SIZE] = {0};
    uint8_t last_discrepancy = 0;
    uint8_t count = 0;
    uint8_t guard = 0;

    // Every pass finds one device; the guard limits the number of passes on a noisy bus
    while (count < max_count && guard++ < 2 * max_count + 4) {
        uint8_t last_zero = 0;

        if (!ow_reset()) break;
        ow_write_byte(OW_CMD_SEARCH_ROM);

        for (uint8_t bit_number = 1; bit_number <= 64; bit_number++) {
            uint8_t id_bit = ow_read_bit();
            uint8_t cmp_id_bit = ow_read_bit();
            uint8_t byte = (bit_number - 1) >> 3;
            uint8_t mask = (uint8_t)(1 << ((bit_number - 1) & 7));
            uint8_t direction;

            if (id_bit && cmp_id_bit) {
                return count;                   // no devices answered
            }
            if (id_bit != cmp_id_bit) {
                direction = id_bit;             // all remaining devices have the same bit
            } else {
                // Discrepancy: devices with 0 and 1 in this position
                if (bit_number < last_discrepancy) direction = (rom[byte] & mask) ? 1 : 0;
                else direction = (bit_number == last_discrepancy) ? 1 : 0;
                if (!direction) last_zero = bit_number;
            }

            if (direction) rom[byte] |= mask;
            else rom[byte] &= (uint8_t)~mask;
            ow_write_bit(direction);
        }

        // Accept only DS18B20 with a valid ROM CRC
        if (rom[0] == DS18B20_FAMILY_CODE && crc8_dallas(rom, 7) == rom[7]) {
            for (uint8_t i = 0; i < DS18B20_ROM_SIZE; i++) roms[count][i] = rom[i];
            count++;
        }

        last_discrepancy = last_zero;
        if (last_discrepancy == 0) break;       // that was the last device
    }
    return count;
}

// Starts a temperature conversion on all sensors (non-blocking)
bool ds18b20_start_conversion(void) {
    if (!ow_select(NULL)) return false;
    ow_write_byte(DS18B20_CMD_CONVERT_T);
    return true;
}

uint16_t ds18b20_conversion_time_ms(uint8_t resolution) {
    if (resolution < 9 || resolution > 12) resolution = 12;
    return (uint16_t)(750 >> (12 - resolution)) + 1;
}

bool ds18b20_read_raw(const uint8_t* rom, int16_t* raw) {
    uint8_t scratchpad[9];

    if (!ow_select(rom)) return false;
    ow_write_byte(DS18B20_CMD_READ_SCRATCH);
    for (uint8_t i = 0; i < sizeof(scratchpad); i++) {
        scratchpad[i] = ow_read_byte();
    }

    // CRC protects against broken wires and noise (all-0xFF means nobody answered)
    if (crc8_dallas(scratchpad, 8) != scratchpad[8]) return false;

    int16_t value = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);

    // Power-on value: the sensor was reset during the conversion
    if (value == DS18B20_POWER_ON_VALUE) return false;

    *raw = value;
    return true;
}

// The resolution is written to the scratchpad only (no EEPROM wear):
// it is re-applied after every power-up and sensor reconnection anyway.
bool ds18b20_set_resolution(uint8_t resolution) {
    if (resolution < 9 || resolution > 12) return false;
    if (!ow_select(NULL)) return false;
    ow_write_byte(DS18B20_CMD_WRITE_SCRATCH);
    ow_write_byte(0x00);                                       // TH register
    ow_write_byte(0x00);                                       // TL register
    ow_write_byte((uint8_t)(((resolution - 9) << 5) | 0x1F));  // Configuration register
    return true;
}
