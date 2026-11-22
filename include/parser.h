#ifndef MINISHELL_PARSER_H
#define MINISHELL_PARSER_H

#include "tokenizer.h"

/*
 * Grammar (documented fully in README):
 *
 *   job        := pipeline [ '&' ]
 *   pipeline   := command ( '|' command )*
 *   command    := word+ redirection*
 *   redirection:= '<' word | '>' word | '>>' word | '2>' word
 *
 * Only one pipeline per line is supported: no ';', '&&', '||', or
 * subshells. The parser never executes anything; it only builds the AST
 * below, which the executor later walks.
 */

typedef struct {
    char **argv;    /* NULL-terminated argv for execvp */
    int argc;
    char *in_file;     /* < */
    char *out_file;    /* > (truncate) */
    char *append_file; /* >> */
    char *err_file;    /* 2> */
} Command;

typedef struct {
    Command *commands;
    int ncommands;
    int background; /* trailing & */
} Pipeline;

/* Parses tokens into a pipeline. Returns 0 on success, -1 on syntax error
 * (message written to errbuf). On success, caller must pipeline_free(). */
int parse_pipeline(const TokenList *tokens, Pipeline *out, char *errbuf, int errbuf_len);

void pipeline_free(Pipeline *p);

#endif
