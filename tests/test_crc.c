#include "test.h"
#include "crc.h"

int main(void) {
    // Standard check values for "123456789"
    CHECK_EQ(crc8_dallas("123456789", 9), 0xA1);
    CHECK_EQ(crc32("123456789", 9), 0xCBF43926u);

    // Real DS18B20 scratchpad (+25.0625 C) and ROM code with valid CRC bytes
    const uint8_t scratchpad[9] = {0x91, 0x01, 0x4B, 0x46, 0x7F, 0xFF, 0x0F, 0x10, 0x25};
    CHECK_EQ(crc8_dallas(scratchpad, 8), scratchpad[8]);
    CHECK_EQ(crc8_dallas(scratchpad, 9), 0);   // CRC over data + CRC is zero

    // A bus without devices reads all ones: must not pass the check
    const uint8_t empty[9] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(crc8_dallas(empty, 8) != empty[8]);

    return TEST_REPORT("crc");
}
