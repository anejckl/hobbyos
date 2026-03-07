# HobbyOS Makefile

# Use x86_64-elf-gcc if available, else fall back to native gcc
CC := $(shell command -v x86_64-elf-gcc 2>/dev/null || echo gcc)
LD := $(shell command -v x86_64-elf-ld 2>/dev/null || echo ld)
OBJCOPY := $(shell command -v x86_64-elf-objcopy 2>/dev/null || echo objcopy)
AS = nasm

CFLAGS = -ffreestanding -mno-red-zone -mcmodel=kernel -Wall -Wextra \
         -fno-stack-protector -fno-pic -nostdlib -nostdinc -Ikernel -O2 -g \
         -mno-sse -mno-sse2 -mno-mmx -mno-avx -MMD -MP
ASFLAGS = -f elf64 -g
LDFLAGS = -T linker.ld -nostdlib -z max-page-size=0x1000

# User program build flags (no -mcmodel=kernel, user code runs at 0x400000)
USER_CFLAGS = -ffreestanding -mno-red-zone -Wall -Wextra \
              -fno-stack-protector -fno-pic -nostdlib -nostdinc -Iuser -O2 -g \
              -mno-sse -mno-sse2 -mno-mmx -mno-avx

# Source files
ASM_SRCS = boot/boot.asm \
           kernel/arch/x86_64/gdt_flush.asm \
           kernel/arch/x86_64/idt_flush.asm \
           kernel/arch/x86_64/isr_stubs.asm \
           kernel/arch/x86_64/usermode.asm \
           kernel/arch/x86_64/fork_return.asm \
           kernel/scheduler/context_switch.asm

C_SRCS = kernel/kernel.c \
         kernel/string.c \
         kernel/arch/x86_64/gdt.c \
         kernel/arch/x86_64/tss.c \
         kernel/arch/x86_64/idt.c \
         kernel/arch/x86_64/isr.c \
         kernel/arch/x86_64/pic.c \
         kernel/memory/pmm.c \
         kernel/memory/vmm.c \
         kernel/memory/kheap.c \
         kernel/memory/user_vm.c \
         kernel/interrupts/interrupts.c \
         kernel/process/process.c \
         kernel/process/user_process.c \
         kernel/scheduler/scheduler.c \
         kernel/syscall/syscall.c \
         kernel/user_programs.c \
         kernel/elf/elf_loader.c \
         kernel/signal/signal.c \
         kernel/fs/vfs.c \
         kernel/fs/ramfs.c \
         kernel/fs/pipe.c \
         kernel/fs/procfs.c \
         kernel/fs/ext2.c \
         kernel/drivers/vga.c \
         kernel/drivers/keyboard.c \
         kernel/drivers/pit.c \
         kernel/drivers/ata.c \
         kernel/drivers/tty.c \
         kernel/drivers/device.c \
         kernel/drivers/dev_null.c \
         kernel/drivers/dev_zero.c \
         kernel/drivers/dev_tty.c \
         kernel/fs/devfs.c \
         kernel/memory/user_access.c \
         kernel/shell/shell.c \
         kernel/debug/debug.c \
         kernel/autotest.c \
         kernel/drivers/pci.c \
         kernel/drivers/e1000.c \
         kernel/net/netbuf.c \
         kernel/net/net.c \
         kernel/net/ethernet.c \
         kernel/net/arp.c \
         kernel/net/ipv4.c \
         kernel/net/icmp.c \
         kernel/net/udp.c \
         kernel/net/tcp.c \
         kernel/net/socket.c

# Object files
ASM_OBJS = $(ASM_SRCS:.asm=.o)
C_OBJS = $(C_SRCS:.c=.o)
C_DEPS = $(C_OBJS:.o=.d)

# User program embedded objects
USER_PROGRAMS = hello counter fork_test cow_test multifork_test pipe_test signal_test procfs_test echo ls ps mkdir touch rm net_test nc httpd ping
USER_EMBED_OBJS = $(patsubst %,user/%_embed.o,$(USER_PROGRAMS))

OBJS = $(ASM_OBJS) $(C_OBJS) $(USER_EMBED_OBJS)

KERNEL_BIN = kernel.bin
ISO = hobbyos.iso
ISO_DIR = isodir

