#include "flash.h"
#include "stm32f0xx.h"

static void flash_unlock(void) {
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xCDEF89AB;
    }
}

static void flash_lock(void) {
    FLASH->CR |= FLASH_CR_LOCK;
}

static void flash_wait(void) {
    while (FLASH->SR & FLASH_SR_BSY);
}

// Interrupts are disabled while Flash is modified: the CPU stalls on Flash access anyway,
// and no ISR may run in the middle of the unlock/program sequence
void flash_erase_page(uintptr_t address) {
    __disable_irq();
    flash_unlock();
    flash_wait();
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPERR; // clear stale flags
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = (uint32_t)address;
    FLASH->CR |= FLASH_CR_STRT;
    flash_wait();
    FLASH->CR &= ~FLASH_CR_PER;
    FLASH->SR = FLASH_SR_EOP;
    flash_lock();
    __enable_irq();
}

void flash_program(uintptr_t address, const void* data, uint32_t size) {
    const uint16_t* half = (const uint16_t*)data;

    __disable_irq();
    flash_unlock();
    flash_wait();
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPERR;
    FLASH->CR |= FLASH_CR_PG;
    for (uint32_t i = 0; i < size / 2; i++) {
        *(__IO uint16_t*)(address + i * 2) = half[i];
        flash_wait();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->SR = FLASH_SR_EOP;
    flash_lock();
    __enable_irq();
}
