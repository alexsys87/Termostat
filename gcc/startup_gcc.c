// Startup code for arm-none-eabi-gcc (the IAR build uses src/system/startup.c).
// Same vector table as the IAR version; SystemInit() is not called here either:
// main() sets up the clock with board_clock_init().

#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

int main(void);

void Reset_Handler(void);
static void Default_Handler(void);

void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void TIM14_IRQHandler(void)   __attribute__((weak, alias("Default_Handler")));

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t vector_table[16 + 32] = {
    (vector_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    [11] = SVC_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,

    [16 + 0]  = Default_Handler, [16 + 1]  = Default_Handler,
    [16 + 2]  = Default_Handler, [16 + 3]  = Default_Handler,
    [16 + 4]  = Default_Handler, [16 + 5]  = Default_Handler,
    [16 + 6]  = Default_Handler, [16 + 7]  = Default_Handler,
    [16 + 8]  = Default_Handler, [16 + 9]  = Default_Handler,
    [16 + 10] = Default_Handler, [16 + 11] = Default_Handler,
    [16 + 12] = Default_Handler, [16 + 13] = Default_Handler,
    [16 + 14] = Default_Handler, [16 + 15] = Default_Handler,
    [16 + 16] = Default_Handler, [16 + 17] = Default_Handler,
    [16 + 19] = TIM14_IRQHandler,
    [16 + 20] = Default_Handler, [16 + 21] = Default_Handler,
    [16 + 22] = Default_Handler, [16 + 23] = Default_Handler,
    [16 + 24] = Default_Handler, [16 + 25] = Default_Handler,
    [16 + 26] = Default_Handler, [16 + 27] = Default_Handler,
    [16 + 28] = Default_Handler, [16 + 30] = Default_Handler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;

    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;

    main();
    while (1);
}

static void Default_Handler(void)
{
    while (1);
}
