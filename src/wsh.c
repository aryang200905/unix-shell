// Author:  Vojtech Aschenbrenner <asch@cs.wisc.edu>, Fall 2023
// Revised: John Shawger <shawgerj@cs.wisc.edu>, Spring 2024
// Revised: Vojtech Aschenbrenner <asch@cs.wisc.edu>, Fall 2024
// Revised: Leshna Balara <lbalara@cs.wisc.edu>, Spring 2025
// Revised: Pavan Thodima <thodima@cs.wisc.edu>, Spring 2026

#include "parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/wait.h>

extern char **environ;
static int g_exit_requested = 0;

static int is_builtin_cmd(const char *cmd) {
  if (cmd == NULL) return 0;
  return (strcmp(cmd, "exit") == 0) || (strcmp(cmd, "cd") == 0) ||
         (strcmp(cmd, "env") == 0);
}

static void print_command_not_found(const char *cmd) {
  fprintf(stderr, "%s: Command not found\n", (cmd != NULL) ? cmd : "");
}

static int run_builtin(char **argv) {
  if (argv == NULL || argv[0] == NULL) return 0;

  if (strcmp(argv[0], "exit") == 0) {
    g_exit_requested = 1;
    return 0;
  }

  if (strcmp(argv[0], "cd") == 0) {
    const char *target = argv[1];
    if (target == NULL) {
      target = getenv("HOME");
      if (target == NULL) {
        fprintf(stderr, "cd: HOME not set\n");
        return 1;
      }
    }
    if (chdir(target) != 0) {
      perror("cd");
      return 1;
    }
    return 0;
  }

  if (strcmp(argv[0], "env") == 0) {
    if (argv[1] == NULL) {
      for (char **e = environ; e != NULL && *e != NULL; e++) {
        size_t len = strlen(*e);
        if (write(STDOUT_FILENO, *e, len) < 0) {
          perror("write");
          return 1;
        }
        if (write(STDOUT_FILENO, "\n", 1) < 0) {
          perror("write");
          return 1;
        }
      }
      return 0;
    }

    int had_error = 0;

    for (int i = 1; argv[i] != NULL; i++) {
      char *arg = argv[i];
      char *eq = strchr(arg, '=');
      if (eq == NULL) {
        if (setenv(arg, "", 1) != 0) {
          perror("setenv");
          had_error = 1;
        }
      } else {
        size_t name_len = (size_t)(eq - arg);
        char *name = (char *)malloc(name_len + 1);
        if (name == NULL) {
          perror("malloc");
          return 1;
        }
        memcpy(name, arg, name_len);
        name[name_len] = '\0';

        const char *val = eq + 1;
        if (setenv(name, val, 1) != 0) {
          perror("setenv");
          had_error = 1;
        }
        free(name);
      }
    }
    return had_error ? 1 : 0;
  }

  return 0;
}

static char *resolve_executable(const char *cmd) {
  if (cmd == NULL || cmd[0] == '\0') return NULL;

  if (strchr(cmd, '/') != NULL) {
    if (access(cmd, X_OK) == 0) {
      return strdup(cmd);
    }
    return NULL;
  }

  const char *path = getenv("PATH");
  if (path == NULL) {
    if (setenv("PATH", "/bin", 1) != 0) {
      perror("setenv");
      path = "/bin";
    } else {
      path = getenv("PATH");
      if (path == NULL) path = "/bin";
    }
  }

  char *path_copy = strdup(path);
  if (path_copy == NULL) {
    perror("strdup");
    return NULL;
  }

  char *start = path_copy;
  while (1) {
    char *colon = strchr(start, ':');
    size_t dir_len = (colon == NULL) ? strlen(start) : (size_t)(colon - start);

    const char *base;
    size_t base_len;

    if (dir_len == 0) {
      base = ".";
      base_len = 1;
    } else {
      base = start;
      base_len = dir_len;
    }

    size_t cmd_len = strlen(cmd);
    char *candidate = (char *)malloc(base_len + 1 + cmd_len + 1);
    if (candidate == NULL) {
      perror("malloc");
      free(path_copy);
      return NULL;
    }

    memcpy(candidate, base, base_len);
    candidate[base_len] = '/';
    memcpy(candidate + base_len + 1, cmd, cmd_len);
    candidate[base_len + 1 + cmd_len] = '\0';

    if (access(candidate, X_OK) == 0) {
      free(path_copy);
      return candidate;
    }

    free(candidate);

    if (colon == NULL) break;
    start = colon + 1;
  }

  free(path_copy);
  return NULL;
}

