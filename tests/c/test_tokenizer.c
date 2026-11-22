#include <stdio.h>
#include <string.h>

#include "tokenizer.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static void test_simple_words(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("echo hello world", &tl, err, sizeof(err)) == 0);
    CHECK(tl.count == 4);
    CHECK(tl.tokens[0].type == TOK_WORD && strcmp(tl.tokens[0].text, "echo") == 0);
    CHECK(tl.tokens[1].type == TOK_WORD && strcmp(tl.tokens[1].text, "hello") == 0);
    CHECK(tl.tokens[2].type == TOK_WORD && strcmp(tl.tokens[2].text, "world") == 0);
    CHECK(tl.tokens[3].type == TOK_EOF);
    token_list_free(&tl);
}

static void test_single_quotes_literal(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("echo 'a $b \"c\" \\d'", &tl, err, sizeof(err)) == 0);
    CHECK(tl.count == 3);
    CHECK(strcmp(tl.tokens[1].text, "a $b \"c\" \\d") == 0);
    token_list_free(&tl);
}

static void test_double_quotes_escape(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("echo \"a\\\"b\\\\c\"", &tl, err, sizeof(err)) == 0);
    CHECK(tl.count == 3);
    CHECK(strcmp(tl.tokens[1].text, "a\"b\\c") == 0);
    token_list_free(&tl);
}

static void test_backslash_escape_outside_quotes(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("echo a\\ b", &tl, err, sizeof(err)) == 0);
    CHECK(tl.count == 3);
    CHECK(strcmp(tl.tokens[1].text, "a b") == 0);
    token_list_free(&tl);
}

static void test_operators(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("a < b > c >> d 2> e | f &", &tl, err, sizeof(err)) == 0);
    TokenType expect[] = {TOK_WORD, TOK_REDIR_IN, TOK_WORD, TOK_REDIR_OUT, TOK_WORD,
                          TOK_REDIR_APPEND, TOK_WORD, TOK_REDIR_ERR, TOK_WORD, TOK_PIPE,
                          TOK_WORD, TOK_AMP, TOK_EOF};
    CHECK(tl.count == (int)(sizeof(expect) / sizeof(expect[0])));
    for (size_t i = 0; i < sizeof(expect) / sizeof(expect[0]) && i < (size_t)tl.count; i++) {
        CHECK(tl.tokens[i].type == expect[i]);
    }
    token_list_free(&tl);
}

static void test_word_glued_to_redirect_target(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("cmd>out.txt", &tl, err, sizeof(err)) == 0);
    CHECK(tl.count == 4);
    CHECK(tl.tokens[0].type == TOK_WORD && strcmp(tl.tokens[0].text, "cmd") == 0);
    CHECK(tl.tokens[1].type == TOK_REDIR_OUT);
    CHECK(tl.tokens[2].type == TOK_WORD && strcmp(tl.tokens[2].text, "out.txt") == 0);
    CHECK(tl.tokens[3].type == TOK_EOF);
    token_list_free(&tl);
}

static void test_unterminated_single_quote(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("echo 'abc", &tl, err, sizeof(err)) == -1);
}

static void test_unterminated_double_quote(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("echo \"abc", &tl, err, sizeof(err)) == -1);
}

static void test_trailing_backslash(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("echo abc\\", &tl, err, sizeof(err)) == -1);
}

static void test_comment(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("echo hi # this is ignored", &tl, err, sizeof(err)) == 0);
    CHECK(tl.count == 3);
    token_list_free(&tl);
}

static void test_empty_line(void) {
    TokenList tl;
    char err[128];
    CHECK(tokenize("   ", &tl, err, sizeof(err)) == 0);
    CHECK(tl.count == 1);
    CHECK(tl.tokens[0].type == TOK_EOF);
    token_list_free(&tl);
}

int main(void) {
    test_simple_words();
    test_single_quotes_literal();
    test_double_quotes_escape();
    test_backslash_escape_outside_quotes();
    test_operators();
    test_word_glued_to_redirect_target();
    test_unterminated_single_quote();
    test_unterminated_double_quote();
    test_trailing_backslash();
    test_comment();
    test_empty_line();

    if (g_failures == 0) {
        printf("tokenizer: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "tokenizer: %d failure(s)\n", g_failures);
    return 1;
}
