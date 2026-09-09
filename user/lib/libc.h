#ifndef LIBC_H
#define LIBC_H

/* Types */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed int         int32_t;
typedef long long          int64_t;
typedef uint64_t           size_t;
typedef int64_t            ssize_t;

#define NULL ((void *)0)
#define bool _Bool
#define true 1
#define false 0

/* stdio */
int printf(const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int puts(const char *s);
int fputs(const char *s, int fd);
int fgets(char *buf, int size, int fd);

/* string */
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
char *strstr(const char *haystack, const char *needle);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
size_t strnlen(const char *s, size_t maxlen);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
char *strdup(const char *s);

/* stdlib */
int atoi(const char *s);
long long atol(const char *s);
void exit(int status);
void abort(void);
char *itoa(int value, char *buf, int base);
char *utoa(uint64_t value, char *buf, int base);
long strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
char *getenv(const char *name);

/* malloc */
void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);

/* Seek constants */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* stdio additions */
int fprintf(int fd, const char *fmt, ...);
int putchar(int c);
int fputc(int c, int fd);
int getchar(void);

/* syscall wrappers */
int64_t sys_write_libc(int fd, const void *buf, size_t len);
int64_t sys_read_libc(int fd, void *buf, size_t len);
int64_t sys_open_libc(const char *path, uint32_t flags);
int64_t sys_close_libc(int fd);
void    sys_exit(int status);
int64_t sys_fork_libc(void);
int64_t sys_execv_libc(const char *path, int argc, char **argv);
int64_t sys_wait_libc(int32_t *status);
int64_t sys_getpid_libc(void);
uint64_t sys_brk_libc(uint64_t new_brk);
void *sys_mmap_libc(void *addr, size_t len, uint32_t prot,
                     uint32_t flags, int32_t fd, uint64_t offset);
int64_t sys_munmap_libc(void *addr, size_t len);

/* stat structure */
struct stat_buf {
    uint32_t type;      /* STAT_FILE or STAT_DIR */
    uint32_t size;
    uint32_t inode;
};
#define STAT_FILE  1
#define STAT_DIR   2

/* New syscall wrappers */
int64_t sys_lseek_libc(int fd, int64_t offset, int whence);
int64_t sys_stat_libc(const char *path, struct stat_buf *sb);
int64_t sys_getdents_libc(int fd, void *buf, uint64_t size);
int64_t sys_dup2_libc(int oldfd, int newfd);
int64_t sys_pipe_libc(int *fds);
int64_t sys_kill_libc(int pid, int sig);
int64_t sys_waitpid_libc(int pid, int *status, int options);
int64_t sys_send_libc(int fd, const void *buf, size_t len, int flags);
int64_t sys_recv_libc(int fd, void *buf, size_t len, int flags);
uint64_t sys_gettime_libc(void);
int64_t sys_rename_libc(const char *old_path, const char *new_path);
int64_t sys_mkdir_libc(const char *path);
int64_t sys_unlink_libc(const char *path);

/* open() flags (mirrors user/syscall.h — libc.h and syscall.h are used by
 * disjoint programs, so duplicating these here is safe) */
#define O_RDONLY   0x00
#define O_WRONLY   0x01
#define O_RDWR     0x02
#define O_CREAT    0x40
#define O_TRUNC    0x200
#define O_APPEND   0x400

/* FILE* stdio layer */
typedef struct FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int fseek(FILE *f, int64_t offset, int whence);
int64_t ftell(FILE *f);
int feof(FILE *f);

#endif /* LIBC_H */
