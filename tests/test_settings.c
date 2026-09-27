#include "test.h"
#include <setjmp.h>
#include "settings.h"
#include "flash.h"

// ==================== Fake Flash with STM32 programming rules ====================
uint32_t test_flash[FLASH_PAGE_SIZE / 4];
static int erase_count = 0;
static int program_errors = 0;
static int32_t program_limit = -1;      // >= 0: power is lost after N half-words
static int bad_cell = -1;               // half-word index that cannot be programmed (worn cell)
static jmp_buf power_loss;

void flash_erase_page(uintptr_t address) {
    CHECK(address == (uintptr_t)test_flash);
    memset(test_flash, 0xFF, sizeof(test_flash));
    erase_count++;
}

void flash_program(uintptr_t address, const void* data, uint32_t size) {
    CHECK(address >= (uintptr_t)test_flash && address + size <= (uintptr_t)test_flash + sizeof(test_flash));
    CHECK(size % 2 == 0);
    uint16_t* dst = (uint16_t*)address;
    const uint16_t* src = (const uint16_t*)data;
    for (uint32_t i = 0; i < size / 2; i++) {
        if (program_limit == 0) longjmp(power_loss, 1);   // the MCU stops here
        if (program_limit > 0) program_limit--;
        if (dst[i] != 0xFFFF) { program_errors++; continue; }   // PGERR: cell not erased
        dst[i] = src[i];
        if ((uint16_t*)test_flash + bad_cell == &dst[i]) dst[i] = 0;  // worn cell stores garbage
    }
}

// ==================== Tests ====================

int main(void) {
    settings_t s, loaded;

    memset(test_flash, 0xFF, sizeof(test_flash));
    CHECK(!settings_load(&loaded));                  // empty Flash

    settings_defaults(&s);
    settings_save(&s);
    CHECK(settings_load(&loaded));
    CHECK(memcmp(&s, &loaded, sizeof(s)) == 0);
    CHECK_EQ(loaded.setpoint, 250);

    // Identical data is not written again
    settings_save(&s);
    CHECK(((settings_t*)test_flash)[1].magic == 0xFFFF);

    // Wear leveling: 12 records per page, one erase per 12 saves
    const int slots = FLASH_PAGE_SIZE / sizeof(settings_t);
    CHECK_EQ(slots, 12);
    erase_count = 0;
    for (int i = 0; i < 3 * slots; i++) {
        s.setpoint = (int16_t)(100 + i);
        settings_save(&s);
        CHECK(settings_load(&loaded));
        CHECK_EQ(loaded.setpoint, 100 + i);
    }
    CHECK_EQ(erase_count, 3);
    CHECK_EQ(program_errors, 0);

    // Power loss in the middle of a write: the previous record stays valid
    s.setpoint = 777;
    program_limit = 10;
    if (setjmp(power_loss) == 0) settings_save(&s);
    program_limit = -1;
    CHECK(settings_load(&loaded));
    CHECK_EQ(loaded.setpoint, 100 + 3 * slots - 1);
    // The next save skips the damaged slot
    s.setpoint = 778;
    settings_save(&s);
    CHECK(settings_load(&loaded));
    CHECK_EQ(loaded.setpoint, 778);
    CHECK_EQ(program_errors, 0);

    // A worn cell in the next slot: the record goes to the following slot, nothing is lost
    bad_cell = (int)(3 * sizeof(settings_t) / 2) + 3;      // inside slot 3, the next free one
    s.setpoint = 779;
    settings_save(&s);
    bad_cell = -1;
    CHECK(settings_load(&loaded));
    CHECK_EQ(loaded.setpoint, 779);
    CHECK(((settings_t*)test_flash)[3].hysteresis == 0);      // damaged copy left in slot 3
    CHECK(((settings_t*)test_flash)[4].setpoint == 779);

    // Statistics update keeps the stored settings, not the unsaved ones
    s.setpoint = 999;                                // not saved
    settings_save_stats(1234, 56);
    CHECK(settings_load(&loaded));
    CHECK_EQ(loaded.setpoint, 779);
    CHECK_EQ(loaded.total_runtime, 1234);
    CHECK_EQ(loaded.relay_cycles, 56);

    // Records of another layout version are ignored
    settings_t old = loaded;
    old.version = 1;
    memset(test_flash, 0xFF, sizeof(test_flash));
    memcpy(test_flash, &old, sizeof(old));
    CHECK(!settings_load(&loaded));

    // A corrupted byte invalidates the record
    memset(test_flash, 0xFF, sizeof(test_flash));
    settings_save(&s);
    ((uint8_t*)test_flash)[10] ^= 0x01;
    CHECK(!settings_load(&loaded));

    return TEST_REPORT("settings");
}
