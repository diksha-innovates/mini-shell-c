#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "builtins.h"
#include "executor.h"
#include "parser.h"
#include "shell.h"
#include "signals.h"
#include "tokenizer.h"

void shell_state_init(ShellState *state) {
    memset(state, 0, sizeof(*state));
    jobtable_init(&state->jobs);
    state->last_status = 0;
}

/* The only "expansion" this shell supports: a WORD that is exactly "$?"
 * is replaced with the decimal exit status of the previous command. No
 * other variable expansion, globbing, or substitution is implemented. */
static void substitute_last_status(Pipeline *p, int last_status) {
    for (int i = 0; i < p->ncommands; i++) {
        Command *c = &p->commands[i];
        for (int j = 0; j < c->argc; j++) {
            if (strcmp(c->argv[j], "$?") == 0) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", last_status);
                free(c->argv[j]);
                c->argv[j] = strdup(buf);
            }
        }
    }
}

static void process_line(ShellState *state, const char *line) {
    TokenList tokens;
    char errbuf[256];

    if (tokenize(line, &tokens, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "minishell: syntax error: %s\n", errbuf);
        state->last_status = 2;
        return;
    }
    if (tokens.count <= 1) { /* just TOK_EOF: blank line */
        token_list_free(&tokens);
        return;
    }

    Pipeline pipeline;
    if (parse_pipeline(&tokens, &pipeline, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "minishell: %s\n", errbuf);
        state->last_status = 2;
        token_list_free(&tokens);
        return;
    }
    token_list_free(&tokens);

    substitute_last_status(&pipeline, state->last_status);
    execute_pipeline(state, &pipeline, line);
    pipeline_free(&pipeline);
}

static int run_interactive_or_piped(ShellState *state) {
    char *line = NULL;
    size_t cap = 0;

    while (!state->exit_requested) {
        if (state->shell_is_interactive) {
            printf("minishell$ ");
            fflush(stdout);
        }

        ssize_t n = getline(&line, &cap, stdin);
        reap_children(state);
        if (n < 0) {
            if (state->shell_is_interactive) {
                printf("\n");
            }
            break;
        }
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }
        if (line[0] == '\0') {
            continue;
        }
        process_line(state, line);
        reap_children(state);
    }

    free(line);
    return state->exit_requested ? state->exit_code : state->last_status;
}

static int run_script_file(ShellState *state, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "minishell: %s: %s\n", path, strerror(errno));
        return 1;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while (!state->exit_requested && (n = getline(&line, &cap, f)) >= 0) {
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }
        if (line[0] != '\0') {
            process_line(state, line);
        }
        reap_children(state);
    }
    free(line);
    fclose(f);
    return state->exit_requested ? state->exit_code : state->last_status;
}

int main(int argc, char **argv) {
    ShellState state;
    shell_state_init(&state);

    /* Line-buffer stdout even when it's not a tty (e.g. piped in tests),
     * so builtin output (pwd, jobs, job-done notices) interleaves with
     * child-process output in the order commands actually ran, instead
     * of sitting in a fully-buffered libc buffer until exit. */
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

    state.shell_is_interactive = isatty(STDIN_FILENO);
    state.shell_terminal = STDIN_FILENO;
    state.shell_pgid = getpid();

    /* Put the shell in its own process group and, if interactive, take
     * control of the controlling terminal. This is what lets us later
     * hand the terminal to foreground job process groups and take it
     * back without the shell itself being signaled by terminal keys. */
    setpgid(0, 0);
    state.shell_pgid = getpgrp();
    if (state.shell_is_interactive) {
        tcsetpgrp(state.shell_terminal, state.shell_pgid);
    }

    shell_install_signal_handlers();

    int rc;
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        process_line(&state, argv[2]);
        reap_children(&state);
        rc = state.exit_requested ? state.exit_code : state.last_status;
    } else if (argc >= 2) {
        state.shell_is_interactive = 0;
        rc = run_script_file(&state, argv[1]);
    } else {
        rc = run_interactive_or_piped(&state);
    }

    return rc;
}
