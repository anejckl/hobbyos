#include "vga.h"
#include "fb.h"
#include "../string.h"
#include "../debug/debug.h"

/* Use variadic arguments via GCC builtins */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)

static uint16_t *vga_buffer;
static uint16_t vga_row;
static uint16_t vga_col;
static uint8_t  vga_color_attr;

/* Shadow text buffer for framebuffer console mode.
 * In fb mode, physical 0xB8000 is not a valid text buffer (GPU repurposes it),
 * so we use this RAM buffer instead. */
static uint16_t fb_shadow_buffer[VGA_WIDTH * VGA_HEIGHT];

/* Framebuffer console state */
#define FB_KERNEL_VA  0xFFFFFF0000000000ULL
#define FB_CHAR_W  8
#define FB_CHAR_H  9  /* 8px glyph + 1px line spacing */
static bool fb_console_active = false;
static uint32_t fb_con_width;   /* pixels */
static uint32_t fb_con_height;  /* pixels */
static uint32_t fb_con_pitch;   /* bytes per row */
static uint32_t fb_con_cols;    /* characters */
static uint32_t fb_con_rows;    /* characters */

/* VGA color to 32-bit RGB mapping */
static const uint32_t vga_to_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,  /* black, blue, green, cyan */
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,  /* red, magenta, brown, light grey */
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,  /* dark grey, light blue, light green, light cyan */
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,  /* light red, light magenta, yellow, white */
};

/* 8x8 bitmap font (95 printable ASCII chars starting from space 0x20) */
static const uint8_t fb_font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* ! */
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* # */
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* $ */
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* % */
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* & */
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* ( */
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* * */
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* , */
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, /* . */
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* / */
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 0 */
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 1 */
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 2 */
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 3 */
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 4 */
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 5 */
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 6 */
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 7 */
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 8 */
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 9 */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, /* : */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, /* ; */
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, /* < */
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, /* = */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* > */
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* ? */
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* @ */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* A */
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* B */
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* C */
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* D */
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* E */
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* F */
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* G */
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* H */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* I */
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* J */
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* K */
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* L */
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, /* M */
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* N */
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* O */
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* P */
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* Q */
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* R */
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* S */
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* T */
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* U */
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* V */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* W */
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, /* X */
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* Y */
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* Z */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* [ */
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* \ */
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* ] */
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* _ */
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* a */
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* b */
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* c */
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00}, /* d */
    {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00}, /* e */
    {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00}, /* f */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* g */
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* h */
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* i */
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* j */
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* k */
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* l */
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, /* m */
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* n */
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* o */
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* p */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* q */
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* r */
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, /* s */
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* t */
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* u */
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* v */
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* w */
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* x */
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* y */
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* z */
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* { */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* | */
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* } */
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* ~ */
};

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static inline uint8_t vga_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static void fb_draw_char(uint32_t px, uint32_t py, char c, uint32_t fg, uint32_t bg) {
    uint32_t *fb = (uint32_t *)FB_KERNEL_VA;
    int idx = (int)(unsigned char)c - 0x20;
    if (idx < 0 || idx >= 95) idx = 0;
    const uint8_t *glyph = fb_font8x8[idx];
    uint32_t stride = fb_con_pitch / 4;
    for (int row = 0; row < 8; row++) {
        if (py + (uint32_t)row >= fb_con_height) break;
        uint32_t *line = fb + (py + (uint32_t)row) * stride + px;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (px + (uint32_t)col < fb_con_width)
                line[col] = (bits & (1 << col)) ? fg : bg;
        }
    }
}

/* Enable framebuffer console mode (called after fb_init) */
void vga_enable_fb_console(void) {
    if (!fb_is_initialized()) return;
    struct fb_info info = fb_get_info();
    fb_con_width  = info.width;
    fb_con_height = info.height;
    fb_con_pitch  = info.pitch;
    fb_con_cols   = info.width / FB_CHAR_W;
    fb_con_rows   = info.height / FB_CHAR_H;

    /* Switch VGA text buffer to RAM shadow. In framebuffer mode, physical
     * 0xB8000 is not a usable text buffer (GPU repurposes it), so all prior
     * writes there were invisible. However, vga_row/vga_col still track the
     * logical cursor position and the shadow buffer entries written by
     * vga_putchar (which wrote to 0xB8000) are lost. We'll re-render what
     * we can: copy the hardware buffer (may be garbage) but then also write
     * fresh entries going forward. The practical effect is that boot messages
     * already emitted appear on serial but NOT on the framebuffer display;
     * new text from this point on will be visible on both. */

    /* Clear the shadow buffer to spaces with current color */
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        fb_shadow_buffer[i] = vga_entry(' ', vga_color_attr);
    vga_buffer = fb_shadow_buffer;

    fb_console_active = true;

    /* NOTE: we intentionally do NOT clear the framebuffer here.
     * A full-screen clear (1024*768 uncacheable writes) is too slow
     * under QEMU TCG emulation. Instead, each character cell is cleared
     * as it's drawn via fb_draw_char which writes both fg and bg pixels. */
}

