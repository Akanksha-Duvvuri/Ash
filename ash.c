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

int takeInput(char input[]) {
    printf("ash> ");

    fflush(stdout);

    if (fgets(input, MAX_INPUT, stdin) == NULL) {
        return 0; 
    }

    input[strcspn(input, "\n")] = '\0';

    return 1;
}

void parseInput(char input[], char *args[]){
    int i = 0;

    char *token = strtok(input, " \t");

    while(token != NULL && i < MAX_ARGS - 1) {
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
                fprintf(stderr, "ash: missing filename\n");
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
            fprintf(stderr, "ash: Home not set\n");
            return;
        }
    }else {
        path = args[1];
    }

    if(chdir(path) != 0){
        perror("ash: cd");
    }
}

void printWorkingDirectory(){
    char cwd[1024];

    if(getcwd(cwd, sizeof(cwd)) == NULL){
        perror("ash: pwd");
        return;
    }

    printf("%s\n", cwd);
}

void printHelp(){
    printf("Built in commands that are supported by this shell");
    printf("cd, pwd, help, exit");

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
        if (background) {
            int fd = open("/dev/null", O_RDONLY);

            if (fd == -1) {
                perror("ash: /dev/null");
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

    if (pipeIndex <= 0 || args[pipeIndex + 1] == NULL) {
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
                perror("ash: waitpid");
                break;
            }
        }

        return;
    }

    if (pid2 == 0) {
        close(pipefd[1]);

        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            perror("ash: dup2");
            _exit(1);
        }

        close(pipefd[0]);

        if (handleRedirection(right) == -1) {
            _exit(1);
        }

        execvp(right[0], right);

        perror("ash: execvp");
        _exit(127);
    }

    close(pipefd[0]);
    close(pipefd[1]);

    while (waitpid(pid1, NULL, 0) == -1) {
        if (errno != EINTR) {
            perror("ash: waitpid");
            break;
        }
    }

    while (waitpid(pid2, NULL, 0) == -1) {
        if (errno != EINTR) {
            perror("ash: waitpid");
            break;
        }
    }
}

void reapbackgroundProcess(){
    int status;
    pid_t pid;

    while((pid = waitpid(-1, &status, WNOHANG)) > 0){
        printf("process %ld finished", (long)pid);

        if(WIFEXITED(status)){
            printf("exit status: %d", WEXITSTATUS(status));
        }else if(WIFSIGNALED(status)){
            printf("signal: %d", WTERMSIG(status));
        }
        printf("\n");
    }
}

int main(){
    char input[MAX_INPUT];

    char *args[MAX_ARGS];

    while(1){
        reapbackgroundProcess();

        if (takeInput(input) == 0){
            break;
        }

        if(strcmp(input, "exit") == 0){
            break;
        }

        if(strlen(input) == 0){
            continue;
        }

        printf("%s\n", input);

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

        // for(int i=0; args[i] != NULL; i++){
        //     printf("args[%d] = %s\n", i, args[i]);
        // }

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