static void exec_external_or_builtin_in_child(char **argv) {
  if (argv == NULL || argv[0] == NULL) {
    _exit(0);
  }

  if (is_builtin_cmd(argv[0])) {
    int rc = run_builtin(argv);
    _exit(rc);
  }

  char *resolved = resolve_executable(argv[0]);
  if (resolved == NULL) {
    print_command_not_found(argv[0]);
    _exit(1);
  }

  execv(resolved, argv);

  free(resolved);
  print_command_not_found(argv[0]);
  _exit(1);
}

static int execute_pipeline(struct pipeline *pl) {
  if (pl == NULL || pl->num_commands <= 0) return 0;

  if (pl->num_commands == 1) {
    char **argv = pl->commands[0].argv;
    if (argv == NULL || argv[0] == NULL) return 0;

    if (is_builtin_cmd(argv[0])) {
      return run_builtin(argv);
    }

    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      exit(1);
    }

    if (pid == 0) {
      exec_external_or_builtin_in_child(argv);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
      perror("waitpid");
      exit(1);
    }

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
  }

  int n = pl->num_commands;
  if (n > 128) {
    fprintf(stderr, "Command not found\n");
    return 1;
  }

  int pipes[127][2];
  for (int i = 0; i < n - 1; i++) {
    if (pipe(pipes[i]) != 0) {
      perror("pipe");
      exit(1);
    }
  }

  pid_t *pids = (pid_t *)malloc((size_t)n * sizeof(pid_t));
  if (pids == NULL) {
    perror("malloc");
    exit(1);
  }

  for (int i = 0; i < n; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      free(pids);
      exit(1);
    }

    if (pid == 0) {
      if (i > 0) {
        if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
          perror("dup2");
          free(pids);
          _exit(1);
        }
      }

      if (i < n - 1) {
        if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
          perror("dup2");
          free(pids);
          _exit(1);
        }
      }

      for (int k = 0; k < n - 1; k++) {
        close(pipes[k][0]);
        close(pipes[k][1]);
      }

      char **argv = pl->commands[i].argv;
      exec_external_or_builtin_in_child(argv);
    }

    pids[i] = pid;
  }

  for (int i = 0; i < n - 1; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }

  int last_status = 0;
  for (int i = 0; i < n; i++) {
    int status = 0;
    if (waitpid(pids[i], &status, 0) < 0) {
      perror("waitpid");
      free(pids);
      exit(1);
    }

    if (i == n - 1) {
      if (WIFEXITED(status)) last_status = WEXITSTATUS(status);
      else last_status = 1;
    }
  }

  free(pids);
  return last_status;
}

static int execute_command_line(struct command_line *cl) {
  if (cl == NULL) return 0;

  int last_status = 0;
  for (int i = 0; i < cl->num_pipelines; i++) {
    last_status = execute_pipeline(&cl->pipelines[i]);
    if (g_exit_requested) {
      break;
    }
  }
  return last_status;
}

char *get_variable(const char *var) {
  const char *v = getenv(var);
  if (v == NULL) return "";
  return (char *)v;
}

int main(int argc, char **argv) {
 FILE *in = NULL;
  int interactive = 0;

  if (argc == 1) {
    in = stdin;
    interactive = isatty(STDIN_FILENO);
  } else if (argc == 2) {
    interactive = 0;
    in = fopen(argv[1], "r");
    if (in == NULL) {
      perror("fopen");
      exit(1);
    }
  } else {
    fprintf(stderr, "Usage: %s [file]\n", argv[0]);
    exit(1);
  }

  char *line = NULL;
  size_t cap = 0;

  int shell_status = 0;

  while (1) {
    if (interactive) {
      printf("wsh> ");
      fflush(stdout);
    }

    ssize_t nread = getline(&line, &cap, in);
    if (nread < 0) {
      break; 
    }

    struct command_line *cl = parse_input(line);
    if (cl == NULL) {
      continue; 
    }

    shell_status = execute_command_line(cl);
    free_command_line(cl);

    if (g_exit_requested) {
      break;
    }
  }

  free(line);

  if (!interactive && in != NULL) {
    fclose(in);
  }

  return shell_status;
}
