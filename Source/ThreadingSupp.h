#pragma once

#include "Common.h"

#include <stdbool.h>
#include <stdint.h>

// Uncomment this to enable POSIX threads on Windows:
// #define FORCE_POSIX_THREADS

////////////////////////////////////////////////////////////////////////////////
//
//  Invariant wrappers for general thread operations and 
//  basic synchronization primitives
//

#if (defined _WIN32 || defined _WIN64) && !defined FORCE_POSIX_THREADS

#   include <windows.h>
#   include <process.h>

typedef _beginthreadex_proc_type threadCallback;
typedef HANDLE threadHandle;
typedef DWORD threadReturn;

inline bool threadInit(threadHandle *pthread, threadCallback callback, void *args) 
{  
    return (*pthread = (HANDLE) _beginthreadex(NULL, 0, callback, args, 0, NULL)) != INVALID_HANDLE_VALUE;
}

inline void threadTerminate(threadHandle *pthread) 
{
    (void) TerminateThread(*pthread, 0);
}

inline void threadWait(threadHandle *pthread, threadReturn *pout)
{
    (void) WaitForSingleObject(*pthread, INFINITE);
    (void) GetExitCodeThread(*pthread, pout);
}

inline void threadClose(threadHandle *pthread)
{
    if (pthread) (void) CloseHandle(*pthread);
}

typedef CRITICAL_SECTION mutexHandle;

inline bool mutexInit(mutexHandle *pmutex)
{
    InitializeCriticalSectionAndSpinCount(pmutex, 4096);
    return 1;
}

inline void mutexAcquire(mutexHandle *pmutex)
{
    EnterCriticalSection(pmutex);
}

inline void mutexRelease(mutexHandle *pmutex)
{
    LeaveCriticalSection(pmutex);
}

inline void mutexClose(mutexHandle *pmutex)
{
    if (pmutex) DeleteCriticalSection(pmutex);
}

typedef CONDITION_VARIABLE conditionHandle;

inline bool conditionInit(conditionHandle *pcondition)
{
    InitializeConditionVariable(pcondition);
    return 1;
}

inline void conditionSignal(conditionHandle *pcondition)
{
    WakeConditionVariable(pcondition);
}

inline void conditionBroadcast(conditionHandle *pcondition)
{
    WakeAllConditionVariable(pcondition);
}

inline void conditionSleep(conditionHandle *pcondition, mutexHandle *pmutex)
{
    SleepConditionVariableCS(pcondition, pmutex, INFINITE);
}

inline void conditionClose(conditionHandle *pcondition)
{
    (void) pcondition;
}

typedef DWORD tlsHandle;

inline bool tlsInit(tlsHandle *ptls)
{
    return (*ptls = TlsAlloc()) != TLS_OUT_OF_INDEXES;
}

inline void tlsAssign(tlsHandle *ptls, void *ptr)
{
    (void) TlsSetValue(*ptls, ptr);
}

inline void *tlsFetch(tlsHandle *ptls)
{
    return TlsGetValue(*ptls);
}

inline void tlsClose(tlsHandle *ptls)
{
    (void) TlsFree(*ptls);
}

#elif defined __unix__ || defined __APPLE__ || ((defined _WIN32 || defined _WIN64) && defined FORCE_POSIX_THREADS)

#   include <pthread.h>

typedef void *(*threadCallback)(void *);
typedef pthread_t threadHandle;
typedef void *threadReturn;

inline bool threadInit(threadHandle *pthread, threadCallback callback, void *args)
{
    return !pthread_create(pthread, NULL, callback, args);
}

inline void threadTerminate(threadHandle *pthread)
{
    (void) pthread_cancel(*pthread);
    (void) pthread_join(*pthread, NULL);
}

inline void threadWait(threadHandle *pthread, threadReturn *pout)
{
    (void) pthread_join(*pthread, pout);
}

inline void threadClose(threadHandle *pthread)
{
    (void) pthread;
}

typedef pthread_mutex_t mutexHandle;

inline bool mutexInit(mutexHandle *pmutex)
{
    return !pthread_mutex_init(pmutex, NULL);
}

inline void mutexAcquire(mutexHandle *pmutex)
{
    (void) pthread_mutex_lock(pmutex);
}

inline void mutexRelease(mutexHandle *pmutex)
{
    (void) pthread_mutex_unlock(pmutex);
}

inline void mutexClose(mutexHandle *pmutex)
{
    if (pmutex) (void) pthread_mutex_destroy(pmutex);
}

typedef pthread_cond_t conditionHandle;

inline bool conditionInit(conditionHandle *pcondition)
{
    return !pthread_cond_init(pcondition, NULL);
}

inline void conditionSignal(conditionHandle *pcondition)
{
    (void) pthread_cond_signal(pcondition);
}

inline void conditionBroadcast(conditionHandle *pcondition)
{
    (void) pthread_cond_broadcast(pcondition);
}

inline void conditionSleep(conditionHandle *pcondition, mutexHandle *pmutex)
{
    (void) pthread_cond_wait(pcondition, pmutex);
}

inline void conditionClose(conditionHandle *pcondition)
{
    if (pcondition) (void) pthread_cond_destroy(pcondition);
}

typedef pthread_key_t tlsHandle;

inline bool tlsInit(tlsHandle *ptls)
{
    return !pthread_key_create(ptls, NULL);
}

inline void tlsAssign(tlsHandle *ptls, void *ptr)
{
    (void) pthread_setspecific(*ptls, ptr);
}

inline void *tlsFetch(tlsHandle *ptls)
{
    return pthread_getspecific(*ptls);
}

inline void tlsClose(tlsHandle *ptls)
{
    (void) pthread_key_delete(*ptls);
}

#endif
