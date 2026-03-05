# HobbyOS

A minimal 64-bit x86_64 hobby operating system kernel, written from scratch in C and assembly. Boots via GRUB Multiboot2 into an interactive shell with preemptive multitasking.

---

## Quick Start

**Docker (recommended — works on any OS):**

```bash
docker build -t hobbyos .
docker run --rm -v "$(pwd)":/hobbyos hobbyos make iso
qemu-system-x86_64 -cdrom hobbyos.iso -serial stdio -m 128M -no-reboot -no-shutdown
```

**Native Linux:**

```bash
sudo apt install nasm gcc binutils make grub-pc-bin grub-common xorriso mtools qemu-system-x86
make run
```

**MSYS2 (Windows):**

```bash
pacman -S nasm mingw-w64-x86_64-gcc make grub xorriso mtools mingw-w64-x86_64-qemu
make iso
qemu-system-x86_64 -cdrom hobbyos.iso -serial stdio -m 128M -no-reboot -no-shutdown
```

---

## Project Structure

```
hobbyos/
├── Makefile                              # Build system (make all / iso / run / debug / clean)
├── Dockerfile                            # Ubuntu 24.04 build environment
├── linker.ld                             # Higher-half kernel linking layout
├── grub.cfg                              # GRUB Multiboot2 boot entry
├── boot/
│   └── boot.asm                          # Multiboot2 header, 32→64-bit transition, page tables
└── kernel/
    ├── kernel.c                          # kernel_main() — boot sequence orchestrator
    ├── common.h                          # Types, I/O ports, CR access, kpanic()
    ├── string.c / string.h               # strlen, strcmp, memcpy, strtok, etc.
    ├── arch/x86_64/
    │   ├── gdt.c / gdt.h                 # Global Descriptor Table (code/data/TSS segments)
    │   ├── gdt_flush.asm                 # Reload segment registers after GDT load
    │   ├── tss.c / tss.h                 # Task State Segment (RSP0 for ring transitions)
    │   ├── idt.c / idt.h                 # Interrupt Descriptor Table (256 gates)
    │   ├── idt_flush.asm                 # lidt instruction wrapper
    │   ├── isr.c / isr.h                 # ISR dispatch + handler registration
    │   ├── isr_stubs.asm                 # 256 ISR entry stubs (macro-generated)
    │   └── pic.c / pic.h                 # 8259 PIC init, remap, EOI, masking
    ├── interrupts/
    │   └── interrupts.c / interrupts.h   # Architecture-independent handler setup
    ├── memory/
    │   ├── pmm.c / pmm.h                 # Physical memory manager (bitmap allocator)
    │   ├── vmm.c / vmm.h                 # Virtual memory manager (4-level paging)
    │   └── kheap.c / kheap.h             # Kernel heap (bump allocator, 4 MB)
    ├── process/
    │   └── process.c / process.h         # Process table, PCB, kernel stack allocation
    ├── scheduler/
    │   ├── scheduler.c / scheduler.h     # Round-robin scheduler with ready queue
    │   └── context_switch.asm            # Save/restore callee-saved registers + RSP/RIP
    ├── drivers/
    │   ├── driver.h                      # Generic driver interface
    │   ├── vga.c / vga.h                 # VGA text mode (80×25), printf, scrolling, cursor
    │   ├── keyboard.c / keyboard.h       # PS/2 keyboard, scan code → ASCII, circular buffer
    │   └── pit.c / pit.h                 # Programmable Interval Timer (system clock)
    ├── shell/
    │   └── shell.c / shell.h             # Interactive command shell (6 commands)
    └── debug/
        └── debug.c / debug.h             # Serial COM1 debug output (38400 baud)
```

---

## Architecture Overview

HobbyOS is a **higher-half kernel** running in x86_64 long mode. The kernel lives at virtual address `0xFFFFFFFF80000000` while physical memory is identity-mapped starting at `0xFFFF800000000000`.

### Boot Sequence

