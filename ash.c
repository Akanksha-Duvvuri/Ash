#include<stdio.h>
#include<string.h>

#define MAX_INPUT 1024

int takeInput(char input[]) {
    printf("ash> ");

    fflush(stdout);

    if (fgets(input, MAX_INPUT, stdin) == NULL) {
        return 0; 
    }

    input[strcspn(input, "\n")] = '\0';

    return 1;  // Successfully read a line
}


int main(){
    char input[MAX_INPUT];

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
    }
    return 0;
}