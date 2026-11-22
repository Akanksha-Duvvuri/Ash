#include<stdio.h>
#include<string.h>

#define MAX_INPUT 1024
#define MAX_ARGS 64

int takeInput(char input[]) {
    printf("ash> ");

    fflush(stdout);

    if (fgets(input, MAX_INPUT, stdin) == NULL) {
        return 0; 
    }

    input[strcspn(input, "\n")] = '\0';

    return 1;  // Successfully read a line
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
    }


    return 0;
}