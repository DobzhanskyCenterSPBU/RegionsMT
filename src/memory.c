#include "np.h"
#include "ll.h"
#include "memory.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

void *malloc2(size_t cap, size_t sz, size_t diff, bool clr)
{
    size_t hi, pr = size_mul(&hi, cap, sz);
    if (!hi)
    {
        size_t car, cap2 = size_add(&car, pr, diff);
        if (!car) return clr ? calloc(cap2, 1) : malloc(cap2);
    }
    errno = ERANGE;
    return NULL;
}

void *malloc3(size_t *restrict p_cap, size_t sz, size_t diff, bool clr)
{
    size_t cap = *p_cap, log2 = size_bit_scan_reverse(cap);
    if ((size_t) ~log2)
    {
        size_t cap2 = (size_t) 1 << log2;
        if ((cap == cap2) || (cap2 <<= 1))
        {
            void *tmp = malloc2(cap2, sz, diff, clr);
            *p_cap = tmp ? cap2 : 0;
            return tmp;
        }
        errno = ERANGE;
    }
    return NULL;
}

void *realloc2(void *src, size_t cap, size_t sz, size_t diff)
{
    size_t hi, pr = size_mul(&hi, cap, sz);
    if (!hi)
    {
        size_t car, cap2 = size_add(&car, pr, diff);
        if (!car) return realloc(src, cap2);
    }
    errno = ERANGE;
    return NULL;
}

void *realloc3(void *src, size_t *restrict p_cap, size_t sz, size_t diff)
{
    size_t cap = *p_cap, log2 = size_bit_scan_reverse(cap);
    if ((size_t) ~log2)
    {
        size_t cap2 = (size_t) 1 << log2;
        if ((cap == cap2) || (cap2 <<= 1))
        {
            void *tmp = realloc2(src, cap2, sz, diff);
            *p_cap = tmp ? cap2 : 0;
            return tmp;
        }
        errno = ERANGE;
    }
    return NULL;
}

bool array_init_strict(void *p_Arr, size_t cap, size_t sz, size_t diff, bool clr)
{
    void **restrict p_arr = p_Arr;
    return (*p_arr = malloc2(cap, sz, diff, clr)) != NULL || !((cap && sz) || diff);
}

bool array_init(void *p_Arr, size_t *restrict p_cap, size_t sz, size_t diff, bool clr)
{
    void **restrict p_arr = p_Arr;
    return (*p_arr = malloc3(p_cap, sz, diff, clr)) != NULL || !((*p_cap && sz) || diff);
}

bool array_resize_strict(void *p_Arr, size_t cap, size_t sz, size_t diff, enum array_resize_mode mode, size_t *restrict args, size_t args_cnt)
{
    void **restrict p_arr = p_Arr;
    size_t car, cnt = size_sum(&car, args, args_cnt);
    if (!car)
    {
        if (((mode & ARRAY_RESIZE_EXTEND_ONLY) && (cnt <= cap)) || ((mode & ARRAY_RESIZE_REDUCE_ONLY) && (cnt >= cap))) return 1;
        void *tmp = realloc2(*p_arr, cnt, sz, diff);
        if (tmp || !((cnt && sz) || diff))
        {
            *p_arr = tmp;
            return 1;
        }
        return 0;
    }
    errno = ERANGE;
    return 0;
}

bool array_resize(void *p_Arr, size_t *restrict p_cap, size_t sz, size_t diff, enum array_resize_mode mode, size_t *restrict args, size_t args_cnt)
{
    void **restrict p_arr = p_Arr;
    size_t car, cnt = size_sum(&car, args, args_cnt);
    if (!car)
    {
        if (((mode & ARRAY_RESIZE_EXTEND_ONLY) && (cnt <= *p_cap)) || ((mode & ARRAY_RESIZE_REDUCE_ONLY) && (cnt >= *p_cap))) return 1;
        void *tmp = realloc3(*p_arr, &cnt, sz, diff);
        if (tmp || !((cnt && sz) || diff))
        {
            *p_arr = tmp;
            *p_cap = cnt;
            return 1;
        }
        return 0;
    }
    errno = ERANGE;
    return 0;
}

bool queue_init(struct queue *restrict queue, size_t cap, size_t sz)
{
    if (!array_init(&queue->arr, &cap, sz, 0, 0)) return 0;

    queue->cap = cap;
    queue->begin = queue->cnt = 0;
    queue->sz = sz;
    return 1;
}

