#ifndef FLASH_H
#define FLASH_H

// Minimal internal Flash driver (STM32F0: 1 KB pages, 16-bit programming)

#include <stdint.h>

#define FLASH_PAGE_SIZE     1024

void flash_erase_page(uintptr_t address);
// Programs 'size' bytes (must be even) into erased Flash
void flash_program(uintptr_t address, const void* data, uint32_t size);

#endif // FLASH_H
