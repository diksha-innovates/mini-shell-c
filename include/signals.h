#ifndef MINISHELL_SIGNALS_H
#define MINISHELL_SIGNALS_H

#include <signal.h>

/* Set by the (async-signal-safe) SIGCHLD handler; the main loop polls it
 * and does all real bookkeeping (waitpid, job table updates, printing)
 * outside of signal context. */
extern volatile sig_atomic_t g_sigchld_received;

/* Installs the shell's signal disposition: SIGCHLD gets our handler;
 * SIGINT/SIGTSTP/SIGTTOU/SIGTTIN are ignored by the shell itself so that
 * terminal-generated signals only affect whichever process group
 * currently owns the terminal (the foreground job, via tcsetpgrp). */
void shell_install_signal_handlers(void);

/* Restores default dispositions for SIGINT/SIGTSTP/SIGTTOU/SIGTTIN/SIGCHLD.
 * Must be called in the child after fork(), before execvp(), so launched
 * jobs behave like normal processes with respect to these signals. */
void child_restore_default_signals(void);

#endif
