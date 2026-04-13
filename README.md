# HobbyOS

A 64-bit x86_64 operating system written from scratch in C and assembly. Features preemptive multitasking, virtual memory with COW fork, a TCP/IP networking stack, ext2 filesystem, a user-space shell with pipes and redirects, and a graphical window manager.

---

## Screenshots

Boot screen with progress bar, then transitions to the interactive shell:

```
  _   _       _     _            ___  ____
 | | | | ___ | |__ | |__  _   _ / _ \/ ___|
 | |_| |/ _ \| '_ \| '_ \| | | | | | \___ \
 |  _  | (_) | |_) | |_) | |_| | |_| |___) |
 |_| |_|\___/|_.__/|_.__/ \__, |\___/|____/
                          |___/

         [========================>         ] 75%
              Detecting peripherals...
```

```
Welcome to HobbyOS!
Type 'help' for a list of commands.

hobbyos> run sh
$ echo Hello | grep Hello
Hello
$ echo test > /file.txt
$ cat /file.txt
test
```

---

## Quick Start

**Docker (recommended):**

```bash
docker build -t hobbyos .
docker run --rm -v "$(pwd)":/hobbyos hobbyos make iso
qemu-system-x86_64 -cdrom hobbyos.iso -serial stdio -m 128M -no-reboot -no-shutdown
```

**Native Linux (Debian/Ubuntu):**

```bash
sudo apt install nasm gcc binutils make grub-pc-bin grub-common xorriso mtools qemu-system-x86
make run
```

---

## Features

### Kernel
- **x86_64 long mode** with higher-half kernel at `0xFFFFFFFF80000000`
- **Multiboot2** boot via GRUB
- **4-level paging** (PML4) with per-process address spaces
- **Preemptive round-robin scheduler** (100 Hz timer, 100 ms quantum)
- **Fork/exec/wait** with copy-on-write (COW) memory
- **Demand paging** and **swap** (16 MB swap area)
- **Signals** (SIGINT, SIGKILL, SIGTERM, SIGCHLD, SIGPIPE) with user-space handlers
- **Pipes** with blocking read/write and EOF detection
- **Syscall interface** (INT 0x80) with 25+ system calls

### Filesystems
- **VFS layer** with mount points
- **RAMFS** — in-memory filesystem for embedded programs
- **ext2** — read/write support via ATA PIO, with journaling
- **procfs** — `/proc/<pid>/status`, `/proc/<pid>/fd`
- **devfs** — `/dev/null`, `/dev/zero`, `/dev/tty`, `/dev/random`, `/dev/fb0`, `/dev/input/*`

### Networking
- **e1000** NIC driver (Intel 82540EM, PCI)
- **Ethernet** frame handling
- **ARP** — address resolution with cache
- **IPv4** — packet send/receive
- **ICMP** — ping works (`ping 10.0.2.2`)
- **UDP** — connectionless datagrams
- **TCP** — connection state machine
- **BSD sockets** — `socket()`, `bind()`, `connect()`, `send()`, `recv()`

### Drivers
- **VGA text mode** (80x25) with framebuffer console (1024x768 32bpp)
- **PS/2 keyboard** (IRQ 1, scan code Set 1, Shift/Ctrl/CapsLock)
- **PS/2 mouse** (IRQ 12, relative movement + buttons)
- **PIT timer** (100 Hz system clock)
- **ATA PIO** disk driver (LBA28 read/write)
- **PCI bus** enumeration
- **Serial COM1** debug output (38400 baud)

### User Space
- **35+ user programs** compiled as flat ELF binaries at `0x400000`
- **User shell (`sh`)** with pipes (`|`), redirects (`>`, `>>`), command chaining (`;`, `&&`)
- **Core utilities**: `cat`, `echo`, `grep`, `head`, `tail`, `ls`, `ps`, `cp`, `mv`, `rm`, `mkdir`, `touch`, `df`, `top`, `kill`, `id`, `whoami`
- **Network tools**: `ping`, `nc`, `httpd`, `ifconfig`, `netstat`
- **Window manager (`wm`)** with themed windows, terminal emulator, task manager, file browser

### Testing
- **278 host unit tests** (string, PMM, printf, ELF, VFS, pipe, signal, netbuf, device, cred, bcache, swap, journal)
- **69 QEMU smoke tests** (boot + autotest integration)
- **38 interactive tests** (keystroke injection via QEMU monitor)
- **Native QEMU sendkeys** test script for Windows

---

## Project Structure

