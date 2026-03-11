#include "syscall.h"

static int sh_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void sh_write(const char *s) { sys_write(1, s, (uint64_t)sh_strlen(s)); }
static int sh_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void sh_strncpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

int _start(void) {
    char line[256];
    while (1) {
        sys_write(1, "$ ", 2);
        /* Read a line */
        int len = 0;
        while (len < 255) {
            char c;
            int64_t r = sys_read(0, &c, 1);
            if (r <= 0) { sys_exit(0); }
            if (c == '\n' || c == '\r') break;
            line[len++] = c;
        }
        line[len] = '\0';
        if (len == 0) continue;
        if (sh_strcmp(line, "exit") == 0) break;

        /* Copy line to token buffer */
        char tok_buf[256];
        sh_strncpy(tok_buf, line, sizeof(tok_buf));

        /* Parse tokens */
        char *tokens[32];
        int ntok = 0;
        char *p = tok_buf;
        while (*p && ntok < 31) {
            while (*p == ' ') p++;
            if (!*p) break;
            tokens[ntok++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
        }
        tokens[ntok] = NULL;
        if (ntok == 0) continue;

        /* Build /bin/<cmd> path */
        char path[64];
        if (tokens[0][0] == '/') {
            /* Absolute path */
            int pi = 0;
            while (tokens[0][pi] && pi < 62) { path[pi] = tokens[0][pi]; pi++; }
            path[pi] = '\0';
        } else {
            /* Prepend /bin/ */
            const char *prefix = "/bin/";
            int pi = 0;
            for (int i = 0; prefix[i]; i++) path[pi++] = prefix[i];
            for (int i = 0; tokens[0][i] && pi < 62; i++) path[pi++] = tokens[0][i];
            path[pi] = '\0';
        }

        int64_t pid = sys_fork();
        if (pid == 0) {
            sys_execv(path, ntok, tokens);
            sh_write("sh: command not found\n");
            sys_exit(1);
        } else if (pid > 0) {
            int32_t status = 0;
            sys_waitpid((int32_t)pid, &status, 0);
        } else {
            sh_write("sh: fork failed\n");
        }
    }
    sys_exit(0);
    return 0;
}
