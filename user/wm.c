/* HobbyOS Window Manager
 * Static-linked user program with event loop, windows, terminal, taskbar.
 */
#include "syscall.h"
#include "ulib.h"
#include "lib/libgfx.c"

/* ---- Utility ---- */
static void *wm_memcpy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void wm_memmove(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
}

static void wm_memset(void *dst, int val, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)val;
}

static uint64_t wm_strlen(const char *s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

static int wm_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(*(unsigned char *)a) - (int)(*(unsigned char *)b);
}

static int wm_strncmp(const char *a, const char *b, uint64_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (int)(*(unsigned char *)a) - (int)(*(unsigned char *)b);
}

static void wm_strncpy(char *dst, const char *src, uint64_t n) {
    uint64_t i;
    for (i = 0; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void wm_itoa(uint64_t val, char *buf, int width) {
    char tmp[20];
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    while (val > 0) { tmp[i++] = '0' + (char)(val % 10); val /= 10; }
    /* Zero-pad to width */
    while (i < width) tmp[i++] = '0';
    for (int j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
}

/* ---- Mouse event struct (must match kernel) ---- */
typedef short int16_t_wm;
struct mouse_event {
    int16_t_wm dx;
    int16_t_wm dy;
    uint8_t buttons;  /* bit0=left, bit1=right, bit2=middle */
};

/* ---- Color Themes (Phase 2) ---- */
#define THEME_COUNT 5

struct wm_theme {
    uint32_t term_fg, term_bg;
    uint32_t titlebar_focused, titlebar_unfocused;
    uint32_t taskbar_bg, taskbar_text;
    uint32_t desktop_top, desktop_bot;
    char name[16];
};

static struct wm_theme themes[THEME_COUNT] = {
    { /* Classic: green on black */
        GFX_RGB(0x00, 0xFF, 0x66), GFX_RGB(0x0A, 0x0A, 0x1A),
        GFX_RGB(0x2A, 0x5A, 0xCC), GFX_RGB(0x55, 0x55, 0x66),
        GFX_RGB(0x1A, 0x1A, 0x2E), GFX_RGB(0xFF, 0xFF, 0xFF),
        GFX_RGB(0x18, 0x30, 0x60), GFX_RGB(0x30, 0x60, 0xB0),
        "Classic"
    },
    { /* Amber */
        GFX_RGB(0xFF, 0xB0, 0x00), GFX_RGB(0x10, 0x08, 0x00),
        GFX_RGB(0x88, 0x55, 0x00), GFX_RGB(0x44, 0x33, 0x22),
        GFX_RGB(0x1A, 0x10, 0x00), GFX_RGB(0xFF, 0xCC, 0x66),
        GFX_RGB(0x20, 0x15, 0x00), GFX_RGB(0x50, 0x30, 0x00),
        "Amber"
    },
    { /* White on black */
        GFX_RGB(0xFF, 0xFF, 0xFF), GFX_RGB(0x0A, 0x0A, 0x0A),
        GFX_RGB(0x44, 0x44, 0x66), GFX_RGB(0x33, 0x33, 0x44),
        GFX_RGB(0x18, 0x18, 0x22), GFX_RGB(0xEE, 0xEE, 0xEE),
        GFX_RGB(0x10, 0x10, 0x20), GFX_RGB(0x30, 0x30, 0x50),
        "White"
    },
    { /* Blue: cyan on navy */
        GFX_RGB(0x00, 0xDD, 0xFF), GFX_RGB(0x00, 0x08, 0x22),
        GFX_RGB(0x00, 0x66, 0xAA), GFX_RGB(0x22, 0x44, 0x66),
        GFX_RGB(0x00, 0x0A, 0x22), GFX_RGB(0x88, 0xDD, 0xFF),
        GFX_RGB(0x00, 0x10, 0x30), GFX_RGB(0x00, 0x40, 0x80),
        "Blue"
    },
    { /* Red */
        GFX_RGB(0xFF, 0x44, 0x44), GFX_RGB(0x18, 0x04, 0x04),
        GFX_RGB(0xAA, 0x22, 0x22), GFX_RGB(0x55, 0x22, 0x22),
        GFX_RGB(0x20, 0x08, 0x08), GFX_RGB(0xFF, 0xAA, 0xAA),
        GFX_RGB(0x20, 0x05, 0x05), GFX_RGB(0x60, 0x10, 0x10),
        "Red"
    },
};
static int current_theme = 0;

/* ---- Colors (derived from theme or static) ---- */
#define COL_BORDER         GFX_RGB(0x33, 0x33, 0x44)
#define COL_WINDOW_BG      GFX_RGB(0x1A, 0x1A, 0x2E)
#define COL_WHITE          GFX_RGB(0xFF, 0xFF, 0xFF)
#define COL_LIGHT_GRAY     GFX_RGB(0xCC, 0xCC, 0xCC)
#define COL_CLOSE_BG       GFX_RGB(0xCC, 0x33, 0x33)
#define COL_MINIMIZE_BG    GFX_RGB(0x88, 0x88, 0x33)
#define COL_MENU_BG        GFX_RGB(0x22, 0x22, 0x3E)
#define COL_MENU_HOVER     GFX_RGB(0x3A, 0x5A, 0x9E)
#define COL_CURSOR_BLACK   GFX_RGB(0x00, 0x00, 0x00)
#define COL_CURSOR_WHITE   GFX_RGB(0xFF, 0xFF, 0xFF)

/* Dynamic colors fetched from current theme */
#define COL_TASKBAR_BTN    GFX_RGB(0x2A, 0x2A, 0x4E)
#define COL_TASKBAR_FOCUS  GFX_RGB(0x3A, 0x5A, 0x9E)

/* ---- Constants ---- */
#define MAX_WINDOWS    16
#define TITLEBAR_H     20
#define BORDER_W       1
#define TASKBAR_H      28
#define CLOSE_BTN_W    16
#define MIN_BTN_W      16
#define RESIZE_BORDER  4

/* ---- Window types ---- */
#define WINTYPE_NORMAL   0
#define WINTYPE_TERMINAL 1
#define WINTYPE_INFO     2
#define WINTYPE_TASKMGR  3
#define WINTYPE_FILEBROWSER 4
#define WINTYPE_FILEVIEWER 5
#define WINTYPE_ABOUT    6

/* ---- Terminal ---- */
#define TERM_COLS 60
#define TERM_ROWS 28
#define TERM_CMDLEN 128

struct wm_terminal {
    char grid[TERM_ROWS][TERM_COLS];
    int cursor_row, cursor_col;
    uint32_t fg, bg;
    char cmd_buf[TERM_CMDLEN];
    int cmd_len;
};

/* ---- File browser state ---- */
#define FB_MAX_ENTRIES 32
#define FB_NAME_LEN 32
struct fb_entry {
    char name[FB_NAME_LEN];
    uint8_t is_dir;
};

struct file_browser {
    struct fb_entry entries[FB_MAX_ENTRIES];
    int entry_count;
    int scroll_offset;
    char current_path[64];
};

/* ---- Window ---- */
struct wm_window {
    int x, y, w, h;
    int content_w, content_h;
    char title[64];
    gfx_surface_t *surface;
    uint8_t visible;
    uint8_t minimized;
    uint8_t type;
    uint8_t workspace;
    struct wm_terminal *term;
    struct file_browser *fbrowser;
    int refresh_counter;
    char file_path[96];
};

/* ---- Hit test ---- */
#define HIT_NONE       0
#define HIT_TITLEBAR   1
#define HIT_CLOSE      2
#define HIT_MINIMIZE   3
#define HIT_CONTENT    4
#define HIT_RESIZE_N   5
#define HIT_RESIZE_S   6
#define HIT_RESIZE_E   7
#define HIT_RESIZE_W   8
#define HIT_RESIZE_NE  9
#define HIT_RESIZE_NW  10
#define HIT_RESIZE_SE  11
#define HIT_RESIZE_SW  12

/* ---- Interaction modes ---- */
#define MODE_NONE   0
#define MODE_DRAG   1
#define MODE_RESIZE 2

/* ---- Global state ---- */
static gfx_ctx_t fb;
static gfx_surface_t *backbuf;
static int screen_w, screen_h;
static int mouse_x, mouse_y;
static int mouse_fd, kb_fd;

static struct wm_window windows[MAX_WINDOWS];
static int window_count;
static int window_order[MAX_WINDOWS]; /* z-order: back to front */
static int order_count;
static int focused_idx;  /* index into windows[] or -1 */

static int mode;
static int drag_win;
static int drag_ox, drag_oy;
static int resize_edge;
static int resize_orig_x, resize_orig_y, resize_orig_w, resize_orig_h;
static uint8_t prev_buttons;

static int running;
static int current_workspace;
static int frame_counter;

/* ---- Right-click menu ---- */
#define MAX_MENU_ITEMS 8
struct menu_item {
    char label[32];
    int id;
};
static struct menu_item desktop_menu[] = {
    {"New Terminal", 1},
    {"System Info", 4},
    {"Task Manager", 5},
    {"Files", 6},
    {"About", 2},
    {"Exit WM", 3},
};
#define MENU_COUNT 6
#define MENU_ITEM_H 20
#define MENU_W 140
static int menu_visible;
static int menu_x, menu_y;
static int menu_hover;  /* -1 = none */

/* Workspace buttons in taskbar */
#define WS_COUNT 4
#define WS_BTN_W 24
#define WS_BTN_H 20
#define WS_BTN_PAD 2
#define WS_AREA_W (WS_COUNT * (WS_BTN_W + WS_BTN_PAD) + WS_BTN_PAD)

/* ---- Mouse cursor bitmap (12x19) ---- */
/* 0=transparent, 1=black, 2=white */
static const uint8_t cursor_bmp[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,1,1,1,1,1,0},
    {1,2,2,2,1,2,1,0,0,0,0,0},
    {1,2,2,1,0,1,2,1,0,0,0,0},
    {1,2,1,0,0,1,2,1,0,0,0,0},
    {1,1,0,0,0,0,1,2,1,0,0,0},
    {1,0,0,0,0,0,1,2,1,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

/* ---- Z-order management ---- */

static void wm_raise_window(int idx) {
    int pos = -1;
    for (int i = 0; i < order_count; i++) {
        if (window_order[i] == idx) { pos = i; break; }
    }
    if (pos < 0) return;
    for (int i = pos; i < order_count - 1; i++)
        window_order[i] = window_order[i + 1];
    window_order[order_count - 1] = idx;
}

static void wm_focus_window(int idx) {
    if (focused_idx >= 0 && focused_idx < MAX_WINDOWS)
        windows[focused_idx].visible = 1;
    focused_idx = idx;
    if (idx >= 0) {
        wm_raise_window(idx);
    }
}

/* ---- Window creation ---- */

static int wm_create_window(int x, int y, int cw, int ch, const char *title) {
    if (window_count >= MAX_WINDOWS) return -1;
    int idx = window_count++;

    struct wm_window *win = &windows[idx];
    win->x = x;
    win->y = y;
    win->content_w = cw;
    win->content_h = ch;
    win->w = cw + 2 * BORDER_W;
    win->h = ch + TITLEBAR_H + 2 * BORDER_W;
    wm_strncpy(win->title, title, 64);
    win->surface = gfx_surface_create((uint32_t)cw, (uint32_t)ch);
    win->visible = 1;
    win->minimized = 0;
    win->type = WINTYPE_NORMAL;
    win->workspace = (uint8_t)current_workspace;
    win->term = (struct wm_terminal *)0;
    win->fbrowser = (struct file_browser *)0;
    win->refresh_counter = 0;

    if (win->surface)
        gfx_surface_clear(win->surface, COL_WINDOW_BG);

    window_order[order_count++] = idx;
    wm_focus_window(idx);
    return idx;
}

/* ---- Terminal ---- */

static struct wm_terminal term_pool[MAX_WINDOWS];
static int term_pool_idx = 0;

static void term_clear(struct wm_terminal *t) {
    wm_memset(t->grid, ' ', TERM_ROWS * TERM_COLS);
    t->cursor_row = 0;
    t->cursor_col = 0;
    t->cmd_len = 0;
    t->cmd_buf[0] = '\0';
}

static void term_scroll_up(struct wm_terminal *t) {
    wm_memmove(&t->grid[0][0], &t->grid[1][0], (TERM_ROWS - 1) * TERM_COLS);
    wm_memset(&t->grid[TERM_ROWS - 1][0], ' ', TERM_COLS);
}

static void term_putchar(struct wm_terminal *t, char c) {
    if (c == '\n') {
        t->cursor_col = 0;
        t->cursor_row++;
        if (t->cursor_row >= TERM_ROWS) {
            term_scroll_up(t);
            t->cursor_row = TERM_ROWS - 1;
        }
        return;
    }
    if (c == '\b') {
        if (t->cursor_col > 0) {
            t->cursor_col--;
            t->grid[t->cursor_row][t->cursor_col] = ' ';
        }
        return;
    }
    if (c == '\t') {
        int spaces = 4 - (t->cursor_col % 4);
        for (int i = 0; i < spaces; i++) term_putchar(t, ' ');
        return;
    }
    if (t->cursor_col >= TERM_COLS) {
        t->cursor_col = 0;
        t->cursor_row++;
        if (t->cursor_row >= TERM_ROWS) {
            term_scroll_up(t);
            t->cursor_row = TERM_ROWS - 1;
        }
    }
    t->grid[t->cursor_row][t->cursor_col] = c;
    t->cursor_col++;
}

static void term_puts(struct wm_terminal *t, const char *s) {
    while (*s) term_putchar(t, *s++);
}

static void term_prompt(struct wm_terminal *t) {
    term_puts(t, "> ");
    t->cmd_len = 0;
    t->cmd_buf[0] = '\0';
}

static void term_put_num(struct wm_terminal *t, uint64_t val) {
    char buf[20];
    wm_itoa(val, buf, 1);
    term_puts(t, buf);
}

/* Forward declaration for exit handling */
static void wm_close_window(int idx);

static void term_exec_cmd(struct wm_terminal *t, int win_idx) {
    t->cmd_buf[t->cmd_len] = '\0';
    term_putchar(t, '\n');

    if (t->cmd_len == 0) {
        /* empty */
    } else if (wm_strcmp(t->cmd_buf, "hello") == 0) {
        term_puts(t, "Hello from HobbyOS WM!\n");
    } else if (wm_strcmp(t->cmd_buf, "clear") == 0) {
        term_clear(t);
    } else if (wm_strcmp(t->cmd_buf, "about") == 0) {
        term_puts(t, "HobbyOS Window Manager v2.0\n");
        term_puts(t, "A hobby operating system\n");
        term_puts(t, "with interactive GUI\n");
    } else if (wm_strcmp(t->cmd_buf, "exit") == 0) {
        wm_close_window(win_idx);
        return; /* don't print prompt */
    } else if (wm_strcmp(t->cmd_buf, "help") == 0) {
        term_puts(t, "Commands:\n");
        term_puts(t, "  hello clear about exit\n");
        term_puts(t, "  help uptime pid theme\n");
        term_puts(t, "  ws <1-4>\n");
    } else if (wm_strcmp(t->cmd_buf, "uptime") == 0) {
        uint64_t ms = sys_gettime();
        uint64_t sec = ms / 1000;
        term_puts(t, "Uptime: ");
        term_put_num(t, sec);
        term_puts(t, "s\n");
    } else if (wm_strcmp(t->cmd_buf, "pid") == 0) {
        uint64_t pid = sys_getpid();
        term_puts(t, "PID: ");
        term_put_num(t, pid);
        term_putchar(t, '\n');
    } else if (wm_strcmp(t->cmd_buf, "theme") == 0) {
        current_theme = (current_theme + 1) % THEME_COUNT;
        /* Update all terminal colors */
        for (int i = 0; i < window_count; i++) {
            if (windows[i].term) {
                windows[i].term->fg = themes[current_theme].term_fg;
                windows[i].term->bg = themes[current_theme].term_bg;
            }
        }
        term_puts(t, "Theme: ");
        term_puts(t, themes[current_theme].name);
        term_putchar(t, '\n');
    } else if (wm_strncmp(t->cmd_buf, "ws ", 3) == 0) {
        int ws = t->cmd_buf[3] - '1';
        if (ws >= 0 && ws < WS_COUNT) {
            current_workspace = ws;
            term_puts(t, "Workspace ");
            term_putchar(t, '1' + (char)ws);
            term_putchar(t, '\n');
        } else {
            term_puts(t, "Usage: ws <1-4>\n");
        }
    } else {
        term_puts(t, "Unknown: ");
        term_puts(t, t->cmd_buf);
        term_putchar(t, '\n');
    }

    term_prompt(t);
}

static void term_render(struct wm_window *win) {
    struct wm_terminal *t = win->term;
    if (!t || !win->surface) return;

    gfx_surface_clear(win->surface, t->bg);
    for (int row = 0; row < TERM_ROWS; row++) {
        for (int col = 0; col < TERM_COLS; col++) {
            char c = t->grid[row][col];
            if (c != ' ') {
                gfx_surface_draw_char(win->surface, col * 8, row * 9,
                                       c, t->fg, t->bg);
            }
        }
    }
    /* Draw cursor */
    if (t->cursor_col < TERM_COLS && t->cursor_row < TERM_ROWS) {
        gfx_surface_fill_rect(win->surface,
                               t->cursor_col * 8, t->cursor_row * 9,
                               8, 9, t->fg);
    }
}

static int wm_create_terminal(int x, int y, const char *title) {
    int cw = TERM_COLS * 8;
    int ch = TERM_ROWS * 9;
    int idx = wm_create_window(x, y, cw, ch, title);
    if (idx < 0) return -1;

    if (term_pool_idx >= MAX_WINDOWS) return -1;
    struct wm_terminal *t = &term_pool[term_pool_idx++];
    t->fg = themes[current_theme].term_fg;
    t->bg = themes[current_theme].term_bg;
    term_clear(t);
    term_puts(t, "HobbyOS Terminal\n");
    term_prompt(t);
    windows[idx].term = t;
    windows[idx].type = WINTYPE_TERMINAL;
    term_render(&windows[idx]);
    return idx;
}

/* ---- Close window ---- */

static void wm_close_window(int idx) {
    if (idx < 0 || idx >= window_count) return;
    windows[idx].visible = 0;
    windows[idx].minimized = 0;
    if (focused_idx == idx) focused_idx = -1;
}

/* ---- System Info Window (Phase 3) ---- */

static void sysinfo_render(struct wm_window *win) {
    gfx_surface_t *s = win->surface;
    if (!s) return;

    uint32_t fg = COL_WHITE;
    uint32_t bg = COL_WINDOW_BG;
    gfx_surface_clear(s, bg);

    gfx_surface_draw_string(s, 12, 8, "HobbyOS v2.0", fg, bg);
    gfx_surface_draw_string(s, 12, 24, "x86-64 Higher-Half Kernel", COL_LIGHT_GRAY, bg);

    /* Uptime */
    uint64_t ms = sys_gettime();
    uint64_t sec = ms / 1000;
    char buf[40];
    char tmp[20];
    /* Build "Uptime: Xs" */
    buf[0] = 'U'; buf[1] = 'p'; buf[2] = 't'; buf[3] = 'i';
    buf[4] = 'm'; buf[5] = 'e'; buf[6] = ':'; buf[7] = ' ';
    wm_itoa(sec, tmp, 1);
    int p = 8;
    for (int i = 0; tmp[i]; i++) buf[p++] = tmp[i];
    buf[p++] = 's'; buf[p] = '\0';
    gfx_surface_draw_string(s, 12, 48, buf, COL_LIGHT_GRAY, bg);

    /* Process count: scan /proc */
    int proc_count = 0;
    char path[32];
    char rbuf[64];
    for (int pid = 1; pid <= 64; pid++) {
        path[0] = '/'; path[1] = 'p'; path[2] = 'r'; path[3] = 'o';
        path[4] = 'c'; path[5] = '/';
        int pos = 6;
        if (pid >= 10) path[pos++] = '0' + (char)(pid / 10);
        path[pos++] = '0' + (char)(pid % 10);
        path[pos++] = '/'; path[pos++] = 's'; path[pos++] = 't';
        path[pos++] = 'a'; path[pos++] = 't'; path[pos++] = 'u';
        path[pos++] = 's'; path[pos] = '\0';
        int fd = (int)sys_open(path, 0);
        if (fd >= 0) {
            if (sys_read(fd, rbuf, sizeof(rbuf) - 1) > 0)
                proc_count++;
            sys_close(fd);
        }
    }
    buf[0] = 'P'; buf[1] = 'r'; buf[2] = 'o'; buf[3] = 'c';
    buf[4] = 'e'; buf[5] = 's'; buf[6] = 's'; buf[7] = 'e';
    buf[8] = 's'; buf[9] = ':'; buf[10] = ' ';
    wm_itoa((uint64_t)proc_count, tmp, 1);
    p = 11;
    for (int i = 0; tmp[i]; i++) buf[p++] = tmp[i];
    buf[p] = '\0';
    gfx_surface_draw_string(s, 12, 68, buf, COL_LIGHT_GRAY, bg);

    gfx_surface_draw_string(s, 12, 88, "Memory: 8MB user heap", COL_LIGHT_GRAY, bg);

    /* Heap usage */
    uint64_t used_kb = gfx_heap_ptr / 1024;
    buf[0] = 'H'; buf[1] = 'e'; buf[2] = 'a'; buf[3] = 'p';
    buf[4] = ' '; buf[5] = 'u'; buf[6] = 's'; buf[7] = 'e';
    buf[8] = 'd'; buf[9] = ':'; buf[10] = ' ';
    wm_itoa(used_kb, tmp, 1);
    p = 11;
    for (int i = 0; tmp[i]; i++) buf[p++] = tmp[i];
    buf[p++] = 'K'; buf[p++] = 'B'; buf[p] = '\0';
    gfx_surface_draw_string(s, 12, 108, buf, COL_LIGHT_GRAY, bg);

    gfx_surface_draw_string(s, 12, 136, "Theme: ", COL_LIGHT_GRAY, bg);
    gfx_surface_draw_string(s, 12 + 7 * 8, 136, themes[current_theme].name, fg, bg);
}

static void wm_create_sysinfo(void) {
    int cw = 320, ch = 160;
    int idx = wm_create_window(180, 120, cw, ch, "System Info");
    if (idx < 0) return;
    windows[idx].type = WINTYPE_INFO;
    sysinfo_render(&windows[idx]);
}

/* ---- Task Manager (Phase 4) ---- */

static void taskmgr_refresh(struct wm_window *win) {
    gfx_surface_t *s = win->surface;
    if (!s) return;

    uint32_t fg = COL_WHITE;
    uint32_t bg = COL_WINDOW_BG;
    gfx_surface_clear(s, bg);

    /* Header */
    gfx_surface_draw_string(s, 8, 4, "PID  NAME             STATE", fg, bg);
    gfx_surface_fill_rect(s, 4, 14, win->content_w - 8, 1, COL_LIGHT_GRAY);

    int row_y = 18;
    char path[32];
    char buf[256];

    for (int pid = 1; pid <= 64 && row_y < win->content_h - 12; pid++) {
        path[0] = '/'; path[1] = 'p'; path[2] = 'r'; path[3] = 'o';
        path[4] = 'c'; path[5] = '/';
        int pos = 6;
        if (pid >= 10) path[pos++] = '0' + (char)(pid / 10);
        path[pos++] = '0' + (char)(pid % 10);
        path[pos++] = '/'; path[pos++] = 's'; path[pos++] = 't';
        path[pos++] = 'a'; path[pos++] = 't'; path[pos++] = 'u';
        path[pos++] = 's'; path[pos] = '\0';

        int fd = (int)sys_open(path, 0);
        if (fd < 0) continue;

        int64_t n = sys_read(fd, buf, sizeof(buf) - 1);
        sys_close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';

        /* Parse: "PID: X  NAME: Y  STATE: Z" */
        char pid_str[8] = "?";
        char name_str[20] = "?";
        char state_str[16] = "?";

        /* Find PID */
        char *p2 = buf;
        while (*p2 && *p2 != '\n') {
            if (p2[0] == 'P' && p2[1] == 'I' && p2[2] == 'D' && p2[3] == ':') {
                p2 += 4;
                while (*p2 == ' ') p2++;
                int k = 0;
                while (*p2 && *p2 != ' ' && *p2 != '\n' && k < 7)
                    pid_str[k++] = *p2++;
                pid_str[k] = '\0';
                break;
            }
            p2++;
        }

        /* Find NAME */
        p2 = buf;
        char *nm = (char *)0;
        for (int i = 0; buf[i]; i++) {
            if (buf[i] == 'N' && buf[i+1] == 'A' && buf[i+2] == 'M' &&
                buf[i+3] == 'E' && buf[i+4] == ':') {
                nm = &buf[i + 5];
                break;
            }
        }
        if (nm) {
            while (*nm == ' ') nm++;
            int k = 0;
            while (*nm && *nm != ' ' && *nm != '\n' && k < 19)
                name_str[k++] = *nm++;
            name_str[k] = '\0';
        }

        /* Find STATE */
        char *st = (char *)0;
        for (int i = 0; buf[i]; i++) {
            if (buf[i] == 'S' && buf[i+1] == 'T' && buf[i+2] == 'A' &&
                buf[i+3] == 'T' && buf[i+4] == 'E' && buf[i+5] == ':') {
                st = &buf[i + 6];
                break;
            }
        }
        if (st) {
            while (*st == ' ') st++;
            int k = 0;
            while (*st && *st != ' ' && *st != '\n' && k < 15)
                state_str[k++] = *st++;
            state_str[k] = '\0';
        }

        /* Format line: "PID  NAME             STATE" */
        char line[48];
        wm_memset(line, ' ', 47);
        line[47] = '\0';
        /* PID at col 0, width 4 */
        for (int i = 0; pid_str[i] && i < 4; i++) line[i] = pid_str[i];
        /* NAME at col 5, width 17 */
        for (int i = 0; name_str[i] && i < 17; i++) line[5 + i] = name_str[i];
        /* STATE at col 22 */
        for (int i = 0; state_str[i] && i < 15; i++) line[22 + i] = state_str[i];
        /* Find actual end */
        int end = 47;
        while (end > 0 && line[end - 1] == ' ') end--;
        line[end] = '\0';

        gfx_surface_draw_string(s, 8, row_y, line, COL_LIGHT_GRAY, bg);
        row_y += 12;
    }
}

static void wm_create_taskmgr(void) {
    int cw = 320, ch = 300;
    int idx = wm_create_window(220, 80, cw, ch, "Task Manager");
    if (idx < 0) return;
    windows[idx].type = WINTYPE_TASKMGR;
    windows[idx].refresh_counter = 0;
    taskmgr_refresh(&windows[idx]);
}

/* ---- File Browser (Phase 5) ---- */

static struct file_browser fb_pool[4];
static int fb_pool_idx = 0;

static void fb_scan_dir(struct file_browser *fbr) {
    fbr->entry_count = 0;

    int fd = (int)sys_open(fbr->current_path, 0);
    if (fd < 0) return;

    uint8_t dirbuf[1024];
    int64_t n = sys_getdents(fd, dirbuf, sizeof(dirbuf));
    sys_close(fd);
    if (n <= 0) return;

    /* Parse entries: [uint32_t inode, uint8_t name_len, char name[]] */
    int off = 0;
    while (off < n && fbr->entry_count < FB_MAX_ENTRIES) {
        if (off + 5 > n) break;
        /* uint32_t inode = *(uint32_t *)&dirbuf[off]; */
        uint8_t name_len = dirbuf[off + 4];
        if (off + 5 + name_len > n) break;

        struct fb_entry *e = &fbr->entries[fbr->entry_count];
        int cpy = name_len < FB_NAME_LEN - 1 ? name_len : FB_NAME_LEN - 1;
        wm_memcpy(e->name, &dirbuf[off + 5], (uint64_t)cpy);
        e->name[cpy] = '\0';

        /* Check if directory via stat */
        char fullpath[96];
        int pp = 0;
        for (int i = 0; fbr->current_path[i] && pp < 90; i++)
            fullpath[pp++] = fbr->current_path[i];
        /* Add slash if needed */
        if (pp > 0 && fullpath[pp - 1] != '/')
            fullpath[pp++] = '/';
        for (int i = 0; e->name[i] && pp < 95; i++)
            fullpath[pp++] = e->name[i];
        fullpath[pp] = '\0';

        struct stat_buf sb;
        e->is_dir = 0;
        if (sys_stat(fullpath, &sb) == 0) {
            if (sb.type == STAT_DIR) e->is_dir = 1;
        }

        fbr->entry_count++;
        off += 5 + name_len;
    }
}

static void fb_render(struct wm_window *win) {
    struct file_browser *fbr = win->fbrowser;
    if (!fbr || !win->surface) return;

    gfx_surface_t *s = win->surface;
    uint32_t fg = COL_WHITE;
    uint32_t bg = COL_WINDOW_BG;
    gfx_surface_clear(s, bg);

    /* Path bar */
    gfx_surface_draw_string(s, 8, 4, fbr->current_path, fg, bg);
    gfx_surface_fill_rect(s, 4, 14, win->content_w - 8, 1, COL_LIGHT_GRAY);

    int row_y = 18;
    for (int i = fbr->scroll_offset; i < fbr->entry_count && row_y < win->content_h - 10; i++) {
        struct fb_entry *e = &fbr->entries[i];
        char line[48];
        int p = 0;
        if (e->is_dir) {
            line[p++] = '['; line[p++] = 'D'; line[p++] = ']';
        } else {
            line[p++] = ' '; line[p++] = ' '; line[p++] = ' ';
        }
        line[p++] = ' ';
        for (int j = 0; e->name[j] && p < 47; j++)
            line[p++] = e->name[j];
        line[p] = '\0';

        gfx_surface_draw_string(s, 8, row_y, line, COL_LIGHT_GRAY, bg);
        row_y += 12;
    }

    if (fbr->entry_count == 0) {
        gfx_surface_draw_string(s, 8, 18, "(empty directory)", COL_LIGHT_GRAY, bg);
    }
}

static void wm_create_filebrowser(void) {
    if (fb_pool_idx >= 4) return;
    int cw = 300, ch = 280;
    int idx = wm_create_window(160, 100, cw, ch, "Files");
    if (idx < 0) return;
    windows[idx].type = WINTYPE_FILEBROWSER;

    struct file_browser *fbr = &fb_pool[fb_pool_idx++];
    wm_strncpy(fbr->current_path, "/", 64);
    fbr->scroll_offset = 0;
    windows[idx].fbrowser = fbr;

    fb_scan_dir(fbr);
    fb_render(&windows[idx]);
}

/* File viewer rendering (reusable for resize) */
static void fileviewer_render(struct wm_window *win) {
    gfx_surface_t *s = win->surface;
    if (!s) return;

    uint32_t bg = COL_WINDOW_BG;
    gfx_surface_clear(s, bg);

    int fd = (int)sys_open(win->file_path, 0);
    if (fd < 0) {
        gfx_surface_draw_string(s, 8, 8, "Cannot open file", COL_LIGHT_GRAY, bg);
        return;
    }

    char buf[1024];
    int64_t n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);

    if (n <= 0) {
        gfx_surface_draw_string(s, 8, 8, "(empty file)", COL_LIGHT_GRAY, bg);
        return;
    }
    buf[n] = '\0';

    /* Render text content line by line */
    int tx = 8, ty = 4;
    int cw = win->content_w;
    int ch = win->content_h;
    for (int i = 0; i < n && ty < ch - 10; i++) {
        if (buf[i] == '\n') {
            tx = 8;
            ty += 10;
        } else if (buf[i] >= ' ' && buf[i] <= '~') {
            if (tx + 8 < cw) {
                gfx_surface_draw_char(s, tx, ty, buf[i], COL_LIGHT_GRAY, bg);
                tx += 8;
            }
        }
    }
}

/* File viewer for clicking files in browser */
static void wm_create_fileviewer(const char *path) {
    int cw = 400, ch = 300;
    int idx = wm_create_window(200, 80, cw, ch, path);
    if (idx < 0) return;
    windows[idx].type = WINTYPE_FILEVIEWER;
    wm_strncpy(windows[idx].file_path, path, 96);
    fileviewer_render(&windows[idx]);
}

/* About window rendering (reusable for resize) */
static void about_render(struct wm_window *win) {
    gfx_surface_t *s = win->surface;
    if (!s) return;
    gfx_surface_clear(s, COL_WINDOW_BG);
    gfx_surface_draw_string(s, 16, 16, "HobbyOS Window Manager v2.0",
        COL_WHITE, COL_WINDOW_BG);
    gfx_surface_draw_string(s, 16, 32, "A hobby operating system",
        COL_LIGHT_GRAY, COL_WINDOW_BG);
    gfx_surface_draw_string(s, 16, 48, "with interactive GUI",
        COL_LIGHT_GRAY, COL_WINDOW_BG);
    gfx_surface_draw_string(s, 16, 72, "Press Escape to exit WM",
        COL_LIGHT_GRAY, COL_WINDOW_BG);
}

/* ---- Hit testing ---- */

static int wm_window_at(int mx, int my) {
    for (int i = order_count - 1; i >= 0; i--) {
        int idx = window_order[i];
        struct wm_window *w = &windows[idx];
        if (!w->visible || w->minimized) continue;
        if (w->workspace != current_workspace) continue;
        if (mx >= w->x && mx < w->x + w->w &&
            my >= w->y && my < w->y + w->h) {
            return idx;
        }
    }
    return -1;
}

static int wm_hit_test(struct wm_window *w, int mx, int my) {
    int rx = mx - w->x;
    int ry = my - w->y;

    /* Resize edges for non-terminal windows (Phase 7) */
    if (w->type != WINTYPE_TERMINAL) {
        int right = w->w;
        int bottom = w->h;

        /* Corners first (larger hit area) */
        if (rx < RESIZE_BORDER && ry < RESIZE_BORDER) return HIT_RESIZE_NW;
        if (rx >= right - RESIZE_BORDER && ry < RESIZE_BORDER) return HIT_RESIZE_NE;
        if (rx < RESIZE_BORDER && ry >= bottom - RESIZE_BORDER) return HIT_RESIZE_SW;
        if (rx >= right - RESIZE_BORDER && ry >= bottom - RESIZE_BORDER) return HIT_RESIZE_SE;

        /* Edges */
        if (ry < RESIZE_BORDER) return HIT_RESIZE_N;
        if (ry >= bottom - RESIZE_BORDER) return HIT_RESIZE_S;
        if (rx < RESIZE_BORDER) return HIT_RESIZE_W;
        if (rx >= right - RESIZE_BORDER) return HIT_RESIZE_E;
    }

    /* Title bar area */
    if (ry >= BORDER_W && ry < BORDER_W + TITLEBAR_H) {
        if (rx >= w->w - BORDER_W - CLOSE_BTN_W)
            return HIT_CLOSE;
        if (rx >= w->w - BORDER_W - CLOSE_BTN_W - MIN_BTN_W)
            return HIT_MINIMIZE;
        return HIT_TITLEBAR;
    }
    return HIT_CONTENT;
}

/* ---- Compose frame: render gradient directly into backbuf ---- */

static void render_wallpaper_to_backbuf(void) {
    struct wm_theme *th = &themes[current_theme];
    int work_h = screen_h - TASKBAR_H;
    int r1 = (th->desktop_top >> 16) & 0xFF;
    int g1 = (th->desktop_top >> 8) & 0xFF;
    int b1 = th->desktop_top & 0xFF;
    int r2 = (th->desktop_bot >> 16) & 0xFF;
    int g2 = (th->desktop_bot >> 8) & 0xFF;
    int b2 = th->desktop_bot & 0xFF;

    for (int y = 0; y < work_h; y++) {
        int r = r1 + (r2 - r1) * y / work_h;
        int g = g1 + (g2 - g1) * y / work_h;
        int b = b1 + (b2 - b1) * y / work_h;
        uint32_t color = GFX_RGB(r, g, b);
        uint32_t *row = &backbuf->data[(uint64_t)y * backbuf->width];
        for (int x = 0; x < screen_w; x++)
            row[x] = color;
    }
}

/* ---- Draw window decorations ---- */

static void draw_window(gfx_surface_t *buf, struct wm_window *w, int is_focused) {
    if (!w->visible || w->minimized) return;
    if (w->workspace != current_workspace) return;

    struct wm_theme *th = &themes[current_theme];

    /* Border */
    gfx_surface_fill_rect(buf, w->x, w->y, w->w, w->h, COL_BORDER);

    /* Title bar */
    uint32_t title_col = is_focused ? th->titlebar_focused : th->titlebar_unfocused;
    gfx_surface_fill_rect(buf, w->x + BORDER_W, w->y + BORDER_W,
                           w->w - 2 * BORDER_W, TITLEBAR_H, title_col);

    /* Title text */
    gfx_surface_draw_string(buf, w->x + BORDER_W + 4, w->y + BORDER_W + 6,
                             w->title, COL_WHITE, title_col);

    /* Close button [X] */
    int cbx = w->x + w->w - BORDER_W - CLOSE_BTN_W;
    gfx_surface_fill_rect(buf, cbx, w->y + BORDER_W, CLOSE_BTN_W, TITLEBAR_H,
                           COL_CLOSE_BG);
    gfx_surface_draw_char(buf, cbx + 4, w->y + BORDER_W + 6, 'X',
                           COL_WHITE, COL_CLOSE_BG);

    /* Minimize button [_] */
    int mbx = cbx - MIN_BTN_W;
    gfx_surface_fill_rect(buf, mbx, w->y + BORDER_W, MIN_BTN_W, TITLEBAR_H,
                           COL_MINIMIZE_BG);
    gfx_surface_draw_char(buf, mbx + 4, w->y + BORDER_W + 6, '_',
                           COL_WHITE, COL_MINIMIZE_BG);

    /* Content area */
    int cx = w->x + BORDER_W;
    int cy = w->y + BORDER_W + TITLEBAR_H;
    gfx_surface_fill_rect(buf, cx, cy, w->content_w, w->content_h, COL_WINDOW_BG);

    /* Blit window surface content */
    if (w->surface)
        gfx_surface_blit(buf, w->surface, cx, cy);
}

/* ---- Draw cursor ---- */

static void draw_cursor(gfx_surface_t *buf, int cx, int cy) {
    for (int row = 0; row < 19; row++) {
        for (int col = 0; col < 12; col++) {
            uint8_t v = cursor_bmp[row][col];
            if (v == 0) continue;
            uint32_t color = (v == 1) ? COL_CURSOR_BLACK : COL_CURSOR_WHITE;
            gfx_surface_put_pixel(buf, cx + col, cy + row, color);
        }
    }
}

/* ---- Taskbar ---- */

static void draw_taskbar(gfx_surface_t *buf) {
    struct wm_theme *th = &themes[current_theme];
    int ty = screen_h - TASKBAR_H;
    gfx_surface_fill_rect(buf, 0, ty, screen_w, TASKBAR_H, th->taskbar_bg);

    /* Workspace buttons */
    for (int i = 0; i < WS_COUNT; i++) {
        int bx = WS_BTN_PAD + i * (WS_BTN_W + WS_BTN_PAD);
        int by = ty + (TASKBAR_H - WS_BTN_H) / 2;
        uint32_t btn_col = (i == current_workspace) ? COL_TASKBAR_FOCUS : COL_TASKBAR_BTN;
        gfx_surface_fill_rect(buf, bx, by, WS_BTN_W, WS_BTN_H, btn_col);
        char num = '1' + (char)i;
        gfx_surface_draw_char(buf, bx + 8, by + 6, num, th->taskbar_text, btn_col);
    }

    /* Window buttons (after workspace area) */
    int bx = WS_AREA_W + 4;
    for (int i = 0; i < window_count; i++) {
        struct wm_window *w = &windows[i];
        if (!w->visible && !w->minimized) continue;
        if (w->workspace != current_workspace) continue;

        int bw = 110;
        if (bx + bw > screen_w - 160) break;

        uint32_t btn_col = (i == focused_idx && !w->minimized)
                               ? COL_TASKBAR_FOCUS : COL_TASKBAR_BTN;
        gfx_surface_fill_rect(buf, bx, ty + 4, bw, TASKBAR_H - 8, btn_col);

        char lbl[14];
        int j;
        for (j = 0; j < 12 && w->title[j]; j++) lbl[j] = w->title[j];
        lbl[j] = '\0';
        gfx_surface_draw_string(buf, bx + 4, ty + 10, lbl, th->taskbar_text, btn_col);

        bx += bw + 4;
    }

    /* Theme name near clock */
    gfx_surface_draw_string(buf, screen_w - 150, ty + 10,
                             themes[current_theme].name, th->taskbar_text, th->taskbar_bg);

    /* Clock at right */
    uint64_t ms = sys_gettime();
    uint64_t total_sec = ms / 1000;
    uint64_t h = total_sec / 3600;
    uint64_t m = (total_sec % 3600) / 60;
    uint64_t s = total_sec % 60;
    char clk[16];
    char tmp[4];
    wm_itoa(h, tmp, 2); clk[0] = tmp[0]; clk[1] = tmp[1];
    clk[2] = ':';
    wm_itoa(m, tmp, 2); clk[3] = tmp[0]; clk[4] = tmp[1];
    clk[5] = ':';
    wm_itoa(s, tmp, 2); clk[6] = tmp[0]; clk[7] = tmp[1];
    clk[8] = '\0';
    gfx_surface_draw_string(buf, screen_w - 72, ty + 10, clk,
                             th->taskbar_text, th->taskbar_bg);
}

/* ---- Context menu ---- */

static void draw_menu(gfx_surface_t *buf) {
    if (!menu_visible) return;

    int mh = MENU_COUNT * MENU_ITEM_H + 4;
    gfx_surface_fill_rect(buf, menu_x, menu_y, MENU_W, mh, COL_MENU_BG);
    /* Border */
    gfx_surface_fill_rect(buf, menu_x, menu_y, MENU_W, 1, COL_BORDER);
    gfx_surface_fill_rect(buf, menu_x, menu_y + mh - 1, MENU_W, 1, COL_BORDER);
    gfx_surface_fill_rect(buf, menu_x, menu_y, 1, mh, COL_BORDER);
    gfx_surface_fill_rect(buf, menu_x + MENU_W - 1, menu_y, 1, mh, COL_BORDER);

    for (int i = 0; i < MENU_COUNT; i++) {
        int iy = menu_y + 2 + i * MENU_ITEM_H;
        uint32_t bg = (i == menu_hover) ? COL_MENU_HOVER : COL_MENU_BG;
        gfx_surface_fill_rect(buf, menu_x + 1, iy, MENU_W - 2, MENU_ITEM_H, bg);
        gfx_surface_draw_string(buf, menu_x + 8, iy + 6,
                                 desktop_menu[i].label, COL_WHITE, bg);
    }
}

static int menu_item_at(int mx, int my) {
    if (!menu_visible) return -1;
    int mh = MENU_COUNT * MENU_ITEM_H + 4;
    if (mx < menu_x || mx >= menu_x + MENU_W ||
        my < menu_y || my >= menu_y + mh)
        return -1;
    int idx = (my - menu_y - 2) / MENU_ITEM_H;
    if (idx < 0 || idx >= MENU_COUNT) return -1;
    return idx;
}

/* ---- Taskbar click ---- */

static int taskbar_workspace_at(int mx, int my) {
    int ty = screen_h - TASKBAR_H;
    int by_start = ty + (TASKBAR_H - WS_BTN_H) / 2;
    if (my < by_start || my >= by_start + WS_BTN_H) return -1;
    for (int i = 0; i < WS_COUNT; i++) {
        int bx = WS_BTN_PAD + i * (WS_BTN_W + WS_BTN_PAD);
        if (mx >= bx && mx < bx + WS_BTN_W) return i;
    }
    return -1;
}

static int taskbar_window_at(int mx) {
    int bx = WS_AREA_W + 4;
    for (int i = 0; i < window_count; i++) {
        struct wm_window *w = &windows[i];
        if (!w->visible && !w->minimized) continue;
        if (w->workspace != current_workspace) continue;
        int bw = 110;
        if (mx >= bx && mx < bx + bw) return i;
        bx += bw + 4;
    }
    return -1;
}

/* ---- File browser click handling ---- */

static void fb_handle_click(struct wm_window *win, int rx, int ry) {
    (void)rx;
    struct file_browser *fbr = win->fbrowser;
    if (!fbr) return;

    /* Content starts at y=18, each row 12px */
    if (ry < 18) return;
    int row = (ry - 18) / 12 + fbr->scroll_offset;
    if (row < 0 || row >= fbr->entry_count) return;

    struct fb_entry *e = &fbr->entries[row];

    if (e->is_dir) {
        /* Navigate into directory */
        char newpath[64];
        int p = 0;
        for (int i = 0; fbr->current_path[i] && p < 60; i++)
            newpath[p++] = fbr->current_path[i];
        if (p > 0 && newpath[p - 1] != '/')
            newpath[p++] = '/';
        for (int i = 0; e->name[i] && p < 63; i++)
            newpath[p++] = e->name[i];
        newpath[p] = '\0';
        wm_strncpy(fbr->current_path, newpath, 64);
        fbr->scroll_offset = 0;
        fb_scan_dir(fbr);
        fb_render(win);
    } else {
        /* Open file viewer */
        char fullpath[96];
        int p = 0;
        for (int i = 0; fbr->current_path[i] && p < 90; i++)
            fullpath[p++] = fbr->current_path[i];
        if (p > 0 && fullpath[p - 1] != '/')
            fullpath[p++] = '/';
        for (int i = 0; e->name[i] && p < 95; i++)
            fullpath[p++] = e->name[i];
        fullpath[p] = '\0';
        wm_create_fileviewer(fullpath);
    }
}

/* ---- Window resize (Phase 7) ---- */

static void resize_apply(int win_idx, int new_w, int new_h) {
    struct wm_window *w = &windows[win_idx];

    /* Enforce minimums */
    if (new_w < 200) new_w = 200;
    if (new_h < 100) new_h = 100;
    /* Max */
    if (new_w > screen_w) new_w = screen_w;
    if (new_h > screen_h - TASKBAR_H) new_h = screen_h - TASKBAR_H;

    int new_cw = new_w - 2 * BORDER_W;
    int new_ch = new_h - TITLEBAR_H - 2 * BORDER_W;
    if (new_cw <= 0 || new_ch <= 0) return;

    /* Allocate new surface (old one leaks, acceptable with bump allocator) */
    gfx_surface_t *new_surf = gfx_surface_create((uint32_t)new_cw, (uint32_t)new_ch);
    if (!new_surf) return;

    gfx_surface_clear(new_surf, COL_WINDOW_BG);

    w->surface = new_surf;
    w->content_w = new_cw;
    w->content_h = new_ch;
    w->w = new_w;
    w->h = new_h;

    /* Re-render content for special window types */
    if (w->type == WINTYPE_TASKMGR) {
        taskmgr_refresh(w);
    } else if (w->type == WINTYPE_FILEBROWSER) {
        fb_render(w);
    } else if (w->type == WINTYPE_INFO) {
        sysinfo_render(w);
    } else if (w->type == WINTYPE_FILEVIEWER) {
        fileviewer_render(w);
    } else if (w->type == WINTYPE_ABOUT) {
        about_render(w);
    }
}

/* ---- Compose frame ---- */

static void compose(void) {
    /* Render gradient wallpaper directly into backbuf */
    render_wallpaper_to_backbuf();

    /* Taskbar background */
    gfx_surface_fill_rect(backbuf, 0, screen_h - TASKBAR_H,
                           screen_w, TASKBAR_H, themes[current_theme].taskbar_bg);

    /* Draw windows back-to-front */
    for (int i = 0; i < order_count; i++) {
        int idx = window_order[i];
        draw_window(backbuf, &windows[idx], idx == focused_idx);
    }

    /* Taskbar */
    draw_taskbar(backbuf);

    /* Context menu */
    draw_menu(backbuf);

    /* Cursor */
    draw_cursor(backbuf, mouse_x, mouse_y);

    /* Blit backbuffer to framebuffer */
    gfx_surface_blit_to_ctx(&fb, backbuf, 0, 0);
}

/* ---- Input polling ---- */

static void poll_mouse(void) {
    struct mouse_event ev;
    int n;
    while ((n = (int)sys_read(mouse_fd, &ev, sizeof(ev))) == (int)sizeof(ev)) {
        mouse_x += ev.dx;
        mouse_y += ev.dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= screen_w) mouse_x = screen_w - 1;
        if (mouse_y >= screen_h - 1) mouse_y = screen_h - 1;

        uint8_t left  = ev.buttons & 1;
        uint8_t right = ev.buttons & 2;
        uint8_t prev_left  = prev_buttons & 1;
        uint8_t prev_right = prev_buttons & 2;

        /* Left button press */
        if (left && !prev_left) {
            /* Menu click? */
            if (menu_visible) {
                int mi = menu_item_at(mouse_x, mouse_y);
                menu_visible = 0;
                if (mi >= 0) {
                    switch (desktop_menu[mi].id) {
                    case 1: /* New Terminal */
                        wm_create_terminal(50 + window_count * 30,
                                            50 + window_count * 30,
                                            "Terminal");
                        break;
                    case 2: { /* About */
                        int aidx = wm_create_window(200, 150, 280, 120, "About");
                        if (aidx >= 0) {
                            windows[aidx].type = WINTYPE_ABOUT;
                            about_render(&windows[aidx]);
                        }
                        break;
                    }
                    case 3: /* Exit WM */
                        running = 0;
                        break;
                    case 4: /* System Info */
                        wm_create_sysinfo();
                        break;
                    case 5: /* Task Manager */
                        wm_create_taskmgr();
                        break;
                    case 6: /* Files */
                        wm_create_filebrowser();
                        break;
                    }
                }
            } else if (mouse_y >= screen_h - TASKBAR_H) {
                /* Taskbar click: check workspace buttons first */
                int ws = taskbar_workspace_at(mouse_x, mouse_y);
                if (ws >= 0) {
                    current_workspace = ws;
                } else {
                    int ti = taskbar_window_at(mouse_x);
                    if (ti >= 0) {
                        if (windows[ti].minimized) {
                            windows[ti].minimized = 0;
                            windows[ti].visible = 1;
                        }
                        wm_focus_window(ti);
                    }
                }
            } else {
                /* Window click */
                int idx = wm_window_at(mouse_x, mouse_y);
                if (idx >= 0) {
                    wm_focus_window(idx);
                    int hit = wm_hit_test(&windows[idx], mouse_x, mouse_y);
                    switch (hit) {
                    case HIT_CLOSE:
                        wm_close_window(idx);
                        break;
                    case HIT_MINIMIZE:
                        windows[idx].minimized = 1;
                        if (focused_idx == idx) focused_idx = -1;
                        break;
                    case HIT_TITLEBAR:
                        mode = MODE_DRAG;
                        drag_win = idx;
                        drag_ox = mouse_x - windows[idx].x;
                        drag_oy = mouse_y - windows[idx].y;
                        break;
                    case HIT_CONTENT:
                        /* File browser click */
                        if (windows[idx].type == WINTYPE_FILEBROWSER) {
                            int rx = mouse_x - windows[idx].x - BORDER_W;
                            int ry = mouse_y - windows[idx].y - BORDER_W - TITLEBAR_H;
                            fb_handle_click(&windows[idx], rx, ry);
                        }
                        break;
                    case HIT_RESIZE_N:
                    case HIT_RESIZE_S:
                    case HIT_RESIZE_E:
                    case HIT_RESIZE_W:
                    case HIT_RESIZE_NE:
                    case HIT_RESIZE_NW:
                    case HIT_RESIZE_SE:
                    case HIT_RESIZE_SW:
                        mode = MODE_RESIZE;
                        drag_win = idx;
                        resize_edge = hit;
                        resize_orig_x = windows[idx].x;
                        resize_orig_y = windows[idx].y;
                        resize_orig_w = windows[idx].w;
                        resize_orig_h = windows[idx].h;
                        drag_ox = mouse_x;
                        drag_oy = mouse_y;
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        /* Left button release */
        if (!left && prev_left) {
            if (mode == MODE_RESIZE && drag_win >= 0 && drag_win < window_count) {
                /* Finalize resize: reallocate surface */
                resize_apply(drag_win, windows[drag_win].w, windows[drag_win].h);
            }
            mode = MODE_NONE;
        }

        /* Right button press */
        if (right && !prev_right) {
            if (wm_window_at(mouse_x, mouse_y) < 0 &&
                mouse_y < screen_h - TASKBAR_H) {
                menu_visible = 1;
                menu_x = mouse_x;
                menu_y = mouse_y;
                int mh = MENU_COUNT * MENU_ITEM_H + 4;
                if (menu_x + MENU_W > screen_w) menu_x = screen_w - MENU_W;
                if (menu_y + mh > screen_h - TASKBAR_H)
                    menu_y = screen_h - TASKBAR_H - mh;
            } else {
                menu_visible = 0;
            }
        }

        /* Dragging */
        if (mode == MODE_DRAG && drag_win >= 0 && drag_win < window_count) {
            windows[drag_win].x = mouse_x - drag_ox;
            windows[drag_win].y = mouse_y - drag_oy;
            if (windows[drag_win].y < 0) windows[drag_win].y = 0;
            if (windows[drag_win].y > screen_h - TASKBAR_H - TITLEBAR_H)
                windows[drag_win].y = screen_h - TASKBAR_H - TITLEBAR_H;
        }

        /* Resizing */
        if (mode == MODE_RESIZE && drag_win >= 0 && drag_win < window_count) {
            int dx = mouse_x - drag_ox;
            int dy = mouse_y - drag_oy;
            int new_x = resize_orig_x;
            int new_y = resize_orig_y;
            int new_w = resize_orig_w;
            int new_h = resize_orig_h;

            if (resize_edge == HIT_RESIZE_E || resize_edge == HIT_RESIZE_NE ||
                resize_edge == HIT_RESIZE_SE) {
                new_w += dx;
            }
            if (resize_edge == HIT_RESIZE_W || resize_edge == HIT_RESIZE_NW ||
                resize_edge == HIT_RESIZE_SW) {
                new_w -= dx;
                new_x += dx;
            }
            if (resize_edge == HIT_RESIZE_S || resize_edge == HIT_RESIZE_SE ||
                resize_edge == HIT_RESIZE_SW) {
                new_h += dy;
            }
            if (resize_edge == HIT_RESIZE_N || resize_edge == HIT_RESIZE_NE ||
                resize_edge == HIT_RESIZE_NW) {
                new_h -= dy;
                new_y += dy;
            }

            /* Enforce minimum size (visual only, surface reallocated on release) */
            if (new_w < 200) { new_w = 200; new_x = resize_orig_x; }
            if (new_h < 100) { new_h = 100; new_y = resize_orig_y; }
            if (new_w > screen_w) new_w = screen_w;
            if (new_h > screen_h - TASKBAR_H) new_h = screen_h - TASKBAR_H;

            windows[drag_win].x = new_x;
            windows[drag_win].y = new_y;
            windows[drag_win].w = new_w;
            windows[drag_win].h = new_h;
            windows[drag_win].content_w = new_w - 2 * BORDER_W;
            windows[drag_win].content_h = new_h - TITLEBAR_H - 2 * BORDER_W;
        }

        /* Update menu hover */
        if (menu_visible) {
            menu_hover = menu_item_at(mouse_x, mouse_y);
        }

        prev_buttons = ev.buttons;
    }
}

static void poll_keyboard(void) {
    char c;
    while (sys_read(kb_fd, &c, 1) == 1) {
        /* Escape: exit WM (unless terminal is focused) */
        if (c == 27) {
            if (focused_idx < 0 || !windows[focused_idx].term) {
                running = 0;
                return;
            }
        }

        /* Route to focused terminal */
        if (focused_idx >= 0 && focused_idx < window_count &&
            windows[focused_idx].term) {
            struct wm_terminal *t = windows[focused_idx].term;
            if (c == '\n') {
                term_exec_cmd(t, focused_idx);
                /* Window may have been closed by 'exit' */
                if (focused_idx >= 0 && focused_idx < window_count &&
                    windows[focused_idx].visible && windows[focused_idx].term)
                    term_render(&windows[focused_idx]);
            } else if (c == '\b') {
                if (t->cmd_len > 0) {
                    t->cmd_len--;
                    term_putchar(t, '\b');
                    term_render(&windows[focused_idx]);
                }
            } else if (c >= ' ' && c <= '~') {
                if (t->cmd_len < TERM_CMDLEN - 1) {
                    t->cmd_buf[t->cmd_len++] = c;
                    term_putchar(t, c);
                    term_render(&windows[focused_idx]);
                }
            }
        }
    }
}

/* ---- Entry point ---- */

void _start(void) {
    /* Init framebuffer */
    if (gfx_init(&fb) != 0) {
        print("wm: failed to open framebuffer\n");
        sys_exit(1);
    }

    screen_w = (int)fb.width;
    screen_h = (int)fb.height;

    /* Allocate back buffer */
    backbuf = gfx_surface_create((uint32_t)screen_w, (uint32_t)screen_h);
    if (!backbuf) {
        print("wm: failed to allocate backbuffer\n");
        gfx_fini(&fb);
        sys_exit(1);
    }

    /* No separate wallpaper surface needed — gradient rendered directly into backbuf */

    /* Open input devices */
    mouse_fd = (int)sys_open("/dev/input/mouse0", 0);
    kb_fd = (int)sys_open("/dev/input/keyboard", 0);

    /* Set non-blocking on input FDs */
    if (mouse_fd >= 0)
        sys_fcntl(mouse_fd, F_SETFL, O_NONBLOCK);
    if (kb_fd >= 0)
        sys_fcntl(kb_fd, F_SETFL, O_NONBLOCK);

    /* Flush stale keyboard/mouse input from before WM started */
    {
        char flush;
        while (sys_read(kb_fd, &flush, 1) == 1)
            ;
        struct mouse_event flush_ev;
        while (sys_read(mouse_fd, &flush_ev, sizeof(flush_ev)) == (int64_t)sizeof(flush_ev))
            ;
    }

    /* Center mouse */
    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;
    focused_idx = -1;
    mode = MODE_NONE;
    prev_buttons = 0;
    menu_visible = 0;
    menu_hover = -1;
    running = 1;
    current_workspace = 0;
    frame_counter = 0;

    /* Create initial terminal window */
    wm_create_terminal(80, 60, "Terminal");

    /* Event loop */
    uint64_t last_frame = sys_gettime();
    while (running) {
        poll_mouse();
        poll_keyboard();

        /* Auto-refresh task manager windows */
        frame_counter++;
        if ((frame_counter % 30) == 0) {
            for (int i = 0; i < window_count; i++) {
                if (windows[i].type == WINTYPE_TASKMGR && windows[i].visible &&
                    !windows[i].minimized && windows[i].workspace == current_workspace) {
                    taskmgr_refresh(&windows[i]);
                }
            }
        }

        compose();

        /* Throttle to ~30fps */
        uint64_t now = sys_gettime();
        uint64_t elapsed = now - last_frame;
        if (elapsed < 33) {
            while (sys_gettime() - last_frame < 33)
                ;
        }
        last_frame = sys_gettime();
    }

    /* Cleanup */
    if (mouse_fd >= 0) sys_close(mouse_fd);
    if (kb_fd >= 0) sys_close(kb_fd);
    gfx_fini(&fb);

    sys_exit(0);
}