bool queue_test(struct queue *restrict queue, size_t diff)
{
    size_t cap = queue->cap;
    if (!array_resize(&queue->arr, &cap, queue->sz, 0, ARRAY_RESIZE_EXTEND_ONLY, ARG_S(queue->cnt, diff))) return 0;
    if (cap == queue->cap) return 1; // Queue has already enough space

    size_t bor, left = size_sub(&bor, queue->begin, queue->cap - queue->cnt);
    if (!bor && left) // queue->begin > queue->cap - queue->cnt
    {
        size_t bor2, left2 = size_sub(&bor2, queue->begin, cap - queue->cnt);
        if (!bor2 && left2) // queue->begin > cap - queue->cnt
        {
            size_t capp_diff = cap - queue->cap, capp_diff_pr = capp_diff * queue->sz;
            memcpy((char *) queue->arr + queue->cap * queue->sz, queue->arr, capp_diff_pr);

            size_t bor3, left3 = size_sub(&bor3, left2, capp_diff);
            if (!bor3 && left3) // queue->begin - cap + queue->cnt > cap - queue->cap
            {
                memcpy(queue->arr, (char *) queue->arr + capp_diff_pr, capp_diff_pr);
                memcpy((char *) queue->arr + capp_diff_pr, (char *) queue->arr + (capp_diff_pr << 1), left3 * queue->sz);
            }
            else memcpy(queue->arr, (char *) queue->arr + capp_diff_pr, left2 * queue->sz);
        }
        else memcpy((char *) queue->arr + queue->cap * queue->sz, queue->arr, left * queue->sz);
    }
    queue->cap = cap;
    return 1;
}

void *queue_peek(struct queue *restrict queue, size_t offset)
{
    if (queue->begin >= queue->cap - offset) return (char *) queue->arr + (queue->begin + offset - queue->cap) * queue->sz;
    return (char *) queue->arr + (queue->begin + offset) * queue->sz;
}

// This function should be called ONLY if 'queue_test' succeeds
static void queue_enqueue_lo(struct queue *restrict queue, void *restrict arr, size_t cnt)
{
    size_t bor, left = size_sub(&bor, queue->begin, queue->cap - queue->cnt);
    if (bor) left += queue->cap;

    size_t diff = queue->cap - left; // Never overflows
    size_t bor2, left2 = size_sub(&bor2, cnt, diff);
    if (!bor2 && left2) // cnt > queue->cap - left
    {
        size_t diff_pr = diff * queue->sz;
        memcpy((char *) queue->arr + left * queue->sz, arr, diff_pr);
        memcpy(queue->arr, (char *) queue->arr + diff_pr, left2 * queue->sz);
    }
    else memcpy((char *) queue->arr + left * queue->sz, arr, cnt * queue->sz);
    
    queue->cnt += cnt;
}

// This function should be called ONLY if 'queue_test' succeeds
static void queue_enqueue_hi(struct queue *restrict queue, void *restrict arr, size_t cnt)
{
    size_t bor, diff = size_sub(&bor, cnt, queue->begin);
    if (!bor && diff) // cnt > queue->begin
    {
        size_t diff_pr = diff * queue->sz, left = queue->cap - diff;
        memcpy((char *) queue->arr + left * queue->sz, arr, diff_pr);
        memcpy(queue->arr, (char *) queue->arr + diff_pr, queue->begin * queue->sz);
        queue->begin = left;
    }
    else
    {
        memcpy((char *) queue->arr + diff * queue->sz, arr, cnt * queue->sz);
        queue->begin = 0 - diff;
    }

    queue->cnt += cnt;
}

bool queue_enqueue(struct queue *restrict queue, bool hi, void *restrict arr, size_t cnt)
{
    if (!queue_test(queue, cnt)) return 0;
    (hi ? queue_enqueue_hi : queue_enqueue_lo)(queue, arr, cnt);
    return 1;
}

void queue_dequeue(struct queue *restrict queue, size_t offset)
{
    if (offset)
    {
        size_t bor, ind = size_sub(&bor, queue->begin, queue->cap - offset);
        if (bor) ind += queue->cap;
        memcpy((char *) queue->arr + ind * queue->sz, (char *) queue->arr + queue->begin * queue->sz, queue->sz);
    }

    queue->cnt--;
    queue->begin++;
    if (queue->begin == queue->cap) queue->begin = 0;
}
