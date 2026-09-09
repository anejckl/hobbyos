# CLAUDE.md — AI Agent Guidance for HobbyOS

## Build Commands

```bash
make              # Compile kernel.bin
make iso          # Build bootable hobbyos.iso (includes GRUB)
make run          # Build ISO + launch QEMU with serial on stdio
make debug        # Build ISO + launch QEMU with GDB stub (port 1234)
make clean        # Remove all build artifacts
make test         # Run all tests (host unit tests + QEMU smoke test)
make test-host    # Run host-side unit tests only (fast, no QEMU)
make test-qemu    # Run QEMU boot smoke test only (requires 'make iso')
make test-interactive  # Interactive QEMU tests (~60-90s, sends keystrokes, checks serial)
make install-hooks # Install git pre-commit hook (runs test-host)
```

## Development Environment

This project is developed **natively on Linux**. `gcc`/`x86_64-elf-gcc`, `nasm`, `make`, `grub-mkrescue`, `xorriso`, `mtools`, and `qemu-system-x86_64` are all installed directly on the host — **no Docker or Windows/Git Bash layer is needed or used.**

### How to Build and Test

```bash
make              # kernel.bin
make iso          # hobbyos.iso
make test         # test-host + test-qemu
make test-host    # fast host unit tests, no QEMU
make test-qemu    # QEMU boot smoke test (builds iso first via dependency)
make test-interactive  # sends keystrokes to QEMU, checks serial (~60-90s)
```

Just run these directly — no volume mounts, no path translation, no container image to build first.

### Toolchain

The Makefile auto-detects a cross toolchain: `CC := x86_64-elf-gcc` if present on `PATH`, else falls back to the system `gcc` (both work — this host has `x86_64-elf-gcc`/`-ld`/`-objcopy` installed, so builds use the cross toolchain automatically). Native `gcc` is a recent version (16.x); its default C standard changed, so `CFLAGS`/`USER_CFLAGS`/`SO_CFLAGS` explicitly pass `-std=c11` — don't remove that flag or headers relying on C11 features may fail to parse.

### GitHub CLI (`gh`)

Not installed in this environment. If GitHub interaction is needed, check with `which gh` first — if absent, use `git` directly (fetch, log, remote) or ask the user to run `gh` commands themselves via `!<command>`.

### Interactive Test Output (`make test-interactive`)

Sends keystrokes to QEMU via monitor socket and checks serial output. Creates a fresh ext2 disk image, boots the OS, waits for autotests to pass, then exercises shell commands, user programs, ext2 operations, TTY editing, and Ctrl+C.

**Output files:**
- `tests/interactive_serial.log` — Full serial transcript from QEMU
- `tests/interactive_results.json` — Structured per-test results (name, passed, duration, output_snippet, error)

**JSON format:**
```json
{"total": 21, "passed": 21, "failed": 0, "tests": [{"name": "boot_and_autotests", "passed": true, ...}, ...]}
```

**For AI agents:** After running `make test-interactive`, read `tests/interactive_results.json` to see which tests passed/failed and inspect `tests/interactive_serial.log` for debugging.

**WARNING — Timing sensitivity:** QEMU TCG (software emulation) has variable timing. If interactive tests fail but `test-host` and `test-qemu` both pass, **re-run once before investigating** — a single failure is likely a timing fluke, not a real bug. Only investigate if failures are reproducible across 2-3 consecutive runs.

### Line Endings

- `.gitattributes` enforces LF in the repository — shouldn't be an issue on native Linux, but if a file was ever edited on Windows check with `file <path>` (should say "ASCII text", not "with CRLF line terminators") and fix with `sed -i 's/\r$//' <file>`.

### Running Interactively (Native QEMU)

```bash
# Basic (no disk):
qemu-system-x86_64 -cdrom hobbyos.iso -serial stdio -m 128M -no-reboot -no-shutdown

# With ext2 disk (needed for file redirects, ext2 commands) + networking, same as `make run`:
qemu-system-x86_64 -cdrom hobbyos.iso -serial stdio -m 128M -no-reboot -no-shutdown \
  -drive file=disk.img,format=raw,if=ide \
  -netdev user,id=net0,hostfwd=tcp::8080-:80 -device e1000,netdev=net0

# With monitor socket (for sendkey injection via tests/test_interactive.py):
qemu-system-x86_64 -cdrom hobbyos.iso -serial stdio -m 128M -no-reboot -no-shutdown \
  -monitor tcp:127.0.0.1:4444,server,nowait \
  -drive file=disk.img,format=raw,if=ide
```

