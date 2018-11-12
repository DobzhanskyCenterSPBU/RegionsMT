#pragma once

///////////////////////////////////////////////////////////////////////////////
//
//  Safe facilities for a memory management
//

#include "common.h"

enum array_flags {
    ARRAY_CLEAR = 1,
    ARRAY_STRICT = 2,
    ARRAY_REDUCE = 4,
    ARRAY_REALLOC = 8 // Default in 'array_test'
};

enum {
    ARRAY_FAILURE = 0,
    ARRAY_SUCCESS,
    ARRAY_UNTOUCHED,
};

void array_broadcast(void *, size_t, size_t, void *);

unsigned array_init(void *, size_t *restrict, size_t, size_t, size_t, enum array_flags);
unsigned array_test_impl(void *, size_t *restrict, size_t, size_t, enum array_flags, size_t *restrict, size_t);

#define array_test(ARR, P_CAP, SZ, DIFF, FLAGS, ...) \
    (array_test_impl((ARR), (P_CAP), (SZ), (DIFF), (FLAGS), ARG(size_t, __VA_ARGS__)))

struct queue {
    void *arr;
    size_t cap, begin, cnt;
};

void queue_close(struct queue *restrict);
bool queue_init(struct queue *restrict, size_t, size_t);
unsigned queue_test(struct queue *restrict, size_t, size_t);
void *queue_fetch(struct queue *restrict, size_t, size_t);
unsigned queue_enqueue(struct queue *restrict, bool, void *restrict, size_t, size_t);
void queue_dequeue(struct queue *restrict, size_t, size_t);

struct persistent_array {
    size_t cap, cnt, off;
    void *ptr[];
};

struct persistent_array *persistent_array_create(size_t, size_t);
void persistent_array_dispose(struct persistent_array *);
unsigned persistent_array_test(struct persistent_array *, size_t, size_t);
void *persistent_array_fetch(struct persistent_array *, size_t, size_t);
