; gdt_flush.asm - Load GDT and reload segment registers

section .text
bits 64

global gdt_flush
global tss_flush

; void gdt_flush(uint64_t gdt_ptr)
; rdi = pointer to GDT descriptor
gdt_flush:
    lgdt [rdi]

    ; Reload CS via far return
    mov ax, 0x10            ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Far jump to reload CS with kernel code segment (0x08)
    ; Use a far return trick: push segment, push return address, retfq
    pop rdi                 ; Save return address
    push 0x08               ; CS selector
    push rdi                ; Return address
    retfq

; void tss_flush(uint16_t selector)
; di = TSS selector
tss_flush:
    ltr di
    ret
