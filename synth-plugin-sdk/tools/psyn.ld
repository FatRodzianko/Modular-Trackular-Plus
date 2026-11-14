/* Linker script for Polyend .psyn relocatable synth plugins (ARMv7E-M Cortex-M7) */
ENTRY(psyn_plugin_entry)

SECTIONS
{
    . = 0x00000000;

    .text ALIGN(8) :
    {
        *(.text.psyn_plugin_entry)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        . = ALIGN(8);
    }

    .rodata ALIGN(8) :
    {
        *(.rodata*)
        . = ALIGN(8);
    }

    .init_array ALIGN(8) :
    {
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        . = ALIGN(8);
    }

    .data ALIGN(8) :
    {
        *(.data*)
        . = ALIGN(8);
    }

    .bss ALIGN(8) (NOLOAD) :
    {
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
    }

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note*)
    }
}
