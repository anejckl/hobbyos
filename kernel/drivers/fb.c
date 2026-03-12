#include "fb.h"
#include "device.h"
#include "../memory/vmm.h"
#include "../string.h"
#include "../debug/debug.h"

/* Framebuffer kernel VA: mapped here */
#define FB_KERNEL_VA  0xFFFFFF0000000000ULL

/* Major/minor for fb0 */
#define FB_MAJOR  29
#define FB_MINOR  0

static bool     fb_initialized = false;
static uint64_t fb_phys_addr   = 0;
static uint32_t fb_width       = 0;
static uint32_t fb_height      = 0;
static uint32_t fb_pitch       = 0;
static uint8_t  fb_bpp         = 0;

static int fb_ops_read(struct device *dev, uint8_t *buf, uint32_t count, uint64_t offset) {
    (void)dev; (void)buf; (void)count; (void)offset;
    return -1;
}

static int fb_ops_write(struct device *dev, const uint8_t *buf, uint32_t count, uint64_t offset) {
    (void)dev; (void)buf; (void)count; (void)offset;
    return -1;
}

static int fb_ops_ioctl(struct device *dev, uint32_t cmd, uint64_t arg) {
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

static int fb_ops_mmap(struct device *dev, uint64_t offset, uint64_t size,
                       uint64_t *phys_out) {
    (void)dev;
    uint64_t fb_size = (uint64_t)fb_pitch * fb_height;
    if (offset + size > fb_size)
        return -1;
    *phys_out = fb_phys_addr + offset;
    return 0;
}

static struct device_ops fb_dev_ops = {
    .open  = NULL,
    .close = NULL,
    .read  = fb_ops_read,
    .write = fb_ops_write,
    .ioctl = fb_ops_ioctl,
    .mmap  = fb_ops_mmap,
};

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

    /* Register /dev/fb0 with extended ops */
    device_register_ex("fb0", DEV_CHAR, FB_MAJOR, FB_MINOR, &fb_dev_ops);
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
