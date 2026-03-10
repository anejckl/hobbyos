; usermode.asm - Enter ring 3 user mode via IRETQ
;
; void enter_usermode(uint64_t entry, uint64_t user_stack,
;                     uint64_t cs, uint64_t ss)
; rdi = user entry point (RIP)
; rsi = user stack pointer (RSP)
; rdx = user CS (e.g. 0x2B)
; rcx = user SS (e.g. 0x33)

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
bits 64

global enter_usermode
global enter_usermode_args

; void enter_usermode(uint64_t entry, uint64_t user_stack, uint64_t cs, uint64_t ss)
; rdi=entry rsi=user_stack rdx=cs rcx=ss
enter_usermode:
    ; Load user data segment into DS, ES, FS, GS
    mov ax, cx
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build IRETQ stack frame: SS, RSP, RFLAGS, CS, RIP
    push rcx            ; SS
    push rsi            ; RSP (user stack)

    ; Push RFLAGS with IF=1 (bit 9) and IOPL=0
    pushfq
    pop rax
    or rax, 0x200       ; Set IF (interrupt flag)
    and rax, ~(3 << 12) ; Clear IOPL to 0
    push rax            ; RFLAGS

    push rdx            ; CS
    push rdi            ; RIP (user entry point)

    iretq

; void enter_usermode_args(uint64_t entry, uint64_t user_stack,
;                          uint64_t cs, uint64_t ss,
;                          uint64_t user_rdi, uint64_t user_rsi)
; rdi=entry rsi=user_stack rdx=cs rcx=ss r8=user_rdi r9=user_rsi
enter_usermode_args:
    ; Load user data segment into DS, ES, FS, GS
    mov ax, cx
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build IRETQ stack frame: SS, RSP, RFLAGS, CS, RIP
    push rcx            ; SS
    push rsi            ; RSP (user stack)

    pushfq
    pop rax
    or rax, 0x200       ; Set IF
    and rax, ~(3 << 12) ; Clear IOPL
    push rax            ; RFLAGS

    push rdx            ; CS
    push rdi            ; RIP

    ; Set user-visible RDI/RSI from r8/r9 before IRETQ
    mov rdi, r8
    mov rsi, r9
    xor rax, rax

    iretq
