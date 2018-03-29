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

enum array_status {
    ARRAY_FAILURE = 0,
    ARRAY_SUCCESS,
    ARRAY_NO_CHANGE
};

enum array_status array_init(void *, size_t *restrict, size_t, size_t, size_t, enum array_flags);
enum array_status array_test(void *, size_t *restrict, size_t, size_t, enum array_flags, size_t *restrict, size_t);

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
