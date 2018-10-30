#include "threadsupp.h"

#if (defined _WIN32 || defined _WIN64) && !defined FORCE_POSIX_THREADS

bool thread_init(thread_handle *p_thread, thread_callback callback, void *args)
{
    return (*p_thread = (HANDLE) _beginthreadex(NULL, 0, callback, args, 0, NULL)) != INVALID_HANDLE_VALUE;
}

/*void thread_terminate(thread_handle *p_thread)
{
    (void) TerminateThread(*p_thread, 0);
}*/

void thread_wait(thread_handle *p_thread, thread_return *p_out)
{
    (void) WaitForSingleObject(*p_thread, INFINITE);
    (void) GetExitCodeThread(*p_thread, (LPDWORD) p_out);
}

void thread_close(thread_handle *p_thread)
{
    if (p_thread) (void) CloseHandle(*p_thread);
}

bool mutex_init(mutex_handle *p_mutex)
{
    InitializeCriticalSectionAndSpinCount(p_mutex, 0);
    return 1;
}

void mutex_acquire(mutex_handle *p_mutex)
{
    EnterCriticalSection(p_mutex);
}

void mutex_release(mutex_handle *p_mutex)
{
    LeaveCriticalSection(p_mutex);
}

void mutex_close(mutex_handle *p_mutex)
{
    if (p_mutex) DeleteCriticalSection(p_mutex);
}

bool condition_init(condition_handle *p_condition)
{
    InitializeConditionVariable(p_condition);
    return 1;
}

void condition_signal(condition_handle *p_condition)
{
    WakeConditionVariable(p_condition);
}

void condition_broadcast(condition_handle *p_condition)
{
    WakeAllConditionVariable(p_condition);
}

void condition_sleep(condition_handle *p_condition, mutex_handle *pmutex)
{
    SleepConditionVariableCS(p_condition, pmutex, INFINITE);
}

void condition_close(condition_handle *p_condition)
{
    (void) p_condition;
}

bool tls_init(tls_handle *p_tls)
{
    return (*p_tls = TlsAlloc()) != TLS_OUT_OF_INDEXES;
}

void tls_assign(tls_handle *p_tls, void *ptr)
{
    (void) TlsSetValue(*p_tls, ptr);
}

void *tls_fetch(tls_handle *p_tls)
{
    return TlsGetValue(*p_tls);
}

void tls_close(tls_handle *p_tls)
{
    (void) TlsFree(*p_tls);
}

#elif defined __unix__ || defined __APPLE__ || ((defined _WIN32 || defined _WIN64) && defined FORCE_POSIX_THREADS)

bool thread_init(thread_handle *p_thread, thread_callback callback, void *args)
{
    return !pthread_create(p_thread, NULL, callback, args);
}

/*void thread_terminate(thread_handle *p_thread)
{
    (void) pthread_cancel(*p_thread);
    (void) pthread_join(*p_thread, NULL);
}*/

void thread_wait(thread_handle *p_thread, thread_return *p_out)
{
    (void) pthread_join(*p_thread, p_out);
}

void thread_close(thread_handle *p_thread)
{
    (void) p_thread;
}

bool mutex_init(mutex_handle *p_mutex)
{
    return !pthread_mutex_init(p_mutex, NULL);
}

void mutex_acquire(mutex_handle *p_mutex)
{
    (void) pthread_mutex_lock(p_mutex);
}

void mutex_release(mutex_handle *p_mutex)
{
    (void) pthread_mutex_unlock(p_mutex);
}

void mutex_close(mutex_handle *p_mutex)
{
    if (p_mutex) (void) pthread_mutex_destroy(p_mutex);
}

bool condition_init(condition_handle *p_condition)
{
    return !pthread_cond_init(p_condition, NULL);
}

void condition_signal(condition_handle *p_condition)
{
    (void) pthread_cond_signal(p_condition);
}

void condition_broadcast(condition_handle *p_condition)
{
    (void) pthread_cond_broadcast(p_condition);
}

void condition_sleep(condition_handle *p_condition, mutex_handle *pmutex)
{
    (void) pthread_cond_wait(p_condition, pmutex);
}

void condition_close(condition_handle *p_condition)
{
    if (p_condition) (void) pthread_cond_destroy(p_condition);
}

bool tls_init(tls_handle *p_tls)
{
    return !pthread_key_create(p_tls, NULL);
}

void tls_assign(tls_handle *p_tls, void *ptr)
{
    (void) pthread_setspecific(*p_tls, ptr);
}

void *tls_fetch(tls_handle *p_tls)
{
    return pthread_getspecific(*p_tls);
}

void tls_close(tls_handle *p_tls)
{
    (void) pthread_key_delete(*p_tls);
}

#endif
