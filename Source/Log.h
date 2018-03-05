#pragma once

#include "Wrappers.h"
#include "x86_64/Spinlock.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

typedef struct {
    FILE *dev;
    spinlockHandle lock;
    volatile size_t size;
} logInfo, *logInfoPtr;

uint64_t getTime();
void logMsg(logInfo *, const char *, ...);
void logTime(logInfo *, uint64_t, const char *, const char *);
void logError(logInfo *, const char *, errno_t);
