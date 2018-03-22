#pragma once

///////////////////////////////////////////////////////////////////////////////
//
//  Safe facilities for a memory management
//

#include "common.h"

enum alloc_flags {
    ALLOC_CLEAR = 1,
    ALLOC_STRICT = 2,
    ALLOC_REDUCE = 4,
};

bool alloc(void *, size_t *restrict, size_t, size_t, size_t, enum alloc_flags);
bool array_init(void *, size_t *restrict, size_t, size_t, enum alloc_flags, size_t *restrict, size_t);

// Helper macros evaluating and inserting the count of arguments
#define ARG(T, ...) \
    ((T []) { __VA_ARGS__ }), countof(((T []) { __VA_ARGS__ }))
#define ARG_S(...) \
    ARG(size_t, __VA_ARGS__)

struct queue {
    void *arr;
    size_t cap, begin, cnt, sz;
};

void queue_close(struct queue *restrict);
bool queue_init(struct queue *restrict, size_t, size_t);
bool queue_test(struct queue *restrict, size_t);
void *queue_peek(struct queue *restrict, size_t);
bool queue_enqueue(struct queue *restrict, bool, void *restrict, size_t);
void queue_dequeue(struct queue *restrict, size_t);
