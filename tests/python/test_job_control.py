"""Interactive, pty-backed integration tests for job control and signals:
background jobs, `jobs`/`fg`/`bg`, SIGINT/SIGTSTP via process groups, and
absence of zombies. These need a real pty (pexpect), since tcsetpgrp /
terminal-generated signals only make sense with a controlling terminal.
"""

import os
import re
import subprocess
import time

import pexpect
import pytest

PROMPT = r"minishell\$ "


@pytest.fixture()
def shell(minishell_bin, shell_cwd):
    child = pexpect.spawn(minishell_bin, cwd=str(shell_cwd), timeout=5)
    child.expect(PROMPT)
    yield child
    if child.isalive():
        child.sendline("exit")
        try:
            child.expect(pexpect.EOF, timeout=2)
        except Exception:
            child.terminate(force=True)


def test_background_job_reports_id_and_pid(shell):
    shell.sendline("sleep 2 &")
    shell.expect(r"\[1\] \d+")
    shell.expect(PROMPT)


def test_jobs_lists_running_background_job(shell):
    shell.sendline("sleep 2 &")
    shell.expect(PROMPT)
    shell.sendline("jobs")
    shell.expect(r"\[1\]\+\s+Running\s+sleep 2 &")
    shell.expect(PROMPT)


def test_sigint_kills_foreground_job_shell_survives(shell):
    shell.sendline("sleep 30")
    shell.expect("sleep 30")
    # Matching the echoed input only proves the pty saw the keystrokes, not
    # that the shell has forked the job and handed it the terminal via
    # tcsetpgrp yet. Give that a moment, or SIGINT can land on the shell
    # (which correctly ignores it) instead of the still-not-foreground job.
    time.sleep(0.3)
    shell.sendcontrol("c")
    shell.expect(PROMPT)
    shell.sendline("echo still_alive")
    shell.expect("still_alive")
    shell.expect(PROMPT)


def test_sigtstp_stops_job_then_bg_then_fg(shell):
    shell.sendline("sleep 30")
    shell.expect("sleep 30")
    time.sleep(0.3)  # see note in test_sigint_kills_foreground_job_shell_survives
    shell.sendcontrol("z")
    shell.expect(r"\[1\]\+\s+Stopped\s+sleep 30")
    shell.expect(PROMPT)

    shell.sendline("jobs")
    shell.expect(r"\[1\]\+\s+Stopped\s+sleep 30")
    shell.expect(PROMPT)

    shell.sendline("bg")
    shell.expect(r"\[1\]\+ sleep 30 &")
    shell.expect(PROMPT)

    shell.sendline("jobs")
    shell.expect(r"\[1\]\+\s+Running\s+sleep 30")
    shell.expect(PROMPT)

    shell.sendline("fg")
    shell.expect("sleep 30")
    shell.sendcontrol("c")
    shell.expect(PROMPT)


def test_exit_status_after_signal(shell):
    shell.sendline("sleep 30")
    shell.expect("sleep 30")
    time.sleep(0.3)  # see note in test_sigint_kills_foreground_job_shell_survives
    shell.sendcontrol("c")
    shell.expect(PROMPT)
    shell.sendline("echo $?")
    shell.expect("130")
    shell.expect(PROMPT)


def test_no_zombies_after_background_job_completes(shell, minishell_bin):
    shell.sendline("sleep 1 &")
    shell.expect(PROMPT)
    time.sleep(1.5)
    shell.sendline("jobs")
    shell.expect(r"\[1\]\+\s+Done\s+sleep 1 &")
    shell.expect(PROMPT)

    ps = subprocess.run(["ps", "-o", "pid,stat,comm"], stdout=subprocess.PIPE, text=True)
    zombies = [
        line for line in ps.stdout.splitlines() if re.search(r"\bZ", line.split()[1] if len(line.split()) > 1 else "")
    ]
    assert zombies == []


def test_multiple_background_jobs_get_sequential_ids(shell):
    shell.sendline("sleep 2 &")
    shell.expect(r"\[1\] \d+")
    shell.expect(PROMPT)
    shell.sendline("sleep 2 &")
    shell.expect(r"\[2\] \d+")
    shell.expect(PROMPT)
