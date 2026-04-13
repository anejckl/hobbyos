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
static bool boot_screen_active = false;  /* suppress VGA text output during boot */
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

    /* During boot screen, only forward to serial — don't draw on screen */
    if (boot_screen_active)
        return;

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

/* ======================================================================
 * Boot screen — text-mode and framebuffer graphical boot display
 * ====================================================================== */

static bool boot_fb_mode = false;  /* true after framebuffer boot screen init */

/* Text-mode ASCII art header (displayed rows 3-10) */
static const char *boot_logo[] = {
    "  _   _       _     _            ___  ____  ",
    " | | | | ___ | |__ | |__  _   _ / _ \\/ ___| ",
    " | |_| |/ _ \\| '_ \\| '_ \\| | | | | | \\___ \\ ",
    " |  _  | (_) | |_) | |_) | |_| | |_| |___) |",
    " |_| |_|\\___/|_.__/|_.__/ \\__, |\\___/|____/ ",
    "                          |___/              ",
};
#define LOGO_LINES 6

/* Draw text-mode progress bar on row 16 */
static void boot_text_bar(int percent) {
    /* Progress bar: [=====>                    ] 35% */
    int bar_w = 40;
    int filled = percent * bar_w / 100;
    if (filled > bar_w) filled = bar_w;

    uint8_t saved_color = vga_color_attr;

    /* Position cursor at row 16, col 19 (centered for 42-char bar) */
    vga_row = 16;
    vga_col = 19;

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_putchar('[');

    for (int i = 0; i < bar_w; i++) {
        if (i < filled) {
            vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
            vga_putchar('=');
        } else if (i == filled) {
            vga_set_color(VGA_WHITE, VGA_BLACK);
            vga_putchar('>');
        } else {
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            vga_putchar(' ');
        }
    }

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_putchar(']');

    /* Percent number */
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_putchar(' ');
    if (percent >= 100) {
        vga_putchar('1'); vga_putchar('0'); vga_putchar('0');
    } else if (percent >= 10) {
        vga_putchar((char)('0' + percent / 10));
        vga_putchar((char)('0' + percent % 10));
    } else {
        vga_putchar((char)('0' + percent));
    }
    vga_putchar('%');

    vga_color_attr = saved_color;
}

/* Clear a specific text-mode row */
static void boot_clear_row(int row) {
    for (int c = 0; c < VGA_WIDTH; c++)
        vga_buffer[row * VGA_WIDTH + c] = vga_entry(' ', vga_color(VGA_LIGHT_GREY, VGA_BLACK));
}

void boot_screen_init(void) {
    boot_screen_active = true;
    vga_clear();

    /* Draw logo centered (80 cols, logo is ~46 chars) */
    uint8_t saved = vga_color_attr;
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    for (int i = 0; i < LOGO_LINES; i++) {
        vga_row = (uint16_t)(4 + i);
        vga_col = 17;
        vga_puts(boot_logo[i]);
    }

    /* Version line */
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_row = 11;
    vga_col = 28;
    vga_puts("v0.1  -  Hobby Kernel");

    /* Draw empty progress bar */
    boot_text_bar(0);

    /* Status line placeholder at row 18 */
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_row = 18;
    vga_col = 25;
    vga_puts("Initializing...");

    vga_color_attr = saved;
}

