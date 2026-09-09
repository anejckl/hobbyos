OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_ldso_start)

SECTIONS {
    . = SIZEOF_HEADERS;

    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.*) *(COMMON) }
    .dynamic : { *(.dynamic) }
    .got    : { *(.got) }
    .got.plt : { *(.got.plt) }
    .rela.dyn : { *(.rela.dyn) }
    .rela.plt : { *(.rela.plt) }

    /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame*) }
}
