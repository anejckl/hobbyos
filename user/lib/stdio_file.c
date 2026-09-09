#include "libc.h"

#define FILE_RBUF_SIZE 512

struct FILE {
    int fd;
    int eof;
    int error;
    int owns_fd;   /* 0 for stdin/stdout/stderr — fclose must not close fd 0/1/2 */
    unsigned char rbuf[FILE_RBUF_SIZE];
    int rbuf_pos;
    int rbuf_len;
};

static struct FILE stdin_file  = { 0, 0, 0, 0, {0}, 0, 0 };
static struct FILE stdout_file = { 1, 0, 0, 0, {0}, 0, 0 };
static struct FILE stderr_file = { 2, 0, 0, 0, {0}, 0, 0 };

FILE *stdin  = &stdin_file;
FILE *stdout = &stdout_file;
FILE *stderr = &stderr_file;

FILE *fopen(const char *path, const char *mode) {
    uint32_t flags;
    if (mode[0] == 'r') {
        flags = O_RDONLY;
    } else if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else {
        return NULL;
    }
    /* 'r+'/'w+'/'a+' - upgrade to read+write */
    if (mode[1] == '+') {
        flags = (flags & ~(uint32_t)(O_WRONLY)) | O_RDWR;
    }

    int fd = (int)sys_open_libc(path, flags);
    if (fd < 0) return NULL;

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        sys_close_libc(fd);
        return NULL;
    }
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    f->owns_fd = 1;
    f->rbuf_pos = 0;
    f->rbuf_len = 0;
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) return NULL;
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    f->owns_fd = 1;
    f->rbuf_pos = 0;
    f->rbuf_len = 0;
    return f;
}

int fclose(FILE *f) {
    if (!f) return -1;
    int r = 0;
    if (f->owns_fd) r = (int)sys_close_libc(f->fd);
    free(f);
    return r;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    unsigned char *out = (unsigned char *)ptr;
    size_t got = 0;

    while (got < total) {
        if (f->rbuf_pos >= f->rbuf_len) {
            int64_t r = sys_read_libc(f->fd, f->rbuf, FILE_RBUF_SIZE);
            if (r <= 0) {
                if (r == 0) f->eof = 1;
                else f->error = 1;
                break;
            }
            f->rbuf_len = (int)r;
            f->rbuf_pos = 0;
        }
        size_t avail = (size_t)(f->rbuf_len - f->rbuf_pos);
        size_t need = total - got;
        size_t take = avail < need ? avail : need;
        memcpy(out + got, f->rbuf + f->rbuf_pos, take);
        f->rbuf_pos += (int)take;
        got += take;
    }
    return got / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || size == 0 || nmemb == 0) return 0;
    size_t total = size * nmemb;
    int64_t w = sys_write_libc(f->fd, ptr, total);
    if (w < 0) {
        f->error = 1;
        return 0;
    }
    return (size_t)w / size;
}

int fseek(FILE *f, int64_t offset, int whence) {
    if (!f) return -1;
    /* Reading buffers ahead of the logical position, so a relative seek from
     * SEEK_CUR must first unwind the buffered-but-unread bytes. */
    if (whence == SEEK_CUR) {
        offset -= (int64_t)(f->rbuf_len - f->rbuf_pos);
    }
    int64_t r = sys_lseek_libc(f->fd, offset, whence);
    if (r < 0) return -1;
    f->rbuf_pos = 0;
    f->rbuf_len = 0;
    f->eof = 0;
    return 0;
}

int64_t ftell(FILE *f) {
    if (!f) return -1;
    int64_t fd_pos = sys_lseek_libc(f->fd, 0, SEEK_CUR);
    if (fd_pos < 0) return -1;
    return fd_pos - (int64_t)(f->rbuf_len - f->rbuf_pos);
}

int feof(FILE *f) {
    if (!f) return 1;
    return f->eof && f->rbuf_pos >= f->rbuf_len;
}