```
hobbyos/
├── boot/boot.asm                    # Multiboot2 header, 32->64-bit, page tables
├── kernel/
│   ├── kernel.c                     # Boot sequence (15+ phases)
│   ├── common.h                     # Types, port I/O, address macros
│   ├── string.c                     # String/memory functions (no libc)
│   ├── arch/x86_64/                 # GDT, IDT, PIC, TSS, ISR stubs, usermode entry
│   ├── memory/                      # PMM, VMM, heap, user address spaces, mmap, swap
│   ├── process/                     # PCB, fork, user process creation
│   ├── scheduler/                   # Round-robin scheduler, context switch
│   ├── syscall/                     # INT 0x80 handler (25+ syscalls)
│   ├── fs/                          # VFS, RAMFS, ext2, procfs, devfs, pipes, epoll
│   ├── net/                         # Ethernet, ARP, IPv4, ICMP, UDP, TCP, sockets
│   ├── drivers/                     # VGA, keyboard, PIT, ATA, PCI, e1000, mouse, FB
│   ├── signal/                      # Signal delivery and handling
│   ├── shell/                       # Kernel shell (14 commands)
│   ├── security/                    # Credentials and permission checks
│   └── debug/                       # Serial COM1 output
├── user/
│   ├── syscall.h                    # User-space syscall wrappers (INT 0x80)
│   ├── sh.c                         # User shell with pipes, redirects, chaining
│   ├── cat.c, echo.c, grep.c, ...  # Core utilities
│   ├── wm.c                         # Window manager
│   └── lib/libgfx.c                # Graphics library for WM
├── tests/
│   ├── test_main.c                  # Host test runner (278 tests)
│   ├── test_interactive.py          # QEMU interactive test suite (38 tests)
│   └── qemu_smoke.exp               # Boot smoke test patterns (69 checks)
├── Makefile                         # Build system
├── Dockerfile                       # Ubuntu 24.04 build environment
├── linker.ld                        # Higher-half kernel linker script
└── grub.cfg                         # GRUB Multiboot2 config
```

---

## Syscalls

| RAX | Name | Args | Returns |
|-----|------|------|---------|
| 0 | `write` | fd, buf, len | bytes written |
| 1 | `exit` | status | - |
| 2 | `getpid` | - | PID |
| 3 | `exec` | path | 0 or -1 |
| 4 | `wait` | status_ptr | child PID |
| 5 | `fork` | - | child PID / 0 |
| 6 | `read` | fd, buf, count | bytes read |
| 7 | `open` | path, flags | fd or -1 |
| 8 | `close` | fd | 0 or -1 |
| 9 | `pipe` | fds[2] | 0 or -1 |
| 10 | `dup2` | oldfd, newfd | newfd or -1 |
| 11 | `kill` | pid, sig | 0 or -1 |
| 12 | `sigaction` | sig, handler | 0 or -1 |
| 13 | `sigreturn` | - | restores context |
| 14 | `getppid` | - | parent PID |
| 25 | `waitpid` | pid, status_ptr, options | child PID |

---

## Memory Layout

| Virtual Range | Size | Purpose |
|---|---|---|
| `0x400000` | varies | User code (per-process) |
| `0x7FFFFF000` | 16 KB | User stack (grows down) |
| `0xFFFF800000000000` | 512 MB | Physical memory direct map |
| `0xFFFFFFFF80000000` | 16 MB | Kernel text/data/bss |
| `0xFFFFFFFF90000000` | 4 MB | Kernel heap |

---

## Building & Testing

### Make Targets

| Target | Description |
|--------|-------------|
| `make` | Compile `kernel.bin` |
| `make iso` | Build bootable `hobbyos.iso` |
| `make run` | Build + launch QEMU |
| `make debug` | Build + QEMU with GDB stub (port 1234) |
| `make clean` | Remove build artifacts |
| `make test` | Run all tests (host + QEMU smoke) |
| `make test-host` | Host unit tests only (fast, no QEMU) |
| `make test-qemu` | QEMU boot smoke test |
| `make test-interactive` | Interactive QEMU tests (38 tests, ~3 min) |

### Run Tests

```bash
# Via Docker (recommended):
docker build -t hobbyos .
docker run --rm -v "$(pwd)":/hobbyos hobbyos make test

# Host unit tests only (fast):
docker run --rm -v "$(pwd)":/hobbyos hobbyos make test-host

# Full interactive suite:
docker run --rm -v "$(pwd)":/hobbyos hobbyos make test-interactive
```

### Debug with GDB

```bash
make debug
# In another terminal:
gdb kernel.bin -ex "target remote localhost:1234" -ex "break kernel_main" -ex "continue"
```

---

## Shell Commands

### Kernel Shell (`hobbyos>`)

| Command | Description |
|---------|-------------|
| `help` | List commands |
| `ps` | List processes |
| `mem` | Memory statistics |
| `uptime` | System uptime |
| `echo` | Print text |
| `clear` | Clear screen |
| `run <prog> [&]` | Run user program (& for background) |
| `ls` | List files |
| `cat <file>` | Read file contents |
| `jobs` | List background processes |
| `fg <job>` | Bring to foreground |
| `kill <pid>` | Send signal to process |
| `proc <pid>` | Process details |
| `startx` | Launch window manager |

### User Shell (`$` — launched with `run sh`)

```bash
echo Hello | grep Hello       # Pipes
echo test > /file.txt         # Output redirect
echo more >> /file.txt        # Append redirect
cat /file.txt                 # Read file
echo A ; echo B               # Command chaining
echo A && echo B              # Conditional chaining
exit                          # Return to kernel shell
```

---

## Compiler Flags

```makefile
-ffreestanding        # No hosted C environment
-mno-red-zone         # ISRs would corrupt the red zone
-mcmodel=kernel       # Kernel at 0xFFFFFFFF80000000
-fno-stack-protector  # No __stack_chk_fail
-nostdlib -nostdinc   # No standard library
-mno-sse -mno-sse2    # No SIMD (ISRs don't save SSE state)
```

---

## License

MIT
