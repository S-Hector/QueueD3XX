/*
    Created By: Hector Soto
    This is a quick replacement for critical section stuff needed to be duplicated for Linux.
*/

#include <pthread.h>
#include "Types.h"

#define CRITICAL_SECTION pthread_mutex_t
#define InitializeCriticalSection(A) pthread_mutex_init(A, 0)
#define EnterCriticalSection pthread_mutex_lock
#define TryEnterCriticalSection pthread_mutex_trylock
#define LeaveCriticalSection pthread_mutex_unlock
#define DeleteCriticalSection pthread_mutex_destroy