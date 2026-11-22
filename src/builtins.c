#include "builtins.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "executor.h"

static const char *kBuiltinNames[] = {"cd", "pwd", "exit", "jobs", "fg", "bg", NULL};

int is_builtin(const char *name) {
    for (int i = 0; kBuiltinNames[i]; i++) {
        if (strcmp(name, kBuiltinNames[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int builtin_cd(Command *cmd) {
    const char *target = cmd->argc >= 2 ? cmd->argv[1] : getenv("HOME");
    if (!target) {
        fprintf(stderr, "minishell: cd: HOME not set\n");
        return 1;
    }
    if (chdir(target) != 0) {
        fprintf(stderr, "minishell: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }
    return 0;
}

static int builtin_pwd(void) {
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) {
        fprintf(stderr, "minishell: pwd: %s\n", strerror(errno));
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}

static const char *job_state_name(JobState s) {
    switch (s) {
        case JOB_RUNNING: return "Running";
        case JOB_STOPPED: return "Stopped";
        case JOB_DONE: return "Done";
    }
    return "?";
}

static int builtin_jobs(ShellState *state) {
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &state->jobs.jobs[i];
        if (j->in_use) {
            printf("[%d]+  %-23s %s\n", j->id, job_state_name(j->state), j->command);
        }
    }
    return 0;
}

static Job *resolve_job_arg(ShellState *state, Command *cmd) {
    if (cmd->argc >= 2) {
        const char *arg = cmd->argv[1];
        if (arg[0] == '%') {
            arg++;
        }
        int id = atoi(arg);
        return jobtable_find_by_id(&state->jobs, id);
    }
    return jobtable_find_most_recent(&state->jobs);
}

static int builtin_fg(ShellState *state, Command *cmd) {
    Job *job = resolve_job_arg(state, cmd);
    if (!job) {
        fprintf(stderr, "minishell: fg: no such job\n");
        return 1;
    }
    printf("%s\n", job->command);
    if (job->state == JOB_STOPPED) {
        killpg(job->pgid, SIGCONT);
        job->state = JOB_RUNNING;
    }
    wait_for_foreground_job(state, job);
    return state->last_status;
}

static int builtin_bg(ShellState *state, Command *cmd) {
    Job *job = resolve_job_arg(state, cmd);
    if (!job) {
        fprintf(stderr, "minishell: bg: no such job\n");
        return 1;
    }
    if (job->state == JOB_STOPPED) {
        killpg(job->pgid, SIGCONT);
        job->state = JOB_RUNNING;
    }
    job->notified = 0;
    printf("[%d]+ %s &\n", job->id, job->command);
    return 0;
}

static int builtin_exit(ShellState *state, Command *cmd) {
    int code = state->last_status;
    if (cmd->argc >= 2) {
        code = atoi(cmd->argv[1]);
    }
    state->exit_requested = 1;
    state->exit_code = code & 0xFF;
    return state->exit_code;
}

int run_builtin(ShellState *state, Command *cmd) {
    const char *name = cmd->argv[0];
    if (strcmp(name, "cd") == 0) return builtin_cd(cmd);
    if (strcmp(name, "pwd") == 0) return builtin_pwd();
    if (strcmp(name, "jobs") == 0) return builtin_jobs(state);
    if (strcmp(name, "fg") == 0) return builtin_fg(state, cmd);
    if (strcmp(name, "bg") == 0) return builtin_bg(state, cmd);
    if (strcmp(name, "exit") == 0) return builtin_exit(state, cmd);
    fprintf(stderr, "minishell: %s: not a builtin\n", name);
    return 127;
}
