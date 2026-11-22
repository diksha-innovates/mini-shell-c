#ifndef MINISHELL_EXECUTOR_H
#define MINISHELL_EXECUTOR_H

#include "jobs.h"
#include "parser.h"
#include "shell.h"

/* Runs one parsed pipeline (which may be a single builtin, a single
 * external command, or a multi-stage external pipeline). Updates
 * state->last_status. `line_text` is the original input line, stored for
 * `jobs` display. */
void execute_pipeline(ShellState *state, Pipeline *p, const char *line_text);

/* Non-blocking reap of any children that have changed state (exited,
 * been signaled, stopped, or continued) since the last call. Prints
 * "Done"/"Stopped" notifications for background jobs. Safe to call any
 * time; drains exactly what's currently reapable via WNOHANG. */
void reap_children(ShellState *state);

/* Gives the terminal to `job`'s process group, waits (blocking) until it
 * either finishes or stops, then takes the terminal back. Used both
 * right after launching a foreground pipeline and by the `fg` builtin. */
void wait_for_foreground_job(ShellState *state, Job *job);

#endif
