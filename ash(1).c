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

volatile sig_atomic_t childSignal = 0; //its like a notification flag. 
//sig_atomic_t is an int type suitable for simple read and writes from a signal handler. and volatile tells the complier that this value may change unexpectedly

//this func runs when ash receives sigchild
void handleSIGCHLD(int signalNumber){ //after a bg command finishes, ash needs to notice a signal so we use this one 
    (void)signalNumber; //contains the signal received
    childSignal = 1;
}

int setupSignals(){ //tells linux how ash should react when it receives certain signals
    struct sigaction sa = {0}; //sigaction is a structure that tells linux what to do when a signal arrives. --- stores instructions for how ash should handle ctrl + c
    sa.sa_handler = SIG_IGN; //sig ign - ignore the signal //sa handler is what should the program do when it receives the signal
    sigemptyset(&sa.sa_mask); //temporarily bloocks other signals while handling one

    if(sigaction(SIGINT, &sa, NULL) == -1){
        perror("sigaction SIGINT");
        return -1;
    }

    struct sigaction sc = {0}; 
    sc.sa_handler = handleSIGCHLD; //run handlesigchild when signal arrives
    sigemptyset(&sc.sa_mask); //lists any extra signals to remporarily block while handling this one, make that list empty
    sc.sa_flags = SA_NOCLDSTOP; //dont notify the parent when a child stops or continues

    if(sigaction(SIGCHLD, &sc, NULL) == -1){  //dont need to save prev settings.
        perror("sigaction SIGCHLD");
        return -1;
    }

    return 0;
}

void resetChildSignals(){ //called inside child after fork and before execvp
    struct sigaction sa = {0}; 
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);

    if(sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("reset child signals");
        _exit(1);
    }
}

int takeInput(char input[]){ //array where the user input is stored. Treated as a pointer to the first character.
    printf("ash> "); //displays shell prompt, no new line because the user types on the same line
    fflush(stdout); //standard output stream - default destination where a program sends its normal text output.
    //printf may temporarily buffer its output, but fflush(stdout) forces pending output to be written. We use this so that the prompt appears before waiting for the user's command.

    errno = 0; //stores error codes from failed operations, setting it to 0 clears the previous err value, helps identify why fgets fails.
    if(fgets(input, MAX_INPUT, stdin) == NULL){ //fgets reads characters from standard input. input is the destination array, max_input is the buffer capacity and stdin is the input stream (usually the terminal)
        if(ferror(stdin)){ //checks whether the input stream encountered a read error or not
            if(errno == EINTR){ //EINTR - the read was intr by a signal (interrupted system call)
                clearerr(stdin); //clear the error indicator on stdin so the stream can be read again
                return -1; //tell main to retry the shell loop
            }
            perror("input"); //print the reason why the input failed
            return 0; //tell main to stop the shell
        }
        return 0; //if no stream err - EOF
    }

    if(strchr(input, '\n') == NULL && strlen(input) == MAX_INPUT - 1){ //checks whether the input buffer filled up without reading a new line.
        //searches for a new line and if no new line is found (NULL), strlen(input) counts char before \0

        //together, they check if the input might exceed our buffer capacity
        int ch; //stores a char returned by getchar
        while((ch = getchar()) != '\n' && ch != EOF){} //read and discard remaining characters. Continue until you encounter a newline or EOF
        fprintf(stderr, "input too long\n"); //prints an error to the std err
        return -1; //discard the input and retry the loop
    }

    input[strcspn(input, "\n")] = '\0'; //fgets normally preserves the newline, strcspn finds the position of the first newline char, and we replace that char with \0

    return 1; //input was successfully read
}

void parseInput(char input[], char *args[]){ //the function doesnt return a value, input contains the full command enter by user/ //char *args[] - array of char pointers, each pointer will point to one argument
    int i = 0; //tracking index of the next argument

    char *token = strtok(input, " \t"); //strtok - seperates a string into tokens 
    //\t is a delimiter

    //strtok changes the original inpput by replacing the delimiters with \0

    while(token != NULL && i < MAX_ARGS - 1){ //keep looping while a valid token exists and we havent reached the arguments limit
        args[i] = token;  //storing address of the token in args[i]
        i++;

        token = strtok(NULL, " \t"); //continue tokenizing the same original string, and NULL tells it to resume where it stopped.
    }

    args[i] = NULL; //mark the end of the argument pointer array, execvp() requires this final NULL pointer
}

