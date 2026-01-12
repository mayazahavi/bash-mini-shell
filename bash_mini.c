/*
 * bash_mini.c
 * System Programming – HW3: Bash-Mini Shell
 *
 * Core requirements:
 *  - Infinite loop: Prompt -> Read -> Parse -> Execute
 *  - Internal commands: exit, cd (cd uses chdir)
 *  - External commands search order: $HOME first, then /bin
 *  - Must verify executable permission (X_OK), not just existence
 *  - Execute external command via fork + exec + wait
 *  - After child ends: print "command executed successfully" message and show Return Code
 *  - If exec fails: use perror() to print kernel error (as required)
 *  - No threads, no system(), no GUI libs
 *  - Efficient buffer handling; avoid unnecessary string copies
 */

#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     // read, write, chdir, access, fork, execv, _exit
#include <sys/wait.h>   // waitpid, WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG
#include <sys/types.h>  // pid_t
#include <string.h>     // strlen, memcpy, strcmp
#include <stdlib.h>     // getenv, malloc, realloc, free
#include <stdio.h>      // perror (required for exec failure reporting)

#define PROMPT        "bash-mini$ "
#define INITIAL_BUFSZ 1024
#define MAX_ARGS      128

/* --------------------- Small I/O helpers ------------------ */

/* write(2) may do partial writes; loop until all bytes are written. */
static void write_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t w = write(fd, buf, len);
        if (w < 0) {
            _exit(1); // fatal I/O error
        }
        buf += (size_t)w;
        len -= (size_t)w;
    }
}

static void print_str(int fd, const char *s) {
    write_all(fd, s, strlen(s));
}

/* Convert non-negative integer to decimal string. */
static void u32_to_str(unsigned int x, char out[32]) {
    int i = 0;
    if (x == 0) {
        out[i++] = '0';
        out[i] = '\0';
        return;
    }
    char tmp[32];
    int t = 0;
    while (x > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (x % 10));
        x /= 10;
    }
    while (t > 0) out[i++] = tmp[--t];
    out[i] = '\0';
}

/* --------------------- Line reading ----------------------- */
/*
 * Read a full line from stdin using read(2).
 * Returns:
 *   1 on success (newline removed)
 *   0 on EOF with no data
 *  -1 on error
 */
static int read_line(char **line, size_t *cap) {
    if (*line == NULL || *cap == 0) {
        *cap = INITIAL_BUFSZ;
        *line = (char *)malloc(*cap);
        if (!*line) return -1;
    }

    size_t len = 0;

    for (;;) {
        if (len + 2 > *cap) {
            size_t new_cap = (*cap) * 2;
            char *tmp = (char *)realloc(*line, new_cap);
            if (!tmp) return -1;
            *line = tmp;
            *cap = new_cap;
        }

        char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r < 0) return -1;

        if (r == 0) { // EOF
            if (len == 0) return 0;
            break; // return partial line
        }

        if (c == '\n') break;
        (*line)[len++] = c;
    }

    (*line)[len] = '\0';
    return 1;
}

/* --------------------- Parsing ---------------------------- */
/* In-place tokenization by spaces/tabs. */
static int parse_tokens(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        if (argc >= max_args - 1) break;
        argv[argc++] = p;

        while (*p != '\0' && *p != ' ' && *p != '\t') p++;

        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
}

/* --------------------- Command search ---------------------- */
/* Must verify executable permission using access(path, X_OK). */
static int is_executable(const char *path) {
    return (access(path, X_OK) == 0);
}

/*
 * Search order required:
 *  1) $HOME/<cmd>
 *  2) /bin/<cmd>
 */
static int find_command_path(const char *cmd, char *out_path, size_t out_sz) {
    const char *home = getenv("HOME");

    if (home && home[0] != '\0') {
        size_t need = strlen(home) + 1 + strlen(cmd) + 1;
        if (need <= out_sz) {
            size_t n = 0;
            memcpy(out_path + n, home, strlen(home));
            n += strlen(home);
            out_path[n++] = '/';
            memcpy(out_path + n, cmd, strlen(cmd));
            n += strlen(cmd);
            out_path[n] = '\0';

            if (is_executable(out_path)) return 1;
        }
    }

    {
        const char *bin = "/bin";
        size_t need = strlen(bin) + 1 + strlen(cmd) + 1;
        if (need <= out_sz) {
            size_t n = 0;
            memcpy(out_path + n, bin, strlen(bin));
            n += strlen(bin);
            out_path[n++] = '/';
            memcpy(out_path + n, cmd, strlen(cmd));
            n += strlen(cmd);
            out_path[n] = '\0';

            if (is_executable(out_path)) return 1;
        }
    }

    return 0;
}

/* --------------------- Internal commands ------------------ */
/* cd must be internal and use chdir(2). */
static void handle_cd(int argc, char *argv[]) {
    const char *target;

    if (argc < 2) {
        target = getenv("HOME");
        if (!target || target[0] == '\0') {
            print_str(STDERR_FILENO, "cd: HOME not set\n");
            return;
        }
    } else {
        target = argv[1];
    }

    if (chdir(target) != 0) {
        perror("cd");
    }
}

/* --------------------- External execution ----------------- */

static void print_unknown_command(const char *cmd) {
    /* Required format from the assignment PDF:
       ]command_name]: Unknown Command[
    */
    print_str(STDERR_FILENO, "]");
    print_str(STDERR_FILENO, cmd);
    print_str(STDERR_FILENO, "]: Unknown Command[\n");
}

static void execute_external(char *argv[]) {
    char path_buf[4096];

    if (!find_command_path(argv[0], path_buf, sizeof(path_buf))) {
        print_unknown_command(argv[0]);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        execv(path_buf, argv);
        /* If execv returns -> failure: must use perror() */
        perror("exec");
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return;
    }

    /* Assignment requires printing a success message + the return code after child ends. */
    print_str(STDOUT_FILENO, "Command executed successfully.\n");

    if (WIFEXITED(status)) {
        unsigned int rc = (unsigned int)WEXITSTATUS(status);
        char num[32];
        u32_to_str(rc, num);

        print_str(STDOUT_FILENO, "Command finished. Return code: ");
        print_str(STDOUT_FILENO, num);
        print_str(STDOUT_FILENO, "\n");
    } else if (WIFSIGNALED(status)) {
        unsigned int sig = (unsigned int)WTERMSIG(status);
        char num[32];
        u32_to_str(sig, num);

        print_str(STDOUT_FILENO, "Command terminated by signal: ");
        print_str(STDOUT_FILENO, num);
        print_str(STDOUT_FILENO, "\n");
    } else {
        print_str(STDOUT_FILENO, "Command finished with unknown status.\n");
    }
}

/* ------------------------------ main ------------------------------ */

int main(void) {
    char *line = NULL;
    size_t cap = 0;

    for (;;) {
        print_str(STDOUT_FILENO, PROMPT);

        int rr = read_line(&line, &cap);
        if (rr < 0) {
            perror("read");
            break;
        }
        if (rr == 0) { // EOF
            print_str(STDOUT_FILENO, "\n");
            break;
        }

        if (line[0] == '\0') continue;

        char *argv[MAX_ARGS];
        int argc = parse_tokens(line, argv, MAX_ARGS);
        if (argc == 0) continue;

        if (strcmp(argv[0], "exit") == 0) {
            break;
        } else if (strcmp(argv[0], "cd") == 0) {
            handle_cd(argc, argv);
        } else {
            execute_external(argv);
        }
    }

    free(line);
    return 0;
}
