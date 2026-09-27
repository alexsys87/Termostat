#ifndef CRC_H
#define CRC_H

#include <stdint.h>

// Dallas/Maxim CRC-8 (1-Wire ROM codes and DS18B20 scratchpad)
uint8_t crc8_dallas(const void* data, uint32_t len);

// CRC-32 (IEEE 802.3, reflected, init 0xFFFFFFFF, final XOR)
uint32_t crc32(const void* data, uint32_t len);

#endif // CRC_H
