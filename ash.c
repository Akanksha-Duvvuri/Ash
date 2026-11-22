#define _POSIX_C_SOURCE 200809L
#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<errno.h>
#include<stdlib.h>
#include<fcntl.h>
#include<sys/stat.h>
#include<signal.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64

volatile sig_atomic_t childSignal = 0;

void handleSIGCHLD(int signalNumber){
    (void)signalNumber;
    childSignal = 1;
}

int setupSignals(){
    struct sigaction sa = {0};
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);

    if(sigaction(SIGINT, &sa, NULL) == -1){
        perror("sigaction SIGINT");
        return -1;
    }

    struct sigaction sc = {0};
    sc.sa_handler = handleSIGCHLD;
    sigemptyset(&sc.sa_mask);
    sc.sa_flags = SA_NOCLDSTOP;

    if(sigaction(SIGCHLD, &sc, NULL) == -1){
        perror("sigaction SIGCHLD");
        return -1;
    }

    return 0;
}

void resetChildSignals(){
    struct sigaction sa = {0};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);

    if(sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("reset child signals");
        _exit(1);
    }
}

int takeInput(char input[]){
    printf("ash> ");
    fflush(stdout);

    errno = 0;
    if(fgets(input, MAX_INPUT, stdin) == NULL){
        if(ferror(stdin)){
            if(errno == EINTR){
                clearerr(stdin);
                return -1;
            }
            perror("input");
            return 0;
        }
        return 0;
    }

    if(strchr(input, '\n') == NULL && strlen(input) == MAX_INPUT - 1){
        int ch;
        while((ch = getchar()) != '\n' && ch != EOF){}
        fprintf(stderr, "input too long\n");
        return -1;
    }

    input[strcspn(input, "\n")] = '\0';

    return 1;
}

void parseInput(char input[], char *args[]){
    int i = 0;

    char *token = strtok(input, " \t");

    while(token != NULL && i < MAX_ARGS - 1){
        args[i] = token;
        i++;

        token = strtok(NULL, " \t");
    }

    args[i] = NULL;
}

int handleRedirection(char *args[]){
    for(int i=0; args[i] != NULL; i++){
        int fd = -1;

        int target = -1;

        if(strcmp(args[i], ">") == 0){
            target = STDOUT_FILENO;

            if(args[i+1] == NULL){
                fprintf(stderr, "missing filename\n");
                return -1;
            }

            fd = open(args[i + 1],
                O_WRONLY | O_CREAT | O_TRUNC,
                0644
            );
        }else if(strcmp(args[i], ">>") == 0){
            target = STDOUT_FILENO;

            if(args[i + 1] == NULL){
                fprintf(stderr, "missing filename\n");
                return -1;
            }
            fd = open(args[i + 1],
                    O_WRONLY | O_CREAT | O_APPEND,
                    0644);
        }else if (strcmp(args[i], "<") == 0) {
            target = STDIN_FILENO;

            if (args[i + 1] == NULL) {
                fprintf(stderr, "missing filename\n");
                return -1;
            }

            fd = open(args[i + 1], O_RDONLY);
        } else {
            continue;
        }

        if(fd == -1){
            perror("redirection");
            return -1;
        }

        if(dup2(fd, target) == -1){
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);

        args[i] = NULL;
        break;
    }

    return 0;
}

void changeDirectory(char *args[]){
    char *path;

    if(args[1] == NULL){
        path = getenv("HOME");

        if(path == NULL){
            fprintf(stderr, "Home not set\n");
            return;
        }
    }else {
        path = args[1];
    }

    if(chdir(path) != 0){
        perror("cd");
    }
}

void printWorkingDirectory(){
    char cwd[1024];

    if(getcwd(cwd, sizeof(cwd)) == NULL){
        perror("pwd");
        return;
    }

    printf("%s\n", cwd);
}

void printHelp(){
    printf("Built in commands that are supported by this shell\n");
    printf("cd, pwd, help, exit\n");

    printf("External Commands supported by the shell: \n");
    printf("ls, date, echo, cat, gcc, etc...\n");

    printf("The commands are executed using fork() and execvp() \n");
}

