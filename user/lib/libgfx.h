#ifndef LIBGFX_H
#define LIBGFX_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed int         int32_t;

#define GFX_RGB(r,g,b) (((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))

typedef struct {
    uint32_t *pixels;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;   /* in bytes */
    uint8_t   bpp;
} gfx_ctx_t;

typedef struct {
    uint32_t *data;
    uint32_t  width;
    uint32_t  height;
} gfx_surface_t;

#define FBIOGET_INFO  0x4600
struct fb_info {
    uint32_t width, height, pitch;
    uint8_t  bpp;
    uint64_t phys_addr;
} __attribute__((packed));

int  gfx_init(gfx_ctx_t *ctx);
void gfx_fini(gfx_ctx_t *ctx);
void gfx_clear(gfx_ctx_t *ctx, uint32_t color);
void gfx_put_pixel(gfx_ctx_t *ctx, int x, int y, uint32_t color);
void gfx_fill_rect(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t color);
void gfx_draw_line(gfx_ctx_t *ctx, int x0, int y0, int x1, int y1, uint32_t color);
void gfx_draw_char(gfx_ctx_t *ctx, int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_string(gfx_ctx_t *ctx, int x, int y, const char *s, uint32_t fg, uint32_t bg);
gfx_surface_t *gfx_surface_create(uint32_t w, uint32_t h);
void gfx_blit(gfx_ctx_t *ctx, gfx_surface_t *src, int dst_x, int dst_y);

#endif /* LIBGFX_H */
