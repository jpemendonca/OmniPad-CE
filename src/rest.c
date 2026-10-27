#include "rest.h"
#include "log.h"
#include "util.h"

#include <pthread.h>

#define REST_QUIET_MS 12000

static pthread_mutex_t g_rest_lock = PTHREAD_MUTEX_INITIALIZER;
static long g_quiet_until;
static int g_resume_pending;

void rest_signal(const char *why)
{
    pthread_mutex_lock(&g_rest_lock);
    long now = now_ms();
    int was_quiet = now < g_quiet_until;
    g_quiet_until = now + REST_QUIET_MS;
    g_resume_pending = 1;
    pthread_mutex_unlock(&g_rest_lock);
    if (was_quiet) log_line("rest: %s, quiet period extended", why);
    else log_line("rest: %s: leaving the system alone for %d s, then releasing every controller",
                  why, REST_QUIET_MS / 1000);
}

int rest_quiet(void)
{
    pthread_mutex_lock(&g_rest_lock);
    int quiet = now_ms() < g_quiet_until;
    pthread_mutex_unlock(&g_rest_lock);
    return quiet;
}

int rest_take_resume(void)
{
    pthread_mutex_lock(&g_rest_lock);
    int ready = g_resume_pending && now_ms() >= g_quiet_until;
    if (ready) g_resume_pending = 0;
    pthread_mutex_unlock(&g_rest_lock);
    return ready;
}
