#ifndef FB_H
#define FB_H

#include "../common.h"

struct fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint64_t phys_addr;
};

#define FBIOGET_INFO  0x4600

/* Initialize framebuffer from Multiboot2 tag */
void fb_init(uint64_t phys, uint32_t w, uint32_t h, uint32_t pitch, uint8_t bpp);

/* Returns true if framebuffer was initialized */
bool fb_is_initialized(void);

/* Framebuffer info for ioctl */
struct fb_info fb_get_info(void);

/* Physical address and size for mmap */
uint64_t fb_get_phys_addr(void);
uint64_t fb_get_size(void);  /* pitch * height */

#endif /* FB_H */
