#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAP 8
#define MAX_WORD_LEN 65536

void token_list_init(TokenList *list) {
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

void token_list_free(TokenList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->tokens[i].text);
    }
    free(list->tokens);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int push_token(TokenList *list, TokenType type, const char *text) {
    if (list->count == list->capacity) {
        int new_cap = list->capacity == 0 ? INITIAL_CAP : list->capacity * 2;
        Token *resized = realloc(list->tokens, (size_t)new_cap * sizeof(Token));
        if (!resized) {
            return -1;
        }
        list->tokens = resized;
        list->capacity = new_cap;
    }
    list->tokens[list->count].type = type;
    list->tokens[list->count].text = text ? strdup(text) : NULL;
    if (text && !list->tokens[list->count].text) {
        return -1;
    }
    list->count++;
    return 0;
}

static int is_operator_start(char c) { return c == '|' || c == '<' || c == '>' || c == '&'; }

int tokenize(const char *line, TokenList *out, char *errbuf, int errbuf_len) {
    token_list_init(out);
    size_t i = 0;
    size_t len = strlen(line);
    char word[MAX_WORD_LEN];

    while (i < len) {
        char c = line[i];

        if (c == ' ' || c == '\t') {
            i++;
            continue;
        }

        if (c == '#') {
            /* comment: rest of line ignored */
            break;
        }

        if (is_operator_start(c)) {
            if (c == '|') {
                push_token(out, TOK_PIPE, NULL);
                i++;
                continue;
            }
            if (c == '&') {
                push_token(out, TOK_AMP, NULL);
                i++;
                continue;
            }
            if (c == '<') {
                push_token(out, TOK_REDIR_IN, NULL);
                i++;
                continue;
            }
            if (c == '>') {
                if (i + 1 < len && line[i + 1] == '>') {
                    push_token(out, TOK_REDIR_APPEND, NULL);
                    i += 2;
                } else {
                    push_token(out, TOK_REDIR_OUT, NULL);
                    i++;
                }
                continue;
            }
        }

        /* Special-case "2>" as stderr redirection: only when the '2' is a
         * standalone token (not part of a larger word) immediately
         * followed by '>'. */
        if (c == '2' && i + 1 < len && line[i + 1] == '>' &&
            (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
            push_token(out, TOK_REDIR_ERR, NULL);
            i += 2;
            continue;
        }

        /* Otherwise, accumulate a WORD, honoring quotes/escapes. */
        size_t wi = 0;
        int in_word = 1;
        while (in_word && i < len) {
            c = line[i];
            if (c == ' ' || c == '\t') {
                break;
            }
            if (is_operator_start(c)) {
                break;
            }
            if (c == '2' && i + 1 < len && line[i + 1] == '>' && wi == 0) {
                /* bare "2>" would have been caught above; if we get here
                 * wi>0 so '2' is part of a larger word (e.g. foo2>bar) and
                 * is treated as a literal char below, not an operator. */
            }
            if (c == '\'') {
                i++;
                int closed = 0;
                while (i < len) {
                    if (line[i] == '\'') {
                        closed = 1;
                        i++;
                        break;
                    }
                    if (wi >= MAX_WORD_LEN - 1) {
                        snprintf(errbuf, (size_t)errbuf_len, "word too long");
                        token_list_free(out);
                        return -1;
                    }
                    word[wi++] = line[i++];
                }
                if (!closed) {
                    snprintf(errbuf, (size_t)errbuf_len, "unterminated single quote");
                    token_list_free(out);
                    return -1;
                }
                continue;
            }
            if (c == '"') {
                i++;
                int closed = 0;
                while (i < len) {
                    if (line[i] == '"') {
                        closed = 1;
                        i++;
                        break;
                    }
                    if (line[i] == '\\' && i + 1 < len &&
                        (line[i + 1] == '"' || line[i + 1] == '\\')) {
                        if (wi >= MAX_WORD_LEN - 1) {
                            snprintf(errbuf, (size_t)errbuf_len, "word too long");
                            token_list_free(out);
                            return -1;
                        }
                        word[wi++] = line[i + 1];
                        i += 2;
                        continue;
                    }
                    if (wi >= MAX_WORD_LEN - 1) {
                        snprintf(errbuf, (size_t)errbuf_len, "word too long");
                        token_list_free(out);
                        return -1;
                    }
                    word[wi++] = line[i++];
                }
                if (!closed) {
                    snprintf(errbuf, (size_t)errbuf_len, "unterminated double quote");
                    token_list_free(out);
                    return -1;
                }
                continue;
            }
            if (c == '\\') {
                if (i + 1 >= len) {
                    snprintf(errbuf, (size_t)errbuf_len, "trailing backslash");
                    token_list_free(out);
                    return -1;
                }
                if (wi >= MAX_WORD_LEN - 1) {
                    snprintf(errbuf, (size_t)errbuf_len, "word too long");
                    token_list_free(out);
                    return -1;
                }
                word[wi++] = line[i + 1];
                i += 2;
                continue;
            }
            if (wi >= MAX_WORD_LEN - 1) {
                snprintf(errbuf, (size_t)errbuf_len, "word too long");
                token_list_free(out);
                return -1;
            }
            word[wi++] = line[i++];
        }
        word[wi] = '\0';
        if (push_token(out, TOK_WORD, word) != 0) {
            snprintf(errbuf, (size_t)errbuf_len, "out of memory");
            token_list_free(out);
            return -1;
        }
    }

    push_token(out, TOK_EOF, NULL);
    return 0;
}
