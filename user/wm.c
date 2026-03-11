#include "lib/libc.h"
#include "lib/libgfx.h"

/* Minimal window manager: opens framebuffer, draws a desktop background */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    gfx_ctx_t ctx;
    if (gfx_init(&ctx) != 0) {
        fputs("wm: failed to open framebuffer\n", 1);
        return 1;
    }

    /* Draw desktop background */
    gfx_clear(&ctx, GFX_RGB(0x22, 0x44, 0x88));

    /* Draw a taskbar */
    gfx_fill_rect(&ctx, 0, (int)ctx.height - 32, (int)ctx.width, 32,
                  GFX_RGB(0x11, 0x22, 0x44));

    /* Draw title */
    gfx_draw_string(&ctx, 8, 8, "HobbyOS Window Manager",
                    GFX_RGB(0xFF, 0xFF, 0xFF), GFX_RGB(0x22, 0x44, 0x88));
    gfx_draw_string(&ctx, 8, 20, "Framebuffer active",
                    GFX_RGB(0xAA, 0xCC, 0xFF), GFX_RGB(0x22, 0x44, 0x88));

    /* Draw clock in taskbar */
    uint64_t ms = sys_gettime_libc();
    char buf[32];
    uint64_t sec = ms / 1000;
    snprintf(buf, sizeof(buf), "Uptime: %lus", sec);
    gfx_draw_string(&ctx, 8, (int)ctx.height - 24, buf,
                    GFX_RGB(0xFF, 0xFF, 0xFF), GFX_RGB(0x11, 0x22, 0x44));

    /* Keep running (in a real WM, this would be the event loop) */
    /* For now, just sleep by busy-waiting a bit then exit */
    uint64_t start = sys_gettime_libc();
    while (sys_gettime_libc() - start < 5000) {
        /* yield */
    }

    gfx_fini(&ctx);
    return 0;
}
