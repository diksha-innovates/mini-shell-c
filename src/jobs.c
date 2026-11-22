#include "jobs.h"

#include <string.h>
#include <sys/wait.h>

void jobtable_init(JobTable *jt) {
    memset(jt, 0, sizeof(*jt));
}

/* Lowest id not currently held by any in-use job, mirroring how bash
 * reuses small job numbers instead of handing out an ever-growing
 * counter (which would make the first backgrounded job show up as, say,
 * [7] just because six foreground commands ran earlier). */
static int lowest_free_job_id(const JobTable *jt) {
    for (int candidate = 1; candidate <= MAX_JOBS; candidate++) {
        int taken = 0;
        for (int i = 0; i < MAX_JOBS; i++) {
            if (jt->jobs[i].in_use && jt->jobs[i].id == candidate) {
                taken = 1;
                break;
            }
        }
        if (!taken) {
            return candidate;
        }
    }
    return -1;
}

int jobtable_add(JobTable *jt, pid_t pgid, const pid_t *pids, int npids, const char *command,
                  int background) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jt->jobs[i].in_use) {
            Job *j = &jt->jobs[i];
            memset(j, 0, sizeof(*j));
            j->id = lowest_free_job_id(jt);
            j->pgid = pgid;
            j->npids = npids < 32 ? npids : 32;
            for (int k = 0; k < j->npids; k++) {
                j->pids[k] = pids[k];
            }
            j->state = JOB_RUNNING;
            j->in_use = 1;
            j->notified = background ? 0 : 1; /* foreground jobs don't need a "Done" line */
            strncpy(j->command, command, sizeof(j->command) - 1);
            j->last_exit_status = 0;
            return j->id;
        }
    }
    return -1;
}

Job *jobtable_find_by_pgid(JobTable *jt, pid_t pgid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jt->jobs[i].in_use && jt->jobs[i].pgid == pgid) {
            return &jt->jobs[i];
        }
    }
    return NULL;
}

Job *jobtable_find_by_id(JobTable *jt, int id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jt->jobs[i].in_use && jt->jobs[i].id == id) {
            return &jt->jobs[i];
        }
    }
    return NULL;
}

Job *jobtable_find_most_recent(JobTable *jt) {
    Job *best = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jt->jobs[i].in_use && (!best || jt->jobs[i].id > best->id)) {
            best = &jt->jobs[i];
        }
    }
    return best;
}

Job *jobtable_record_status(JobTable *jt, pid_t pid, int status) {
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jt->jobs[i];
        if (!j->in_use) {
            continue;
        }
        for (int k = 0; k < j->npids; k++) {
            if (j->pids[k] == pid) {
                /* Always record the raw status so statuses[] never keeps a
                 * stale WIFSTOPPED bit pattern around after a SIGCONT. */
                j->statuses[k] = status;
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    j->finished[k] = 1;
                    j->last_exit_status =
                        WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                } else {
                    /* WIFSTOPPED or WIFCONTINUED: neither finished nor exited. */
                    j->finished[k] = 0;
                }
                jobtable_refresh_state(j);
                return j;
            }
        }
    }
    return NULL;
}

void jobtable_refresh_state(Job *job) {
    int all_finished = 1;
    int any_stopped = 0;
    for (int k = 0; k < job->npids; k++) {
        if (!job->finished[k]) {
            all_finished = 0;
        }
        if (WIFSTOPPED(job->statuses[k])) {
            any_stopped = 1;
        }
    }
    if (all_finished) {
        job->state = JOB_DONE;
    } else if (any_stopped) {
        /* Only "stopped" if the not-yet-finished ones are the stopped ones. */
        int running_unfinished = 0;
        for (int k = 0; k < job->npids; k++) {
            if (!job->finished[k] && !WIFSTOPPED(job->statuses[k])) {
                running_unfinished = 1;
            }
        }
        job->state = running_unfinished ? JOB_RUNNING : JOB_STOPPED;
    } else {
        job->state = JOB_RUNNING;
    }
}

void jobtable_reap_done(JobTable *jt) {
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jt->jobs[i];
        if (j->in_use && j->state == JOB_DONE && j->notified) {
            j->in_use = 0;
        }
    }
}
