/* POSIX-shaped shims for the vendored TCC source, backed by HobbyOS's real
 * syscall wrappers (sys_*_libc, declared in user/lib/libc.h). Kept private
 * to the TCC build — no shared HobbyOS libc file depends on this. */
#include "../lib/libc.h"
#include "compat/sys/time.h"
#include "compat/time.h"

int errno;

/* ---- unistd.h / fcntl.h ---- */

int open(const char *path, int flags, ...) {
    /* Only O_CREAT paths pass a mode argument; HobbyOS has no notion of
     * file permission bits on ext2 writes yet, so it's read and ignored. */
    return (int)sys_open_libc(path, (uint32_t)flags);
}

int close(int fd) {
    return (int)sys_close_libc(fd);
}

int64_t read(int fd, void *buf, size_t count) {
    return sys_read_libc(fd, buf, count);
}

int64_t write(int fd, const void *buf, size_t count) {
    return sys_write_libc(fd, buf, count);
}

int64_t lseek(int fd, int64_t offset, int whence) {
    return sys_lseek_libc(fd, offset, whence);
}

int unlink(const char *path) {
    return (int)sys_unlink_libc(path);
}

char *getcwd(char *buf, size_t size) {
    /* Only reachable behind do_debug (stab debug-info emission) —
     * HobbyOS has no per-process working directory concept yet. */
    if (size < 2) return NULL;
    buf[0] = '/';
    buf[1] = '\0';
    return buf;
}

/* ---- sys/time.h ---- */

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    return 0;
}

/* ---- time.h ---- */

time_t time(time_t *tloc) {
    if (tloc) *tloc = 0;
    return 0;
}

struct tm *localtime(const time_t *timep) {
    (void)timep;
    static struct tm zeroed;
    return &zeroed;
}

/* ---- dlfcn.h ----
 * TCC uses these to link against / resolve symbols in real host .so files
 * (native linking) and its -run JIT mode — neither applies on HobbyOS. */

void *dlopen(const char *filename, int flag) {
    (void)filename; (void)flag;
    return (void *)0;
}

int dlclose(void *handle) {
    (void)handle;
    return 0;
}

const char *dlerror(void) {
    return "dynamic loading is not supported on HobbyOS";
}

void *dlsym(void *handle, const char *symbol) {
    (void)handle; (void)symbol;
    return (void *)0;
}

/* ---- stdio.h ---- */

int fflush(FILE *f) {
    /* fwrite() already goes straight through a write() syscall with no
     * internal buffering to flush. */
    (void)f;
    return 0;
}

int remove(const char *path) {
    return (int)sys_unlink_libc(path);
}

static void tcc_write_str(int fd, const char *s) {
    sys_write_libc(fd, s, strlen(s));
}

void perror(const char *s) {
    if (s && s[0]) {
        tcc_write_str(2, s);
        tcc_write_str(2, ": ");
    }
    tcc_write_str(2, "error\n");
}

int tcc_vfprintf(FILE *f, const char *fmt, __builtin_va_list ap) {
    char buf[1024];
    __builtin_va_list ap2;
    __builtin_va_copy(ap2, ap);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);

    if (n < 0) { __builtin_va_end(ap2); return n; }
    if ((size_t)n < sizeof(buf)) {
        fwrite(buf, 1, (size_t)n, f);
        __builtin_va_end(ap2);
        return n;
    }

    /* Truncated — retry once with a heap buffer sized exactly to the
     * true length vsnprintf reported. */
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) {
        fwrite(buf, 1, sizeof(buf) - 1, f);
        __builtin_va_end(ap2);
        return (int)(sizeof(buf) - 1);
    }
    int n2 = vsnprintf(big, (size_t)n + 1, fmt, ap2);
    __builtin_va_end(ap2);
    fwrite(big, 1, (size_t)n2, f);
    free(big);
    return n2;
}

int tcc_fprintf(FILE *f, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = tcc_vfprintf(f, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int tcc_fputs(const char *s, FILE *f) {
    size_t len = strlen(s);
    return (int)fwrite(s, 1, len, f);
}

int tcc_fputc(int c, FILE *f) {
    unsigned char ch = (unsigned char)c;
    if (fwrite(&ch, 1, 1, f) != 1) return -1;
    return c;
}

/* No length limit, matching real sprintf — callers are trusted to size
 * their own buffers, same as upstream TCC's usage. */
int sprintf(char *buf, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(buf, (size_t)0x7fffffff, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

/* Minimal sscanf: only supports "%d" and literal characters (its one
 * call site parses a "MAJOR.MINOR.PATCH" version string) — not a general
 * sscanf implementation. */
int sscanf(const char *str, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int matched = 0;
    while (*fmt) {
        if (fmt[0] == '%' && fmt[1] == 'd') {
            int neg = 0;
            int v = 0;
            int any = 0;
            if (*str == '-') { neg = 1; str++; }
            while (*str >= '0' && *str <= '9') {
                v = v * 10 + (*str - '0');
                str++;
                any = 1;
            }
            if (!any) break;
            *__builtin_va_arg(ap, int *) = neg ? -v : v;
            matched++;
            fmt += 2;
        } else if (*fmt == *str) {
            fmt++;
            str++;
        } else {
            break;
        }
    }
    __builtin_va_end(ap);
    return matched;
}