```
GRUB loads kernel.bin at physical 0x100000
 │
 ▼
boot.asm — 32-bit entry (_start)
 ├─ Verify CPUID + long mode support
 ├─ Build page tables (2 MB huge pages):
 │   ├─ Identity-map first 2 MB          (boot continuity)
 │   ├─ 0xFFFFFFFF80000000 → 0x0         (kernel, 16 MB)
 │   └─ 0xFFFF800000000000 → 0x0         (physical map, 512 MB)
 ├─ Enable PAE → set EFER.LME → enable paging
 └─ Far jump to 64-bit long_mode_start
     ├─ Reload segment registers
     ├─ Set up 16 KB kernel stack
     └─ call kernel_main(magic, multiboot_info)
         │
         ▼
kernel_main() initialization phases:
 1. vga_init()           — VGA text output
 2. debug_init()         — Serial COM1 (38400 baud)
 3. gdt_init()           — GDT with kernel code/data/TSS
 4. pic_init()           — Remap IRQs to INT 32–47
 5. idt_init()           — Install 256 ISR stubs
 6. interrupts_init()    — Register breakpoint + page fault handlers
 7. pit_init(100)        — 100 Hz timer (scheduler tick source)
 8. keyboard_init()      — PS/2 IRQ 1 handler
 9. pmm_init()           — Parse Multiboot2 memory map, build bitmap
10. vmm_init()           — Adopt boot page tables (CR3)
11. kheap_init()         — Map 4 MB heap at 0xFFFFFFFF90000000
12. process_init()       — Clear process table (64 slots)
13. scheduler_init()     — Create idle process
14. sti()                — Enable interrupts
15. shell_run()          — Enter interactive shell (never returns)
```

### Memory Layout

| Virtual Address Range               | Size   | Description                         |
|--------------------------------------|--------|-------------------------------------|
| `0x0000000000000000–0x00000000001FFFFF` | 2 MB   | Identity map (boot only)          |
| `0xFFFF800000000000–0xFFFF80001FFFFFFF` | 512 MB | Physical memory direct map        |
| `0xFFFFFFFF80000000–0xFFFFFFFF80FFFFFF` | 16 MB  | Kernel text/data/bss              |
| `0xFFFFFFFF90000000–0xFFFFFFFF903FFFFF` | 4 MB   | Kernel heap (bump allocator)      |

**Address conversion macros** (defined in `kernel/common.h`):

```c
PHYS_TO_VIRT(addr)         // addr + 0xFFFF800000000000
VIRT_TO_PHYS(addr)         // addr - 0xFFFF800000000000
KERNEL_PHYS_TO_VIRT(addr)  // addr + 0xFFFFFFFF80000000
KERNEL_VIRT_TO_PHYS(addr)  // addr - 0xFFFFFFFF80000000
```

---

## Subsystems

### VGA Driver

Text mode display on the standard VGA framebuffer.

- **Files:** `kernel/drivers/vga.c`, `kernel/drivers/vga.h`
- **Resolution:** 80×25 characters at `0xFFFF8000000B8000` (physical `0xB8000`)
- **Features:** Scrolling, hardware cursor, tab expansion, backspace, 16 colors
- **Public API:**

| Function | Description |
|----------|-------------|
| `vga_init()` | Clear screen, reset cursor |
| `vga_clear()` | Clear entire screen |
| `vga_putchar(c)` | Print one character |
| `vga_puts(str)` | Print string |
| `vga_printf(fmt, ...)` | Formatted print (`%s`, `%d`, `%u`, `%x`, `%p`, `%c`, `%%`) |
| `vga_set_color(fg, bg)` | Set text color (VGA_BLACK..VGA_WHITE) |
| `vga_backspace()` | Erase previous character |

### Keyboard Driver

PS/2 keyboard with interrupt-driven input and scan code Set 1 translation.

- **Files:** `kernel/drivers/keyboard.c`, `kernel/drivers/keyboard.h`
- **Interrupt:** IRQ 1 → INT 33
- **Buffer:** 256-byte circular buffer
- **Modifiers:** Shift, Ctrl, Caps Lock
- **Public API:**

| Function | Description |
|----------|-------------|
| `keyboard_init()` | Register IRQ 1 handler, unmask PIC |
| `keyboard_getchar()` | Blocking read — halts until key available |
| `keyboard_haschar()` | Non-blocking check |

