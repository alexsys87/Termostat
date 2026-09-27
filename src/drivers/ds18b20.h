#ifndef DS18B20_H
#define DS18B20_H

// DS18B20 driver on a 1-Wire bus (external power, several sensors on one line)

#include <stdint.h>
#include <stdbool.h>

#define DS18B20_ROM_SIZE        8
#define DS18B20_FAMILY_CODE     0x28

void ds18b20_init(void);

// Searches the bus, stores up to max_count ROM codes of DS18B20 sensors. Returns the number found.
uint8_t ds18b20_search(uint8_t roms[][DS18B20_ROM_SIZE], uint8_t max_count);

// Starts a conversion on all sensors at once (non-blocking)
bool ds18b20_start_conversion(void);

// Maximum conversion time for the given resolution (94 / 188 / 375 / 750 ms)
uint16_t ds18b20_conversion_time_ms(uint8_t resolution);

// Reads and validates the scratchpad of one sensor (rom == NULL: the only sensor on the bus).
// Raw value is in 1/16 C units.
bool ds18b20_read_raw(const uint8_t* rom, int16_t* raw);

// Sets resolution 9..12 bits on all sensors (scratchpad only, no EEPROM wear)
bool ds18b20_set_resolution(uint8_t resolution);

#endif // DS18B20_H
