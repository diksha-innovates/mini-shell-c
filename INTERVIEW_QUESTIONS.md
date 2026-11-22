# Interview Questions — mini-shell-c

All answers are written in my own voice, grounded in the actual code in this repo.
File/function references are real — check them if you want more detail than fits here.

## a) Pitches & architecture walkthrough

**1. Give me the 60-second pitch.**
I built a small interactive Unix shell in C17 — tokenizer, a hand-written recursive
parser that builds an AST, and a fork/exec executor with real job control. It supports
pipelines, I/O redirection, background jobs, and Ctrl-C/Ctrl-Z handling through process
groups and `tcsetpgrp`, the same mechanism bash uses. I didn't try to be POSIX-complete
— no variable expansion except `$?`, no `;`/`&&`/`||`. The point was to actually touch
`fork`, `execvp`, `pipe`, `waitpid` with `WUNTRACED`/`WCONTINUED`, and async-signal-safe
handlers, not to re-implement bash.

**2. Give me the 3-minute version.**
Input flows through four stages, each a separate module. `tokenizer.c` turns a raw
line into tokens, handling quotes and escapes — single quotes are fully literal, double
quotes only let backslash escape `\` and `"`, and outside quotes backslash escapes the
next character. `parser.c` turns tokens into a `Pipeline` of `Command`s — argv array
plus up to four redirection targets — and critically never executes anything; that's a
hard boundary I kept so parsing bugs can't have side effects. `executor.c` is where the
real systems work is: for an `n`-stage pipeline, it opens `n-1` pipes, forks `n`
children, wires `dup2` for pipe ends and any explicit redirection, and sets up a
process group — the first child becomes the group leader, later children join it, and
both parent and child call `setpgid` to avoid a race. For a foreground job it hands the
terminal to that process group with `tcsetpgrp`, then blocks in `waitpid(-1, &status,
WUNTRACED)` — waiting on `-1` rather than just this job's pids so it can also reap and
report on unrelated background jobs that finish while it's blocked. `jobs.c` is the job
table: fixed array of 64 slots, ids are the lowest currently-free integer (not a
monotonic counter), and `jobtable_record_status` is the one place that interprets a
`waitpid` status into exited/stopped/continued. `signals.c` makes the shell ignore
`SIGINT`/`SIGTSTP` itself (so Ctrl-C at the prompt doesn't kill the shell) and resets
those to default in children right after fork. `SIGCHLD`'s handler does nothing but set
a flag — all real work happens in the main loop, which is the textbook async-signal-
safe pattern.

**3. Walk me through the architecture, but assume I'm about to read the code.**
Start at `main.c:process_line` — it's the glue: tokenize, parse, substitute `$?`,
execute, free. Then `include/shell.h` for the `ShellState` struct everything hangs off
(job table, shell's own pgid, terminal fd, last exit status). Then `executor.c:
run_external_pipeline` for the fork/pipe/dup2/setpgid choreography, and
`executor.c:wait_for_foreground_job` for the terminal handoff and wait loop. `jobs.c`
and `builtins.c` are supporting cast. I'd read them in that order.

## b) Module-by-module

**4. What does `tokenizer.c` do and why is it structured as a single `tokenize()`
function instead of a proper lexer state machine with separate functions per state?**
It is a state machine, just not factored into separate named functions — the states
(bare word, single-quoted, double-quoted, escaped) are inline `while` loops inside the
word-accumulation loop in `tokenize()` (`src/tokenizer.c`). I kept it as one function
because the states aren't reused anywhere else and splitting them into separate
functions would mean passing the scanner position (`i`), the output buffer, and the
word index between four or five functions for no real benefit — it would read like
indirection, not structure.

**5. What breaks if you remove the `2>` special case in the tokenizer?**
`echo oops 2> err.txt` would tokenize `2` as a word character glued onto nothing special
— actually it'd still produce a WORD "2" followed by `>` as a REDIR_OUT operator,
which changes semantics completely: `2` becomes a second argument to `echo`, and the
redirection becomes stdout truncation to `err.txt`, not stderr. The guard in
`tokenizer.c` (`c == '2' && line[i+1] == '>' && (i==0 || preceded by whitespace)`) is
what keeps `2>` atomic while still letting `foo2>bar` (where `2` is stuck to a longer
word) tokenize as a plain word followed by `>` — see
`test_word_glued_to_redirect_target` in `tests/c/test_tokenizer.c`.

**6. Why does `parser.c` build an entire AST instead of executing commands as they're
parsed (which would be simpler)?**
Two reasons. First, correctness: a parse error partway through a pipeline (e.g. missing
filename after the last stage's redirect) must not have already forked and exec'd the
earlier stages — if parsing could execute, a malformed command could leave orphaned
processes running. Second, testability: `tests/c/test_parser.c` tests the AST directly
with no process spawning at all, which is both faster and lets CI catch parser
regressions without touching `fork`.

**7. `Command` has four separate `char *` fields for redirection targets (`in_file`,
`out_file`, `append_file`, `err_file`) instead of a generic redirection list. Why?**
Because the grammar only ever allows one of each kind per command (you can't have two
stdout redirects on one command in this shell's grammar), a list would let the parser
accept `cmd > a > b` with no way to express "last one wins" vs "both apply" without
extra logic. Four fixed fields make "at most one of each" structurally enforced instead
of a runtime check. The cost is it doesn't generalize to fd-number redirection like
`3>&1` — out of scope here (see Limitations in the README), and if I added it I'd
likely switch to a generic list at that point.

**8. What does `jobs.c:lowest_free_job_id` do and why did you add it?**
It scans the fixed-size job table for the smallest integer not currently assigned to
an in-use job. I originally used a monotonic counter (`next_id++` on every pipeline,
foreground or background). The bug: foreground commands also got a job id (briefly,
until they finished), so if you ran three foreground commands before your first `&`,
that first background job showed up as `[4]`, not `[1]` — technically consistent but
surprising and not what bash does. Switching to "lowest free slot" fixed it; see the
Design decisions section of the README for the longer version.

**9. `builtins.c` runs `cd`/`pwd`/`exit`/`jobs`/`fg`/`bg` directly in the shell process.
What would break if you forked for these instead?**
`cd` and `exit` specifically: `chdir()` in a forked child only changes that child's
cwd, so the parent shell's working directory would never actually change — `cd` would
silently do nothing from the user's perspective. `exit` forked would just exit a
child, not the shell. `jobs`/`fg`/`bg` need direct access to `ShellState`'s job table
and need to call `tcsetpgrp` from the process that's actually the shell (the terminal's
foreground-process-group concept is per session/terminal, and the operations only make
sense coming from the real shell process).

**10. Why can't builtins run as one stage of a pipeline (`jobs | wc -l`)?**
Because running a builtin as a forked pipeline stage would silently behave differently
from running it standalone (per #9 — a forked `cd`/`exit` is a no-op from the user's
perspective), and I didn't want to implement "some builtins fork-safe, some aren't" as
a special case quietly. `execute_pipeline` only takes the builtin fast path when
`p->ncommands == 1`; otherwise every stage goes through `execvp`. Documented as a
limitation, not silently broken.

**11. What does `signals.c:child_restore_default_signals` undo, and why is it called in
the child, not the parent?**
It resets `SIGINT`, `SIGTSTP`, `SIGTTOU`, `SIGTTIN`, `SIGQUIT`, and `SIGCHLD` to
`SIG_DFL`. The shell process has `SIGINT`/`SIGTSTP`/etc. set to `SIG_IGN` (installed by
`shell_install_signal_handlers`) so that the shell itself survives Ctrl-C at the
prompt. Every `fork`ed child inherits that `SIG_IGN` disposition — if I didn't reset it,
every launched program (including things like `vim` or `less` that expect Ctrl-C/Ctrl-Z
to work normally) would also ignore those signals, which is wrong. It's called in the
child, right after `fork()` and before `setpgid`/`dup2`/`execvp`, because that's the
window where the process is still "the shell's code running as the child" before it
becomes "the user's program."

**12. What exactly does the `SIGCHLD` handler do, and why so little?**
`sigchld_handler` in `signals.c` does exactly one thing: `g_sigchld_received = 1`. That's
it. The reasoning is async-signal-safety: a signal handler can be invoked while the
main program is in the middle of almost any libc call, and most libc functions
(`malloc`, `printf`, `waitpid` in some implementations with internal locking, etc.) are
not safe to call from inside a handler because they might already hold a lock the
handler's thread is about to need again. Setting a `volatile sig_atomic_t` is one of
the few operations POSIX guarantees is safe. All the real work — `waitpid`, updating
the job table, printing "Done"/"Stopped" — happens in `reap_children()`, called from
the ordinary flow of `main.c`'s loop, not from signal context.

## c) Design trade-offs / "why not X"

**13. Why not use `readline` for the prompt instead of `getline`?**
`getline` is POSIX/glibc-standard, zero extra dependencies, and this shell doesn't need
history or line editing to demonstrate the systems concepts it's built around (fork,
pipes, signals, job control). Adding `readline` would mean a new third-party dependency
and linking complexity for a feature (arrow-key history) that's orthogonal to what the
project is actually testing.

**14. Why not support `;`, `&&`, `||`?**
Because it changes the grammar from "one pipeline" to "a list of pipelines joined by
control operators with short-circuit semantics," which interacts with job control in
ways that need actual design decisions (what does `sleep 5 && echo done &` even mean?
is the whole list one job or two?). I scoped it out explicitly and documented it rather
than bolting on a half-correct version.

**15. Why does `execute_pipeline` always route through `jobtable_add` even for
foreground commands that will finish in milliseconds — isn't that wasted table churn?**
It's the simplest way to reuse one code path (`wait_for_foreground_job`) for both "a
job that might get Ctrl-Z'd mid-flight" and "a job that's definitely going to finish
immediately" — the executor can't know in advance which one it is. The table is a
fixed 64-entry array with O(1) slot allocation (linear scan, but 64 entries is nothing),
so the "churn" is negligible. The alternative — special-casing "don't register a job
unless it backgrounds or gets stopped" — would mean duplicating the wait loop for the
"definitely foreground, definitely simple" case, which is exactly the kind of
duplication that caused the original job-id numbering bug (see Q8).

**16. Why `waitpid(-1, ...)` instead of waiting only on the specific job's pids?**
Covered in the README's Design decisions, but short version: if the shell only waits on
its own foreground job's pids, a background job finishing while the shell is blocked
wouldn't get noticed until the next blocking wait — which could be an arbitrarily long
time later if the user then runs another long-running foreground command. Waiting on
`-1` and dispatching by which job actually owns the returned pid (via
`jobtable_record_status`) means any job's state change gets handled the moment it
happens, foreground or not.

**17. Why is the job table a fixed-size array (`MAX_JOBS = 64`) instead of a dynamic
list?**
Interactive shells don't have hundreds of concurrent jobs in practice, and a fixed
array means no `malloc`/`free` in the job-bookkeeping hot path (which runs from
contexts adjacent to signal-flag-driven code) and no failure mode beyond "table full,"
which `jobtable_add` reports by returning `-1` rather than crashing. If this needed to
scale to thousands of jobs I'd reach for a dynamic array, but that's not this project's
problem.

**18. Why C17 instead of C++ for a shell, given the rest of the portfolio is C++?**
Because a shell's core job is wrapping POSIX syscalls (`fork`, `execvp`, `pipe`,
`waitpid`, `tcsetpgrp`) almost directly, and the assignment specifically wanted C here —
it's valuable to show RAII-free manual resource management (every `fork`'d fd closed
explicitly, every `malloc`'d token freed explicitly) since that's the actual skill
being tested, not whatever C++ would paper over.

## d) Deep language questions (C)

**19. Where does this code rely on undefined behavior, and how do you know it doesn't
hit it?**
The riskiest spots are buffer indexing in `tokenizer.c`'s word-accumulation loop (bounds
checked against `MAX_WORD_LEN` before every write) and the fixed-size `pipefds`/`pids`
arrays in `executor.c` (bounded by `MAX_PIPES = 63`, checked before each pipeline stage
loop iteration via `ncommands >= MAX_COMMANDS` in the parser). I verified there's no UB
by running the full test suite under both AddressSanitizer (catches out-of-bounds
reads/writes, use-after-free) and UndefinedBehaviorSanitizer (catches signed overflow,
misaligned access, etc.) — both configured as CMake options (`ENABLE_ASAN`,
`ENABLE_UBSAN`) and run by `scripts/build_and_test.sh`.

**20. Explain the fork/setpgid race and how this code avoids it.**
After `fork()`, there's no guarantee about which process — parent or child — runs
first. If the parent needs to call `tcsetpgrp(terminal, pgid)` right after forking all
pipeline stages, but a child hasn't called `setpgid` yet, the child could still be in
the shell's process group when the terminal signal arrives, or `tcsetpgrp` could fail
outright. The standard fix (used in `executor.c:run_external_pipeline`) is to have
*both* sides set the process group: the child calls `setpgid(0, pgid)` (or `setpgid(0,
0)` for the first child) immediately after fork, before any `dup2`/`exec`; the parent
also calls `setpgid(pid, pgid)` right after each fork. Whichever runs first "wins" and
the second call is a harmless no-op (same pgid). Only after all children are forked and
in their group does the parent call `tcsetpgrp`.

**21. Why does every forked child call `_exit()` instead of `exit()` on failure (e.g.
after a failed `execvp`)?**
`exit()` flushes stdio buffers and runs `atexit` handlers — in a child that's about to
report an error and die, that risks double-flushing a buffer it inherited a copy of
from the parent (the classic fork-then-exit stdio duplication bug), which is exactly
the bug I found and fixed with the `fflush(stdout)` before `fork()` in
`run_external_pipeline` — but `_exit()` is the belt-and-suspenders fix on top of that:
it skips all of that and just terminates, which is the correct thing for a child that
knows it's not going to become a well-behaved independent process (it failed to
`execvp`).

**22. What's the lifetime of the `argv` array passed to `execvp`, and who owns it?**
`Command.argv` is a `NULL`-terminated, heap-allocated array of heap-allocated strings,
built incrementally by `parser.c:command_add_arg` via `realloc`. Ownership: the
`Pipeline` that contains the `Command` owns it until `pipeline_free()` is called, which
frees every `argv[i]` string then the array itself (`parser.c:command_free`). In the
forked child right before `execvp`, we never free it — the child either `execvp`s
successfully (and the whole address space is replaced, so the leak is moot) or exits
via `_exit()` (ditto, OS reclaims everything). The parent frees it after
`execute_pipeline` returns, in `main.c:process_line`'s `pipeline_free(&pipeline)` call.

**23. Why `strdup` everywhere in the tokenizer/parser instead of pointing into the
original line buffer?**
Because the original line buffer (`getline`'s buffer in `main.c`) is reused across
loop iterations — `getline(&line, &cap, stdin)` may realloc or overwrite `line` on the
next call. If tokens pointed into it instead of owning their own copies, every token
from a previous line would become a dangling/stale pointer the moment the next line
was read. `strdup` trades a small allocation per token for correctness that doesn't
depend on the caller's buffer-reuse strategy.

**24. Explain the memory model implications of the `volatile sig_atomic_t
g_sigchld_received` flag — would `volatile` alone be safe on a multi-core machine?**
`volatile` only guarantees the compiler won't optimize away the read/write or reorder
it relative to other volatile accesses in program order — it says nothing about memory
visibility across cores or about atomicity of access shapes wider than the
architecture's natural read/write granularity. In this specific case it's actually
fine, for a C-specific reason rather than a general one: signal handlers in this
program run on the *same thread* (there's no `pthread_sigmask`/multi-threading here),
so there's no cross-core visibility question — it's a single-threaded program where a
signal handler briefly interrupts the main flow of control. `sig_atomic_t` specifically
guarantees that reads/writes of it can't be torn by signal delivery, which `volatile`
alone wouldn't guarantee on all hardware for a `sig_atomic_t`-sized value. If this were
multi-threaded, I'd need `atomic_bool`/`sig_atomic_t` with explicit memory-order
reasoning or a `pthread_mutex`, not just `volatile`.

**25. Alignment/ABI: does anything in this codebase depend on struct layout?**
No — everything stays within one process's own compilation (no shared-memory IPC, no
binary struct serialization across a process boundary), so the compiler is free to
lay out `Command`/`Pipeline`/`Job` however it likes; there's no ABI contract to honor.
(Contrast this with `plugin-system-cpp` elsewhere in this portfolio, where POD-struct
layout across a `dlopen` boundary *is* an explicit contract.)

**26. Exceptions vs. error codes — which did you use and why?**
C has no exceptions, so every fallible operation here returns an error via return code
(`-1`/`0` convention, or an `errbuf` out-parameter for human-readable messages — see
`tokenize()`'s and `parse_pipeline()`'s signatures). The discipline this forces: every
call site has to actually check the return value, which I did consistently (e.g.
`main.c:process_line` checks `tokenize()`'s return before touching the result).

**27. What's the complexity of the STL-equivalent operations here — e.g.
`jobtable_find_by_id`?**
There's no STL in C, but the equivalent reasoning applies: `jobtable_find_by_id`,
`jobtable_find_by_pgid`, `jobtable_find_most_recent`, and `lowest_free_job_id` are all
O(`MAX_JOBS`) = O(64) linear scans over a fixed array — effectively O(1) in practice
since the bound is a small compile-time constant, which is why I didn't reach for a
hash map keyed by job id; the constant-factor overhead of a hash map would exceed the
cost of scanning 64 array slots.

## e) OS / systems concepts

**28. What is a process group and why does job control need one per pipeline?**
A process group is a set of processes the kernel treats as a unit for signal delivery
purposes — sending a signal to a process group (e.g. via `killpg`, or implicitly via a
terminal-generated signal going to whichever group currently owns the controlling
terminal) delivers it to every member. A pipeline needs its own group because Ctrl-C
should stop *all* stages of `a | b | c` together, not just the first or last — if `b`
kept running after `a` got killed, you'd get a hung pipeline. `run_external_pipeline`
makes the first forked stage the group leader (`setpgid(0,0)`) and every later stage
join that same group.

**29. What does `tcsetpgrp` actually do, and what's the controlling terminal?**
The controlling terminal is the tty session-leader-associated terminal that generates
signals (`SIGINT` for Ctrl-C, `SIGTSTP` for Ctrl-Z, `SIGQUIT` for Ctrl-\) and delivers
them to whichever process group is currently its "foreground" group.
`tcsetpgrp(fd, pgid)` is the syscall that changes which process group is foreground for
that terminal. The shell calls it twice per foreground job: once to hand control to the
job's pgid before waiting, and once to take it back (`tcsetpgrp(fd, shell_pgid)`) after
the job exits or stops — see `wait_for_foreground_job` in `executor.c`.

**30. Why does the shell need to ignore `SIGTTOU`?**
`SIGTTOU` is sent to a background process group that tries to write to the controlling
terminal (or, relevantly here, that calls `tcsetpgrp` while not itself the foreground
group). If the shell didn't ignore `SIGTTOU`, calling `tcsetpgrp(terminal,
shell_pgid)` to reclaim the terminal after a job finishes could itself stop the shell,
since at that exact moment the shell's group isn't yet the foreground group — it's in
the process of becoming it.

**31. What's a zombie process, and how does this shell avoid leaving any?**
A zombie is a process that has exited but whose exit status hasn't been collected via
`wait`/`waitpid` yet — the kernel keeps a minimal entry (pid, exit status) until
someone reaps it. This shell avoids them by calling `reap_children()` (which loops
`waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)` until there's nothing left to
reap) after every processed line in `main.c`, in addition to the blocking
`waitpid(-1, ..., WUNTRACED)` loop in `wait_for_foreground_job`. Verified directly: the
pytest integration suite's `test_no_zombies_after_pipeline` and
`test_no_zombies_after_background_job_completes` check `ps` output for `Z` state after
pipelines and background jobs complete.

**32. What happens to a backgrounded job if the shell itself exits (e.g. on EOF) without
waiting for it?**
It gets reparented to the system's init/launchd process (pid 1) and keeps running
independently — it does *not* become a zombie under the shell, because the shell
process itself is gone, not just failing to reap. I verified this directly: see the
manual zombie-check in the build notes — backgrounding two `sleep` jobs then letting
the shell hit EOF immediately leaves no zombies and no orphaned `minishell` process,
just the detached `sleep` processes running to completion under init.

**33. Why does redirection use `dup2` instead of just passing a different fd to
`execvp` somehow?**
`execvp` doesn't take fd arguments — the called program always reads/writes fds 0/1/2
(stdin/stdout/stderr) by convention, inherited across `exec`. `dup2(fd, STDOUT_FILENO)`
makes fd 1 *become* a duplicate of whatever we opened (a file, or a pipe's write end),
so when the exec'd program writes to fd 1 — which is all it knows how to do — it's
actually writing wherever we redirected. This is also exactly why we must close the
original `fd` after the `dup2` (see `apply_redirections` in `executor.c`) — otherwise
we leak an extra fd pointing at the same file/pipe.

**34. Why must every pipe fd be closed in every child, even the ends it's not using?**
Two reasons. First, correctness for pipeline EOF detection: `cat | wc -l` only sees EOF
on its stdin pipe once *every* writer fd to that pipe is closed; if an unrelated
sibling child still holds the write end open (even unused), `wc` would block forever
waiting for more input that will never come. Second, fd exhaustion: a long-running
shell session that leaked a couple of fds per pipeline would eventually hit the
process's fd limit. `run_external_pipeline` closes every `pipefds[k][0]`/`pipefds[k][1]`
in every child after the relevant `dup2` calls, and the parent closes all of them too
once every child is forked.

**35. Virtual memory: what does `fork()` actually copy, and is it expensive?**
Conceptually `fork()` duplicates the entire address space, but in practice it's
copy-on-write (COW) — the kernel marks the parent's pages as shared read-only between
parent and child, and only actually copies a page when either side writes to it. For
this shell, most forked children immediately call `execvp`, which discards the entire
address space and maps in a new program — so the COW pages from `fork()` are almost
never actually copied; `fork()`+`execvp()` is cheap specifically because of this.

**36. How does dynamic linking relate to this project, if at all?**
Indirectly: every external command this shell launches (`ls`, `grep`, `cat`, etc.) is
itself a dynamically linked executable, and `execvp`'s job includes triggering the
dynamic linker (`dyld` on macOS, `ld.so` on Linux) to resolve the new program's shared
library dependencies before `main()` runs. The shell itself doesn't do any dynamic
linking of its own (no `dlopen` anywhere in this project — that's `plugin-system-cpp`,
elsewhere in this portfolio).

**37. Explain the pointer ownership story for a `TokenList`.**
`TokenList.tokens` is a `realloc`-grown array of `Token` structs (`tokenizer.c`); each
`Token` with `type == TOK_WORD` owns a separately `strdup`'d `text` pointer, everything
else has `text == NULL`. `token_list_free` frees every non-NULL `text`, then the array.
The caller (`main.c:process_line`) always calls `token_list_free(&tokens)` on every
exit path, including the two error paths (`tokenize` failed, `parse_pipeline` failed)
— I double-checked this specifically because it's the classic "forgot to free on the
error path" bug shape.

**38. What's the difference between `char **argv` and `char *argv[]` here, and does it
matter which one `Command.argv`'s type reads as?**
`Command.argv` is declared `char **argv` (`parser.h`). As a declaration, `char **argv`
and `char *argv[]` are interchangeable for a parameter/field of pointer-to-pointer
semantics — `argv[i]` means the same thing either way, there's no array-vs-pointer
distinction once you're past a top-level array parameter (which itself decays to a
pointer). It matters more for documentation intent: `char **argv` signals "a
heap-managed, resizable list of string pointers," which is accurate here since it's
built via `realloc` in `command_add_arg`, not a fixed-size array.

**39. Where does this code use raw file descriptors directly (not through a `FILE*`),
and why?**
Everywhere redirection and pipes are involved — `open()`, `pipe()`, `dup2()`, and
`close()` in `executor.c`'s `apply_redirections` and `run_external_pipeline` all
operate on raw `int` fds, not `FILE*`. That's a hard requirement, not a style choice:
`dup2` is what makes fd 0/1/2 point somewhere else for the *next* `execvp`, and the
exec'd program only knows about raw fds (it has no concept of this process's `FILE*`
objects, which don't survive `exec` — only the underlying fd table does, because fds
are a kernel-level concept, not a libc one).

**40. What fds does a freshly-forked pipeline child inherit, and which does it have to
close?**
By default, `fork()` duplicates the entire fd table — the child inherits *every* open
fd the parent had, including every pipe's read and write end for *every* stage of the
pipeline, not just its own neighbors. `run_external_pipeline` explicitly closes every
`pipefds[k][0]`/`pipefds[k][1]` pair in each child after it's done its own `dup2` calls
— without that, a downstream stage would inherit (and leave open) write ends it never
uses, which breaks EOF detection for the stage reading from that pipe (see Q34).

**41. Explain `O_CREAT`, `O_TRUNC`, and `O_APPEND` as used in `apply_redirections`, and
what mode bits matter.**
`O_CREAT` creates the file if it doesn't exist (using the `mode_t` argument, `0644`
here, for the permission bits on creation — subject to the umask). `O_TRUNC` (used for
`>`) truncates an existing file to zero length atomically as part of the `open()` call
— important because it means `echo x > f` can't leave stale trailing data from a
longer previous write. `O_APPEND` (used for `>>`) makes every subsequent `write()`
atomically seek to end-of-file first, which matters for correctness if multiple
writers touch the same file concurrently (not a scenario this shell specifically
creates, but it's the right flag for "append" semantics regardless).

**42. What's the signal-safety concern with calling `printf` from inside a signal
handler, concretely?**
`printf` (and `malloc`, which `printf` may call internally for buffering) are not on
POSIX's async-signal-safe function list because they can take internal locks (e.g. on
the `FILE*`'s buffer). If the main program is interrupted by a signal while it already
holds that lock (say, it's itself mid-`printf`) and the handler tries to `printf` too,
you get self-deadlock — the handler waits forever for a lock the interrupted code will
never release because the handler hasn't returned yet. That's exactly why
`sigchld_handler` in `signals.c` does nothing but set a `sig_atomic_t` flag, and every
actual `printf` (job notifications) happens later, in ordinary (non-handler) code.

**43. Does this project use `sigaction` or `signal()`, and why does it matter?**
`sigaction` (see `signals.c`), not the older `signal()`. `sigaction` gives explicit
control over the signal mask active during handler execution (`sa_mask`) and flags
like `SA_RESTART` (used here on `SIGCHLD` so interrupted blocking syscalls like
`waitpid` automatically retry instead of failing with `EINTR` — though the code also
defensively checks for `EINTR` and retries manually in `wait_for_foreground_job`'s
loop, belt-and-suspenders). `signal()`'s behavior around whether a handler
auto-resets and exactly which signals get blocked during handler execution varies by
historical Unix flavor — `sigaction` is unambiguous and portable.

**44. Are there any sockets or filesystem-watching mechanisms in this project?**
No — this shell has no networking at all (contrast with `http-proxy-server` elsewhere
in this portfolio, which is all sockets), and no filesystem-event watching (no
`inotify`/`FSEvents`). The filesystem interaction here is limited to `open`/`dup2`
for redirection and `chdir`/`getcwd` for `cd`/`pwd` — plain synchronous syscalls, no
async I/O of any kind.

**45. What's the difference between a process and a thread, and which does this shell
use for concurrency?**
A process has its own address space, fd table, and signal dispositions; a thread
shares all of that with its siblings in the same process and only has its own stack
and register state. This shell uses processes exclusively (`fork` per pipeline stage)
— there is no threading anywhere in this codebase. That's a deliberate match to how
real shells implement job control: you *want* each pipeline stage to have its own
address space (so one stage crashing doesn't corrupt another) and its own fd table
(so redirection via `dup2` only affects that stage), which threads wouldn't give you.

## f) Concurrency, correctness, edge cases

**46. Walk me through exactly what happens when the user presses Ctrl-Z during a
foreground job.**
The terminal driver sends `SIGTSTP` to whichever process group currently owns the
terminal — which, because `wait_for_foreground_job` already called
`tcsetpgrp(terminal, job->pgid)`, is the job's group, not the shell's. Every process in
that group gets stopped by the kernel (default `SIGTSTP` disposition, since
`child_restore_default_signals` reset it in each child after fork). The shell's
blocking `waitpid(-1, &status, WUNTRACED)` wakes up with `WIFSTOPPED(status)` true for
one of the job's pids; `jobtable_record_status` marks that pid's `finished[k] = 0` and
`jobtable_refresh_state` sets the job's aggregate state to `JOB_STOPPED` once every
non-finished pid is in the stopped state. `wait_for_foreground_job`'s loop condition
(`job->state == JOB_DONE || job->state == JOB_STOPPED`) breaks, the shell reclaims the
terminal via `tcsetpgrp(terminal, shell_pgid)`, prints the `[id]+  Stopped ...` line,
and returns to the prompt with the job still sitting in the table.

**47. What was the `WIFCONTINUED` bug exactly, and how would you have caught it without
manual pty testing?**
Covered in the README's Design decisions in detail, but the short version for
"how would you catch it automatically": a unit test can't catch this because it's an
interaction between the kernel's signal delivery and multi-state bookkeeping — you
need an actual stopped-then-continued process. The fix was validated with a real pty
(`pexpect`) sending literal Ctrl-Z then driving `bg`, and that interactive test became
the permanent regression test `test_sigtstp_stops_job_then_bg_then_fg` in
`tests/python/test_job_control.py`. The lesson generalizes: anything that depends on
`waitpid` status bits beyond the two obvious ones (exited, stopped) needs an
integration test that can actually produce all four `WIF*` states, not just unit tests
on the bookkeeping functions in isolation.

**48. How did you discover the stdout-buffering ordering bug, and why didn't the
automated tests catch it?**
By manually piping a multi-command script into the shell and eyeballing the output
order — `pwd`'s output appeared at the very end of the combined stream instead of where
it was logically run, because the shell's own libc stdout buffer was fully-buffered
(not a tty) and only flushed at process exit, while every external child (its own
separate buffer) flushed at its own exit. The automated pytest suite didn't catch it
because each test only asserts on one command's isolated output — it never asserted on
the *relative order* of a builtin's output versus a child process's output within the
same run. That's a real gap; I didn't add a dedicated ordering-regression test for it
(I'd add one using `subprocess.run` checking that builtin and external output interleave
in command order if I revisited this).

**49. Why does `wait_for_foreground_job` handle *other* jobs' notifications
(`notify_job_change(owner)`) inside what's supposed to be "waiting for job X"?**
Because the blocking call is `waitpid(-1, ...)`, not `waitpid(job->pgid, ...)` — any
child's status change wakes it up, not just job X's. If I only acted on changes to `X`
and silently discarded information about other jobs, a background job's "Done"
notification could be lost entirely (its status was already consumed by this `waitpid`
call and won't come again) or at best delayed. So the loop has to handle "it's some
other job" as a real case, not just an edge case to ignore.

**50. What happens if two background jobs finish at nearly the same instant?**
Each delivers its own `SIGCHLD` (POSIX doesn't guarantee one signal per child if
multiple arrive before the handler runs — signals of the same type can be merged/
coalesced), but that's fine here because the handler doesn't try to process each
delivery individually — it just sets one flag, and `reap_children()`'s `while` loop
calls `waitpid(..., WNOHANG, ...)` repeatedly until it returns `0` (nothing left),
draining every pending child status regardless of how many `SIGCHLD` deliveries
actually happened. This is precisely why handlers that only set a flag are robust to
signal coalescing in a way that "do the work per-signal" handlers aren't.

**51. Is there a race between a job finishing and the user typing `jobs` right after
backgrounding it?**
No observable incorrect state, by construction: `jobtable_add` happens synchronously in
`run_external_pipeline` right after forking, before control returns to the main loop
and the user could type anything — so by the time `jobs` could possibly run, the job is
already registered. The only timing question is whether the job has *finished* by the
time `jobs` runs, which is a real race but not a bug — it's inherent to "did it finish
yet," and the job table correctly reflects whatever `waitpid` has told it so far.

## g) Testing, sanitizers, CI, benchmarking

**52. Why split tests into C unit tests (CTest) and Python integration tests (pytest)
instead of one or the other?**
Different things are cheap to test at different levels. Tokenizer/parser logic is pure
(no I/O, no processes) — C unit tests linking directly against `minishell_core` give
fast, precise, process-spawn-free coverage of quoting/escaping/grammar edge cases
(`tests/c/test_tokenizer.c`, `tests/c/test_parser.c`). Job control and signal handling
are fundamentally about process/terminal interaction that a pure-C unit test can't
exercise without reimplementing half of pexpect inside the test binary — Python's
`pexpect` gives a real pty cheaply. Using the right tool for each layer kept both
suites small and fast (full `pytest` run is ~11-15s).

**53. Why do the job-control tests need a real pty, specifically?**
`tcsetpgrp` and terminal-generated `SIGINT`/`SIGTSTP` only mean anything in the context
of an actual controlling terminal — a plain pipe (`subprocess.PIPE`) has no concept of
"foreground process group" for signal delivery, and `isatty()` on a pipe is false, so
the shell wouldn't even call `tcsetpgrp` in the first place (see `main.c`:
`state.shell_is_interactive = isatty(STDIN_FILENO)`). `pexpect.spawn` allocates a real
pty, making the shell behave exactly as it would in a real terminal session.

**54. You hit a real race condition writing the pty tests (sending Ctrl-C right after
the echoed command). Explain it and the fix.**
`pexpect`'s `child.expect("sleep 30")` matches the terminal's *echo* of the typed
input, which the pty produces as soon as it receives the keystrokes — that happens
before the shell has even read the line via `getline`, let alone forked the job and
called `tcsetpgrp` to hand it the terminal. If `sendcontrol("c")` fires in that window,
`SIGINT` goes to whatever process group *is* currently foreground, which at that
instant is still the shell's own group — and the shell ignores `SIGINT` by design, so
nothing happens, and the test then times out waiting for a prompt that won't appear
until the (now orphaned, ignored-signal) `sleep 30` finishes 30 seconds later. The fix
is a small `time.sleep(0.3)` between seeing the echo and sending the control character,
giving the shell time to actually fork and reassign the terminal. This is a test-
harness timing issue, not a shell bug — I verified the shell's actual behavior (ignore
SIGINT while still foreground) is correct Unix shell semantics.

**55. What's the difference between what ASan and UBSan each catch, and why run both?**
AddressSanitizer instruments memory accesses to catch out-of-bounds reads/writes,
use-after-free, and (on Linux) use-after-scope — it's about *memory safety*.
UndefinedBehaviorSanitizer instruments operations with UB per the C standard — signed
integer overflow, misaligned pointer access, invalid enum values, null pointer
arithmetic — independent of whether that UB happens to corrupt memory. They catch
different bug classes, so both are wired as separate CMake options (`ENABLE_ASAN`,
`ENABLE_UBSAN`) and both are built and tested by `scripts/build_and_test.sh`.

**56. Why doesn't this project run with LeakSanitizer / `detect_leaks=1`?**
I tried it directly on this machine (macOS/Apple Silicon) and AddressSanitizer reports
"detect_leaks is not supported on this platform" and aborts immediately — LeakSanitizer
needs process-wide stop-the-world introspection that Apple's sandboxed/hardened runtime
doesn't support the same way Linux does. The CI matrix does run `ubuntu-latest`, where
LSan is available as part of ASan by default, so leak detection does happen there even
though it's not exercised locally on macOS. This is a documented, verified platform
limitation, not an oversight.

**57. Why is there no ThreadSanitizer build for this project, when the global rules
mention it for multithreaded projects?**
This shell is single-threaded by design — job control is modeled as separate *processes*
(via `fork`), not threads, which is exactly how real shells do it and avoids an entire
class of shared-mutable-state bugs. `SIGCHLD` touches one global (`g_sigchld_received`),
read/written only from signal-handler context and the single main thread, never from a
second thread — there's no data race to find. TSan is appropriately reserved for the
genuinely multithreaded projects in this portfolio (`http-proxy-server`'s thread pool).

**58. How would you benchmark this shell, and why doesn't the README include numbers?**
A shell's interesting "performance" questions are mostly fork/exec latency and pipeline
throughput, which are dominated by the kernel and the exec'd programs, not by this
shell's own code — benchmarking `tokenize()`/`parse_pipeline()` in isolation (they're
each a single linear pass) wouldn't tell an honest story about "shell performance" the
way, say, matmul throughput does for `tensor-graph-engine`. I deliberately didn't invent
benchmark numbers for a project where the bottleneck is syscall/kernel overhead outside
my control, rather than present a misleading microbenchmark as if it mattered.

## h) Real debugging stories

**59. Tell me about a real bug you hit and how you found it.**
The `WIFCONTINUED` misclassification (Q38/README). I was manually driving the shell
through a pty script: stop a job with Ctrl-Z, confirm `jobs` shows "Stopped," run `bg`,
then immediately saw `[1]+  Done  sleep 30` print — one line after the `bg` confirmation
— even though the sleep had maybe run for half a second of its 30. That's physically
impossible for a real 30-second sleep to finish, so it was clearly a bookkeeping bug,
not a real completion. I traced it to `jobtable_record_status`'s original `if
(WIFSTOPPED(status)) finished=0; else finished=1;` — the `else` branch silently
swallowed `WIFCONTINUED` (sent after the `SIGCONT` from `bg`) and treated it as "the
process is done," which is wrong on two counts: continued isn't done, and the stale
`WIFSTOPPED` status bits left in `statuses[]` also weren't being overwritten, so
`jobtable_refresh_state`'s aggregation was working off outdated data too.

**60. Tell me about a bug that didn't crash anything but was still wrong.**
The stdout-ordering bug (Q39) — nothing crashed, no test failed, the shell just printed
output in the wrong relative order when piped non-interactively, because its own
builtin output sat in a fully-buffered libc stream until exit while child processes
flushed on their own. It's the kind of bug that's invisible if you only test one
command's output in isolation (which is exactly what my pytest suite does, per-test) but
very visible the moment a human runs a multi-command script and reads the transcript
top to bottom.

**61. Did you hit the fork/setpgid race in practice, or was that defensive coding?**
Mostly defensive/informed-by-the-well-known-pitfall rather than something I personally
watched fail — it's a textbook race (every "write a shell" guide, including Stevens'
APUE, calls it out explicitly) and the fix is cheap enough (one extra `setpgid` call on
the parent side) that I included it from the start rather than waiting to reproduce a
timing-dependent failure that might only show up occasionally under load.

## i) Scaling / production follow-ups

**62. How would you make this 10x faster?**
Honestly, the shell's own code isn't the bottleneck for anything that matters — fork
+execvp latency is dominated by the kernel and dynamic linker, not by tokenizing or
parsing a line. If forced to find 10x somewhere, I'd look at reducing fork overhead for
very short-lived commands via `posix_spawn` (which can skip some of the fork-then-exec
COW setup on some platforms) — but that's a kernel-level win, not an algorithmic one in
this codebase.

**63. What would you need to add to call this "production ready"?**
Real expansion (`$VAR`, globbing) and control operators (`;`, `&&`, `||`) — without
those it's not usable as a daily-driver shell, just a demonstration of the job-control
mechanics. I'd also want a `select`/`poll`-driven main loop (so background job
completion is reported the instant `SIGCHLD` fires instead of on the next `getline`
return), proper `$PATH` caching, and a real history/line-editing story (readline or
similar) rather than bare `getline`.

**64. How would this look distributed — e.g. a shell that can run commands on remote
hosts?**
That's really a different project (something like a cluster job scheduler, or SSH
multiplexing) rather than a natural extension of this one — this shell's entire design
rests on local process groups and a local controlling terminal, both of which stop
being meaningful the moment "the job" is running on another machine. I'd keep the
local tokenizer/parser (that part generalizes fine) but the executor and job-control
layer would need to become an RPC client instead of `fork`/`waitpid`.

## j) Authorship cross-questions

**65. Did you write every line of this, or generate it?**
I used AI-assisted tooling heavily while building this, the same way I'd use any
other dev tool — but I made the actual design calls (what grammar to support, which
`waitpid` flags to use, how to structure the job table) and I'm the one who ran the
pty-based manual tests that caught the two real bugs described above (Q38/Q39). If
you ask me to change the fork/setpgid ordering or explain why `reap_children` uses
`WNOHANG` while `wait_for_foreground_job` doesn't, I can do that live — that's the bar
I'd hold myself to for "did I actually understand what I shipped."

**66. If I asked you to add `;` support right now, where would you start?**
In `parser.h`/`parser.c` — I'd change the top-level grammar from "one pipeline" to "a
list of pipelines," probably by having `tokenize()` emit a `TOK_SEMICOLON` token (the
tokenizer already has the infrastructure for single-character operators — see how `|`
and `&` are handled) and adding a loop in a new `parse_command_list()` that calls
`parse_pipeline()` repeatedly, splitting on `;` and `TOK_EOF`. Then `main.c` would loop
over the resulting list calling `execute_pipeline()` for each, which already works
standalone — the main new design question is what `&` means for one pipeline in a `;`-
separated list (I'd keep it per-pipeline, matching bash).

**67. What's the one part of this codebase you'd want to rewrite if you had another
week?**
The main loop's reaping strategy (`reap_children()` called synchronously after every
`getline`). It works and is well-tested, but it's not actually event-driven — a
background job could finish and not get its "Done" notification printed until the user
hits Enter on something else. A `poll()`-based main loop watching both stdin and a
self-pipe written to from the `SIGCHLD` handler (the "self-pipe trick," since you can't
safely call `write()` to a pipe *from* the handler without it being specifically
designed for that — which the self-pipe trick is) would make notifications immediate.

**68. Why is there a fixed `MAX_WORD_LEN` (65536) buffer in the tokenizer instead of a
growable buffer like `argv`'s `realloc` pattern?**
Mostly consistency of risk profile: a single shell word longer than 64KB is not a
realistic interactive-shell input, so a fixed stack buffer with an explicit bounds
check (`if (wi >= MAX_WORD_LEN - 1) { ...error...}`, checked before every write in
`tokenizer.c`) gets me a hard, test-covered upper bound with zero allocation in the hot
per-character loop, versus `realloc`-growing a word buffer the way `command_add_arg`
grows `argv` (which *is* the right call there, since argument counts vary over a much
wider practical range than any single word's length does).

**69. Why does `main.c` support three modes (interactive REPL, `-c COMMAND`, and a
script-file argument) instead of just the REPL?**
Because the Python integration tests need a deterministic, non-interactive way to run
a fixed sequence of commands and check output/exit status — piping into the REPL mode
already covers most of that (`run_shell` in `tests/python/conftest.py` does exactly
this), but `-c` and script-file mode are the same pattern real shells support (`sh -c
'...'`, `sh script.sh`) and cost very little extra code (`run_script_file` in
`main.c` is a near-duplicate of the REPL's loop, minus the prompt).

**70. Why does `main.c` only print the `minishell$ ` prompt when `isatty(STDIN_FILENO)`
is true?**
So piped/scripted input (which is exactly what every automated test does) gets clean
stdout with no prompt noise mixed in — `conftest.py`'s `run_shell` would otherwise have
to strip `minishell$ ` out of every expected-output string. It also matches real shell
behavior: bash doesn't print a PS1 prompt when its stdin isn't a terminal either.

**71. The exit status for "killed by a signal" is computed as `128 + signal_number`
(see `jobtable_record_status`'s `WIFSIGNALED` branch and `wait_for_foreground_job`'s
stopped-case fallback). Where does that convention come from, and did you invent it?**
It's the standard Unix shell convention (bash, dash, zsh all do this) — not something I
invented. The reasoning: exit statuses are conventionally in `0..255`, and
`128 + signal` leaves a distinguishable, greppable range (`129..255` roughly) that
can't collide with a program's own deliberate `exit()` codes (which are almost always
`<128` in practice), while still encoding *which* signal killed it (`$?  - 128` recovers
the signal number). I followed the convention because tests and scripts that pipe into
other tools expect it (e.g. `echo $?` showing `130` after Ctrl-C, which is
`128 + SIGINT(2)`).

**72. What does the `notified` field on a `Job` actually prevent?**
Printing the same "Done" line more than once. `reap_children()` can observe a job
that's already `JOB_DONE` on more than one call before `jobtable_reap_done()` actually
frees its slot (e.g. if `waitpid` returns the same already-handled pid's status is
impossible, but the *job* stays in the done-and-notified state across multiple
`reap_children()` calls until the slot is reused) — `notified` is the guard in
`notify_job_change()` (`executor.c`) that makes printing idempotent: once set, that job
won't print "Done" again even if `reap_children()` runs several more times before the
slot is recycled.

**73. If I added a 65th concurrent job, what actually happens, step by step?**
`jobtable_add` (`jobs.c`) loops over all `MAX_JOBS` (64) slots looking for one with
`in_use == 0`; if none exists, it falls through the loop and returns `-1` — no crash,
no silent corruption. The caller, `run_external_pipeline` in `executor.c`, would need
to check that return value (worth double-checking: currently it calls
`jobtable_find_by_id(&state->jobs, id)` immediately after, and `id == -1` would make
that lookup fail too, returning `NULL` — this is a real edge case I'd want an explicit
"job table full" error message for rather than relying on the downstream `NULL` to be
handled gracefully everywhere; flagging it here as an honest gap rather than claiming
it's already bulletproof).

**74. How does the CI matrix (`macos-latest`, `ubuntu-latest`) differ from what you
actually verified locally?**
Locally (Apple Silicon macOS) I ran all three CMake configurations (Debug, ASan,
UBSan) plus the full pytest suite, three times each, and confirmed LeakSanitizer is
unavailable on this platform (`detect_leaks is not supported on this platform`). The
`ubuntu-latest` CI leg runs the identical steps — same `scripts/build_and_test.sh`-
equivalent commands inlined into `.github/workflows/ci.yml` — but I have not personally
watched it go green on a real Linux box as part of this build; it's written to be
portable (POSIX APIs throughout, no macOS-only calls), but "written to be portable"
and "verified on Linux" are different claims, and I'm being explicit about which one
I'm making.

**75. Why does `jobtable_reap_done` only free a job's slot if it's both `JOB_DONE` and
`notified`, instead of freeing it as soon as it's done?**
So `jobs`/`fg`/`bg` and the "Done" notification itself have a chance to see and report
the job in its finished state before the slot disappears out from under them. If a job
were freed the instant `waitpid` reported it exited, a `jobs` command racing against
`reap_children()` could either miss showing "Done" entirely or, worse, read from a
slot that's already been reused by a brand-new job with a different command string.
Requiring `notified` first guarantees the user-visible "Done" line always gets printed
before the bookkeeping for that job disappears.

## k) Cheat sheet — 15 facts to memorize

1. Grammar: `job := pipeline ['&']`, `pipeline := command ('|' command)*`, `command :=
   word+ redirection*`. No `;`/`&&`/`||`/subshells/globbing — documented, not accidental.
2. `$?` is the *only* expansion supported — exact-match substitution on a standalone
   `$?` token, done in `main.c:substitute_last_status` before `execute_pipeline`.
3. Tokenizer quoting: single quotes = fully literal; double quotes = `\` escapes only
   `\`/`"`; unquoted = `\` escapes the next char.
4. Parsing never executes anything — a hard boundary, verified by the fact that
   `tests/c/test_parser.c` never forks a process.
5. Pipeline execution: `n` stages → `n-1` pipes → `n` forks; every unused fd closed in
   every child (`executor.c:run_external_pipeline`).
6. Process groups: first forked stage is the leader (`setpgid(0,0)`); later stages join
   it; both parent and child call `setpgid` (race fix).
7. `SIGINT`/`SIGTSTP`/`SIGTTOU`/`SIGTTIN`/`SIGQUIT` are `SIG_IGN` in the shell, reset to
   `SIG_DFL` in every child right after `fork`, before `exec`.
8. `SIGCHLD` handler does exactly one thing: sets `g_sigchld_received` (async-signal-
   safe). All reaping happens in `reap_children()`/`wait_for_foreground_job()` outside
   signal context.
9. Foreground wait uses `waitpid(-1, &status, WUNTRACED)` — `-1`, not the job's own
   pids — so it also catches other jobs changing state while blocked.
10. Job ids: lowest currently-free integer, not a monotonic counter (bug fixed; see
    `jobs.c:lowest_free_job_id`).
11. `WIFCONTINUED` must be handled explicitly in `jobtable_record_status` — treating
    "not stopped" as "done" is a real bug I hit and fixed.
12. `stdout` is forced line-buffered (`setvbuf(..., _IOLBF, ...)`) even when not a tty,
    plus an `fflush(stdout)` before every `fork()` — fixes a real output-ordering bug.
13. No zombies: `reap_children()` drains `waitpid(WNOHANG|WUNTRACED|WCONTINUED)` after
    every line; verified by pytest (`test_no_zombies_after_*`).
14. Tests: 2 C/CTest unit tests (tokenizer, parser, no process spawning) + 31 Python
    (pytest) integration tests — 24 non-interactive (`subprocess`) + 7 pty-based
    (`pexpect`, needed for real `tcsetpgrp`/signal semantics).
15. Sanitizers: ASan + UBSan both run (CMake options `ENABLE_ASAN`/`ENABLE_UBSAN`);
    LeakSanitizer unavailable on macOS (`detect_leaks is not supported on this
    platform`) but active under ASan on the `ubuntu-latest` CI leg.
