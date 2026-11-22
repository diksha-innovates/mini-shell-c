#ifndef MINISHELL_BUILTINS_H
#define MINISHELL_BUILTINS_H

#include "parser.h"
#include "shell.h"

int is_builtin(const char *name);

/* Executes a builtin command in the shell's own process (never forked).
 * Returns the exit status to expose as $?. May set state->exit_requested. */
int run_builtin(ShellState *state, Command *cmd);

#endif
