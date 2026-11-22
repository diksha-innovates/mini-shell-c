#include "executor.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <sys/wait.h>

#include "builtins.h"
#include "signals.h"

#define MAX_PIPES 63

static const char *job_state_label(JobState s) {
    switch (s) {
        case JOB_RUNNING: return "Running";
        case JOB_STOPPED: return "Stopped";
        case JOB_DONE: return "Done";
    }
    return "?";
}

static void notify_job_change(Job *job) {
    if (job->state == JOB_DONE && !job->notified) {
        printf("[%d]+  %-23s %s\n", job->id, job_state_label(job->state), job->command);
        job->notified = 1;
    } else if (job->state == JOB_STOPPED) {
        printf("[%d]+  %-23s %s\n", job->id, job_state_label(job->state), job->command);
    }
}

void reap_children(ShellState *state) {
    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (pid <= 0) {
            break;
        }
        Job *job = jobtable_record_status(&state->jobs, pid, status);
        if (job) {
            notify_job_change(job);
        }
    }
    jobtable_reap_done(&state->jobs);
}

void wait_for_foreground_job(ShellState *state, Job *job) {
    if (state->shell_is_interactive) {
        tcsetpgrp(state->shell_terminal, job->pgid);
    }

    for (;;) {
        int status;
        pid_t wpid = waitpid(-1, &status, WUNTRACED);
        if (wpid < 0) {
            if (errno == EINTR) {
                continue;
            }
            break; /* ECHILD: nothing left to wait for */
        }
        Job *owner = jobtable_record_status(&state->jobs, wpid, status);
        if (!owner) {
            continue;
        }
        if (owner == job) {
            if (job->state == JOB_DONE || job->state == JOB_STOPPED) {
                break;
            }
        } else {
            notify_job_change(owner);
        }
    }

    if (state->shell_is_interactive) {
        tcsetpgrp(state->shell_terminal, state->shell_pgid);
    }

    if (job->state == JOB_DONE) {
        state->last_status = job->last_exit_status;
        job->in_use = 0;
    } else if (job->state == JOB_STOPPED) {
        printf("[%d]+  %-23s %s\n", job->id, job_state_label(job->state), job->command);
        job->notified = 1; /* already told the user it's stopped */
        state->last_status = 128 + SIGTSTP;
    }
    jobtable_reap_done(&state->jobs);
}

static int open_redir(const char *path, int flags, mode_t mode) {
    int fd = open(path, flags, mode);
    if (fd < 0) {
        fprintf(stderr, "minishell: %s: %s\n", path, strerror(errno));
    }
    return fd;
}

/* Applies this command's redirections to the already-pipe-connected
 * stdin/stdout, and to stderr. Called in the child, after dup2'ing pipe
 * ends but before execvp. Returns 0 on success, -1 on failure (caller
 * should _exit). */
static int apply_redirections(const Command *cmd) {
    if (cmd->in_file) {
        int fd = open_redir(cmd->in_file, O_RDONLY, 0);
        if (fd < 0) return -1;
        if (dup2(fd, STDIN_FILENO) < 0) { close(fd); return -1; }
        close(fd);
    }
    if (cmd->out_file) {
        int fd = open_redir(cmd->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return -1;
        if (dup2(fd, STDOUT_FILENO) < 0) { close(fd); return -1; }
        close(fd);
    } else if (cmd->append_file) {
        int fd = open_redir(cmd->append_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) return -1;
        if (dup2(fd, STDOUT_FILENO) < 0) { close(fd); return -1; }
        close(fd);
    }
    if (cmd->err_file) {
        int fd = open_redir(cmd->err_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return -1;
        if (dup2(fd, STDERR_FILENO) < 0) { close(fd); return -1; }
        close(fd);
    }
    return 0;
}

static void run_external_pipeline(ShellState *state, Pipeline *p, const char *line_text) {
    int n = p->ncommands;
    int pipefds[MAX_PIPES][2];

    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipefds[i]) != 0) {
            fprintf(stderr, "minishell: pipe: %s\n", strerror(errno));
            state->last_status = 1;
            return;
        }
    }

    pid_t pids[MAX_PIPES + 1];
    pid_t pgid = 0;

    fflush(stdout); /* avoid children flushing a duplicated copy of our buffer */

    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "minishell: fork: %s\n", strerror(errno));
            state->last_status = 1;
            return;
        }
        if (pid == 0) {
            child_restore_default_signals();
            if (i == 0) {
                setpgid(0, 0);
            } else {
                setpgid(0, pgid);
            }

            if (i > 0) {
                dup2(pipefds[i - 1][0], STDIN_FILENO);
            }
            if (i < n - 1) {
                dup2(pipefds[i][1], STDOUT_FILENO);
            }
            for (int k = 0; k < n - 1; k++) {
                close(pipefds[k][0]);
                close(pipefds[k][1]);
            }

            if (apply_redirections(&p->commands[i]) != 0) {
                _exit(1);
            }

            execvp(p->commands[i].argv[0], p->commands[i].argv);
            fprintf(stderr, "minishell: %s: %s\n", p->commands[i].argv[0], strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }

        if (i == 0) {
            pgid = pid;
        }
        setpgid(pid, pgid); /* also set from parent side; harmless if child beat us to it */
        pids[i] = pid;
    }

    for (int i = 0; i < n - 1; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }

    int id = jobtable_add(&state->jobs, pgid, pids, n, line_text, p->background);
    Job *job = jobtable_find_by_id(&state->jobs, id);

    if (p->background) {
        printf("[%d] %d\n", job->id, (int)pids[n - 1]);
        state->last_status = 0;
    } else {
        wait_for_foreground_job(state, job);
    }
}

static int pipeline_is_single_builtin(const Pipeline *p) {
    return p->ncommands == 1 && is_builtin(p->commands[0].argv[0]);
}

void execute_pipeline(ShellState *state, Pipeline *p, const char *line_text) {
    if (pipeline_is_single_builtin(p)) {
        if (p->background) {
            fprintf(stderr, "minishell: builtins cannot be run in the background\n");
            state->last_status = 1;
            return;
        }
        state->last_status = run_builtin(state, &p->commands[0]);
        return;
    }
    run_external_pipeline(state, p, line_text);
}
