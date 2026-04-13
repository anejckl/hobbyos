#include "syscall.h"

/* ---- String helpers ---- */
static int sh_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void sh_err(const char *s) { sys_write(2, s, (uint64_t)sh_strlen(s)); }

static int sh_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void sh_strncpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

/* ---- Data structures ---- */
#define MAX_CMDS   8
#define MAX_ARGS  16
#define MAX_LINE 256

struct command {
    char *argv[MAX_ARGS + 1];   /* NULL-terminated */
    int argc;
    char *redir_in;             /* < filename */
    char *redir_out;            /* > or >> filename */
    int append;                 /* 1 if >>, 0 if > */
};

struct pipeline {
    struct command cmds[MAX_CMDS];
    int ncmds;
};

/* Chaining operators between pipeline segments */
#define OP_NONE  0
#define OP_SEMI  1   /* ; */
#define OP_AND   2   /* && */
#define OP_OR    3   /* || */

#define MAX_SEGMENTS 8

struct segment {
    char *start;
    int op;    /* operator AFTER this segment (OP_NONE for last) */
};

/* ---- Parsing ---- */

/* Split line into segments by ;, &&, || */
static int split_segments(char *line, struct segment *segs) {
    int n = 0;
    segs[0].start = line;
    segs[0].op = OP_NONE;

    char *p = line;
    while (*p && n < MAX_SEGMENTS - 1) {
        if (*p == '&' && *(p + 1) == '&') {
            *p = '\0';
            segs[n].op = OP_AND;
            n++;
            segs[n].start = p + 2;
            segs[n].op = OP_NONE;
            p += 2;
        } else if (*p == '|' && *(p + 1) == '|') {
            *p = '\0';
            segs[n].op = OP_OR;
            n++;
            segs[n].start = p + 2;
            segs[n].op = OP_NONE;
            p += 2;
        } else if (*p == ';') {
            *p = '\0';
            segs[n].op = OP_SEMI;
            n++;
            segs[n].start = p + 1;
            segs[n].op = OP_NONE;
            p++;
        } else {
            p++;
        }
    }
    return n + 1;
}

/* Parse a single pipeline segment: split by |, then tokenize each command */
static int parse_pipeline(char *text, struct pipeline *pl) {
    pl->ncmds = 0;

    /* Split by | (but not ||, which was already consumed) */
    char *parts[MAX_CMDS];
    int nparts = 0;
    parts[nparts++] = text;
    char *p = text;
    while (*p && nparts < MAX_CMDS) {
        /* Skip || since those are already removed by segment splitting */
        if (*p == '|' && *(p + 1) != '|') {
            *p = '\0';
            parts[nparts++] = p + 1;
            p++;
        } else {
            p++;
        }
    }

    for (int i = 0; i < nparts; i++) {
        struct command *cmd = &pl->cmds[i];
        cmd->argc = 0;
        cmd->redir_in = NULL;
        cmd->redir_out = NULL;
        cmd->append = 0;

        /* Tokenize by spaces */
        p = parts[i];
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;

            /* Check for >> (must check before >) */
            if (*p == '>' && *(p + 1) == '>') {
                p += 2;
                while (*p == ' ') p++;
                cmd->redir_out = p;
                cmd->append = 1;
                while (*p && *p != ' ') p++;
                if (*p) *p++ = '\0';
                continue;
            }
            /* Check for > */
            if (*p == '>') {
                p++;
                while (*p == ' ') p++;
                cmd->redir_out = p;
                cmd->append = 0;
                while (*p && *p != ' ') p++;
                if (*p) *p++ = '\0';
                continue;
            }
            /* Check for < */
            if (*p == '<') {
                p++;
                while (*p == ' ') p++;
                cmd->redir_in = p;
                while (*p && *p != ' ') p++;
                if (*p) *p++ = '\0';
                continue;
            }

            /* Regular token */
            if (cmd->argc < MAX_ARGS) {
                cmd->argv[cmd->argc++] = p;
            }
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
        }
        cmd->argv[cmd->argc] = NULL;
    }

    pl->ncmds = nparts;
    return nparts;
}

/* ---- Path resolution ---- */
static void resolve_path(const char *name, char *pathbuf, int size) {
    if (name[0] == '/') {
        sh_strncpy(pathbuf, name, size);
    } else {
        const char *prefix = "/bin/";
        int pi = 0;
        for (int i = 0; prefix[i] && pi < size - 1; i++) pathbuf[pi++] = prefix[i];
        for (int i = 0; name[i] && pi < size - 1; i++) pathbuf[pi++] = name[i];
        pathbuf[pi] = '\0';
    }
}