### PIT Timer

System clock source driving the scheduler.

- **Files:** `kernel/drivers/pit.c`, `kernel/drivers/pit.h`
- **Interrupt:** IRQ 0 → INT 32
- **Frequency:** 100 Hz (10 ms per tick), configurable via `pit_init(freq)`
- **Scheduler integration:** `pit_handler()` calls `scheduler_tick()` every tick
- **Public API:**

| Function | Description |
|----------|-------------|
| `pit_init(freq)` | Program PIT channel 0, register IRQ 0 |
| `pit_get_ticks()` | Total ticks since boot |
| `pit_get_uptime_seconds()` | Seconds since boot |

### GDT / TSS

Flat segmentation model with a Task State Segment for stack switching.

- **Files:** `kernel/arch/x86_64/gdt.c`, `gdt.h`, `gdt_flush.asm`, `tss.c`, `tss.h`
- **Segments:** Null (0x00), Kernel Code (0x08), Kernel Data (0x10), TSS (0x18)
- **TSS purpose:** Holds RSP0 — updated by scheduler on context switch
- **Public API:** `gdt_init()`, `tss_set_rsp0(rsp)`

### IDT / PIC / Interrupts

Full 256-entry IDT with 8259 PIC support and registered C handlers.

- **Files:** `kernel/arch/x86_64/idt.c`, `isr.c`, `isr_stubs.asm`, `pic.c`, `kernel/interrupts/interrupts.c`
- **PIC remap:** Master IRQ 0–7 → INT 32–39, Slave IRQ 8–15 → INT 40–47
- **ISR stubs:** 256 macro-generated entries; push uniform frame (error code + int number + all GPRs)
- **Dispatch:** `isr_handler()` looks up `isr_handlers[int_no]` and calls if registered
- **Default handlers:** Breakpoint (INT 3), Page Fault (INT 14, prints CR2 + error code)
- **Public API:**

| Function | Description |
|----------|-------------|
| `idt_init()` | Install all 256 ISR stubs into IDT, load IDTR |
| `isr_register_handler(n, fn)` | Register a C handler for interrupt `n` |
| `pic_init()` | Remap PIC, mask all IRQs |
| `pic_send_eoi(irq)` | Acknowledge interrupt (master + slave if ≥ 8) |
| `pic_clear_mask(irq)` | Unmask (enable) specific IRQ line |
| `pic_set_mask(irq)` | Mask (disable) specific IRQ line |
| `interrupts_init()` | Register default exception handlers |

### Physical Memory Manager

Bitmap-based page frame allocator initialized from the Multiboot2 memory map.

- **Files:** `kernel/memory/pmm.c`, `kernel/memory/pmm.h`
- **Page size:** 4 KB
- **Reserves:** First 1 MB + kernel image + bitmap itself
- **Max physical:** Capped at 512 MB (matches direct map region)
- **Public API:**

| Function | Description |
|----------|-------------|
| `pmm_init(multiboot_info_phys)` | Parse memory map, build bitmap |
| `pmm_alloc_page()` | Allocate one 4 KB frame (returns physical addr, 0 on fail) |
| `pmm_free_page(phys)` | Free one frame |
| `pmm_get_free_pages()` | Count of free frames |
| `pmm_get_total_pages()` | Count of total usable frames |

### Virtual Memory Manager

4-level page table walker using the boot-established mappings.

- **Files:** `kernel/memory/vmm.c`, `kernel/memory/vmm.h`
- **Page tables:** PML4 → PDPT → PD → PT (standard x86_64)
- **Flags:** `PTE_PRESENT`, `PTE_WRITABLE`, `PTE_USER`, `PTE_HUGE`, `PTE_NX`
- **Public API:**

| Function | Description |
|----------|-------------|
| `vmm_init()` | Read CR3, adopt boot page tables |
| `vmm_map_page(virt, phys, flags)` | Create PTE, allocates intermediate tables, flushes TLB |
| `vmm_unmap_page(virt)` | Remove PTE, flush TLB |
| `vmm_get_physical(virt)` | Walk tables, return physical address |

### Kernel Heap

Simple bump allocator for dynamic kernel allocations.

