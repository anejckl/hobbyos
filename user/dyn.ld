OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS {
    . = 0x400000;

    .interp : {
        KEEP(*(.interp))
    }
    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data.rel.ro : { *(.data.rel.ro*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.*) *(COMMON) }
    .dynamic : { *(.dynamic) }
    .got    : { *(.got) }
    .got.plt : { *(.got.plt) }
    .rela.dyn : { *(.rela.dyn) }
    .rela.plt : { *(.rela.plt) }
    .plt    : { *(.plt) }

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
