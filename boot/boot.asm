; boot.asm - Multiboot2 header + 32-bit to 64-bit long mode transition
; This file handles the entire boot process from GRUB to kernel_main

section .multiboot_header
align 8
multiboot_header_start:
    dd 0xE85250D6                   ; Multiboot2 magic
    dd 0                            ; Architecture: i386 (32-bit protected mode)
    dd multiboot_header_end - multiboot_header_start  ; Header length
    dd -(0xE85250D6 + 0 + (multiboot_header_end - multiboot_header_start))  ; Checksum

    ; Framebuffer tag: request 1024x768x32bpp (optional)
    align 8
    dw 5            ; type = framebuffer
    dw 1            ; flags = 1 (optional — boot even without fb)
    dd 20           ; size
    dd 1024         ; width
    dd 768          ; height
    dd 32           ; depth

    ; End tag (must be 8-byte aligned)
    align 8
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
multiboot_header_end:

; ============================================================================
; 32-bit Bootstrap Code
; ============================================================================
section .boot
bits 32

global _start
extern kernel_main

_start:
    ; Save multiboot info pointer (ebx) and magic (eax)
    mov esi, ebx                    ; Multiboot2 info ptr → esi (will be arg2 in 64-bit)

    ; Set up a temporary stack
    mov esp, boot_stack_top

    ; Save multiboot magic on the stack (setup_page_tables clobbers edi)
    push eax

    ; Check for CPUID support
    call check_cpuid
    ; Check for long mode support
    call check_long_mode

    ; Set up page tables for 64-bit mode
    call setup_page_tables
    ; Enable paging and enter long mode
    call enable_long_mode

    ; Restore multiboot magic into edi (arg1 for kernel_main)
    pop edi

    ; Load temporary 64-bit GDT and far jump to 64-bit code
    lgdt [boot_gdt64_pointer]
    jmp 0x08:long_mode_start

; ----------------------------------------------------------------------------
; Check CPUID availability by toggling EFLAGS.ID bit
; ----------------------------------------------------------------------------
check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21               ; Toggle ID bit
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid
    ret
.no_cpuid:
    mov al, 'C'
    jmp boot_error

; ----------------------------------------------------------------------------
; Check for long mode (CPUID extended function 0x80000001, bit 29 of EDX)
; ----------------------------------------------------------------------------
check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    ret
.no_long_mode:
    mov al, 'L'
    jmp boot_error

; ----------------------------------------------------------------------------
; Set up initial page tables for the jump to long mode
; Identity-map first 2MB (for boot code continuity)
; Map 0xFFFFFFFF80000000 → first 2MB (higher-half kernel)
; Map 0xFFFF800000000000 → first 512MB (physical memory access)
; Uses 2MB huge pages for simplicity
; ----------------------------------------------------------------------------
setup_page_tables:
    ; Clear page table memory (all 7 page tables)
    mov edi, boot_pml4
    xor eax, eax
    mov ecx, (7 * 4096) / 4        ; 7 pages worth of dwords
    cld                              ; Ensure forward direction
    rep stosd

    ; PML4[0] → boot_pdpt_low (for identity map)
    mov eax, boot_pdpt_low
    or eax, 0x03                    ; Present + Writable
    mov [boot_pml4], eax

    ; PML4[511] → boot_pdpt_high (for 0xFFFFFFFF80000000 kernel map)
    ; 0xFFFFFFFF80000000 → PML4 index = 511
    mov eax, boot_pdpt_high
    or eax, 0x03
    mov [boot_pml4 + 511 * 8], eax

    ; PML4[256] → boot_pdpt_phys (for 0xFFFF800000000000 physical map)
    ; 0xFFFF800000000000 → PML4 index = 256
    mov eax, boot_pdpt_phys
    or eax, 0x03
    mov [boot_pml4 + 256 * 8], eax

    ; --- Identity map: first 2MB ---
    ; PDPT_low[0] → boot_pd_low
    mov eax, boot_pd_low
    or eax, 0x03
    mov [boot_pdpt_low], eax

    ; PD_low[0] → 2MB huge page at physical 0x0
    mov dword [boot_pd_low], 0x83   ; Present + Writable + Huge (2MB)
    mov dword [boot_pd_low + 4], 0

    ; --- Higher-half kernel map: 0xFFFFFFFF80000000 → physical 0 ---
    ; 0xFFFFFFFF80000000: PDPT index = 510 (bits 38:30)
    mov eax, boot_pd_high
    or eax, 0x03
    mov [boot_pdpt_high + 510 * 8], eax

    ; Map first 16MB (8 x 2MB pages) for kernel space
    mov ecx, 0
    mov ebx, boot_pd_high
