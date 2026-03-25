# wsh - The Wisconsin Shell

`wsh` is an interactive Unix-like command-line shell written in C. It supports standard shell features like built-in commands, external program execution, piping, environment variables, and both interactive and batch execution modes.

## Features

- **Interactive & Batch Modes**: Run commands interactively with a prompt (`wsh> `) or pass a script file for batch execution.
- **External Program Execution**: Resolves external programs using the `PATH` environment variable and executes them with support for arbitrary arguments.
- **Pipelines**: Supports connecting the output of one command to the input of another using the `|` operator (up to 128 commands in a single pipeline), running commands simultaneously.
- **Environment Variables**: Supports expansion of environment variables (e.g., `$USER`) within commands.
- **Built-in Commands**:
  - `cd [dir]`: Change the current working directory.
  - `env`: Display or modify environment variables.
  - `exit`: Terminate the shell.

## Building

To build the shell, simply run:

```bash
make
```

This will produce two executables:
- `wsh`: The optimized version of the shell.
- `wsh-dbg`: A debug version built with debugging symbols.

## Usage

### Interactive Mode

Run the shell without any arguments to enter interactive mode:

```bash
$ ./wsh
wsh> ls -l | wc -l
wsh> echo "Hello World"
wsh> exit
```

### Batch Mode

Pass a file containing shell commands to execute it in batch mode:

```bash
$ ./wsh script.sh
```

### Environment Variables

Set environment variables using the `env` builtin:

```bash
wsh> env MY_VAR=Hello
wsh> echo $MY_VAR
Hello
wsh> env MY_VAR # Clears the variable
```

## Running Tests

An automated test suite is provided. To run all tests:

```bash
cd tests
./run-tests.sh
```

To run a specific test by its number:

```bash
./run-tests.sh -t 1
```

## Contributing

Contributions, issues, and feature requests are welcome! Feel free to check the issues page.
