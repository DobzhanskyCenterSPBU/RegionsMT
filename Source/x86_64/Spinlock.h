#pragma once

#include <inttypes.h>

#if defined __x86_64 || defined _M_X64

typedef volatile uint32_t spinlockHandle;

void spinlockAcquire(spinlockHandle *);
void spinlockRelease(spinlockHandle *);
uint32_t spinlockTest(spinlockHandle *);

#   define SPINLOCK_INIT 0

#endif