- VGA output appears in the QEMU GUI window; serial debug output appears in the terminal
- Use `run_in_background: true` when launching from Claude Code so the window stays open for the user
- **CRITICAL: Disk must use `if=ide` without `index=N`.** The kernel ATA driver only checks the primary IDE controller (0x1F0). Using `index=1` puts the disk on the secondary controller, which the driver won't detect. The `-cdrom` flag uses a separate IDE channel and doesn't conflict.
- Create/refresh the ext2 disk image with `make disk.img`, or directly: `dd if=/dev/zero of=disk.img bs=1M count=16 && mkfs.ext2 -F disk.img`

### Native Tool Availability

| Tool | Available? | Notes |
|------|-----------|-------|
| `gcc` | Yes | System package |
| `x86_64-elf-gcc` / `-ld` / `-objcopy` | Yes | Cross toolchain — preferred by the Makefile when present |
| `nasm` | Yes | |
| `make` | Yes | |
| `grub-mkrescue` | Yes | Needs `xorriso` + `mtools`, both present |
| `qemu-system-x86_64` | Yes | |
| `gdb` | Yes | For `make debug` |
| `docker` | No | Not needed — everything builds natively |
| `gh` (GitHub CLI) | No | Use `git` directly, or ask the user to run `gh` via `!<command>` |
| `git` | Yes | |

## Mandatory Rules

1. **Run `make test` before every commit.** A pre-commit hook enforces `make test-host` automatically (install via `make install-hooks`).
2. **Never remove critical compiler flags** (see "Do NOT" section below).
3. **Never use libc headers** in kernel code — only `kernel/common.h`.

## Do NOT

| Do NOT | Why | Instead |
|--------|-----|---------|
| `#include <stdint.h>` or any `<std*.h>` | Kernel has no libc | `#include "common.h"` or `#include "../common.h"` |
| Use `float`, `double`, or SSE intrinsics | ISRs don't save SSE/FPU state; `-mno-sse` enforced | Use integer math only |
| Dereference raw physical addresses | Paging is active; raw physical = page fault | `PHYS_TO_VIRT(addr)` or `KERNEL_PHYS_TO_VIRT(addr)` |
| Remove `-mno-red-zone` from CFLAGS | ISR stack pushes corrupt the red zone in leaf functions | Flag is mandatory — never touch it |
| Remove `-mno-sse -mno-sse2 -mno-mmx -mno-avx` | GCC would emit SIMD instructions; ISRs don't save that state | Flags are mandatory — never touch them |
| Remove `-mcmodel=kernel` | Kernel is at `0xFFFFFFFF80000000`; default model can't address it | Flag is mandatory — never touch it |
| Remove `-ffreestanding -nostdlib -nostdinc` | There is no libc to link against | Flags are mandatory — never touch them |
| Remove `-fno-stack-protector` | No `__stack_chk_fail` symbol available | Flag is mandatory — never touch it |
| Change ISR stub push order without updating C struct | `isr_stubs.asm` and `struct interrupt_frame` must match exactly | Always change both together |
| Disable PIT (IRQ 0) | Timer drives `scheduler_tick()` — scheduling stops entirely | Leave PIT enabled |
| Use `printf` / `malloc` / `exit` in kernel code | These are libc functions — they don't exist | Use `vga_printf`, `kmalloc`, `kpanic` |

## Project Map

