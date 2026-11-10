#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 128
#define MAX_CMDS 32

typedef struct {
    char *argv[MAX_ARGS];
    char *input;
    char *output;
    int append;
    int argc;
} Command;

typedef struct {
    Command cmds[MAX_CMDS];
    int count;
    int background;
} Pipeline;

static void free_tokens(char **tokens, int n) {
    for (int i = 0; i < n; i++) free(tokens[i]);
}

static int add_token(char **tokens, int *n, const char *start, size_t len) {
    if (*n >= 512) { fprintf(stderr, "minish: too many tokens\n"); return -1; }
    tokens[*n] = strndup(start, len);
    if (!tokens[*n]) { perror("strndup"); return -1; }
    ++*n;
    return 0;
}

/* Tokenizer: supports single/double quotes, backslash escapes, and operators. */
static int tokenize(const char *line, char **tokens, int *count) {
    int n = 0;
    const char *p = line;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (strchr("|<>&", *p)) {
            size_t len = (*p == '>' && p[1] == '>') ? 2 : 1;
            if (add_token(tokens, &n, p, len) < 0) goto fail;
            p += len;
            continue;
        }
        char *word = malloc(strlen(p) + 1);
        if (!word) { perror("malloc"); goto fail; }
        size_t k = 0;
        char quote = 0;
        int started = 0;
        while (*p) {
            if (!quote && (isspace((unsigned char)*p) || strchr("|<>&", *p))) break;
            started = 1;
            if (*p == '\\' && p[1] && quote != '\'') {
                p++;
                word[k++] = *p++;
            } else if (*p == '\'' && quote != '"') {
                quote = (quote == '\'') ? 0 : '\'';
                p++;
            } else if (*p == '"' && quote != '\'') {
                quote = (quote == '"') ? 0 : '"';
                p++;
            } else {
                word[k++] = *p++;
            }
        }
        if (quote) {
            fprintf(stderr, "minish: unmatched quote\n");
            free(word);
            goto fail;
        }
        if (started) {
            word[k] = '\0';
            if (n >= 512) { fprintf(stderr, "minish: too many tokens\n"); free(word); goto fail; }
            tokens[n++] = word;
        } else free(word);
    }
    *count = n;
    return 0;
fail:
    free_tokens(tokens, n);
    *count = 0;
    return -1;
}

static int parse(char **tokens, int n, Pipeline *pl) {
    memset(pl, 0, sizeof(*pl));
    pl->count = 1;
    int current = 0;
    for (int i = 0; i < n; i++) {
        char *t = tokens[i];
        Command *c = &pl->cmds[current];
        if (!strcmp(t, "|")) {
            if (c->argc == 0 || current + 1 >= MAX_CMDS) goto syntax;
            c->argv[c->argc] = NULL;
            current++;
            pl->count++;
        } else if (!strcmp(t, "<") || !strcmp(t, ">") || !strcmp(t, ">>")) {
            if (i + 1 >= n || !strcmp(tokens[i+1], "|") || !strcmp(tokens[i+1], "&") ||
                !strcmp(tokens[i+1], "<") || !strcmp(tokens[i+1], ">") || !strcmp(tokens[i+1], ">>")) goto syntax;
            char *filename = tokens[++i];
            if (!strcmp(t, "<")) c->input = filename;
            else { c->output = filename; c->append = !strcmp(t, ">>"); }
        } else if (!strcmp(t, "&")) {
            if (i != n - 1) goto syntax;
            pl->background = 1;
        } else {
            if (c->argc >= MAX_ARGS - 1) goto syntax;
            c->argv[c->argc++] = t;
            c->argv[c->argc] = NULL;
        }
    }
    if (pl->cmds[current].argc == 0) goto syntax;
    return 0;
syntax:
    fprintf(stderr, "minish: invalid command syntax\n");
    return -1;
}

static int builtin(Command *c) {
    const char *name = c->argv[0];
    if (!strcmp(name, "cd")) {
        if (c->argc > 2) { fprintf(stderr, "cd: too many arguments\n"); return 1; }
        const char *dir = c->argc == 2 ? c->argv[1] : getenv("HOME");
        if (!dir) { fprintf(stderr, "cd: HOME not set\n"); return 1; }
        if (chdir(dir) < 0) { perror("cd"); return 1; }
        return 0;
    }
    if (!strcmp(name, "pwd")) {
        char *cwd = getcwd(NULL, 0);
        if (!cwd) { perror("pwd"); return 1; }
        puts(cwd);
        free(cwd);
        return 0;
    }
    if (!strcmp(name, "help")) {
        puts("minish built-ins: cd [dir], pwd, help, exit [status]");
        puts("Supports: commands, pipes |, redirection < > >>, background &");
        return 0;
    }
    return -1;
}

