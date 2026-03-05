; isr_stubs.asm - 256 ISR entry stubs + common handler
; Handles error code vs no-error-code exceptions uniformly

section .text
bits 64

extern isr_handler

; Macro for ISR stubs that DON'T push an error code
%macro ISR_NOERRCODE 1
isr_stub_%1:
    push 0                  ; Push dummy error code
    push %1                 ; Push interrupt number
    jmp isr_common_stub
%endmacro

; Macro for ISR stubs that DO push an error code
%macro ISR_ERRCODE 1
isr_stub_%1:
    ; CPU already pushed error code
    push %1                 ; Push interrupt number
    jmp isr_common_stub
%endmacro

; CPU Exceptions (0-31)
ISR_NOERRCODE 0     ; Division By Zero
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; Non Maskable Interrupt
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Overflow
ISR_NOERRCODE 5     ; Bound Range Exceeded
ISR_NOERRCODE 6     ; Invalid Opcode
ISR_NOERRCODE 7     ; Device Not Available
ISR_ERRCODE   8     ; Double Fault
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun (legacy)
ISR_ERRCODE   10    ; Invalid TSS
ISR_ERRCODE   11    ; Segment Not Present
ISR_ERRCODE   12    ; Stack-Segment Fault
ISR_ERRCODE   13    ; General Protection Fault
ISR_ERRCODE   14    ; Page Fault
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; x87 Floating-Point
ISR_ERRCODE   17    ; Alignment Check
ISR_NOERRCODE 18    ; Machine Check
ISR_NOERRCODE 19    ; SIMD Floating-Point
ISR_NOERRCODE 20    ; Virtualization Exception
ISR_ERRCODE   21    ; Control Protection Exception
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_ERRCODE   29    ; VMM Communication Exception
ISR_ERRCODE   30    ; Security Exception
ISR_NOERRCODE 31

; IRQs and remaining vectors (32-255): no error codes
%assign i 32
%rep 224
ISR_NOERRCODE i
%assign i i+1
%endrep

; Common interrupt handler stub
isr_common_stub:
    ; Save all general-purpose registers
    ; Stack currently: SS, RSP, RFLAGS, CS, RIP, err_code, int_no
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass pointer to interrupt frame (current RSP)
    mov rdi, rsp

    ; Call C handler
    call isr_handler

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove int_no and err_code from stack
    add rsp, 16

    ; Return from interrupt
    iretq

; ============================================================================
; ISR stub table - array of 256 function pointers
; ============================================================================
section .data

global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+i
%assign i i+1
%endrep
