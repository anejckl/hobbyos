#include "keyboard.h"
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

static void kb_buffer_push(char c) {
    uint32_t next = (kb_write_idx + 1) % KB_BUFFER_SIZE;
    if (next != kb_read_idx) {
        kb_buffer[kb_write_idx] = c;
        kb_write_idx = next;
    }
}

static void keyboard_handler(struct interrupt_frame *frame) {
    (void)frame;
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
        return;
    case 0x1D:               /* Left Ctrl */
        kb_ctrl = true;
        return;
    case 0x3A:               /* Caps Lock */
        kb_caps = !kb_caps;
        return;
    }

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

    if (c != 0)
        kb_buffer_push(c);
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
