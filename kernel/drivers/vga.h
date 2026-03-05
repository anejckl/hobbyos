#ifndef VGA_H
#define VGA_H

#include "../common.h"

/* VGA text mode constants */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  0xFFFF8000000B8000ULL  /* Physical 0xB8000 via PHYS_MAP_BASE */

/* VGA colors */
enum vga_color {
    VGA_BLACK        = 0,
    VGA_BLUE         = 1,
    VGA_GREEN        = 2,
    VGA_CYAN         = 3,
    VGA_RED          = 4,
    VGA_MAGENTA      = 5,
    VGA_BROWN        = 6,
    VGA_LIGHT_GREY   = 7,
    VGA_DARK_GREY    = 8,
    VGA_LIGHT_BLUE   = 9,
    VGA_LIGHT_GREEN  = 10,
    VGA_LIGHT_CYAN   = 11,
    VGA_LIGHT_RED    = 12,
    VGA_LIGHT_MAGENTA= 13,
    VGA_YELLOW       = 14,
    VGA_WHITE        = 15,
};

void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_printf(const char *fmt, ...);

/* For shell backspace support */
void vga_backspace(void);
uint16_t vga_get_row(void);
uint16_t vga_get_col(void);

#endif /* VGA_H */
