#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS 256
#define MAX_COMMANDS 64

static void command_init(Command *c) {
    c->argv = NULL;
    c->argc = 0;
    c->in_file = NULL;
    c->out_file = NULL;
    c->append_file = NULL;
    c->err_file = NULL;
}

static void command_free(Command *c) {
    if (c->argv) {
        for (int i = 0; i < c->argc; i++) {
            free(c->argv[i]);
        }
        free(c->argv);
    }
    free(c->in_file);
    free(c->out_file);
    free(c->append_file);
    free(c->err_file);
    command_init(c);
}

void pipeline_free(Pipeline *p) {
    if (!p->commands) {
        return;
    }
    for (int i = 0; i < p->ncommands; i++) {
        command_free(&p->commands[i]);
    }
    free(p->commands);
    p->commands = NULL;
    p->ncommands = 0;
}

static int command_add_arg(Command *c, const char *word) {
    if (c->argc >= MAX_ARGS - 1) {
        return -1;
    }
    char **resized = realloc(c->argv, (size_t)(c->argc + 2) * sizeof(char *));
    if (!resized) {
        return -1;
    }
    c->argv = resized;
    c->argv[c->argc] = strdup(word);
    if (!c->argv[c->argc]) {
        return -1;
    }
    c->argc++;
    c->argv[c->argc] = NULL;
    return 0;
}

int parse_pipeline(const TokenList *tokens, Pipeline *out, char *errbuf, int errbuf_len) {
    out->commands = NULL;
    out->ncommands = 0;
    out->background = 0;

    Command commands[MAX_COMMANDS];
    int ncommands = 0;
    command_init(&commands[0]);
    int have_word_in_current = 0;

    int i = 0;
    int n = tokens->count;

    if (n == 0 || tokens->tokens[0].type == TOK_EOF) {
        snprintf(errbuf, (size_t)errbuf_len, "empty command");
        return -1;
    }

    while (i < n && tokens->tokens[i].type != TOK_EOF) {
        Token *t = &tokens->tokens[i];
        switch (t->type) {
            case TOK_WORD:
                if (command_add_arg(&commands[ncommands], t->text) != 0) {
                    snprintf(errbuf, (size_t)errbuf_len, "too many arguments");
                    goto fail;
                }
                have_word_in_current = 1;
                i++;
                break;

            case TOK_PIPE:
                if (!have_word_in_current) {
                    snprintf(errbuf, (size_t)errbuf_len, "syntax error near unexpected token '|'");
                    goto fail;
                }
                ncommands++;
                if (ncommands >= MAX_COMMANDS) {
                    snprintf(errbuf, (size_t)errbuf_len, "too many pipeline stages");
                    goto fail;
                }
                command_init(&commands[ncommands]);
                have_word_in_current = 0;
                i++;
                break;

            case TOK_REDIR_IN:
            case TOK_REDIR_OUT:
            case TOK_REDIR_APPEND:
            case TOK_REDIR_ERR: {
                TokenType rtype = t->type;
                i++;
                if (i >= n || tokens->tokens[i].type != TOK_WORD) {
                    snprintf(errbuf, (size_t)errbuf_len, "expected filename after redirection");
                    goto fail;
                }
                const char *fname = tokens->tokens[i].text;
                Command *cur = &commands[ncommands];
                char **target = rtype == TOK_REDIR_IN ? &cur->in_file
                                 : rtype == TOK_REDIR_OUT ? &cur->out_file
                                 : rtype == TOK_REDIR_APPEND ? &cur->append_file
                                                              : &cur->err_file;
                free(*target);
                *target = strdup(fname);
                if (!*target) {
                    snprintf(errbuf, (size_t)errbuf_len, "out of memory");
                    goto fail;
                }
                i++;
                break;
            }

            case TOK_AMP:
                if (!have_word_in_current) {
                    snprintf(errbuf, (size_t)errbuf_len, "syntax error near unexpected token '&'");
                    goto fail;
                }
                out->background = 1;
                i++;
                if (i < n && tokens->tokens[i].type != TOK_EOF) {
                    snprintf(errbuf, (size_t)errbuf_len, "'&' must be the last token");
                    goto fail;
                }
                break;

            case TOK_EOF:
                break;
        }
    }

    if (!have_word_in_current) {
        snprintf(errbuf, (size_t)errbuf_len, "syntax error: trailing '|'");
        goto fail;
    }

    ncommands++;
    out->commands = calloc((size_t)ncommands, sizeof(Command));
    if (!out->commands) {
        snprintf(errbuf, (size_t)errbuf_len, "out of memory");
        goto fail;
    }
    for (int k = 0; k < ncommands; k++) {
        out->commands[k] = commands[k];
    }
    out->ncommands = ncommands;
    return 0;

fail:
    for (int k = 0; k <= ncommands && k < MAX_COMMANDS; k++) {
        command_free(&commands[k]);
    }
    return -1;
}
