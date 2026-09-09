/* Trivial syscall-only "hello world" for TCC Milestone 3 — no libc, no crt,
 * matching the same raw-_start convention every hand-written HobbyOS user
 * program uses. HobbyOS syscall ABI: RAX=number, RDI/RSI/RDX=args,
 * INT 0x80. SYS_WRITE=0, SYS_EXIT=1. */
__asm__(
    ".globl _start\n"
    "_start:\n"
    "    mov $0, %rax\n"
    "    mov $1, %rdi\n"
    "    lea msg(%rip), %rsi\n"
    "    mov $12, %rdx\n"
    "    int $0x80\n"
    "    mov $1, %rax\n"
    "    mov $0, %rdi\n"
    "    int $0x80\n"
    "msg: .ascii \"tcc works!\\n\"\n"
);