void boot_screen_status(const char *msg, int percent) {
    if (boot_fb_mode) {
        /* Framebuffer mode: draw graphical progress bar + status */
        uint32_t *fb = (uint32_t *)FB_KERNEL_VA;
        uint32_t stride = fb_con_pitch / 4;

        /* Progress bar dimensions */
        uint32_t bar_x = fb_con_width / 2 - 200;
        uint32_t bar_y = fb_con_height / 2 + 40;
        uint32_t bar_w = 400;
        uint32_t bar_h = 20;

        /* Draw bar outline (dark grey) */
        uint32_t outline_col = 0x404040;
        for (uint32_t x = bar_x - 1; x <= bar_x + bar_w; x++) {
            fb[(bar_y - 1) * stride + x] = outline_col;
            fb[(bar_y + bar_h) * stride + x] = outline_col;
        }
        for (uint32_t y = bar_y; y < bar_y + bar_h; y++) {
            fb[y * stride + bar_x - 1] = outline_col;
            fb[y * stride + bar_x + bar_w] = outline_col;
        }

        /* Fill bar interior */
        uint32_t filled_w = (uint32_t)percent * bar_w / 100;
        if (filled_w > bar_w) filled_w = bar_w;
        for (uint32_t y = bar_y; y < bar_y + bar_h; y++) {
            for (uint32_t x = 0; x < bar_w; x++) {
                uint32_t col;
                if (x < filled_w) {
                    /* Gradient green */
                    col = 0x00AA44;
                } else {
                    col = 0x1A1A2E;  /* dark bg */
                }
                fb[y * stride + bar_x + x] = col;
            }
        }

        /* Draw status text below bar */
        uint32_t text_y = bar_y + bar_h + 12;
        /* Clear previous text (80 chars wide) */
        for (uint32_t y = text_y; y < text_y + 9 && y < fb_con_height; y++)
            for (uint32_t x = bar_x; x < bar_x + bar_w; x++)
                fb[y * stride + x] = 0x0F0F23;

        /* Center the message text */
        int msg_len = 0;
        while (msg[msg_len]) msg_len++;
        uint32_t text_x = bar_x + (bar_w - (uint32_t)msg_len * 8) / 2;
        for (int i = 0; msg[i]; i++) {
            fb_draw_char(text_x + (uint32_t)i * 8, text_y, msg[i],
                         0x888899, 0x0F0F23);
        }
        return;
    }

    /* Text mode: update status line and progress bar */
    boot_clear_row(18);

    uint8_t saved = vga_color_attr;
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Center the message on row 18 */
    int len = 0;
    while (msg[len]) len++;
    int start_col = (80 - len) / 2;
    if (start_col < 0) start_col = 0;
    vga_row = 18;
    vga_col = (uint16_t)start_col;
    vga_puts(msg);

    /* Update progress bar */
    boot_text_bar(percent);

    vga_color_attr = saved;
}

void boot_screen_fb_init(void) {
    boot_fb_mode = true;

    /* Clear entire framebuffer to dark blue-black */
    uint32_t *fb = (uint32_t *)FB_KERNEL_VA;
    uint32_t stride = fb_con_pitch / 4;
    uint32_t bg = 0x0F0F23;
    for (uint32_t y = 0; y < fb_con_height; y++)
        for (uint32_t x = 0; x < fb_con_width; x++)
            fb[y * stride + x] = bg;

    /* Draw title "HobbyOS" large — using 3x scale of 8x8 font */
    const char *title = "HobbyOS";
    int title_len = 7;
    uint32_t scale = 3;
    uint32_t char_w = 8 * scale;
    uint32_t total_w = (uint32_t)title_len * char_w;
    uint32_t tx = (fb_con_width - total_w) / 2;
    uint32_t ty = fb_con_height / 2 - 60;

    for (int i = 0; i < title_len; i++) {
        int idx = (int)(unsigned char)title[i] - 0x20;
        if (idx < 0 || idx >= 95) idx = 0;
        const uint8_t *glyph = fb_font8x8[idx];
        uint32_t cx = tx + (uint32_t)i * char_w;
        for (uint32_t row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (uint32_t col = 0; col < 8; col++) {
                uint32_t color = (bits & (1 << col)) ? 0x55BBEE : bg;
                /* Draw scaled pixel */
                for (uint32_t sy = 0; sy < scale; sy++)
                    for (uint32_t sx = 0; sx < scale; sx++) {
                        uint32_t px = cx + col * scale + sx;
                        uint32_t py = ty + row * scale + sy;
                        if (px < fb_con_width && py < fb_con_height)
                            fb[py * stride + px] = color;
                    }
            }
        }
    }

    /* Draw "v0.1" subtitle below title */
    const char *sub = "v0.1 - Hobby Kernel";
    int sub_len = 0;
    while (sub[sub_len]) sub_len++;
    uint32_t sub_x = (fb_con_width - (uint32_t)sub_len * 8) / 2;
    uint32_t sub_y = ty + 8 * scale + 8;
    for (int i = 0; sub[i]; i++)
        fb_draw_char(sub_x + (uint32_t)i * 8, sub_y, sub[i], 0x556677, bg);
}

void boot_screen_finish(void) {
    if (boot_fb_mode) {
        /* Clear framebuffer for normal text console */
        uint32_t *fb = (uint32_t *)FB_KERNEL_VA;
        uint32_t stride = fb_con_pitch / 4;
        for (uint32_t y = 0; y < fb_con_height; y++)
            for (uint32_t x = 0; x < fb_con_width; x++)
                fb[y * stride + x] = 0x000000;
    }

    boot_fb_mode = false;
    boot_screen_active = false;

    /* Reset VGA text state for normal console */
    vga_row = 0;
    vga_col = 0;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_buffer[i] = vga_entry(' ', vga_color(VGA_LIGHT_GREY, VGA_BLACK));
}
