#ifndef MINISHELL_JOBS_H
#define MINISHELL_JOBS_H

#include <sys/types.h>

#define MAX_JOBS 64

typedef enum { JOB_RUNNING, JOB_STOPPED, JOB_DONE } JobState;

typedef struct {
    int id;             /* 1-based job id shown to the user */
    pid_t pgid;         /* process group id of the job */
    pid_t pids[32];     /* member pids, for reaping/status bookkeeping */
    int npids;
    int statuses[32];   /* exit/stop status as returned by waitpid, per pid */
    int finished[32];   /* 1 once that pid has been reaped to completion */
    JobState state;
    int in_use;
    int notified;       /* whether "Done"/"Stopped" has been printed already */
    char command[256];  /* original command line, for `jobs` display */
    int last_exit_status; /* exit status of the last stage, once JOB_DONE */
} Job;

typedef struct {
    Job jobs[MAX_JOBS];
} JobTable;

void jobtable_init(JobTable *jt);

/* Registers a new job with the given pgid/pids and command text. Returns
 * the job's id, or -1 if the table is full. */
int jobtable_add(JobTable *jt, pid_t pgid, const pid_t *pids, int npids, const char *command,
                  int background);

Job *jobtable_find_by_pgid(JobTable *jt, pid_t pgid);
Job *jobtable_find_by_id(JobTable *jt, int id);
Job *jobtable_find_most_recent(JobTable *jt);

/* Records the wait() status for one member pid of whichever job owns it.
 * Returns the job it belongs to, or NULL if not found (e.g. unknown pid). */
Job *jobtable_record_status(JobTable *jt, pid_t pid, int status);

/* Recomputes aggregate job state from member statuses. */
void jobtable_refresh_state(Job *job);

/* Removes jobs that are JOB_DONE and have already been notified/reaped by
 * the caller (freeing their slot for reuse). */
void jobtable_reap_done(JobTable *jt);

#endif