- **Files:** `kernel/memory/kheap.c`, `kernel/memory/kheap.h`
- **Location:** `0xFFFFFFFF90000000`, 4 MB
- **Alignment:** All allocations 16-byte aligned
- **Public API:**

| Function | Description |
|----------|-------------|
| `kheap_init()` | Allocate physical pages, map heap region |
| `kmalloc(size)` | Allocate memory (returns NULL if exhausted) |
| `kfree(ptr)` | No-op (bump allocator cannot free) |

### Process Manager

Static process table with PCB (Process Control Block) management.

- **Files:** `kernel/process/process.c`, `kernel/process/process.h`
- **Limit:** 64 processes (`MAX_PROCESSES`)
- **Stack:** 8 KB kernel stack per process
- **States:** `UNUSED`, `READY`, `RUNNING`, `BLOCKED`, `TERMINATED`
- **Context struct:** Callee-saved registers (r15, r14, r13, r12, rbx, rbp) + rsp + rip
- **Public API:**

| Function | Description |
|----------|-------------|
| `process_init()` | Zero out process table |
| `process_create(name, entry)` | Allocate PCB + kernel stack, set initial context |
| `process_get_by_pid(pid)` | Look up process |
| `process_table_get()` | Return pointer to process table |
| `process_get_count()` | Count of active processes |

### Scheduler

Preemptive round-robin scheduler driven by the PIT timer.

- **Files:** `kernel/scheduler/scheduler.c`, `scheduler.h`, `context_switch.asm`
- **Algorithm:** FIFO ready queue, processes requeued after running
- **Quantum:** 10 ticks = 100 ms (at 100 Hz PIT)
- **Idle process:** Auto-created at init, halts CPU when nothing else to run
- **Context switch:** Assembly routine saves/loads callee-saved regs, RSP, RIP; updates TSS RSP0
- **Public API:**

| Function | Description |
|----------|-------------|
| `scheduler_init()` | Create idle process, init ready queue |
| `scheduler_add(proc)` | Enqueue process into ready queue |
| `scheduler_tick()` | Called from PIT; triggers `schedule()` every 10 ticks |
| `schedule()` | Pick next process, context switch |
| `scheduler_get_current()` | Return currently running process |

### Shell

Interactive command-line interface with tokenized input parsing.

- **Files:** `kernel/shell/shell.c`, `kernel/shell/shell.h`
- **Prompt:** `hobbyos> ` (green text)
- **Input buffer:** 256 characters, up to 16 arguments
- **Public API:** `shell_run()` — enters infinite command loop (never returns)

### Debug Serial

Printf-style output over COM1 for host-side debugging.

- **Files:** `kernel/debug/debug.c`, `kernel/debug/debug.h`
- **Port:** COM1 (`0x3F8`), 38400 baud, 8-N-1
- **Newline handling:** LF → CR+LF for terminal compatibility
- **Public API:**

| Function | Description |
|----------|-------------|
| `debug_init()` | Configure COM1 serial port |
| `debug_putchar(c)` | Send one byte |
| `debug_puts(str)` | Send string |
| `debug_printf(fmt, ...)` | Formatted serial output |

---

## Shell Commands

| Command   | Description                | Example                  |
|-----------|----------------------------|--------------------------|
| `help`    | List all available commands | `help`                   |
| `ps`      | Show running processes (PID, state, name) | `ps`     |
| `mem`     | Display memory statistics (total/used/free pages + MB) | `mem` |
| `uptime`  | Show system uptime (min, sec, ticks) | `uptime`        |
| `echo`    | Print arguments to screen  | `echo hello world`       |
| `clear`   | Clear the VGA screen       | `clear`                  |

---

## Building

### Prerequisites

| Tool | Purpose |
|------|---------|
| `nasm` | Assembler (x86_64 ELF output) |
| `gcc` | C compiler (or `x86_64-elf-gcc` cross-compiler) |
| `ld` | Linker (or `x86_64-elf-ld`) |
| `make` | Build orchestration |
| `grub-mkrescue` | ISO creation with GRUB bootloader |
| `xorriso` | ISO image manipulation (used by grub-mkrescue) |
| `mtools` | FAT filesystem tools (used by grub-mkrescue) |
| `grub-pc-bin` | GRUB i386-pc modules for BIOS boot |

