; idt_flush.asm - Load IDT

section .text
bits 64

global idt_flush

; void idt_flush(uint64_t idt_ptr)
; rdi = pointer to IDT descriptor
idt_flush:
    lidt [rdi]
    ret
