import os
import shutil
import subprocess

import pytest

BIN_ENV_VAR = "MINISHELL_BIN"


def _find_binary():
    env = os.environ.get(BIN_ENV_VAR)
    if env and os.path.exists(env):
        return os.path.abspath(env)
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    candidate = os.path.join(repo_root, "build", "minishell")
    if os.path.exists(candidate):
        return candidate
    raise RuntimeError(
        "Could not locate the minishell binary. Build it first (cmake --build build) "
        f"or set {BIN_ENV_VAR}."
    )


@pytest.fixture(scope="session")
def minishell_bin():
    return _find_binary()


@pytest.fixture()
def shell_cwd(tmp_path):
    """A fresh, empty temp directory for each test to run the shell in."""
    return tmp_path


def run_shell(minishell_bin, script_text, cwd, timeout=5):
    """Feeds `script_text` (newline-separated commands) to minishell over
    stdin, non-interactively, and returns (stdout, stderr, returncode)."""
    proc = subprocess.run(
        [minishell_bin],
        input=script_text,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    return proc.stdout, proc.stderr, proc.returncode


def which_in(cwd, name):
    return shutil.which(name) is not None
