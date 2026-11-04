/*
 * Linker script for E16 applications.
 *
 * 0x08000000 - 0x08003FFF  OXI bootloader (do not overwrite)
 * 0x08004000 - 0x0807FFFF  application
 *
 * The application stops at 512 KB because the GD32F405 only runs the first
 * 512 KB of flash without wait states, and the clock setup relies on that.
 *
 * The last word of RAM is shared with the bootloader: writing
 * BOOTLOADER_MAGIC there and resetting starts a firmware update.
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH    (rx)  : ORIGIN = 0x08004000, LENGTH = 512K - 16K
  RAM      (xrw) : ORIGIN = 0x20000000, LENGTH = 128K - 4
  BOOT_RAM (rw)  : ORIGIN = 0x2001FFFC, LENGTH = 4
  CCMRAM   (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

_estack = (ORIGIN(RAM) + LENGTH(RAM)) & ~7;
_min_stack_size = 0x800;

/* Upper bound for the newlib heap (see bsp/src/sbrk.c): leaves the fixed
 * stack reservation below _estack untouched. */
_heap_limit = _estack - _min_stack_size;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
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

  .bss :
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

  /* Not initialised at startup: only use it for buffers you fill yourself. */
  .ccmram (NOLOAD) :
  {
    . = ALIGN(4);
    *(.ccmram)
    *(.ccmram*)
    . = ALIGN(4);
  } > CCMRAM

  .boot_flag (NOLOAD) :
  {
    KEEP(*(.boot_flag))
  } > BOOT_RAM

  ._stack_check :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _min_stack_size;
    . = ALIGN(8);
  } > RAM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