int handleRedirection(char *args[]){ //redirection - changing where a program gets its input or sends its output
    for(int i=0; args[i] != NULL; i++){ 
        int fd = -1; //will store the number returned by open

        int target = -1; //which target standard stream we want to change

        if(strcmp(args[i], ">") == 0){
            target = STDOUT_FILENO; //its one, user wants to change where the progra prints its output - so change stdout

            if(args[i+1] == NULL){
                fprintf(stderr, "missing filename\n");
                return -1;
            }

            fd = open(args[i + 1],
                O_WRONLY | O_CREAT | O_TRUNC, //erase prev contents if it exists
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
            target = STDIN_FILENO; //its 0 because we want to change where the program reads input from

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

void changeDirectory(char *args[]){ //dont use fork because cd would run inside the child and the parent (ash) would stay in the old dir 
    char *path; //need to know which dir to endet - path is a pointer to a string containing the destination folder

    if(args[1] == NULL){ //cd only - go home
        path = getenv("HOME"); //env variables - a named setting available to programs

        if(path == NULL){
            fprintf(stderr, "Home not set\n");
            return;
        }
    }else {
        path = args[1];
    }

    if(chdir(path) != 0){ //chdir - change directory
        perror("cd");
    }
}

void printWorkingDirectory(){ //tells you which folder youre currently inside
    char cwd[1024]; //currently working dir - stored in an array because a dir path is a string and strings are stored as characters in C

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

void executeCommand(char *args[], int background) { //array containing the command and its arguments, background indicates whether the command should run in the bg or not
    pid_t pid = fork(); //pid_t is a POSIX data type used to represent process ids 
    if (pid < 0) { //if fork returns -1 ---> process creation failed
        perror("fork");
        return;
    }

    if (pid == 0) { //executing inside the child process, the banch executes only in the child process
        resetChildSignals(); //shell ignores Ctrl+C so that the shell itself doesnt terminate when you press it 

        if (background) { //block runs only if the command ends with &
            if(setpgid(0, 0) == -1){ //sets the child process's process group id to its own pid (to seperate a standalone bg command from the shells foreground process group)
                perror("setpgid");
                _exit(1); //exit with 1
            }

            int fd = open("/dev/null", O_RDONLY); //It's a special device file in Linux.

// When you read from it, you immediately receive end-of-file.

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

        if (handleRedirection(args) == -1) { //before running the command - check if the user wants io redirection
            _exit(1); //if it failed - stop the child process, dont want the command to run when io isnt set clearly
        }

        execvp(args[0], args); //execvp finds te ls program and runs it, execvp replaces the childs program with the program the user requested. 
        perror("execvp");
        _exit(127); //127 is an exit status commonly used when a command cant be found. Stop the child so it doesnt continue running the shells own code. 
    }

    if (background) { //check whether & is there or not 
        if(setpgid(pid, pid) == -1 && errno != EACCES /*the child may have started another program*/&& errno != ESRCH/*the target child may no longer be available to change its group*/ */){ //parent child also tries to put bg child into tis own group. Because after fork we dk which is gonna run first. 
            //eacces, esrch are two errors that can happen because of timing. 
            perror("setpgid");
        }
        printf("Background process started: PID %ld\n",(long)pid);
        return;
    }

    int status; //when the child finishes, need to know how it finished. waitpid puts this info into status
    pid_t result; //check whether waitpid worked correctly or returned an error

    do {
        result = waitpid(pid, &status, 0); //while noerror, wait for the child to finish, on success waitpid returns the childs process id. 
    } while (result == -1 && errno == EINTR); //wait because sometimes a signal interrups waitpid before the chidl finishes. 

    if (result == -1) {
    perror("waitpid");
    }else if(WIFSIGNALED(status) && WTERMSIG(status) == SIGINT){ //checks if a signal ended the child, if so which one? sigint - the signal normally sent by crtl + c
        printf("\n");
        fflush(stdout);
    }
}

void executePipe(char *args[]) {
    int pipeIndex = -1; //need to know where the pipe is

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
        resetChildSignals(); //let it respond normally to signals such at strl c
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) { //making stdout point to the pipes end
            perror("dup2");
            _exit(1);
        }

        close(pipefd[1]); //points to the pipe, we no longer need the extra write end descriptor

        if (handleRedirection(left) == -1) { //check if it has file redirection - exit
            _exit(1);
        }

        execvp(left[0], left); //let the left command run, because stdout was changed above, ls sends its output into the pipe
        perror("execvp");
        _exit(127);
    }

    pid_t pid2 = fork();

    if (pid2 < 0) {
        perror("fork");

        close(pipefd[0]);
        close(pipefd[1]);

        while (waitpid(pid1, NULL, 0) == -1) { //child one may be running already
            if (errno != EINTR) {
                perror("waitpid");
                break;
            }
        }

        return;
    }

    if (pid2 == 0) {
        resetChildSignals();
        close(pipefd[1]); //close the write end, child 2 reads only from the pipe

        if (dup2(pipefd[0], STDIN_FILENO) == -1) { //normally, sort can read input from the keyboard, but we want it to read the output prodiced by ls. so we make stdin refer to the pipes read end
            perror("dup2");
            _exit(1);
        }

        close(pipefd[0]);

        if (handleRedirection(right) == -1) {
            _exit(1);
        }

        execvp(right[0], right); //run the right command, reads from the pipe because we changed its stdin during dup2. stdout is still the terminal so the sorted result appears on the screen

        perror("execvp");
        _exit(127);
    }

    //parent - ash isnt reading or writing pipe data so just close both pipe ends. 
    close(pipefd[0]);
    close(pipefd[1]);

    while (waitpid(pid1, NULL, 0) == -1) { //while we wait for the first child to run, if the err isnt interrupt then stop retrying
        if (errno != EINTR) {
            perror("waitpid");
            break;
        }
    }

    while (waitpid(pid2, NULL, 0) == -1) { //wait for the second child to finish, ash shouldnt display its next prompt until the whole pipeline finishes.
        if (errno != EINTR) {
            perror("waitpid");
            break;
        }
    }
}

void reapbackgroundProcess(){ //once a bg process gets over, linux keeps a small record of that finished process until its parent collects information about its exit. Until this happens, the finished child process is called a zombie process. 
    int status; //gets info on how the child finished. 
    pid_t pid; //stores the pid returned by waitpid()

    while(1){
        pid = waitpid(-1, &status, WNOHANG); //waitpid - collects child exit info
//-1 -- check for any child process, status -- store how it finished, WNOHANG - dont make ash wait if no child has finished yet. 

//+ve pid - finished child was collected
//0 - children exist but none are ready
//-ve pid - err occured
        if(pid <= 0){ //if no finished child is availably for us to collect rn, then return to ash
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
