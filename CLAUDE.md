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

This project is developed on **Windows 11 with Git Bash**. There is **no native gcc, nasm, or make** — all building and testing happens through **Docker**. QEMU **is** installed natively for interactive testing.

### How to Build and Test (the ONLY way that works)

```bash
# Build the Docker image (one-time, has gcc + nasm + grub + qemu):
MSYS_NO_PATHCONV=1 docker build -t hobbyos-test .

# Run any make target inside Docker:
MSYS_NO_PATHCONV=1 docker run --rm -v "C:/Users/Uporabnik/Documents/hobbyos:/hobbyos" hobbyos-test make test
MSYS_NO_PATHCONV=1 docker run --rm -v "C:/Users/Uporabnik/Documents/hobbyos:/hobbyos" hobbyos-test make test-host
MSYS_NO_PATHCONV=1 docker run --rm -v "C:/Users/Uporabnik/Documents/hobbyos:/hobbyos" hobbyos-test make iso
MSYS_NO_PATHCONV=1 docker run --rm -v "C:/Users/Uporabnik/Documents/hobbyos:/hobbyos" hobbyos-test make test-qemu
MSYS_NO_PATHCONV=1 docker run --rm -v "C:/Users/Uporabnik/Documents/hobbyos:/hobbyos" hobbyos-test make test-interactive
```

### Critical: Docker Volume Mount on Windows/MSYS2

- **Always** prefix Docker commands with `MSYS_NO_PATHCONV=1` — without it, Git Bash mangles `/hobbyos` into `C:/Program Files/Git/hobbyos`.
- **Always** use the full Windows path `C:/Users/Uporabnik/Documents/hobbyos:/hobbyos` — do NOT use `$(pwd)` as it expands incorrectly in Git Bash.
- The Dockerfile's `WORKDIR` is `/hobbyos`, which matches the mount target.

### GitHub CLI (`gh`)

```bash
# gh is installed at this path — add to PATH before use:
export PATH="$PATH:/c/Program Files/GitHub CLI"

# Then use normally:
gh run list
gh run view <run-id>
gh run watch <run-id>
gh repo view --json visibility
```

### Checking CI Status

```bash
export PATH="$PATH:/c/Program Files/GitHub CLI"

# List recent workflow runs:
gh run list --limit 5

# Watch a running workflow (blocks until done):
gh run watch <run-id>

# View details of a specific run:
gh run view <run-id>

# Rerun a failed workflow:
gh run rerun <run-id>

# If rerun fails with "cannot be retried" (e.g. startup_failure),
# push an empty commit to trigger a fresh run:
git commit --allow-empty -m "Trigger CI" && git push
```

### Interactive Test Output (`make test-interactive`)

Runs 18 tests by sending keystrokes to QEMU via monitor socket and checking serial output. Creates a fresh ext2 disk image, boots the OS, waits for autotests to pass, then exercises shell commands, user programs, ext2 operations, TTY editing, and Ctrl+C.

**Output files:**
- `tests/interactive_serial.log` — Full serial transcript from QEMU
- `tests/interactive_results.json` — Structured per-test results (name, passed, duration, output_snippet, error)

**JSON format:**
```json
{"total": 18, "passed": 18, "failed": 0, "tests": [{"name": "boot_and_autotests", "passed": true, ...}, ...]}
```

**For AI agents:** After running `make test-interactive`, read `tests/interactive_results.json` to see which tests passed/failed and inspect `tests/interactive_serial.log` for debugging.

### Line Endings

- `.gitattributes` enforces LF in the repository.
- Windows tools may create files with CRLF. Fix with: `sed -i 's/\r$//' <file>`
- If `make` fails with "No rule to make target", check for CRLF in the Makefile: `file Makefile` should say "ASCII text", NOT "with CRLF line terminators".

### Running Interactively (Native QEMU)

After building the ISO via Docker, launch QEMU natively to get a GUI window for interactive testing:

```bash
/c/msys64/ucrt64/bin/qemu-system-x86_64.exe \
  -cdrom C:/Users/Uporabnik/Documents/hobbyos/hobbyos.iso \
  -serial stdio -m 128M -no-reboot -no-shutdown
```

