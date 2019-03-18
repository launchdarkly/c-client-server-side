#include "string.h"

#include "ldinternal.h"

#ifdef _WIN32
    /* placeholder */
#else
    #include <time.h>
    #include <unistd.h>
#endif

bool
LDi_sleepMilliseconds(const unsigned long milliseconds)
{
    int status;

    if ((status = usleep(1000 * milliseconds)) != 0) {
        char msg[256];

        LD_ASSERT(snprintf(msg, sizeof(msg), "usleep failed with: %s",
            strerror(status)) >= 0);

        LD_LOG(LD_LOG_CRITICAL, msg);

        return false;
    }

    return true;
}

static bool
getTime(unsigned long *const resultMilliseconds, clockid_t clockid)
{
    int status; struct timespec spec;

    LD_ASSERT(resultMilliseconds);

    if ((status = clock_gettime(clockid, &spec)) != 0) {
        char msg[256];

        LD_ASSERT(snprintf(msg, sizeof(msg), "clock_gettime failed with: %s",
            strerror(status)) >= 0);

        LD_LOG(LD_LOG_CRITICAL, msg);

        return false;
    }

    *resultMilliseconds = (spec.tv_sec * 1000) + (spec.tv_nsec / 1000000);

    return true;
}

bool
LDi_getMonotonicMilliseconds(unsigned long *const resultMilliseconds)
{
    return getTime(resultMilliseconds, CLOCK_MONOTONIC);
}

bool
LDi_getUnixMilliseconds(unsigned long *const resultMilliseconds)
{
    return getTime(resultMilliseconds, CLOCK_REALTIME);
}

bool
LDi_createthread(ld_thread_t *const thread,
    THREAD_RETURN (*const routine)(void *), void *const argument)
{
    #ifdef _WIN32
        ld_thread_t attempt = CreateThread(NULL, 0, routine, argument, 0, NULL);

        if (attempt == NULL) {
            return false;
        } else {
            *thread = attempt;

            return true;
        }
    #else
        int status;

        if ((status = pthread_create(thread, NULL, routine, argument)) != 0) {
            char msg[256];

            LD_ASSERT(snprintf(msg, sizeof(msg), "pthread_create failed: %s",
                strerror(status)) >= 0);

            LD_LOG(LD_LOG_CRITICAL, msg);
        }

        return status == 0;
    #endif
}

bool
LDi_jointhread(ld_thread_t thread)
{
    #ifdef _WIN32
        return WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0;
    #else
        int status;

        if ((status = pthread_join(thread, NULL)) != 0) {
            char msg[256];

            LD_ASSERT(snprintf(msg, sizeof(msg), "pthread_join failed: %s",
                strerror(status)) >= 0);

            LD_LOG(LD_LOG_CRITICAL, msg);
        }

        return status == 0;
    #endif
}

bool
LDi_rwlockinit(ld_rwlock_t *const lock)
{
    #ifdef _WIN32
        LD_ASSERT(lock);

        InitializeSRWLock(lock);

        return true;
    #else
        int status;

        LD_ASSERT(lock);

        if ((status = pthread_rwlock_init(lock, NULL)) != 0) {
            char msg[256];

            LD_ASSERT(snprintf(msg, sizeof(msg),
                "pthread_rwlock_init failed: %s", strerror(status)) >= 0);

            LD_LOG(LD_LOG_CRITICAL, msg);
        }

        return status == 0;
    #endif
}

bool
LDi_rwlockdestroy(ld_rwlock_t *const lock)
{
    #ifdef _WIN32
        LD_ASSERT(lock);

        return true;
    #else
        int status;

        LD_ASSERT(lock);

        if ((status = pthread_rwlock_destroy(lock)) != 0) {
            char msg[256];

            LD_ASSERT(snprintf(msg, sizeof(msg),
                "pthread_rwlock_destroy failed: %s", strerror(status)) >= 0);

            LD_LOG(LD_LOG_CRITICAL, msg);
        }

        return status == 0;
    #endif
}

bool
LDi_rdlock(ld_rwlock_t *const lock)
{
    #ifdef _WIN32
        LD_ASSERT(lock);

        AcquireSRWLockShared(lock);

        return true;
    #else
        int status;

        LD_ASSERT(lock);

        if ((status = pthread_rwlock_rdlock(lock)) != 0) {
            char msg[256];

            LD_ASSERT(snprintf(msg, sizeof(msg),
                "pthread_rwlock_rdlock failed: %s", strerror(status)) >= 0);

            LD_LOG(LD_LOG_CRITICAL, msg);
        }

        return status == 0;
    #endif
}

bool
LDi_wrlock(ld_rwlock_t *const lock)
{
    #ifdef _WIN32
        LD_ASSERT(lock);

        AcquireSRWLockExclusive(lock);

        return true;
    #else
        int status;

        LD_ASSERT(lock);

        if ((status = pthread_rwlock_wrlock(lock)) != 0) {
            char msg[256];

            LD_ASSERT(snprintf(msg, sizeof(msg),
                "pthread_rwlock_wrlock failed: %s", strerror(status)) >= 0);

            LD_LOG(LD_LOG_CRITICAL, msg);
        }

        return status == 0;
    #endif
}

bool
LDi_rdunlock(ld_rwlock_t *const lock)
{
    #ifdef _WIN32
        LD_ASSERT(lock);

        ReleaseSRWLockShared(lock);

        return true;
    #else
        int status;

        LD_ASSERT(lock);

        if ((status = pthread_rwlock_unlock(lock)) != 0) {
            char msg[256];

            LD_ASSERT(snprintf(msg, sizeof(msg),
                "pthread_rwlock_unlock failed: %s", strerror(status)) >= 0);

            LD_LOG(LD_LOG_CRITICAL, msg);
        }

        return status == 0;
    #endif
}

bool
LDi_wrunlock(ld_rwlock_t *const lock)
{
    #ifdef _WIN32
        LD_ASSERT(lock);

        ReleaseSRWLockExclusive(lock);

        return true;
    #else
        int status;

        LD_ASSERT(lock);

        if ((status = pthread_rwlock_unlock(lock)) != 0) {
            char msg[256];

            LD_ASSERT(snprintf(msg, sizeof(msg),
                "pthread_rwlock_unlock failed: %s", strerror(status)) >= 0);

            LD_LOG(LD_LOG_CRITICAL, msg);
        }

        return status == 0;
    #endif
}
