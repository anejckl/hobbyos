# HobbyOS Makefile

# Use x86_64-elf-gcc if available, else fall back to native gcc
CC := $(shell command -v x86_64-elf-gcc 2>/dev/null || echo gcc)
LD := $(shell command -v x86_64-elf-ld 2>/dev/null || echo ld)
OBJCOPY := $(shell command -v x86_64-elf-objcopy 2>/dev/null || echo objcopy)
AS = nasm

CFLAGS = -ffreestanding -mno-red-zone -mcmodel=kernel -Wall -Wextra \
         -fno-stack-protector -fno-pic -nostdlib -nostdinc -Ikernel -O2 -g \
         -mno-sse -mno-sse2 -mno-mmx -mno-avx
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
         kernel/fs/vfs.c \
         kernel/fs/ramfs.c \
         kernel/drivers/vga.c \
         kernel/drivers/keyboard.c \
         kernel/drivers/pit.c \
         kernel/shell/shell.c \
         kernel/debug/debug.c

# Object files
ASM_OBJS = $(ASM_SRCS:.asm=.o)
C_OBJS = $(C_SRCS:.c=.o)

# User program embedded objects
USER_PROGRAMS = hello counter
USER_EMBED_OBJS = $(patsubst %,user/%_embed.o,$(USER_PROGRAMS))

OBJS = $(ASM_OBJS) $(C_OBJS) $(USER_EMBED_OBJS)

KERNEL_BIN = kernel.bin
ISO = hobbyos.iso
ISO_DIR = isodir

.PHONY: all clean iso run debug test test-host test-qemu install-hooks

all: $(KERNEL_BIN)

$(KERNEL_BIN): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# --- User program build pipeline ---
# Compile user .c to .o (with user CFLAGS, not kernel CFLAGS)
user/%.o: user/%.c user/syscall.h
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

run: iso
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -m 128M \
		-no-reboot -no-shutdown

debug: iso
	@echo "Starting QEMU in debug mode..."
	@echo "Connect GDB with: target remote localhost:1234"
	qemu-system-x86_64 -cdrom $(ISO) -serial stdio -m 128M \
		-no-reboot -no-shutdown -s -S -d int,cpu_reset

# Host-side unit tests (fast, no QEMU needed)
test-host:
	gcc -fno-builtin -o tests/run_tests tests/test_main.c tests/test_string.c tests/test_pmm.c tests/test_printf.c tests/test_elf.c tests/test_vfs.c -Itests -Wall -Wextra
	./tests/run_tests

# QEMU smoke test (boots kernel, checks serial output)
test-qemu: iso
	bash tests/qemu_smoke.sh

# Run all tests
test: test-host test-qemu

# Install git hooks
install-hooks:
	cp scripts/pre-commit .git/hooks/pre-commit
	chmod +x .git/hooks/pre-commit
	@echo "Pre-commit hook installed."

clean:
	rm -f $(OBJS) $(KERNEL_BIN) $(ISO) tests/run_tests tests/serial_output.log
	rm -f user/*.o user/*.elf user/*.bin
	rm -f kernel/elf/*.o kernel/fs/*.o
	rm -rf $(ISO_DIR)
