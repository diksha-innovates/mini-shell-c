#include "signals.h"

#include <stddef.h>

volatile sig_atomic_t g_sigchld_received = 0;

static void sigchld_handler(int signo) {
    (void)signo;
    g_sigchld_received = 1; /* only async-signal-safe operation; real work in main loop */
}

void shell_install_signal_handlers(void) {
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);

    struct sigaction sa_ign;
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags = 0;
    sigaction(SIGINT, &sa_ign, NULL);
    sigaction(SIGTSTP, &sa_ign, NULL);
    sigaction(SIGTTOU, &sa_ign, NULL);
    sigaction(SIGTTIN, &sa_ign, NULL);
    sigaction(SIGQUIT, &sa_ign, NULL);
}

void child_restore_default_signals(void) {
    struct sigaction sa_dfl;
    sa_dfl.sa_handler = SIG_DFL;
    sigemptyset(&sa_dfl.sa_mask);
    sa_dfl.sa_flags = 0;
    sigaction(SIGINT, &sa_dfl, NULL);
    sigaction(SIGTSTP, &sa_dfl, NULL);
    sigaction(SIGTTOU, &sa_dfl, NULL);
    sigaction(SIGTTIN, &sa_dfl, NULL);
    sigaction(SIGQUIT, &sa_dfl, NULL);
    sigaction(SIGCHLD, &sa_dfl, NULL);
}