.map_kernel:
    mov eax, ecx
    shl eax, 21                     ; ecx * 2MB
    or eax, 0x83                    ; Present + Writable + Huge
    mov [ebx + ecx * 8], eax
    mov dword [ebx + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 8                      ; 8 entries = 16MB
    jne .map_kernel

    ; --- Physical memory map: 0xFFFF800000000000 → first 512MB ---
    ; Map 512MB using 2MB pages: need 256 entries in one PD
    mov eax, boot_pd_phys
    or eax, 0x03
    mov [boot_pdpt_phys], eax       ; PDPT[0] → boot_pd_phys

    mov ecx, 0
    mov ebx, boot_pd_phys
.map_phys:
    mov eax, ecx
    shl eax, 21                     ; ecx * 2MB
    or eax, 0x83                    ; Present + Writable + Huge
    mov [ebx + ecx * 8], eax
    mov dword [ebx + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 256                    ; 256 entries = 512MB
    jne .map_phys

    ret

; ----------------------------------------------------------------------------
; Enable PAE, set EFER.LME, enable paging
; ----------------------------------------------------------------------------
enable_long_mode:
    ; Load PML4 address into CR3
    mov eax, boot_pml4
    mov cr3, eax

    ; Enable PAE (CR4.PAE = bit 5)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Set EFER.LME (Long Mode Enable) - MSR 0xC0000080, bit 8
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable paging (CR0.PG = bit 31) and write protect (CR0.WP = bit 16)
    mov eax, cr0
    or eax, (1 << 31) | (1 << 16)
    mov cr0, eax

    ret

; ----------------------------------------------------------------------------
; Boot error: print character in AL to VGA and halt
; ----------------------------------------------------------------------------
boot_error:
    mov dword [0xB8000], 0x4F524F45  ; "ER" in red on white
    mov dword [0xB8004], 0x4F3A4F52  ; "R:" in red on white
    mov byte  [0xB8008], al
    mov byte  [0xB8009], 0x4F
    hlt

; ============================================================================
; 64-bit Long Mode Code
; ============================================================================
section .boot
bits 64

long_mode_start:
    ; Reload segment registers with 64-bit data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up the kernel stack (in higher-half)
    mov rsp, kernel_stack_top

    ; edi already has multiboot2 magic from 32-bit code
    ; esi already has multiboot2 info pointer
    ; Zero-extend esi to rsi for 64-bit
    mov edi, edi                    ; Zero-extend to rdi
    mov esi, esi                    ; Zero-extend to rsi

    ; Jump to kernel_main(multiboot_magic, multiboot_info)
    call kernel_main

    ; If kernel_main returns, halt
.halt:
    cli
    hlt
    jmp .halt

; ============================================================================
; Boot GDT (temporary, replaced by kernel GDT later)
; ============================================================================
section .boot_data

align 16
boot_gdt64:
    dq 0                            ; Null descriptor
    ; Code segment: Execute/Read, 64-bit, Present, Ring 0
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
    ; Data segment: Read/Write, Present, Ring 0
    dq (1 << 41) | (1 << 44) | (1 << 47)

boot_gdt64_pointer:
    dw $ - boot_gdt64 - 1          ; Limit
    dq boot_gdt64                   ; Base address

; ============================================================================
; Page Tables (must be page-aligned)
; ============================================================================
section .boot_bss nobits
align 4096

boot_pml4:      resb 4096
boot_pdpt_low:  resb 4096
boot_pdpt_high: resb 4096
boot_pdpt_phys: resb 4096
boot_pd_low:    resb 4096
boot_pd_high:   resb 4096
boot_pd_phys:   resb 4096

; ============================================================================
; Boot Stack (16KB)
; ============================================================================
section .boot_bss nobits
align 16
boot_stack_bottom:
    resb 16384
boot_stack_top:

; ============================================================================
; Kernel Stack (16KB, in BSS so it's in higher-half)
; ============================================================================
section .bss
align 16
global kernel_stack_bottom
global kernel_stack_top
kernel_stack_bottom:
    resb 16384
kernel_stack_top:
