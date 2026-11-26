// Shader / pipeline job queue helpers: head enqueue for urgent jobs and work stealing by argument.
// From BOTWRecomp e8fac0f (job_queue.h; TOTK keeps its own gated worker pop in exec.c).
#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct MwJob {
    void (*fn)(void *);
    void *arg;
    struct MwJob *next;
} MwJob;

// false: out of memory (nothing queued - the caller runs the job itself)
static inline bool mw_job_push(pthread_mutex_t *lock, pthread_cond_t *cond, MwJob **head, MwJob **tail,
                               void (*fn)(void *), void *arg, bool urgent) {
    MwJob *j = (MwJob *)malloc(sizeof *j);
    if (!j) return false;
    j->fn = fn;
    j->arg = arg;
    j->next = NULL;
    pthread_mutex_lock(lock);
    if (urgent) {
        j->next = *head;
        *head = j;
        if (!*tail) *tail = j;
    } else {
        if (*tail) (*tail)->next = j;
        else *head = j;
        *tail = j;
    }
    pthread_cond_signal(cond);
    pthread_mutex_unlock(lock);
    return true;
}

// Takes the first queued job whose argument is `arg` out of the queue (NULL: none, running or done); the caller frees it.
static inline MwJob *mw_job_steal(pthread_mutex_t *lock, MwJob **head, MwJob **tail, const void *arg) {
    pthread_mutex_lock(lock);
    MwJob *prev = NULL, *cur = *head;
    while (cur && cur->arg != arg) {
        prev = cur;
        cur = cur->next;
    }
    if (cur) {
        if (prev) prev->next = cur->next;
        else *head = cur->next;
        if (!cur->next) *tail = prev;
        cur->next = NULL;
    }
    pthread_mutex_unlock(lock);
    return cur;
}
