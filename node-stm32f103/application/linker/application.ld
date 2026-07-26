/* STM32F103C8T6 relocatable application linker script.
 * The application begins after the 24 KiB bootloader and ends before the
 * final two 1 KiB internal metadata pages.
 */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08006000, LENGTH = 38K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x000;
_Min_Stack_Size = 0x400;

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(0x200);
        __vector_table_start__ = .;
        KEEP(*(.isr_vector))
        . = ALIGN(4);
        __vector_table_end__ = .;
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    ._user_heap_stack (NOLOAD) :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM

    /DISCARD/ :
    {
        *(.comment*)
        *(.note*)
        *(.eh_frame*)
    }
}

ASSERT(ORIGIN(FLASH) == 0x08006000, "Application Flash origin changed")
ASSERT((ORIGIN(FLASH) + LENGTH(FLASH)) == 0x0800F800, "Application must end at metadata page")
ASSERT(__vector_table_start__ == 0x08006000, "Application vector table must begin at Flash origin")
ASSERT(_etext <= 0x0800F800, "Application code exceeds its Flash partition")
ASSERT(_ebss <= (_estack - _Min_Stack_Size), "Application RAM overflow")