void executeCommand(char *args[], int background) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        resetChildSignals();

        if (background) {
            if(setpgid(0, 0) == -1){
                perror("setpgid");
                _exit(1);
            }

            int fd = open("/dev/null", O_RDONLY);

            if (fd == -1) {
                perror("/dev/null");
                _exit(1);
            }

            if (dup2(fd, STDIN_FILENO) == -1) {
                perror("dup2");
                close(fd);
                _exit(1);
            }

            close(fd);
        }

        if (handleRedirection(args) == -1) {
            _exit(1);
        }

        execvp(args[0], args);
        perror("execvp");
        _exit(127);
    }

    if (background) {
        if(setpgid(pid, pid) == -1 && errno != EACCES && errno != ESRCH){
            perror("setpgid");
        }
        printf("Background process started: PID %ld\n",(long)pid);
        return;
    }

    int status;
    pid_t result;

    do {
        result = waitpid(pid, &status, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
    perror("waitpid");
    }else if(WIFSIGNALED(status) && WTERMSIG(status) == SIGINT){
        printf("\n");
        fflush(stdout);
    }
}

void executePipe(char *args[]) {
    int pipeIndex = -1;

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            pipeIndex = i;
            break;
        }
    }

    if (pipeIndex <= 0 || args[pipeIndex + 1] == NULL || strcmp(args[pipeIndex + 1], "|") == 0 || strcmp(args[pipeIndex + 1], ">") == 0 || strcmp(args[pipeIndex + 1], ">>") == 0 || strcmp(args[pipeIndex + 1], "<") == 0) {
        fprintf(stderr, "invalid pipe syntax\n");
        return;
    }

    args[pipeIndex] = NULL;
    char **left = args;
    char **right = &args[pipeIndex + 1];

    for (int i = 0; right[i] != NULL; i++) {
        if (strcmp(right[i], "|") == 0) {
            fprintf(stderr,
                    "multiple pipes not supported\n");
            return;
        }
    }

    int pipefd[2];

    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid1 = fork();

    if (pid1 < 0) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (pid1 == 0) {
        resetChildSignals();
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            _exit(1);
        }

        close(pipefd[1]);

        if (handleRedirection(left) == -1) {
            _exit(1);
        }

        execvp(left[0], left);
        perror("execvp");
        _exit(127);
    }

    pid_t pid2 = fork();

    if (pid2 < 0) {
        perror("fork");

        close(pipefd[0]);
        close(pipefd[1]);

        while (waitpid(pid1, NULL, 0) == -1) {
            if (errno != EINTR) {
                perror("waitpid");
                break;
            }
        }

        return;
    }

    if (pid2 == 0) {
        resetChildSignals();
        close(pipefd[1]);

        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            perror("dup2");
            _exit(1);
        }

        close(pipefd[0]);

        if (handleRedirection(right) == -1) {
            _exit(1);
        }

        execvp(right[0], right);

        perror("execvp");
        _exit(127);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    while (waitpid(pid1, NULL, 0) == -1) {
        if (errno != EINTR) {
            perror("waitpid");
            break;
        }
    }

    while (waitpid(pid2, NULL, 0) == -1) {
        if (errno != EINTR) {
            perror("waitpid");
            break;
        }
    }
}

void reapbackgroundProcess(){
    int status;
    pid_t pid;

    while(1){
        pid = waitpid(-1, &status, WNOHANG);

        if(pid <= 0){
            break;
        }
    }
}

int main(){
    char input[MAX_INPUT];

    char *args[MAX_ARGS];

    if(setupSignals() == -1){
        return 1;
    }

    while(1){
        if(childSignal){
            childSignal = 0;
        }
        reapbackgroundProcess();

        int inputResult = takeInput(input);
        if(inputResult == -1){
            continue;
        }
        if(inputResult == 0){
            break;
        }

        if(strlen(input) == 0){
            continue;
        }

        parseInput(input, args);

        if(args[0] == NULL){
            continue;
        }

        int background = 0;
        int argc = 0;

        while(args[argc] != NULL){
            argc++;
        }

        if(argc > 0 && strcmp(args[argc - 1], "&") == 0){
            background = 1;
            args[argc - 1] = NULL;
        }

        if(args[0] == NULL){
            fprintf(stderr, "missing command\n");
            continue;
        }

        if(strcmp(args[0], ">") == 0 || strcmp(args[0], ">>") == 0 || strcmp(args[0], "<") == 0){
            fprintf(stderr, "missing command before redirection\n");
            continue;
        }

        if(strcmp(args[0], "exit") == 0){
            break;
        }else if(strcmp(args[0], "cd") == 0){
            changeDirectory(args);
        }else if(strcmp(args[0], "pwd") == 0){
            printWorkingDirectory();
        }else if(strcmp(args[0], "help") == 0){
            printHelp();
        }else {
            int hasPipe = 0;
            for (int i = 0; args[i] != NULL; i++) {
                if (strcmp(args[i], "|") == 0) {
                    hasPipe = 1;
                    break;
                }
            }

            if (hasPipe) {
                executePipe(args);
            }else {
                executeCommand(args, background);
            }
        }

    }

    return 0;
}
