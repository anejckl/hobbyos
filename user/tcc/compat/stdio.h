/* HobbyOS compat shim for TCC: stdio.h.
 * FILE/stdin/stdout/stderr/fopen/fdopen/fclose/fread/fwrite/fseek/ftell/
 * feof/printf/snprintf/vsnprintf all already exist with matching POSIX
 * signatures in the shared HobbyOS libc.
 *
 * fprintf/vfprintf do NOT reuse the shared libc's names: user/lib/stdio.c
 * already defines `int fprintf(int fd, const char *fmt, ...)` (fd-based,
 * used by other programs) which would collide at link time with TCC's
 * POSIX-shaped `fprintf(FILE*, ...)`. Route through renamed symbols
 * implemented in user/tcc/hobbyos_compat.c instead. */
#ifndef HOBBYOS_TCC_STDIO_H
#define HOBBYOS_TCC_STDIO_H

#include "../../lib/libc.h"

#define fprintf  tcc_fprintf
#define vfprintf tcc_vfprintf
#define fputs    tcc_fputs
#define fputc    tcc_fputc

int tcc_fprintf(FILE *f, const char *fmt, ...);
int tcc_vfprintf(FILE *f, const char *fmt, __builtin_va_list ap);
int tcc_fputs(const char *s, FILE *f);
int tcc_fputc(int c, FILE *f);
int fflush(FILE *f);
int remove(const char *path);
void perror(const char *s);
int sprintf(char *buf, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);

#endif
