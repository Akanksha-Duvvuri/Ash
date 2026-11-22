#include<stdio.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<errno.h>
#include<stdlib.h>

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

void executeCommand(char *args[]){
    pid_t pid = fork();

    if(pid < 0){
        perror("fork");
        return;
    } else if(pid == 0){
        execvp(args[0], args);
        perror("execvp");
        _exit(127);
    } else {
        int status;
        pid_t result;

        do{
            result = waitpid(pid, &status, 0);
        } while (result == -1 && errno == EINTR);

        if(result == -1) {
            perror("waitpid");
        }
    }
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

    printf("  cd              Change working directory\n");
    printf("  pwd             Print working directory\n");
    printf("  help            Display this help message\n");
    printf("  exit            Exit the shell\n");

    printf("External Commands supported by the shell: \n");

    printf("ls, date, echo, cat, gcc, etc...\n");

    printf("The commands are executed using fork() and execvp() \n");
}

int main(){
    char input[MAX_INPUT];

    char *args[MAX_ARGS];

    while(1){
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

        for(int i=0; args[i] != NULL; i++){
            printf("args[%d] = %s\n", i, args[i]);
        }

        if(strcmp(args[0], "exit") == 0){
            break;
        }else if(strcmp(args[0], "cd") == 0){
            changeDirectory(args);
        }else if(strcmp(args[0], "pwd") == 0){
            printWorkingDirectory();
        }else if(strcmp(args[0], "help") == 0){
            printHelp();
        } else {
            executeCommand(args);
        }
    }

    return 0;
}