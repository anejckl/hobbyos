/* linker.ld - Higher-half kernel linking layout for x86_64 */

ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;
KERNEL_LMA = 0x100000;  /* 1MB physical */

SECTIONS
{
    /* Boot sections at low physical addresses (needed before paging) */
    . = 0x100000;

    .multiboot_header : AT(0x100000)
    {
        KEEP(*(.multiboot_header))
    }

    .boot : AT(ADDR(.boot))
    {
        *(.boot)
    }

    .boot_data : AT(ADDR(.boot_data))
    {
        *(.boot_data)
    }

    .boot_bss (NOLOAD) : AT(ADDR(.boot_bss))
    {
        *(.boot_bss)
    }

    /* Record the end of boot sections for calculating kernel LMA offset */
    _boot_end = .;

    /* Align to 2MB boundary for the higher-half kernel */
    . = ALIGN(0x200000);
    _kernel_phys_start = .;

    /* Higher-half kernel sections */
    . += KERNEL_VMA;

    .text ALIGN(4K) : AT(_kernel_phys_start)
    {
        _text_start = .;
        *(.text .text.*)
        _text_end = .;
    }

    .rodata ALIGN(4K) : AT(_kernel_phys_start + (_rodata_start - _text_start))
    {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    }

    .data ALIGN(4K) : AT(_kernel_phys_start + (_data_start - _text_start))
    {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    }

    .bss ALIGN(4K) : AT(_kernel_phys_start + (_bss_start - _text_start))
    {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    }

    . = ALIGN(4K);
    _kernel_end = .;
    _kernel_phys_end = _kernel_phys_start + (_kernel_end - _text_start);

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note.*)
    }
}
