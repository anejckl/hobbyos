; context_switch.asm - Save/restore callee-saved registers + switch RSP
;
; struct context layout (must match process.h):
;   offset 0:  r15
;   offset 8:  r14
;   offset 16: r13
;   offset 24: r12
;   offset 32: rbx
;   offset 40: rbp
;   offset 48: rsp
;   offset 56: rip

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
bits 64

global context_switch

; void context_switch(struct context *old, struct context *new_ctx)
; rdi = old context pointer
; rsi = new context pointer
context_switch:
    ; Save callee-saved registers into old context
    mov [rdi + 0],  r15
    mov [rdi + 8],  r14
    mov [rdi + 16], r13
    mov [rdi + 24], r12
    mov [rdi + 32], rbx
    mov [rdi + 40], rbp

    ; Save current RSP
    mov [rdi + 48], rsp

    ; Save return address (RIP) - it's on the stack from the call instruction
    ; We use the address right after this function returns
    lea rax, [rel .return]
    mov [rdi + 56], rax

    ; Load new context
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov rbx, [rsi + 32]
    mov rbp, [rsi + 40]

    ; Switch stack
    mov rsp, [rsi + 48]

    ; Jump to new process's saved RIP
    jmp [rsi + 56]

.return:
    ret