/* Disable framebuffer console (when WM takes over) */
void vga_disable_fb_console(void) {
    fb_console_active = false;
    /* Restore VGA buffer to hardware address (only useful if VGA text mode
     * is somehow re-enabled, but keeps state consistent). */
    vga_buffer = (uint16_t *)VGA_MEMORY;
}

/* Repaint the entire framebuffer from the shadow text buffer.
 * Call this after a user program (e.g. WM) has overwritten the framebuffer. */
void vga_repaint_fb_console(void) {
    if (!fb_console_active) return;

    /* Clear the entire framebuffer to black */
    uint32_t *fb = (uint32_t *)FB_KERNEL_VA;
    uint32_t stride = fb_con_pitch / 4;
    for (uint32_t y = 0; y < fb_con_height; y++)
        for (uint32_t x = 0; x < fb_con_width; x++)
            fb[y * stride + x] = 0x000000;

    /* Redraw all characters from shadow buffer */
    for (int row = 0; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            uint16_t entry = vga_buffer[row * VGA_WIDTH + col];
            char ch = (char)(entry & 0xFF);
            uint8_t attr = (uint8_t)(entry >> 8);
            if (ch == ' ' || ch == 0) continue;
            uint32_t fg = vga_to_rgb[attr & 0x0F];
            uint32_t bg = vga_to_rgb[(attr >> 4) & 0x0F];
            fb_draw_char((uint32_t)col * FB_CHAR_W,
                         (uint32_t)row * FB_CHAR_H, ch, fg, bg);
        }
    }
}

static void vga_update_cursor(void) {
    uint16_t pos = vga_row * VGA_WIDTH + vga_col;
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static void vga_scroll(void) {
    if (vga_row < VGA_HEIGHT)
        return;

    /* Move all lines up by one in VGA buffer */
    memmove(vga_buffer, vga_buffer + VGA_WIDTH,
            (VGA_HEIGHT - 1) * VGA_WIDTH * 2);
    for (int i = 0; i < VGA_WIDTH; i++)
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = vga_entry(' ', vga_color_attr);

    /* Scroll the framebuffer pixels to match the shadow buffer */
    if (fb_console_active) {
        uint32_t *fb = (uint32_t *)FB_KERNEL_VA;
        uint32_t stride = fb_con_pitch / 4;
        uint32_t text_w = VGA_WIDTH * FB_CHAR_W;
        uint32_t text_pixel_h = VGA_HEIGHT * FB_CHAR_H;

        /* Move pixel rows up by FB_CHAR_H (one text row) */
        for (uint32_t y = 0; y < text_pixel_h - FB_CHAR_H; y++) {
            uint32_t *dst_row = fb + y * stride;
            uint32_t *src_row = fb + (y + FB_CHAR_H) * stride;
            for (uint32_t x = 0; x < text_w; x++)
                dst_row[x] = src_row[x];
        }
        /* Clear the last text row on framebuffer */
        for (uint32_t y = text_pixel_h - FB_CHAR_H; y < text_pixel_h; y++) {
            uint32_t *row = fb + y * stride;
            for (uint32_t x = 0; x < text_w; x++)
                row[x] = 0x000000;
        }
    }

    vga_row = VGA_HEIGHT - 1;
}

void vga_init(void) {
    vga_buffer = (uint16_t *)VGA_MEMORY;
    vga_row = 0;
    vga_col = 0;
    vga_color_attr = vga_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_buffer[i] = vga_entry(' ', vga_color_attr);
    vga_row = 0;
    vga_col = 0;
    if (fb_console_active) {
        /* Clear only the text area (80*25 cells), not the full 1024x768 fb */
        uint32_t *fb = (uint32_t *)FB_KERNEL_VA;
        uint32_t stride = fb_con_pitch / 4;
        uint32_t text_h = VGA_HEIGHT * FB_CHAR_H;
        uint32_t text_w = VGA_WIDTH * FB_CHAR_W;
        for (uint32_t y = 0; y < text_h && y < fb_con_height; y++)
            for (uint32_t x = 0; x < text_w && x < fb_con_width; x++)
                fb[y * stride + x] = 0x000000;
    } else {
        vga_update_cursor();
    }
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color_attr = vga_color(fg, bg);
}

void vga_putchar(char c) {
    if (debug_serial_ready) {
        if (c == '\n')
            debug_putchar('\r');
        debug_putchar(c);
    }

    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    } else if (c == '\b') {
        vga_backspace();
        return;
    } else {
        vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color_attr);
        if (fb_console_active) {
            uint32_t fg = vga_to_rgb[vga_color_attr & 0x0F];
            uint32_t bg = vga_to_rgb[(vga_color_attr >> 4) & 0x0F];
            fb_draw_char((uint32_t)vga_col * FB_CHAR_W,
                         (uint32_t)vga_row * FB_CHAR_H, c, fg, bg);
        }
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    }

    vga_scroll();

    if (!fb_console_active)
        vga_update_cursor();
}

