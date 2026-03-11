global _start
extern main
extern sys_exit

section .text
_start:
    mov  rdi, [rsp]       ; argc
    lea  rsi, [rsp+8]     ; argv
    call main
    mov  rdi, rax
    call sys_exit
    ud2
