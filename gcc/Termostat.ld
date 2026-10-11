/* STM32F030F4P6 memory map for arm-none-eabi-gcc (mirrors ewarm/Termostat.icf).
 * The last Flash page (0x08003C00..0x08003FFF) holds the settings (src/app/settings.c)
 * and is not used by the linker. */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 15K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4K
}

_stack_size = 0x400;
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH

    _sidata = LOADADDR(.data);

    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    .stack_check (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + _stack_size;
    } > RAM

    /DISCARD/ :
    {
        *(.init) *(.fini) *(.init_array*) *(.fini_array*)
    }
}