void vga_puts(const char *s) {
    while (*s)
        vga_putchar(*s++);
}

void vga_backspace(void) {
    if (vga_col > 0) {
        vga_col--;
    } else if (vga_row > 0) {
        vga_row--;
        vga_col = VGA_WIDTH - 1;
    }
    vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_color_attr);
    if (fb_console_active) {
        uint32_t bg = vga_to_rgb[(vga_color_attr >> 4) & 0x0F];
        fb_draw_char((uint32_t)vga_col * FB_CHAR_W,
                     (uint32_t)vga_row * FB_CHAR_H, ' ', bg, bg);
    } else {
        vga_update_cursor();
    }
}

uint16_t vga_get_row(void) { return vga_row; }
uint16_t vga_get_col(void) { return vga_col; }

/* Simple integer to string conversion */
static void print_uint(uint64_t val, int base, int pad_zero, int width,
                       int left_align) {
    char buf[24];
    int i = 0;
    const char *digits = "0123456789abcdef";

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = digits[val % base];
            val /= base;
        }
    }

    if (left_align) {
        /* Print digits first, then pad with spaces */
        for (int j = i - 1; j >= 0; j--)
            vga_putchar(buf[j]);
        for (int j = i; j < width; j++)
            vga_putchar(' ');
    } else {
        /* Pad first, then digits */
        while (i < width)
            buf[i++] = pad_zero ? '0' : ' ';
        for (int j = i - 1; j >= 0; j--)
            vga_putchar(buf[j]);
    }
}

static void print_int(int64_t val, int width, int left_align) {
    if (val < 0) {
        vga_putchar('-');
        val = -val;
        if (width > 0) width--;
    }
    print_uint((uint64_t)val, 10, 0, width, left_align);
}

void vga_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            vga_putchar(*fmt++);
            continue;
        }
        fmt++;

        /* Check for flags, zero-pad and width */
        int left_align = 0;
        int pad_zero = 0;
        int width = 0;
        if (*fmt == '-') {
            left_align = 1;
            fmt++;
        }
        if (*fmt == '0') {
            pad_zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            vga_puts(s);
            if (left_align) {
                int len = (int)strlen(s);
                for (int j = len; j < width; j++)
                    vga_putchar(' ');
            }
            break;
        }
        case 'd': {
            int64_t val = va_arg(ap, int64_t);
            print_int(val, width, left_align);
            break;
        }
        case 'u': {
            uint64_t val = va_arg(ap, uint64_t);
            print_uint(val, 10, pad_zero, width, left_align);
            break;
        }
        case 'x': {
            uint64_t val = va_arg(ap, uint64_t);
            print_uint(val, 16, pad_zero, width, left_align);
            break;
        }
        case 'p': {
            uint64_t val = va_arg(ap, uint64_t);
            vga_puts("0x");
            print_uint(val, 16, 1, 16, 0);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            vga_putchar(c);
            break;
        }
        case '%':
            vga_putchar('%');
            break;
        default:
            vga_putchar('%');
            vga_putchar(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}
