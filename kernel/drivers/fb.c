#include "fb.h"
#include "device.h"
#include "../memory/vmm.h"
#include "../string.h"
#include "../debug/debug.h"

/* Framebuffer kernel VA: mapped here */
#define FB_KERNEL_VA  0xFFFFFF0000000000ULL

static bool     fb_initialized = false;
static uint64_t fb_phys_addr   = 0;
static uint32_t fb_width       = 0;
static uint32_t fb_height      = 0;
static uint32_t fb_pitch       = 0;
static uint8_t  fb_bpp         = 0;

static int fb_dev_read(struct device *dev, uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf; (void)count;
    return -1;
}

static int fb_dev_write(struct device *dev, const uint8_t *buf, uint32_t count) {
    (void)dev; (void)buf; (void)count;
    return -1;
}

static int fb_dev_ioctl(struct device *dev, uint32_t cmd, uint64_t arg) {
    (void)dev;
    if (cmd == FBIOGET_INFO) {
        struct fb_info *info = (struct fb_info *)arg;
        if (!info) return -1;
        info->width     = fb_width;
        info->height    = fb_height;
        info->pitch     = fb_pitch;
        info->bpp       = fb_bpp;
        info->phys_addr = fb_phys_addr;
        return 0;
    }
    return -1;
}

void fb_init(uint64_t phys, uint32_t w, uint32_t h, uint32_t pitch, uint8_t bpp) {
    fb_phys_addr = phys;
    fb_width     = w;
    fb_height    = h;
    fb_pitch     = pitch;
    fb_bpp       = bpp;

    /* Map framebuffer into kernel VA space */
    uint64_t size = (uint64_t)pitch * h;
    uint64_t n_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    vmm_map_range(phys, FB_KERNEL_VA, n_pages,
                  PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE);

    fb_initialized = true;
    debug_printf("fb: %ux%u bpp=%u pitch=%u phys=0x%x mapped to 0x%x\n",
                 (uint64_t)w, (uint64_t)h, (uint64_t)bpp, (uint64_t)pitch,
                 phys, FB_KERNEL_VA);

    /* Register /dev/fb0 */
    struct device *dev = device_register("fb0", fb_dev_read, fb_dev_write);
    if (dev) {
        dev->ioctl = fb_dev_ioctl;
    }
}

bool fb_is_initialized(void) {
    return fb_initialized;
}

struct fb_info fb_get_info(void) {
    struct fb_info info;
    info.width     = fb_width;
    info.height    = fb_height;
    info.pitch     = fb_pitch;
    info.bpp       = fb_bpp;
    info.phys_addr = fb_phys_addr;
    return info;
}

uint64_t fb_get_phys_addr(void) {
    return fb_phys_addr;
}

uint64_t fb_get_size(void) {
    return (uint64_t)fb_pitch * fb_height;
}