/* ---- Redirect setup (in child, before exec) ---- */
static void setup_redirects(struct command *cmd) {
    if (cmd->redir_in) {
        int64_t fd = sys_open(cmd->redir_in, O_RDONLY);
        if (fd < 0) {
            sh_err("sh: cannot open input file\n");
            sys_exit(1);
        }
        sys_dup2((int)fd, 0);
        sys_close((int)fd);
    }
    if (cmd->redir_out) {
        uint64_t flags = O_CREAT | O_WRONLY;
        flags |= cmd->append ? O_APPEND : O_TRUNC;
        int64_t fd = sys_open(cmd->redir_out, flags);
        if (fd < 0) {
            sh_err("sh: cannot open output file\n");
            sys_exit(1);
        }
        sys_dup2((int)fd, 1);
        sys_close((int)fd);
    }
}

/* ---- Pipeline execution ---- */
static int execute_pipeline(struct pipeline *pl) {
    if (pl->ncmds == 0 || pl->cmds[0].argc == 0)
        return 0;

    int n = pl->ncmds;

    /* Single command — no pipes needed */
    if (n == 1) {
        /* Built-in: exit */
        if (sh_strcmp(pl->cmds[0].argv[0], "exit") == 0)
            sys_exit(0);

        char path[64];
        resolve_path(pl->cmds[0].argv[0], path, sizeof(path));

        int64_t pid = sys_fork();
        if (pid == 0) {
            setup_redirects(&pl->cmds[0]);
            sys_execv(path, pl->cmds[0].argc, pl->cmds[0].argv);
            sh_err("sh: command not found: ");
            sh_err(pl->cmds[0].argv[0]);
            sh_err("\n");
            sys_exit(127);
        } else if (pid > 0) {
            int32_t status = 0;
            sys_waitpid((int32_t)pid, &status, 0);
            return status;
        } else {
            sh_err("sh: fork failed\n");
            return 1;
        }
    }

    /* Multi-command pipeline */
    int pipefds[MAX_CMDS - 1][2];
    int64_t pids[MAX_CMDS];

    /* Create all pipes */
    for (int i = 0; i < n - 1; i++) {
        if (sys_pipe(pipefds[i]) < 0) {
            sh_err("sh: pipe failed\n");
            return 1;
        }
    }

    /* Fork all children */
    for (int i = 0; i < n; i++) {
        pids[i] = sys_fork();
        if (pids[i] == 0) {
            /* Child i */
            /* Wire up stdin from previous pipe */
            if (i > 0) {
                sys_dup2(pipefds[i - 1][0], 0);
            }
            /* Wire up stdout to next pipe */
            if (i < n - 1) {
                sys_dup2(pipefds[i][1], 1);
            }
            /* Close ALL pipe fds */
            for (int j = 0; j < n - 1; j++) {
                sys_close(pipefds[j][0]);
                sys_close(pipefds[j][1]);
            }
            /* File redirects override pipe ends */
            setup_redirects(&pl->cmds[i]);

            char path[64];
            resolve_path(pl->cmds[i].argv[0], path, sizeof(path));
            sys_execv(path, pl->cmds[i].argc, pl->cmds[i].argv);
            sh_err("sh: command not found: ");
            sh_err(pl->cmds[i].argv[0]);
            sh_err("\n");
            sys_exit(127);
        } else if (pids[i] < 0) {
            sh_err("sh: fork failed\n");
        }
    }

    /* Parent: close all pipe fds */
    for (int i = 0; i < n - 1; i++) {
        sys_close(pipefds[i][0]);
        sys_close(pipefds[i][1]);
    }

    /* Wait for all children, return last child's status */
    int32_t last_status = 0;
    for (int i = 0; i < n; i++) {
        if (pids[i] > 0) {
            int32_t status = 0;
            sys_waitpid((int32_t)pids[i], &status, 0);
            if (i == n - 1) last_status = status;
        }
    }

    return last_status;
}

/* ---- Main loop ---- */
int _start(void) {
    char line[MAX_LINE];

    while (1) {
        sys_write(1, "$ ", 2);

        /* Read a line */
        int len = 0;
        while (len < MAX_LINE - 1) {
            char c;
            int64_t r = sys_read(0, &c, 1);
            if (r <= 0) sys_exit(0);
            if (c == '\n' || c == '\r') break;
            line[len++] = c;
        }
        line[len] = '\0';
        if (len == 0) continue;

        /* Built-in: exit (before parsing) */
        if (sh_strcmp(line, "exit") == 0)
            break;

        /* Make a mutable copy for parsing */
        char buf[MAX_LINE];
        sh_strncpy(buf, line, sizeof(buf));

        /* Split into chained segments */
        struct segment segs[MAX_SEGMENTS];
        int nsegs = split_segments(buf, segs);

        int last_status = 0;
        for (int i = 0; i < nsegs; i++) {
            /* Check chaining condition (operator before this segment) */
            if (i > 0) {
                int prev_op = segs[i - 1].op;
                if (prev_op == OP_AND && last_status != 0) continue;
                if (prev_op == OP_OR && last_status == 0) continue;
            }

            struct pipeline pl;
            parse_pipeline(segs[i].start, &pl);

            if (pl.ncmds > 0 && pl.cmds[0].argc > 0) {
                last_status = execute_pipeline(&pl);
            }
        }
    }

    sys_exit(0);
    return 0;
}
