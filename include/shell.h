#ifndef MINISHELL_SHELL_H
#define MINISHELL_SHELL_H

#include <sys/types.h>

#include "jobs.h"

typedef struct {
    JobTable jobs;
    pid_t shell_pgid;
    int shell_terminal;     /* usually STDIN_FILENO */
    int shell_is_interactive;
    int last_status;        /* value exposed as $? */
    int exit_requested;
    int exit_code;
} ShellState;

void shell_state_init(ShellState *state);

#endif