static int redirect_io(Command *c) {
    if (c->input) {
        int fd = open(c->input, O_RDONLY);
        if (fd < 0) { perror(c->input); return -1; }
        if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
        close(fd);
    }
    if (c->output) {
        int flags = O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC);
        int fd = open(c->output, flags, 0666);
        if (fd < 0) { perror(c->output); return -1; }
        if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2"); close(fd); return -1; }
        close(fd);
    }
    return 0;
}

static void reap_background(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        fprintf(stderr, "[background pid %ld finished]\n", (long)pid);
}

static int execute(Pipeline *pl) {
    if (pl->count == 1 && !pl->background && !pl->cmds[0].input && !pl->cmds[0].output) {
        Command *c = &pl->cmds[0];
        if (!strcmp(c->argv[0], "exit")) {
            int code = 0;
            if (c->argc > 2) { fprintf(stderr, "exit: too many arguments\n"); return 1; }
            if (c->argc == 2) {
                char *end;
                errno = 0;
                long value = strtol(c->argv[1], &end, 10);
                if (errno || *end) { fprintf(stderr, "exit: numeric argument required\n"); return 2; }
                code = (unsigned char)value;
            }
            exit(code);
        }
        int result = builtin(c);
        if (result >= 0) return result;
    }

    int prev_read = -1;
    pid_t pids[MAX_CMDS];
    int launched = 0;
    for (int i = 0; i < pl->count; i++) {
        int pipefd[2] = {-1, -1};
        if (i + 1 < pl->count && pipe(pipefd) < 0) { perror("pipe"); break; }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            if (pipefd[0] != -1) { close(pipefd[0]); close(pipefd[1]); }
            break;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            if (prev_read != -1) {
                if (dup2(prev_read, STDIN_FILENO) < 0) { perror("dup2"); _exit(1); }
            }
            if (pipefd[1] != -1) {
                if (dup2(pipefd[1], STDOUT_FILENO) < 0) { perror("dup2"); _exit(1); }
            }
            if (prev_read != -1) close(prev_read);
            if (pipefd[0] != -1) close(pipefd[0]);
            if (pipefd[1] != -1) close(pipefd[1]);
            Command *c = &pl->cmds[i];
            if (redirect_io(c) < 0) _exit(1);
            if (!strcmp(c->argv[0], "exit")) _exit(0);
            int result = builtin(c);
            if (result >= 0) { fflush(stdout); _exit(result); }
            execvp(c->argv[0], c->argv);
            perror(c->argv[0]);
            _exit(errno == ENOENT ? 127 : 126);
        }
        pids[launched++] = pid;
        if (prev_read != -1) close(prev_read);
        if (pipefd[1] != -1) close(pipefd[1]);
        prev_read = pipefd[0];
    }
    if (prev_read != -1) close(prev_read);
    if (pl->background) {
        for (int i = 0; i < launched; i++) printf("[background pid %ld]\n", (long)pids[i]);
        return 0;
    }
    int result = 0;
    for (int i = 0; i < launched; i++) {
        int status;
        pid_t w;
        do { w = waitpid(pids[i], &status, 0); } while (w < 0 && errno == EINTR);
        if (w < 0) { perror("waitpid"); result = 1; }
        else if (i == launched - 1) {
            if (WIFEXITED(status)) result = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) result = 128 + WTERMSIG(status);
        }
    }
    return result;
}

int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    char *line = NULL;
    size_t cap = 0;
    int interactive = isatty(STDIN_FILENO);
    while (1) {
        reap_background();
        if (interactive) { fputs("minish> ", stdout); fflush(stdout); }
        errno = 0;
        ssize_t len = getline(&line, &cap, stdin);
        if (len < 0) {
            if (ferror(stdin) && errno == EINTR) { clearerr(stdin); continue; }
            if (interactive) putchar('\n');
            break;
        }
        char *tokens[512] = {0};
        int n = 0;
        if (tokenize(line, tokens, &n) < 0) continue;
        if (n > 0) {
            Pipeline pl;
            if (parse(tokens, n, &pl) == 0) execute(&pl);
        }
        free_tokens(tokens, n);
    }
    free(line);
    return 0;
}