```
hobbyos/
├── Makefile                    # Build system — targets: all, iso, run, debug, clean, test
├── Dockerfile                  # Ubuntu 24.04 build environment
├── linker.ld                   # Higher-half kernel link layout (VMA 0xFFFFFFFF80000000)
├── grub.cfg                    # GRUB Multiboot2 boot entry
├── CLAUDE.md                   # This file — AI agent guidance
├── CONTRIBUTING.md             # Human contributor guide
├── .editorconfig               # Editor settings (4-space indent, LF, UTF-8)
├── .github/workflows/test.yml  # CI — runs test-host + test-qemu on push/PR
│
├── boot/
│   └── boot.asm                # Multiboot2 header, 32→64-bit transition, page tables
│
├── kernel/
│   ├── kernel.c                # kernel_main() — boot sequence orchestrator (15 phases)
│   ├── common.h                # Types (uint8_t..uint64_t), port I/O, address macros, kpanic
│   ├── string.c / string.h     # strlen, strcmp, memcpy, strtok, etc. (no libc)
│   │
│   ├── arch/x86_64/
│   │   ├── gdt.c / gdt.h       # GDT: null, code(0x08), data(0x10), TSS(0x18), user code(0x28), user data(0x30)
│   │   ├── gdt_flush.asm       # Reload segment registers
│   │   ├── tss.c / tss.h       # TSS for RSP0 ring-transition stack
│   │   ├── idt.c / idt.h       # 256-entry IDT, INT 0x80 set to DPL=3 for user syscalls
│   │   ├── idt_flush.asm       # LIDT wrapper
│   │   ├── isr.c / isr.h       # ISR dispatch + handler registration table
│   │   ├── isr_stubs.asm       # 256 macro-generated ISR entry stubs
│   │   ├── pic.c / pic.h       # 8259 PIC: remap IRQs 0-15 → INT 32-47
│   │   └── usermode.asm / .h   # Ring 3 entry via IRETQ (enter_usermode)
│   │
│   ├── interrupts/
│   │   └── interrupts.c / .h   # Default exception handlers (breakpoint, page fault)
│   │
│   ├── memory/
│   │   ├── pmm.c / pmm.h       # Bitmap page frame allocator (4 KB pages, ≤512 MB)
│   │   ├── vmm.c / vmm.h       # 4-level page table walker (PML4→PDPT→PD→PT)
│   │   ├── kheap.c / kheap.h   # Bump allocator heap at 0xFFFFFFFF90000000 (4 MB)
│   │   └── user_vm.c / .h      # Per-process address spaces (PML4 clone + user page mapping)
│   │
│   ├── process/
│   │   ├── process.c / .h      # PCB table (64 slots), 8 KB kernel stack per process
│   │   └── user_process.c / .h # User-mode process creation (address space + trampoline)
│   │
│   ├── syscall/
│   │   └── syscall.c / .h      # INT 0x80 handler: 15 syscalls (write/exit/getpid/exec/wait/fork/read/open/close/pipe/dup2/kill/sigaction/sigreturn/getppid)
│   │
│   ├── scheduler/
│   │   ├── scheduler.c / .h    # Round-robin, 100 ms quantum (10 ticks at 100 Hz)
│   │   └── context_switch.asm  # Save/restore callee-saved regs + RSP + RIP
│   │
│   ├── drivers/
│   │   ├── driver.h             # Generic driver interface
│   │   ├── vga.c / vga.h       # 80×25 text mode, printf (%s %d %u %x %p %c %%)
│   │   ├── keyboard.c / .h     # PS/2 (IRQ 1) + COM1 serial RX (IRQ 4), scan code Set 1, circular buffer
│   │   ├── pit.c / pit.h       # PIT channel 0, calls scheduler_tick()
│   │   └── ata.c / ata.h       # ATA PIO disk driver (LBA28, read/write sectors)
│   │
│   ├── fs/
│   │   ├── vfs.c / vfs.h       # VFS: node table, open/read/write/close, mount points
│   │   ├── ramfs.c / ramfs.h   # RAM filesystem: files backed by embedded .rodata
│   │   ├── pipe.c / pipe.h     # Pipe ring buffer (4KB) with blocking read/write
│   │   ├── procfs.c / procfs.h # /proc virtual filesystem (/proc/<pid>/status, /proc/<pid>/fd)
│   │   └── ext2.c / ext2.h     # ext2 filesystem reader (via ATA PIO)
│   │
│   ├── signal/
│   │   └── signal.c / signal.h  # Signal delivery (SIGINT, SIGKILL, SIGTERM, SIGCHLD, SIGPIPE)
│   │
│   ├── shell/
│   │   └── shell.c / shell.h   # 14 commands: help, ps, mem, uptime, echo, clear, run, ls, jobs, proc, fg, bg, kill, cat
│   │
│   ├── user_programs.c / .h    # Embedded user program registry (find by name)
│   │
│   └── debug/
│       └── debug.c / debug.h   # COM1 serial (38400 baud, 8-N-1, IER RX enabled so keystrokes raise IRQ 4)
│
├── user/
│   ├── syscall.h               # User-space syscall wrappers (inline INT 0x80)
│   ├── hello.c                 # "Hello from user mode!" + PID
│   ├── counter.c               # Counts 1-5 with delays
│   ├── fork_test.c             # Fork + wait test
│   ├── cow_test.c              # COW fork isolation test
│   ├── multifork_test.c        # Multiple fork + wait test
│   ├── pipe_test.c             # Pipe communication test
│   ├── signal_test.c           # Signal delivery test
│   ├── procfs_test.c           # /proc/self/status read test
│   ├── libc_test.c             # Exercises user/lib/*.c (malloc stability, FILE*, deep recursion)
│   ├── user.ld                 # User program linker script (entry at 0x400000)
│   │
│   ├── lib/                    # Extended libc, statically linked into programs that use it
│   │   ├── libc.h              # Shared declarations (types, stdio/string/stdlib/malloc, sys_*_libc wrappers)
│   │   ├── libc.c               # Raw sys_*_libc syscall wrappers
│   │   ├── malloc.c            # Free-list allocator (prev+next, backward+forward coalescing)
│   │   ├── stdio.c             # printf/vsnprintf/snprintf/puts/fputs/fgets (real __builtin_va_arg)
│   │   ├── stdio_file.c        # FILE* layer (fopen/fread/fwrite/fseek/...) over the fd syscalls
│   │   ├── string.c            # memmove/memcmp/strcat/strncat/strdup/...
│   │   └── stdlib.c            # strtol/strtoul/qsort/getenv (stub, always NULL)
│   │
│   └── tcc/                    # Vendored TCC (self-hosting toolchain, see below), builds to /bin/tcc
│       ├── tcc.c, libtcc.c, tccpp.c, tccgen.c, tccelf.c, x86_64-gen.c,
│       │   x86_64-asm.c, x86_64-link.c, tccasm.c, i386-asm.c   # TCC's own sources (ONE_SOURCE amalgamation via tcc.c)
│       ├── config.h            # Hand-written (normally ./configure-generated)
│       ├── hobbyos_compat.c    # POSIX-shaped shims backed by sys_*_libc (open/read/write/gettimeofday/printf/...)
│       ├── crt0.c              # _start entry point for the tcc binary itself
│       └── compat/             # Minimal stdio.h/stdlib.h/string.h/... so TCC's own source compiles under -nostdinc
│
├── scripts/
│   └── pre-commit              # Git hook — runs make test-host before commits
│
└── tests/
    ├── test_main.c / .h        # Minimal test runner (no external deps)
    ├── stubs.h                  # Host-side stubs for kernel hardware constructs
    ├── test_string.c            # Tests kernel/string.c (includes actual source)
    ├── test_pmm.c              # Tests PMM bitmap functions (includes actual source)
    ├── test_printf.c           # Tests printf integer formatting (replicated logic)
    ├── test_libc_gapfill.c     # Tests user/lib/*.c additions (memmove, qsort, strtol, ...)
    ├── tcc_fixtures/           # .c fixtures tcc compiles inside QEMU during interactive tests
    ├── qemu_smoke.sh           # QEMU boot smoke test script
    ├── qemu_smoke.exp          # Expected serial output patterns
    └── test_interactive.py     # Interactive QEMU test suite (sendkey-based)
```

