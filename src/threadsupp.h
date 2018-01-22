#pragma once

////////////////////////////////////////////////////////////////////////////////
//
//  Invariant wrappers for general thread operations and basic synchronization 
//  primitives
//

// Uncomment this to enable POSIX threads on Windows:
// #define FORCE_POSIX_THREADS

#include <stdbool.h>
#include <stdint.h>

#if (defined _WIN32 || defined _WIN64) && !defined FORCE_POSIX_THREADS

#   include <windows.h>
#   include <process.h>

#define thread_callback_convention __stdcall
typedef _beginthreadex_proc_type thread_callback;
typedef HANDLE thread_handle;
typedef unsigned thread_return;
typedef CRITICAL_SECTION mutex_handle;
typedef CONDITION_VARIABLE condition_handle;
typedef DWORD tls_handle;

#elif defined __unix__ || defined __APPLE__ || ((defined _WIN32 || defined _WIN64) && defined FORCE_POSIX_THREADS)

#   include <pthread.h>

#define thread_callback_convention
typedef void *(*thread_callback)(void *);
typedef pthread_t thread_handle;
typedef void *thread_return;
typedef pthread_mutex_t mutex_handle;
typedef pthread_cond_t condition_handle;
typedef pthread_key_t tls_handle;

#endif

bool thread_init(thread_handle *, thread_callback, void *);
void thread_terminate(thread_handle *);
void thread_wait(thread_handle *, thread_return *);
void thread_close(thread_handle *);

bool mutex_init(mutex_handle *);
void mutex_acquire(mutex_handle *);
void mutex_release(mutex_handle *);
void mutex_close(mutex_handle *);

bool condition_init(condition_handle *);
void condition_signal(condition_handle *);
void condition_broadcast(condition_handle *);
void condition_sleep(condition_handle *, mutex_handle *);
void condition_close(condition_handle *);

bool tls_init(tls_handle *ptls);
void tls_assign(tls_handle *ptls, void *ptr);
void *tls_fetch(tls_handle *ptls);
void tls_close(tls_handle *ptls);
