; fork_return.asm — Trampoline for forked child's first return to user mode.
;
; When context_switch jumps here, RSP points at a pre-built interrupt frame
; on the child's kernel stack (matching isr_common_stub layout).
; We pop all registers and iretq back to user mode with RAX=0.

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
bits 64

global fork_return_trampoline

fork_return_trampoline:
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
    pop rax        ; This is 0 (child's fork return value)
    add rsp, 16    ; Skip int_no + err_code
    iretq