## Architecture Quick Reference

### Memory Layout

| Virtual Range | Size | Purpose |
|---|---|---|
| `0x400000` | varies | User code base (per-process address space) |
| `0x7FFFFF000` | 4 KB | User stack (1 page, grows down from `0x800000000`) |
| `0x0–0x1FFFFF` | 2 MB | Identity map (boot only) |
| `0xFFFF800000000000–0xFFFF80001FFFFFFF` | 512 MB | Physical memory direct map |
| `0xFFFFFFFF80000000–0xFFFFFFFF80FFFFFF` | 16 MB | Kernel text/data/bss |
| `0xFFFFFFFF90000000–0xFFFFFFFF903FFFFF` | 4 MB | Kernel heap |

### Key Constants (kernel/common.h)

| Constant | Value |
|---|---|
| `PAGE_SIZE` | 4096 |
| `KERNEL_VMA` | `0xFFFFFFFF80000000` |
| `PHYS_MAP_BASE` | `0xFFFF800000000000` |

### Address Conversion

- `PHYS_TO_VIRT(addr)` — physical → direct-map virtual
- `VIRT_TO_PHYS(addr)` — direct-map virtual → physical
- `KERNEL_PHYS_TO_VIRT(addr)` — physical → kernel-space virtual
- `KERNEL_VIRT_TO_PHYS(addr)` — kernel-space virtual → physical

### GDT Segments

| Selector | Segment |
|---|---|
| 0x00 | Null |
| 0x08 | Kernel Code (64-bit, DPL=0) |
| 0x10 | Kernel Data (DPL=0) |
| 0x18 | TSS (16 bytes, slots 3-4) |
| 0x28 | User Code (64-bit, DPL=3) — RPL3 selector: `0x2B` |
| 0x30 | User Data (DPL=3) — RPL3 selector: `0x33` |

