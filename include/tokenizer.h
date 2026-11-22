#ifndef MINISHELL_TOKENIZER_H
#define MINISHELL_TOKENIZER_H

/*
 * Tokenizer: turns a raw input line into a flat array of tokens.
 * Quoting rules (documented in README, intentionally simpler than bash):
 *   - Single quotes: everything inside is literal, no escapes at all.
 *   - Double quotes: backslash only escapes \, ", or a following newline;
 *     all other characters (including $) are literal (no expansion).
 *   - Outside quotes: backslash escapes the single next character.
 *   - Unquoted whitespace (space/tab) separates words.
 *   - Operators | < > >> 2> & are recognized only outside quotes and only
 *     when not glued to other word characters that would make them part
 *     of a word (e.g. "2>" must appear as the literal two characters).
 */

typedef enum {
    TOK_WORD,
    TOK_PIPE,       /* | */
    TOK_REDIR_IN,   /* < */
    TOK_REDIR_OUT,  /* > */
    TOK_REDIR_APPEND, /* >> */
    TOK_REDIR_ERR,  /* 2> */
    TOK_AMP,        /* & */
    TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char *text; /* heap-allocated; NULL for pure operator tokens other than WORD */
} Token;

typedef struct {
    Token *tokens;
    int count;
    int capacity;
} TokenList;

/* Result of tokenizing: 0 on success, -1 on a syntax error (e.g. unterminated quote). */
int tokenize(const char *line, TokenList *out, char *errbuf, int errbuf_len);

void token_list_init(TokenList *list);
void token_list_free(TokenList *list);

#endif