.PHONY: all clean iso run debug test test-host test-qemu test-interactive install-hooks

all: $(KERNEL_BIN)

$(KERNEL_BIN): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# --- User program build pipeline ---
# Compile user .c to .o (with user CFLAGS, not kernel CFLAGS)
user/%.o: user/%.c user/syscall.h user/ulib.h
	$(CC) $(USER_CFLAGS) -c $< -o $@

# Link user .o to .elf using user linker script
user/%.elf: user/%.o user/user.ld
	$(LD) -T user/user.ld -nostdlib -o $@ $<

# Embed ELF directly as .rodata object (cd to user/ so symbols are _binary_<name>_elf_*)
user/%_embed.o: user/%.elf
	cd user && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $*.elf $*_embed.o

iso: $(KERNEL_BIN)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_BIN) $(ISO_DIR)/boot/kernel.bin
	cp grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

# ext2 disk image with user programs
disk.img: $(patsubst %,user/%.elf,$(USER_PROGRAMS))
	dd if=/dev/zero of=disk.img bs=1M count=16
	mkfs.ext2 -F disk.img
	mkdir -p /tmp/hobbyos_mnt
	mount -o loop disk.img /tmp/hobbyos_mnt || true
	mkdir -p /tmp/hobbyos_mnt/bin || true
	for prog in $(USER_PROGRAMS); do \
		cp user/$$prog.elf /tmp/hobbyos_mnt/bin/$$prog 2>/dev/null || true; \
	done
	umount /tmp/hobbyos_mnt 2>/dev/null || true
	rm -rf /tmp/hobbyos_mnt

# QEMU flags for disk support
QEMU_DISK_FLAGS = $(if $(wildcard disk.img),-drive file=disk.img$(comma)format=raw$(comma)if=ide,)
comma := ,

# QEMU flags for networking (e1000 NIC with SLIRP user-mode networking)
QEMU_NET_FLAGS = -netdev user$(comma)id=net0$(comma)hostfwd=tcp::8080-:80 -device e1000$(comma)netdev=net0

run: iso
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -m 128M \
		-no-reboot -no-shutdown $(QEMU_DISK_FLAGS) $(QEMU_NET_FLAGS)

debug: iso
	@echo "Starting QEMU in debug mode..."
	@echo "Connect GDB with: target remote localhost:1234"
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -m 128M \
		-no-reboot -no-shutdown -s -S -d int,cpu_reset $(QEMU_DISK_FLAGS) $(QEMU_NET_FLAGS)

# Host-side unit tests (fast, no QEMU needed)
test-host:
	gcc -fno-builtin -o tests/run_tests tests/test_main.c tests/test_string.c tests/test_pmm.c tests/test_refcount.c tests/test_printf.c tests/test_elf.c tests/test_vfs.c tests/test_pipe.c tests/test_signal.c tests/test_netbuf.c tests/test_checksum.c -Itests -Wall -Wextra
	./tests/run_tests

# QEMU smoke test (boots kernel, checks serial output)
test-qemu: iso
	bash tests/qemu_smoke.sh

# Interactive QEMU tests (sends keystrokes, checks serial output)
test-interactive: iso
	@echo "Creating fresh ext2 disk image..."
	dd if=/dev/zero of=disk.img bs=1M count=16 2>/dev/null
	mkfs.ext2 -F disk.img >/dev/null 2>&1
	python3 tests/test_interactive.py

# Run all tests
test: test-host test-qemu

# Install git hooks
install-hooks:
	cp scripts/pre-commit .git/hooks/pre-commit
	chmod +x .git/hooks/pre-commit
	@echo "Pre-commit hook installed."

clean:
	rm -f $(OBJS) $(C_DEPS) $(KERNEL_BIN) $(ISO) tests/run_tests tests/serial_output.log tests/interactive_serial.log tests/interactive_results.json
	rm -f user/*.o user/*.elf user/*.bin
	rm -f kernel/elf/*.o kernel/fs/*.o kernel/signal/*.o kernel/drivers/*.o kernel/net/*.o
	find . -name '*.d' -delete 2>/dev/null || true
	rm -rf $(ISO_DIR)

# Include auto-generated header dependencies
-include $(C_DEPS)