### Syscalls (INT 0x80)

| RAX | Name | Args | Returns |
|-----|------|------|---------|
| 0 | `sys_write` | RDI=fd, RSI=buf, RDX=len | bytes written |
| 1 | `sys_exit` | RDI=status | never returns |
| 2 | `sys_getpid` | — | PID |
| 3 | `sys_exec` | RDI=path | 0 or -1 |
| 4 | `sys_wait` | RDI=status_ptr | child PID or -1 |
| 5 | `sys_fork` | — | child PID (parent) / 0 (child) |
| 6 | `sys_read` | RDI=fd, RSI=buf, RDX=count | bytes read |
| 7 | `sys_open` | RDI=path, RSI=flags | fd or -1 |
| 8 | `sys_close` | RDI=fd | 0 or -1 |
| 9 | `sys_pipe` | RDI=fds[2] | 0 or -1 |
| 10 | `sys_dup2` | RDI=oldfd, RSI=newfd | newfd or -1 |
| 11 | `sys_kill` | RDI=pid, RSI=sig | 0 or -1 |
| 12 | `sys_sigaction` | RDI=sig, RSI=handler | 0 or -1 |
| 13 | `sys_sigreturn` | — | restores pre-signal context |
| 14 | `sys_getppid` | — | parent PID |
| 25 | `sys_waitpid` | RDI=pid, RSI=status_ptr, RDX=options | child PID or -1 |

### IRQ Mapping (8259 PIC)

- Master: IRQ 0–7 → INT 32–39
- Slave: IRQ 8–15 → INT 40–47
- Timer = IRQ 0 (INT 32), Keyboard = IRQ 1 (INT 33), COM1 serial RX = IRQ 4 (INT 36)

## TCC Self-Hosting Toolchain

