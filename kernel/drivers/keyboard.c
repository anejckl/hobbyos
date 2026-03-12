#include "keyboard.h"
#include "tty.h"
#include "pit.h"
#include "../arch/x86_64/isr.h"
#include "../interrupts/interrupts.h"

/* Circular buffer */
#define KB_BUFFER_SIZE 256
static char kb_buffer[KB_BUFFER_SIZE];
static volatile uint32_t kb_read_idx = 0;
static volatile uint32_t kb_write_idx = 0;

/* Modifier state */
static volatile bool kb_shift = false;
static volatile bool kb_ctrl = false;
static volatile bool kb_caps = false;

/* Tick when modifier was last pressed (for stuck-key detection).
 * When QEMU loses/regains window focus, key release events can be lost,
 * leaving modifiers stuck. Auto-clear after 3 seconds of no re-press. */
#define MODIFIER_TIMEOUT_TICKS 300  /* 3 seconds at 100 Hz */
static volatile uint64_t kb_ctrl_tick = 0;
static volatile uint64_t kb_shift_tick = 0;

/* US QWERTY scan code to ASCII (set 1, make codes only) */
static const char scancode_to_ascii[128] = {
    0,   27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',  /* 0x00-0x0E */
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',     /* 0x0F-0x1C */
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',           /* 0x1D-0x29 */
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',                 /* 0x2A-0x35 */
    0,   '*', 0, ' ', 0,                                                           /* 0x36-0x3A */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                                 /* 0x3B-0x44 F1-F10 */
    0, 0,                                                                           /* 0x45-0x46 NumLk ScrLk */
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0,                                    /* 0x47-0x53 */
};

static const char scancode_to_ascii_shift[128] = {
    0,   27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,   '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0,
};

static void keyboard_handler(struct interrupt_frame *frame) {
    (void)frame;

    /* Check PS/2 status: bit 5 (AUXDATA) indicates data from mouse, not keyboard.
     * QEMU 10.1.3 can route mouse bytes through IRQ 1 — skip them. */
    uint8_t status = inb(0x64);
    if (status & 0x20) {
        inb(KEYBOARD_DATA_PORT);  /* drain the mouse byte */
        return;
    }

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    /* Key release (break code) */
    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        if (key == 0x2A || key == 0x36)  /* Left/Right Shift */
            kb_shift = false;
        if (key == 0x1D)                  /* Left Ctrl */
            kb_ctrl = false;
        return;
    }

    /* Key press (make code) */
    switch (scancode) {
    case 0x2A: case 0x36:   /* Left/Right Shift */
        kb_shift = true;
        kb_shift_tick = pit_get_ticks();
        return;
    case 0x1D:               /* Left Ctrl */
        kb_ctrl = true;
        kb_ctrl_tick = pit_get_ticks();
        return;
    case 0x3A:               /* Caps Lock */
        kb_caps = !kb_caps;
        return;
    }

    /* Auto-clear stuck modifiers (e.g., QEMU focus loss dropped release event) */
    uint64_t now = pit_get_ticks();
    if (kb_ctrl && (now - kb_ctrl_tick) > MODIFIER_TIMEOUT_TICKS)
        kb_ctrl = false;
    if (kb_shift && (now - kb_shift_tick) > MODIFIER_TIMEOUT_TICKS)
        kb_shift = false;

    char c;
    if (kb_shift)
        c = scancode_to_ascii_shift[scancode];
    else
        c = scancode_to_ascii[scancode];

    /* Apply caps lock to letters */
    if (kb_caps && c >= 'a' && c <= 'z')
        c -= 32;
    else if (kb_caps && c >= 'A' && c <= 'Z')
        c += 32;

    /* Ctrl+key: send control character to TTY */
    if (kb_ctrl) {
        if (c >= 'a' && c <= 'z') {
            tty_input_char((char)(c - 'a' + 1));
            return;
        }
    }

    if (c != 0) {
        tty_input_char(c);

        /* Also write to raw buffer for /dev/input/keyboard */
        uint32_t next = (kb_write_idx + 1) % KB_BUFFER_SIZE;
        if (next != kb_read_idx) {
            kb_buffer[kb_write_idx] = c;
            kb_write_idx = next;
        }
    }
}

void keyboard_init(void) {
    kb_read_idx = 0;
    kb_write_idx = 0;
    irq_register_handler(1, keyboard_handler);
}

int keyboard_haschar(void) {
    return kb_read_idx != kb_write_idx;
}

char keyboard_getchar(void) {
    /* Blocking wait */
    while (kb_read_idx == kb_write_idx) {
        hlt();   /* Wait for interrupt */
    }
    char c = kb_buffer[kb_read_idx];
    kb_read_idx = (kb_read_idx + 1) % KB_BUFFER_SIZE;
    return c;
}
