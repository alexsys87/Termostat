#include "crc.h"

// Bitwise implementations: slower than tables but save Flash

uint8_t crc8_dallas(const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint8_t crc = 0;
    while (len--) {
        uint8_t byte = *p++;
        for (uint8_t i = 0; i < 8; i++) {
            uint8_t mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;   // x^8 + x^5 + x^4 + 1, reflected
            byte >>= 1;
        }
    }
    return crc;
}

uint32_t crc32(const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;
    while (len--) {
        crc ^= *p++;
        for (uint8_t i = 0; i < 8; i++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
        }
    }
    return ~crc;
}