### Makefile Targets

| Target | Command | Description |
|--------|---------|-------------|
| `all` | `make` or `make all` | Compile to `kernel.bin` |
| `iso` | `make iso` | Build `hobbyos.iso` (includes GRUB) |
| `run` | `make run` | Build ISO + launch QEMU |
| `debug` | `make debug` | Build ISO + launch QEMU with GDB stub |
| `clean` | `make clean` | Remove all build artifacts |
| `test` | `make test` | Run all tests (unit + QEMU smoke) |
| `test-host` | `make test-host` | Host-side unit tests only (fast) |
| `test-qemu` | `make test-qemu` | QEMU boot smoke test |

### Method 1: Docker (any host OS)

```bash
# Build the Docker image (one-time)
docker build -t hobbyos .

# Compile kernel and create ISO
docker run --rm -v "$(pwd)":/hobbyos hobbyos make iso

# Run in QEMU on host
qemu-system-x86_64 -cdrom hobbyos.iso -serial stdio -m 128M -no-reboot -no-shutdown
```

### Method 2: Native Linux (Debian/Ubuntu)

```bash
# Install dependencies
sudo apt install nasm gcc binutils make grub-pc-bin grub-common xorriso mtools qemu-system-x86

# Build and run
make run
```

### Method 3: MSYS2 (Windows)

```bash
# Install dependencies (from MSYS2 terminal)
pacman -S nasm mingw-w64-x86_64-gcc make grub xorriso mtools mingw-w64-x86_64-qemu

# Build ISO
make iso

# Run
qemu-system-x86_64 -cdrom hobbyos.iso -serial stdio -m 128M -no-reboot -no-shutdown
```

## Testing

Run the full test suite:

```bash
make test
```

This runs:
- **Host unit tests** (`make test-host`): Tests string functions, memory bitmap, printf formatting
- **QEMU smoke test** (`make test-qemu`): Boots the kernel and verifies all subsystems initialize

Run individually:

```bash
make test-host   # Fast — no QEMU needed
make test-qemu   # Requires QEMU and a built ISO
```

---

### Compiler Flags Explained

```makefile
-ffreestanding        # No hosted C environment (no stdlib)
-mno-red-zone         # Disable red zone — interrupts would corrupt it
-mcmodel=kernel       # Kernel lives in upper 2 GB of address space
-fno-stack-protector  # No __stack_chk_fail (no libc to provide it)
-fno-pic              # No position-independent code (kernel is at fixed address)
-nostdlib -nostdinc   # No standard library or headers
-mno-sse -mno-sse2 -mno-mmx -mno-avx  # No SIMD — kernel doesn't save SSE state
-O2 -g                # Optimize + debug symbols
```

---

## Running & Debugging

### Standard Run

```bash
qemu-system-x86_64 -cdrom hobbyos.iso -serial stdio -m 128M -no-reboot -no-shutdown
```

Serial output appears in the terminal. VGA output appears in the QEMU window.

### Debug with GDB