- QEMU binary path: `/c/msys64/ucrt64/bin/qemu-system-x86_64.exe`
- VGA output appears in the QEMU GUI window; serial debug output appears in the terminal
- Use `run_in_background: true` when launching from Claude Code so the window stays open for the user

### Native Tool Availability

| Tool | Available? | Path / How to use |
|------|-----------|-------------------|
| `gcc` | No | Use Docker |
| `nasm` | No | Use Docker |
| `make` | No (in bash) | Use Docker |
| `grub-mkrescue` | No | Use Docker |
| `qemu-system-x86_64` | **Yes** | `/c/msys64/ucrt64/bin/qemu-system-x86_64.exe` |
| `docker` | Yes | Direct from Git Bash |
| `gh` (GitHub CLI) | Yes | `/c/Program Files/GitHub CLI/gh` (needs PATH) |
| `git` | Yes | Direct from Git Bash |

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
| Call `kfree()` and expect memory back | Bump allocator — `kfree` is a no-op | Allocate carefully; memory is not reclaimed |
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
│   │   ├── keyboard.c / .h     # PS/2, IRQ 1, scan code Set 1, circular buffer
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
│       └── debug.c / debug.h   # COM1 serial (38400 baud, 8-N-1)
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
│   └── user.ld                 # User program linker script (entry at 0x400000)
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
    ├── qemu_smoke.sh           # QEMU boot smoke test script
    ├── qemu_smoke.exp          # Expected serial output patterns
    └── test_interactive.py     # Interactive QEMU test suite (18 tests, sendkey-based)
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

### IRQ Mapping (8259 PIC)

- Master: IRQ 0–7 → INT 32–39
- Slave: IRQ 8–15 → INT 40–47
- Timer = IRQ 0 (INT 32), Keyboard = IRQ 1 (INT 33)

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

4. **`kfree()` is a no-op.** The bump allocator cannot reclaim memory.

5. **Context switch only saves callee-saved registers** (r12-r15, rbx, rbp). Caller-saved regs are on the ISR stack frame.

6. **Disabling PIT stops scheduling entirely.** Timer IRQ drives `scheduler_tick()`.

7. **`keyboard_getchar()` is blocking** — it halts until a keypress interrupt.

8. **Boot page tables use 2 MB huge pages.** VMM adds 4 KB pages on top but doesn't replace them.

9. **All `debug_printf` args are 64-bit.** `%d` expects `int64_t`, `%u`/`%x` expect `uint64_t`. Cast smaller types: `(uint64_t)my_int`.

10. **`strtok` is not reentrant.** It uses a static `strtok_state` variable. Don't call from interrupt handlers.

11. **Per-process FD table** has 16 slots (PROCESS_MAX_FDS). FDs 0/1/2 are FD_CONSOLE by default. FD types: FD_NONE, FD_VFS, FD_PIPE_READ, FD_PIPE_WRITE, FD_CONSOLE.

12. **Pipes block the calling process** when reading from an empty pipe or writing to a full pipe. `pipe_read()` returns 0 (EOF) when all write ends are closed.

13. **Signals**: SIGKILL cannot be caught. Signal handlers must call `sys_sigreturn()` to restore pre-signal context. Default action for SIGCHLD is ignore; for SIGINT/SIGTERM/SIGKILL/SIGPIPE is terminate.

14. **User programs are flat binaries at `0x400000`.** They use `user/syscall.h` for syscalls, NOT kernel headers. Compiled with `USER_CFLAGS` (no `-mcmodel=kernel`).

12. **User program build pipeline:** `gcc -c` → `ld -T user/user.ld` → `objcopy -O binary` → `objcopy -I binary -O elf64-x86-64` (embeds as `.rodata` in kernel). Symbols: `_binary_<name>_bin_start/end`.

13. **Per-process address spaces** copy PML4[256] (phys direct map) and PML4[511] (kernel) from boot PML4. User pages go in PML4[0]. PTE_USER must be set at ALL page table levels.

14. **`vga_printf` has no width/alignment support** (`%-4u`, `%-10s` print literally). Use manual padding if needed.
