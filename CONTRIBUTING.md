# Contributing to HobbyOS

## Prerequisites

| Tool | Purpose |
|------|---------|
| `nasm` | Assembler (x86_64 ELF) |
| `gcc` | C compiler (or `x86_64-elf-gcc` cross-compiler) |
| `make` | Build orchestration |
| `grub-mkrescue`, `xorriso`, `mtools` | ISO creation |
| `qemu-system-x86_64` | Testing and running |

See [README.md](README.md) for installation instructions per platform.

## Workflow

1. **Fork** the repository and clone your fork
2. **Create a branch**: `git checkout -b my-feature`
3. **Make your changes**
4. **Run tests**: `make test` (mandatory — must pass before every commit)
5. **Commit** with a clear message
6. **Push** and open a Pull Request

## Tests Must Pass

```bash
make test       # Runs all tests
make test-host  # Host-side unit tests only (fast)
make test-qemu  # QEMU boot smoke test
```

`make test` must pass before every commit and PR. If you add new functionality, add corresponding tests in `tests/`.

## Code Style

- **Indentation**: 4 spaces (no tabs)
- **Braces**: K&R style
- **Types**: Use `uint8_t`, `uint32_t`, `uint64_t`, `size_t`, `bool` from `kernel/common.h`
- **No stdlib**: Never use `<stdint.h>`, `<string.h>`, or any standard headers in kernel code
- **Variadic args**: Use `__builtin_va_list`, not `<stdarg.h>`

## Adding Features

### Shell Command

1. Add `static void cmd_name(int argc, char **argv)` in `kernel/shell/shell.c`
2. Add entry to `commands[]` before the `{NULL, NULL, NULL}` sentinel
3. Rebuild and test

### Driver

1. Create `kernel/drivers/mydevice.c` and `kernel/drivers/mydevice.h`
2. Add the `.c` file to `C_SRCS` in the Makefile
3. Call `mydevice_init()` from `kernel_main()` in `kernel/kernel.c`
4. For IRQ-driven drivers, register a handler and unmask the PIC line

### Interrupt Handler

1. Register with `isr_register_handler(vector, handler_fn)`
2. For hardware IRQs (vector 32–47): unmask via `pic_clear_mask(irq)`
3. Always call `pic_send_eoi(irq)` at the end of IRQ handlers

## Commit Messages

Use short, descriptive messages:

```
Add RTC driver with IRQ 8 handler
Fix page fault handler to print faulting address
Add 'reboot' shell command
```

## Important Notes

- Never remove compiler flags `-mno-red-zone`, `-mno-sse`, `-mcmodel=kernel` — they are required for correctness
- Never dereference raw physical addresses — use `PHYS_TO_VIRT()`
- ISR stub push order in `isr_stubs.asm` must match the `interrupt_frame` struct in C
- See [README.md](README.md) for full architecture documentation
