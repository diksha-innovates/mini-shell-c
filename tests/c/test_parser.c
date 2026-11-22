#include <stdio.h>
#include <string.h>

#include "parser.h"
#include "tokenizer.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static int parse_line(const char *line, Pipeline *p, char *err, int errlen) {
    TokenList tl;
    char terr[128];
    if (tokenize(line, &tl, terr, sizeof(terr)) != 0) {
        snprintf(err, (size_t)errlen, "%s", terr);
        return -1;
    }
    int rc = parse_pipeline(&tl, p, err, errlen);
    token_list_free(&tl);
    return rc;
}

static void test_single_command(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("echo hello", &p, err, sizeof(err)) == 0);
    CHECK(p.ncommands == 1);
    CHECK(p.background == 0);
    CHECK(strcmp(p.commands[0].argv[0], "echo") == 0);
    CHECK(strcmp(p.commands[0].argv[1], "hello") == 0);
    CHECK(p.commands[0].argv[2] == NULL);
    pipeline_free(&p);
}

static void test_pipeline_multi_stage(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("ls -l | grep foo | wc -l", &p, err, sizeof(err)) == 0);
    CHECK(p.ncommands == 3);
    CHECK(strcmp(p.commands[0].argv[0], "ls") == 0);
    CHECK(strcmp(p.commands[1].argv[0], "grep") == 0);
    CHECK(strcmp(p.commands[2].argv[0], "wc") == 0);
    pipeline_free(&p);
}

static void test_redirections(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("sort < in.txt > out.txt 2> err.txt", &p, err, sizeof(err)) == 0);
    CHECK(p.ncommands == 1);
    CHECK(strcmp(p.commands[0].in_file, "in.txt") == 0);
    CHECK(strcmp(p.commands[0].out_file, "out.txt") == 0);
    CHECK(strcmp(p.commands[0].err_file, "err.txt") == 0);
    pipeline_free(&p);
}

static void test_append_redirection(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("cat >> out.log", &p, err, sizeof(err)) == 0);
    CHECK(strcmp(p.commands[0].append_file, "out.log") == 0);
    CHECK(p.commands[0].out_file == NULL);
    pipeline_free(&p);
}

static void test_background_flag(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("sleep 5 &", &p, err, sizeof(err)) == 0);
    CHECK(p.background == 1);
    CHECK(p.commands[0].argc == 2);
    pipeline_free(&p);
}

static void test_redirection_per_stage(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("sort < a.txt | uniq > b.txt", &p, err, sizeof(err)) == 0);
    CHECK(p.ncommands == 2);
    CHECK(strcmp(p.commands[0].in_file, "a.txt") == 0);
    CHECK(p.commands[1].out_file != NULL && strcmp(p.commands[1].out_file, "b.txt") == 0);
    pipeline_free(&p);
}

static void test_leading_pipe_is_error(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("| echo hi", &p, err, sizeof(err)) == -1);
}

static void test_trailing_pipe_is_error(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("echo hi |", &p, err, sizeof(err)) == -1);
}

static void test_redirect_missing_target_is_error(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("echo hi >", &p, err, sizeof(err)) == -1);
}

static void test_amp_not_last_is_error(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("echo hi & echo bye", &p, err, sizeof(err)) == -1);
}

static void test_empty_command_is_error(void) {
    Pipeline p;
    char err[128];
    CHECK(parse_line("", &p, err, sizeof(err)) == -1);
}

int main(void) {
    test_single_command();
    test_pipeline_multi_stage();
    test_redirections();
    test_append_redirection();
    test_background_flag();
    test_redirection_per_stage();
    test_leading_pipe_is_error();
    test_trailing_pipe_is_error();
    test_redirect_missing_target_is_error();
    test_amp_not_last_is_error();
    test_empty_command_is_error();

    if (g_failures == 0) {
        printf("parser: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "parser: %d failure(s)\n", g_failures);
    return 1;
}