HobbyOS can compile and run its own C programs from inside itself via a vendored, ported [TCC](https://bellard.org/tcc/) (Tiny C Compiler), built as `/bin/tcc`. Milestones 0-4 of the self-hosting plan are done: TCC boots, compiles trivial syscall-only programs, and compiles/links/runs programs against the real extended libc (`printf`/`malloc`/`fork`/`wait`).

**Compiling something inside HobbyOS:**
```
run tcc -static -nostdlib -o /path/to/output /path/to/source.c [more.c/.o inputs...]
```
Always pass `-static -nostdlib` — TCC's default dynamic-link output path builds a `.dynsym`/`.dynamic`/`PT_INTERP` section set this port has never exercised and will crash; static output sidesteps `user/ld.so.c`'s narrow relocation support entirely (see Gotcha below).

**Using the real libc from TCC-compiled code:** don't have TCC compile `user/lib/stdio.c` (or anything else that itself implements a variadic function body with real `__builtin_va_arg`) — link the **precompiled** `user/lib/hobbyc.o` instead (`make user/lib/hobbyc.o`, a partial-link of all of `user/lib/*.c` via `ld -r`, built by the host cross-toolchain). A program merely *calling* a variadic function like `printf(fmt, ...)` needs no special support on the caller side; only `printf`'s own implementation needs real `va_arg`, and real upstream TCC has no target-side `__builtin_va_list`/`va_arg` support on x86-64 outside its own runtime library (`libtcc1.a`), which this port deliberately doesn't ship (see the `tccrun.c` exclusion note in the Makefile). Example:
```
run tcc -static -nostdlib -o /tmp/prog /tmp/prog.c /tcc_tests/hobbyc.o
```
`user/lib/libc.h` handles this with `#ifdef __TINYC__` (TCC always predefines this): `va_list` becomes a plain `void *` stand-in under TCC (fine — TCC only ever needs the *declaration* to parse, never to act on a real `va_list` value, since the implementing `.c` file is never compiled by TCC) and the real `__builtin_va_list` under the host cross-compiler (which does implement `stdio.c`'s bodies).

**Fixtures:** `tests/tcc_fixtures/hello.c` (raw syscalls only, no libc) and `tests/tcc_fixtures/libc_demo.c` (real libc + fork/wait) are shipped to `/tcc_tests/` on the disk image for `tests/test_interactive.py`'s `tcc_*` tests to compile and run.

## Conventions

### C Style

- Kernel C: no stdlib, no libc headers
- Use types from `kernel/common.h`: `uint8_t`, `uint32_t`, `uint64_t`, `size_t`, `bool`
- Always `#include "../common.h"` (or `"common.h"` from kernel/), never `<stdint.h>`
- Variadic functions use `__builtin_va_list`, not `<stdarg.h>`
- 4-space indentation, K&R braces
- See `.editorconfig` for full formatting rules

### File Placement

- New drivers → `kernel/drivers/`
- New arch code → `kernel/arch/x86_64/`
- New memory subsystems → `kernel/memory/`
- New user programs → `user/` (add to `USER_PROGRAMS` list in Makefile)
- Add new kernel `.c` files to `C_SRCS` in the Makefile
- Add new kernel `.asm` files to `ASM_SRCS` in the Makefile

## Code Templates

### New Shell Command

In `kernel/shell/shell.c`:

```c
/* 1. Add the handler function (above the commands[] array) */
static void cmd_mycommand(int argc, char **argv) {
    (void)argc; (void)argv;  /* suppress unused warnings */
    /* vga_printf auto-mirrors to serial — no need for separate debug_printf */
    vga_printf("My command output\n");
}

/* 2. Add to commands[] array (before the {NULL,NULL,NULL} sentinel) */
{"mycommand", "Description of my command", cmd_mycommand},
```

### New Driver

Create `kernel/drivers/mydevice.h`:

```c
#ifndef MYDEVICE_H
#define MYDEVICE_H

#include "../common.h"

void mydevice_init(void);

#endif /* MYDEVICE_H */
```

Create `kernel/drivers/mydevice.c`:

```c
#include "mydevice.h"
#include "../string.h"
#include "../debug/debug.h"

void mydevice_init(void) {
    /* Configure device via port I/O */
    outb(0xNNN, 0xVV);

    debug_printf("My device initialized\n");
}
```

Then:
1. Add `kernel/drivers/mydevice.c` to `C_SRCS` in the Makefile
2. `#include "drivers/mydevice.h"` in `kernel/kernel.c`
3. Call `mydevice_init()` in `kernel_main()` at the appropriate phase

### New IRQ-Driven Driver

```c
#include "mydevice.h"
#include "../arch/x86_64/isr.h"
#include "../arch/x86_64/pic.h"
#include "../debug/debug.h"

#define MY_IRQ 5  /* IRQ number (0-15) */

static void mydevice_handler(struct interrupt_frame *frame) {
    (void)frame;

    /* Read device status, handle data */
    uint8_t status = inb(0xNNN);

    /* MUST send EOI at end of every IRQ handler */
    pic_send_eoi(MY_IRQ);
}

void mydevice_init(void) {
    /* Register handler for INT 32+IRQ */
    isr_register_handler(32 + MY_IRQ, mydevice_handler);

    /* Unmask IRQ line on PIC */
    pic_clear_mask(MY_IRQ);

    debug_printf("My device initialized on IRQ %d\n", (int64_t)MY_IRQ);
}
```

### New Test File

Create `tests/test_myfeature.c`:

```c
#include "test_main.h"
#include <string.h>

void test_myfeature_basic(void) {
    TEST("myfeature does X", 1 + 1 == 2);
    TEST("myfeature does Y", 42 > 0);
}

void test_myfeature_suite(void) {
    printf("=== My feature tests ===\n");
    test_myfeature_basic();
}
```

Then:
1. Add `void test_myfeature_suite(void);` to `tests/test_main.h`
2. Call `test_myfeature_suite();` in `tests/test_main.c`
3. Add `tests/test_myfeature.c` to the gcc line in the Makefile `test-host` target

## Troubleshooting

### Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `undefined reference to 'outb'` | Missing `#include "../common.h"` | Add the include — `outb`/`inb` are `static inline` in `common.h` |
| `undefined reference to '__stack_chk_fail'` | Something removed `-fno-stack-protector` | Restore the flag in CFLAGS |
| `relocation truncated to fit: R_X86_64_32S` | Something removed `-mcmodel=kernel` | Restore the flag — kernel addresses need the kernel memory model |
| `undefined reference to 'printf'` / `'malloc'` | Used libc function in kernel code | Use `vga_printf`, `kmalloc` instead — there is no libc |
| `error: unknown type name 'uint64_t'` | Missing `common.h` include | Add `#include "../common.h"` (path depends on file location) |
| NASM: `error: symbol '...' not defined` | Label typo or missing `global`/`extern` | Check spelling; declare `global` in .asm, `extern` in C |
| Linker: `multiple definition of '...'` | Function defined in header without `static inline` | Use `static inline` for header-defined functions, or move to .c file |
| `grub-mkrescue: not found` | Build tools not installed | Install: `apt install grub-pc-bin grub-common xorriso mtools` |

### Runtime Errors

| Symptom | Cause | Fix |
|---------|-------|-----|
| Triple fault on boot | Page table setup wrong, or kernel too large for mapping | Check `boot.asm` page tables; verify kernel fits in 16 MB |
| Page fault at `0x000000000000XXXX` | Dereferencing physical address without `PHYS_TO_VIRT()` | Wrap address in `PHYS_TO_VIRT()` |
| Page fault at `0xFFFFFFFF9XXXXXXX` | Heap overflow (> 4 MB allocated) | Reduce allocations or increase heap size in `kheap.c` |
| Corrupted registers after interrupt | ISR stub push order doesn't match `interrupt_frame` struct | Verify both match exactly |
| No serial output | Forgot to call `debug_init()`, or QEMU not using `-serial stdio` | Ensure `debug_init()` runs early; use `-serial stdio` flag |
| Keyboard not working | IRQ 1 masked, or handler not registered | Check `keyboard_init()` calls `pic_clear_mask(1)` and `isr_register_handler(33, ...)` |
| No scheduling / single-tasking | PIT disabled or `scheduler_tick()` not called from PIT handler | Verify PIT IRQ 0 handler calls `scheduler_tick()` |
| QEMU smoke test fails | Kernel crashes before printing all boot messages | Run `make debug`, attach GDB, check which phase crashes |

### Test Errors

| Error | Cause | Fix |
|-------|-------|-----|
| `gcc: command not found` during `make test-host` | No host compiler | Install gcc: `apt install gcc` or `pacman -S gcc` |
| Test compiles but segfaults | Stubs missing or test accessing NULL | Check `tests/stubs.h` covers all referenced symbols |
| QEMU smoke test timeout | Kernel hangs during boot | Run `make run` manually, check which subsystem hangs |
| Smoke test: pattern not found | Boot message text changed | Update `tests/qemu_smoke.exp` to match current `kernel_main()` debug_printf calls |

## Gotchas

1. **ISR stub push order must match `interrupt_frame` struct.** Changing `isr_stubs.asm` or the C struct independently causes silent register corruption.

2. **Never use SSE/AVX.** The kernel doesn't save SSE state on context switch. GCC must not emit SSE instructions — enforced by `-mno-sse`.

3. **Never dereference raw physical addresses.** Always use `PHYS_TO_VIRT()` or `KERNEL_PHYS_TO_VIRT()`.

4. **`kernel/memory/kheap.c` is a real first-fit free-list allocator, not a bump allocator.** `kfree()` genuinely reclaims memory, with both forward and backward coalescing (`kheap.c:225-241`). This corrects a long-standing wrong claim in this file, which in turn led a past debugging session to misdiagnose a real bug (see next gotcha) — verify this kind of claim against the actual source before relying on it. Page-aligned allocations (`kmalloc_page_aligned`, used only for kernel stacks) go through separate logic from plain `kmalloc`/`kfree` and are worth extra scrutiny if touched (see next gotcha).

5. **`kmalloc_page_aligned()` had a real algorithm bug (fixed): a candidate block's alignment "waste" (padding before the next page boundary) was only handled when exactly 0 or `>= BLOCK_HDR_SIZE + MIN_BLOCK_SIZE` (40) bytes — 1..39 bytes of waste (which depends only on a block's address modulo `PAGE_SIZE`, nothing about its size) fell through neither branch and rejected the block outright, even as the *only* free block in the entire heap and vastly bigger than needed.** Reproduced in `tests/test_kheap.c` with a single ~4MB free block still failing a 20KB page-aligned request. This — not "kfree is a no-op" — was the real mechanism behind the kernel-stack-exhaustion CI bug fixed in `kernel/process/process.c` (commit `5815234`); that fix (a dedicated free list for kernel stacks) is still kept as a performance optimization, not because it's still needed for correctness.

6. **Context switch only saves callee-saved registers** (r12-r15, rbx, rbp). Caller-saved regs are on the ISR stack frame.

7. **Disabling PIT stops scheduling entirely.** Timer IRQ drives `scheduler_tick()`.

8. **`keyboard_getchar()` is blocking** — it halts until a keypress interrupt. The ring buffer is fed by two IRQs: PS/2 on IRQ 1 *and* COM1 serial RX on IRQ 4, so headless/remote deployments (ttyd wrapping QEMU, virtio-serial, etc.) work without a PS/2 controller at all.

9. **Boot page tables use 2 MB huge pages.** VMM adds 4 KB pages on top but doesn't replace them.

10. **All `debug_printf` args are 64-bit.** `%d` expects `int64_t`, `%u`/`%x` expect `uint64_t`. Cast smaller types: `(uint64_t)my_int`.

11. **`strtok` is not reentrant.** It uses a static `strtok_state` variable. Don't call from interrupt handlers.

12. **Per-process FD table** has 16 slots (PROCESS_MAX_FDS). FDs 0/1/2 are FD_CONSOLE by default. FD types: FD_NONE, FD_VFS, FD_PIPE_READ, FD_PIPE_WRITE, FD_CONSOLE.

13. **Pipes block the calling process** when reading from an empty pipe or writing to a full pipe. `pipe_read()` returns 0 (EOF) when all write ends are closed.

14. **Signals**: SIGKILL cannot be caught. Signal handlers must call `sys_sigreturn()` to restore pre-signal context. Default action for SIGCHLD is ignore; for SIGINT/SIGTERM/SIGKILL/SIGPIPE is terminate.

15. **User programs are flat binaries at `0x400000`.** They use `user/syscall.h` for syscalls, NOT kernel headers. Compiled with `USER_CFLAGS` (no `-mcmodel=kernel`).

16. **User program build pipeline:** `gcc -c` → `ld -T user/user.ld` → `objcopy -O binary` → `objcopy -I binary -O elf64-x86-64` (embeds as `.rodata` in kernel). Symbols: `_binary_<name>_bin_start/end`.

17. **Per-process address spaces** copy PML4[256] (phys direct map) and PML4[511] (kernel) from boot PML4. User pages go in PML4[0]. PTE_USER must be set at ALL page table levels.

18. **`vga_printf` supports `%-Nu` `%-Ns` `%-Nd` `%-Nx`** (left-aligned with width N), `%0Nu` `%0Nx` (zero-padded), and `%Nu` (right-aligned). All args are 64-bit (see gotcha 10).

19. **`vga_putchar()` auto-mirrors to serial** (COM1) once `debug_init()` completes. Shell commands should use `vga_printf()` — no separate `debug_printf()` needed. Interactive tests validate output via the serial log.

20. **PS/2 mouse init must drain ACK and unmask cascade.** After enabling the mouse (`0xF4`), it sends an ACK byte (0xFA) on port 0x60. This byte MUST be read immediately — if left in the PS/2 output buffer, it blocks ALL keyboard input. Also, IRQ 12 (mouse) is on the slave PIC, so cascade (IRQ 2) on the master PIC must be unmasked, or the ACK will never be read by the handler.

21. **Native QEMU (10.1.3) `-serial file:` is broken on Windows** — output is never flushed to disk. Use `-serial stdio` for interactive use or `-serial tcp:` for programmatic access.

22. **A PT_LOAD segment's `elf_data`/`elf_data_filesz` must be adjusted for page alignment, not just `elf_vaddr`.** `elf_load()` page-aligns `seg_start` down from `p_vaddr` for demand paging, but the segment's *content* pointer (`data + p_offset`) and its valid-length (`p_filesz`) must be shifted backward/lengthened by that same `p_vaddr & (PAGE_SIZE-1)` delta — otherwise every demand-page fault in that segment reads from the wrong file offset, silently returning zero-filled bytes for data (e.g. GOT entries) that genuinely exists in the file. GCC/`ld`-produced segments are always page-aligned so this never showed up before TCC's own segment layout (which isn't) started getting executed.

23. **TCC (`user/tcc/`) has no target-side `va_list`/`va_arg` support on x86-64 outside its own runtime library, which this port doesn't ship.** Don't have TCC compile a `.c` file that *implements* a variadic function (real `__builtin_va_arg` body) — link the precompiled `user/lib/hobbyc.o` instead. Calling a variadic function needs nothing special on the caller side. `user/lib/libc.h` picks the right `va_list` per toolchain via `#ifdef __TINYC__`.

24. **TCC output must be `-static -nostdlib`.** The default dynamic-link path builds `.dynsym`/`.dynamic`/`PT_INTERP` sections `user/ld.so.c` was never built to handle (its relocation support is narrow — `MAX_LIBS 8`, only a few relocation types) and will crash.

25. **`user/lib/*.c` gets compiled twice, by two different compilers, and must parse under both.** The host cross-toolchain compiles it into `hobbyc.o` (and into `tcc.elf`/`libc_test.elf` directly); nothing in it should assume GCC-only builtins without a `__TINYC__`-guarded fallback (see gotcha 23).