```bash
# Terminal 1: Start QEMU paused, waiting for GDB
make debug

# Terminal 2: Attach GDB
gdb kernel.bin
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

The `debug` target adds `-s -S -d int,cpu_reset`:
- `-s` — GDB stub on port 1234
- `-S` — Freeze CPU at startup (wait for GDB `continue`)
- `-d int,cpu_reset` — Log interrupts and CPU resets to stderr

### Useful QEMU Flags

| Flag | Purpose |
|------|---------|
| `-serial stdio` | Route COM1 serial to terminal (debug output) |
| `-m 128M` | Set RAM to 128 MB |
| `-no-reboot` | Halt instead of rebooting on triple fault |
| `-no-shutdown` | Keep QEMU open after guest shutdown |
| `-d int` | Log all interrupts to stderr |
| `-d cpu_reset` | Log CPU resets |
| `-D logfile.txt` | Write QEMU debug log to file |
| `-monitor stdio` | QEMU monitor instead of serial (use `-serial file:serial.log` separately) |
| `-curses` | Text-mode display (no GUI window needed) |

---

## Key Design Decisions & Gotchas

- **No SSE/AVX.** The kernel is compiled with `-mno-sse -mno-sse2 -mno-mmx -mno-avx`. Interrupt handlers don't save/restore SSE state, so any SSE use would silently corrupt registers across context switches. GCC must never emit SSE instructions.

- **No red zone.** `-mno-red-zone` is mandatory. The System V ABI red zone (128 bytes below RSP) would be destroyed by hardware interrupt stack pushes. Without this flag, local variables in leaf functions get corrupted by interrupts.

- **ISR stub push order matters.** `isr_stubs.asm` pushes a uniform frame: dummy error code (if needed) → interrupt number → all GPRs. The `interrupt_frame` struct in C **must** match this exact layout in reverse. Changing either side without the other causes silent corruption.

- **Higher-half kernel.** The kernel runs at `0xFFFFFFFF80000000` (requires `-mcmodel=kernel`). Physical memory is accessed via a direct map at `0xFFFF800000000000`. Never dereference raw physical addresses — always use `PHYS_TO_VIRT()`.

- **Bump allocator `kfree()` is a no-op.** The kernel heap uses a bump allocator for simplicity. Allocated memory is never reclaimed. This is fine for a hobby OS but would need a real free-list allocator for production use.

- **Context switch saves only callee-saved registers.** The `context_switch()` assembly only saves r15, r14, r13, r12, rbx, rbp, rsp, rip. Caller-saved registers (rax, rcx, rdx, rsi, rdi, r8–r11) are already saved by the ISR stub frame. The `struct cpu_context` layout **must** match `context_switch.asm`.

- **PIT drives the scheduler.** The PIT fires at 100 Hz. Every 10 ticks (100 ms), `scheduler_tick()` calls `schedule()`. Disabling the PIT interrupt stops scheduling entirely.

- **Keyboard input is blocking.** `keyboard_getchar()` halts the CPU in a loop (`hlt`) until a key is available. This works because the shell runs in the context of the main kernel thread, and `hlt` wakes on any interrupt.

- **Boot page tables use 2 MB huge pages.** The identity map, kernel map, and physical memory map all use 2 MB pages set up in `boot.asm`. The VMM can add 4 KB mappings on top of these but does not replace the boot tables.

---

## Adding New Features

### Adding a Shell Command

1. Write your command function in `kernel/shell/shell.c`:

```c
static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    // Your implementation here
    vga_printf("Rebooting...\n");
}
```

2. Add an entry to the `commands[]` array (before the `{NULL, NULL, NULL}` sentinel):

```c
{"reboot", "Reboot the system", cmd_reboot},
```

3. Rebuild: `make iso`

### Adding a Driver

1. Create `kernel/drivers/mydevice.c` and `kernel/drivers/mydevice.h`
2. Implement `mydevice_init()` and any I/O functions
3. If interrupt-driven, register a handler in `mydevice_init()`:
   ```c
   #include "../arch/x86_64/isr.h"
   #include "../arch/x86_64/pic.h"

   static void mydevice_handler(struct interrupt_frame *frame) {
       (void)frame;
       // Handle interrupt
       pic_send_eoi(MY_IRQ_NUMBER);
   }

   void mydevice_init(void) {
       isr_register_handler(32 + MY_IRQ_NUMBER, mydevice_handler);
       pic_clear_mask(MY_IRQ_NUMBER);
   }
   ```
4. Add source file to `C_SRCS` in the Makefile
5. Call `mydevice_init()` from `kernel_main()` in `kernel/kernel.c`

### Adding an Interrupt Handler

1. Pick the interrupt vector (exceptions: 0–31, IRQs: 32–47, software: 48–255)
2. Register in `kernel/interrupts/interrupts.c` or your driver's init:
   ```c
   isr_register_handler(VECTOR_NUMBER, my_handler);
   ```
3. For hardware IRQs, unmask the PIC line:
   ```c
   pic_clear_mask(irq_number);  // irq_number = vector - 32
   ```
4. Always send EOI at the end of IRQ handlers:
   ```c
   pic_send_eoi(irq_number);
   ```

---

## License

MIT
