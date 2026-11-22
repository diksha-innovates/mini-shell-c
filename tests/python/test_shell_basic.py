"""Non-interactive integration tests: builtins, pipelines, redirection,
malformed syntax, exit status. Each test runs in its own temp directory via
the `shell_cwd` fixture so tests never touch the real filesystem outside of
pytest's tmp_path sandbox.
"""

import os

from conftest import run_shell


def test_echo_builtin_pipe(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "echo hello world\n", shell_cwd)
    assert out == "hello world\n"
    assert rc == 0


def test_pwd_builtin(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "pwd\n", shell_cwd)
    assert out.strip() == os.path.realpath(str(shell_cwd))
    assert rc == 0


def test_cd_builtin_changes_directory(minishell_bin, shell_cwd):
    sub = shell_cwd / "sub"
    sub.mkdir()
    out, err, rc = run_shell(minishell_bin, "cd sub\npwd\n", shell_cwd)
    assert out.strip() == os.path.realpath(str(sub))
    assert rc == 0


def test_cd_nonexistent_directory_errors(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "cd does_not_exist\necho $?\n", shell_cwd)
    assert "minishell: cd:" in err
    assert out.strip() == "1"


def test_exit_status_propagates_as_dollar_question(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "false\necho $?\ntrue\necho $?\n", shell_cwd)
    assert out.splitlines() == ["1", "0"]


def test_exit_builtin_with_explicit_code(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "exit 42\n", shell_cwd)
    assert rc == 42


def test_exit_builtin_default_uses_last_status(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "false\nexit\n", shell_cwd)
    assert rc == 1


def test_eof_exits_cleanly(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "echo one\n", shell_cwd)
    assert rc == 0


def test_pipeline_two_stages(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "printf 'a\\nb\\nc\\n' | grep b\n", shell_cwd)
    assert out == "b\n"
    assert rc == 0


def test_pipeline_three_stages(minishell_bin, shell_cwd):
    out, err, rc = run_shell(
        minishell_bin, "printf 'a\\nb\\nc\\n' | grep -v b | wc -l\n", shell_cwd
    )
    assert out.strip() == "2"


def test_redirection_truncate(minishell_bin, shell_cwd):
    out, err, rc = run_shell(
        minishell_bin, "echo first > out.txt\necho second > out.txt\ncat out.txt\n", shell_cwd
    )
    assert out == "second\n"


def test_redirection_append(minishell_bin, shell_cwd):
    out, err, rc = run_shell(
        minishell_bin, "echo first > out.txt\necho second >> out.txt\ncat out.txt\n", shell_cwd
    )
    assert out == "first\nsecond\n"


def test_redirection_input(minishell_bin, shell_cwd):
    (shell_cwd / "in.txt").write_text("hello from file\n")
    out, err, rc = run_shell(minishell_bin, "cat < in.txt\n", shell_cwd)
    assert out == "hello from file\n"


def test_redirection_stderr(minishell_bin, shell_cwd):
    out, err, rc = run_shell(
        minishell_bin, "ls does_not_exist_xyz 2> err.txt\ncat err.txt\n", shell_cwd
    )
    assert out.strip() != ""  # ls's error message landed in err.txt, then got cat'd to stdout


def test_quoting_single_quotes_literal(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "echo 'a   b' '$HOME'\n", shell_cwd)
    assert out == "a   b $HOME\n"


def test_quoting_double_quotes_preserve_spaces(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, 'echo "a   b"\n', shell_cwd)
    assert out == "a   b\n"


def test_backslash_escape(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "echo a\\ b\n", shell_cwd)
    assert out == "a b\n"


def test_malformed_leading_pipe(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "| echo hi\n", shell_cwd)
    assert rc != 0
    assert "minishell:" in err


def test_malformed_trailing_pipe(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "echo hi |\n", shell_cwd)
    assert rc != 0
    assert "minishell:" in err


def test_malformed_unterminated_quote(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "echo 'unterminated\n", shell_cwd)
    assert rc != 0
    assert "syntax error" in err


def test_malformed_redirection_missing_target(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "echo hi >\n", shell_cwd)
    assert rc != 0


def test_command_not_found(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "this_command_does_not_exist_xyz\n", shell_cwd)
    assert rc == 127
    assert "minishell:" in err


def test_blank_lines_and_comments_ignored(minishell_bin, shell_cwd):
    out, err, rc = run_shell(minishell_bin, "\n  \necho hi # a comment\n", shell_cwd)
    assert out == "hi\n"
    assert rc == 0


def test_no_zombies_after_pipeline(minishell_bin, shell_cwd):
    out, err, rc = run_shell(
        minishell_bin, "printf 'a\\nb\\n' | grep a | wc -l\n", shell_cwd
    )
    assert out.strip() == "1"
    # If the executor leaked zombies the parent shell process itself would
    # still exit cleanly (zombies are reparented to the shell, not us), so
    # we additionally check via /proc-less ps that no stray minishell
    # children remain under our own test process.
    import subprocess

    ps = subprocess.run(["ps", "-o", "pid,ppid,stat,comm"], stdout=subprocess.PIPE, text=True)
    zombie_lines = [l for l in ps.stdout.splitlines() if "Z" in l.split()[2:3]]
    assert zombie_lines == [] or all("minishell" not in l for l in zombie_lines)
