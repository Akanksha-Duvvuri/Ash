# ASH — A Mini Linux Shell in C

## Project Overview

The project demonstrates fundamental Operating Systems concepts including process creation, program execution, process synchronization, inter-process communication, file descriptor management, and basic signal handling.

The shell provides an interactive interface for executing Linux commands and supports built-in commands, input/output redirection, a single pipe, and background execution.

## Features

- Interactive shell prompt (`ash>`)
- Execution of external Linux commands
- Built-in commands: `cd`, `pwd`, `help`, `exit`
- Input redirection (`<`)
- Output redirection (`>`)
- Append redirection (`>>`)
- Single pipe between two external commands (`|`)
- Background command execution (`&`)
- Basic signal handling for Ctrl+C
- Nonblocking background child-process cleanup
- Error handling for invalid commands and common execution failures

## Technologies Used

- **Programming Language:** C
- **Platform:** Linux
- **Compiler:** GCC
- **Interface:** Command-line terminal
- **APIs:** POSIX system calls and C standard-library functions

## Compilation and Execution
Compile the source code:

```bash
gcc -Wall -Wextra ash.c -o ash
```
Run the shell:

```bash
./ash
```

The shell displays:

```text
ash>
```
Type `exit` or press Ctrl+D at an empty prompt to terminate.

## Operating Systems Concepts

- **Process Creation:** `fork()` creates a child process.
- **Program Execution:** `execvp()` replaces the child process with the requested executable.
- **Process Synchronization:** `waitpid()` waits for foreground commands to finish.
- **Inter-Process Communication:** `pipe()` connects two processes.
- **File Descriptor Management:** `open()`, `dup2()`, and `close()` implement redirection.
- **Background Execution:** Commands can run without blocking the shell.
- **Signal Handling:** `sigaction()` configures basic signal behavior, including Ctrl+C handling.

## Limitations

- Maximum input buffer size is 1024 bytes.
- Maximum argument-pointer array size is 64.
- Commands are primarily tokenized using whitespace.
- Quoted arguments containing spaces are not fully supported.
- Redirection operators must be separated by spaces.
- Only one redirection operator is supported per command.
- Only one pipe connecting two external commands is supported.
- Background pipelines and full job control are not supported.
- Command history and arrow-key editing are not implemented.
- Background child completion notifications may affect interactive prompt display.

----------------------------------------------
Akanksha D
SE25UCSE070
CSE 2