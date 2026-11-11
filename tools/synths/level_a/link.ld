/* Level A: the harness at the Tracker+'s addresses, so tools/emu/machine.py loads it as a firmware */
MEMORY
{
    FLASH (rx) : ORIGIN = 0x60000000, LENGTH = 16M
    DTCM (rw)  : ORIGIN = 0x20000000, LENGTH = 0x50000
    SDRAM (rw) : ORIGIN = 0x80000000, LENGTH = 64M
}
SECTIONS
{
    .text : { *(.text*) *(.audio_ram_functions*) *(.rodata*) } > FLASH
    .data : { *(.data*) } > DTCM AT > FLASH
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > DTCM
    .noinit (NOLOAD) : { *(.noinit*) } > SDRAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.init_array*) }
}
