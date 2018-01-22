#include "ll.h"

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <immintrin.h>

#if defined __GNUC__ || defined __clang__
#   define DECLARE_LOAD_ACQUIRE(TYPE, PREFIX) \
        TYPE PREFIX ## _load_acquire(volatile TYPE *src) \
        { \
            return __atomic_load_n(src, __ATOMIC_ACQUIRE); \
        }

void spinlock_acquire(spinlock_handle *p_spinlock)
{
    for (unsigned int tmp = 0; !__atomic_compare_exchange_n(p_spinlock, &tmp, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED); tmp = 0)
        while (__atomic_load_n(p_spinlock, __ATOMIC_ACQUIRE)) _mm_pause();
}

void spinlock_release(spinlock_handle *p_spinlock)
{
    __atomic_clear(p_spinlock, __ATOMIC_RELEASE);
}

void *double_lock_execute(spinlock_handle *p_spinlock, double_lock_callback init, double_lock_callback comm, void *init_args, void *comm_args)
{
    void *res = NULL;
    unsigned int tmp = 0;
    switch (__atomic_load_n(p_spinlock, __ATOMIC_ACQUIRE))
    {
    case 0:
        if (__atomic_compare_exchange_n(p_spinlock, &tmp, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            if (init) res = init(init_args);
            __atomic_store_n(p_spinlock, 2, __ATOMIC_RELEASE);
            break;
        }
    case 1:
        while (__atomic_load_n(p_spinlock, __ATOMIC_ACQUIRE) != 2) _mm_pause();
    case 2:
        if (comm) res = comm(comm_args);
    }
    return res;
}

void bit_set_interlocked(volatile uint8_t *arr, size_t bit)
{
    __atomic_or_fetch(arr + (bit >> 3), 1 << (bit & 7), __ATOMIC_ACQ_REL);
}

void bit_reset_interlocked(volatile uint8_t *arr, size_t bit)
{
    __atomic_and_fetch(arr + (bit >> 3), ~(1 << (bit & 7)), __ATOMIC_ACQ_REL);
}

void bit_set2_interlocked(volatile uint8_t *arr, size_t bit)
{
    uint8_t pos = bit & 7;
    if (pos < 7) __atomic_or_fetch(arr + (bit >> 3), 3u << pos, __ATOMIC_ACQ_REL);
    else __atomic_or_fetch((volatile uint16_t *) (arr + (bit >> 3)), 0x180, __ATOMIC_ACQ_REL);
}

void size_inc_interlocked(volatile size_t *mem, const void *arg)
{
    (void) arg;
    __atomic_add_fetch(mem, 1, __ATOMIC_ACQ_REL);
}

void size_dec_interlocked(volatile size_t *mem, const void *arg)
{
    (void) arg;
    __atomic_sub_fetch(mem, 1, __ATOMIC_ACQ_REL);
}

uint32_t uint32_bit_scan_reverse(uint32_t x)
{
    return x ? ((sizeof(unsigned) << 3) - __builtin_clz((unsigned) x) - 1) : UINT_MAX;
}

uint32_t uint32_bit_scan_forward(uint32_t x)
{
    return x ? __builtin_ctz((unsigned) x) : UINT_MAX;
}

#   ifdef __x86_64__

size_t size_mul(size_t *p_hi, size_t a, size_t b)
{
    union { unsigned __int128 val; struct { size_t lo, hi; }; } res = { .val = (unsigned __int128) a * (unsigned __int128) b };
    *p_hi = res.hi;
    return res.lo;
}

size_t size_add(size_t *p_car, size_t x, size_t y)
{
    union { unsigned __int128 val; struct { size_t lo, hi; }; } res = { .val = (unsigned __int128) x + (unsigned __int128) y };
    *p_car = res.hi;
    return res.lo;
}

size_t size_sub(size_t *p_bor, size_t x, size_t y)
{
    union { unsigned __int128 val; struct { size_t lo, hi; }; } res = { .val = (unsigned __int128) x - (unsigned __int128) y };
    *p_bor = 0 - res.hi;
    return res.lo;
}

size_t size_sum(size_t *p_hi, size_t *args, size_t args_cnt)
{
    union { unsigned __int128 val; struct { size_t lo, hi; }; } res = { .val = 0 };
    for (size_t i = 0; i < args_cnt; res.val += args[i++]);
    *p_hi = res.hi;
    return res.lo;
}

size_t size_bit_scan_reverse(size_t x)
{
    return x ? SIZE_BIT - __builtin_clzll(x) - 1 : SIZE_MAX;
}

size_t size_bit_scan_forward(size_t x)
{
    return x ? __builtin_ctzll(x) : SIZE_MAX;
}

#   endif 
#elif defined _MSC_BUILD
#   include <intrin.h>
#   define DECLARE_LOAD_ACQUIRE(TYPE, PREFIX) \
        TYPE PREFIX ## _load_acquire(volatile TYPE *src) \
        { \
            TYPE res = *src; \
            _ReadBarrier(); \
            return res; \
        }
#   define DECLARE_STORE_RELEASE(TYPE, PREFIX) \
        void PREFIX ## _store_release(volatile TYPE *dst, TYPE val) \
        { \
            _WriteBarrier(); \
            *dst = val; \
        }

static DECLARE_LOAD_ACQUIRE(long, spinlock)
static DECLARE_STORE_RELEASE(long, spinlock)

void spinlock_acquire(spinlock_handle *p_spinlock)
{
    while (_InterlockedCompareExchange(p_spinlock, 1, 0))
    {
        for (;;)
        {
            if (spinlock_load_acquire(p_spinlock)) _mm_pause();
            else break;
        }
    }
}

void spinlock_release(spinlock_handle *p_spinlock)
{
    spinlock_store_release(p_spinlock, 0);
}

void *double_lock_execute(spinlock_handle *p_spinlock, double_lock_callback init, double_lock_callback comm, void *init_args, void *comm_args)
{
    void *res = NULL;
    switch (spinlock_load_acquire(p_spinlock))
    {
    case 0:
        if (!_InterlockedCompareExchange(p_spinlock, 1, 0))
        {
            if (init) res = init(init_args);
            spinlock_store_release(p_spinlock, 2);
            break;
        }
    case 1:
        while (spinlock_load_acquire(p_spinlock) != 2) _mm_pause();
    case 2:
        if (comm) res = comm(comm_args);
    }
    return res;
}

void bit_set_interlocked(volatile uint8_t *arr, size_t bit)
{
    _InterlockedOr8((volatile char *) (arr + (bit >> 3)), 1 << (bit & 7));
}

void bit_reset_interlocked(volatile uint8_t *arr, size_t bit)
{
    _InterlockedAnd8((volatile char *) (arr + (bit >> 3)), ~(1 << (bit & 7)));
}

void bit_set2_interlocked(volatile uint8_t *arr, size_t bit)
{
    uint8_t pos = bit & 7;
    if (pos < 7) _InterlockedOr8((volatile char *) (arr + (bit >> 3)), 3u << pos);
    else _InterlockedOr16((volatile short *) (arr + (bit >> 3)), 0x180);
}

uint32_t uint32_bit_scan_reverse(uint32_t x)
{
    unsigned long res;
    return _BitScanReverse(&res, (unsigned long) x) ? res : UINT32_MAX;
}

uint32_t uint32_bit_scan_forward(uint32_t x)
{
    unsigned long res;
    return _BitScanForward(&res, (unsigned long) x) ? res : UINT32_MAX;
}

#   ifdef _M_X64

void size_inc_interlocked(volatile size_t *mem, const void *arg)
{
    (void) arg;
    _InterlockedIncrement64((volatile __int64 *) mem);
}

void size_dec_interlocked(volatile size_t *mem, const void *arg)
{
    (void) arg;
    _InterlockedDecrement64((volatile __int64 *) mem);
}

size_t size_mul(size_t *p_hi, size_t a, size_t b)
{
    unsigned __int64 hi;
    size_t res = (size_t) _umul128((unsigned __int64) a, (unsigned __int64) b, &hi);
    *p_hi = (size_t) hi;
    return res;
}

size_t size_add(size_t *p_car, size_t x, size_t y)
{
    unsigned __int64 res;
    *p_car = _addcarry_u64(0, x, y, &res);
    return res;
}

size_t size_sub(size_t *p_bor, size_t x, size_t y)
{
    unsigned __int64 res;
    *p_bor = _subborrow_u64(0, x, y, &res);
    return res;
}

size_t size_sum(size_t *p_hi, size_t *args, size_t args_cnt)
{
    unsigned __int64 lo = 0, hi = 0;
    for (size_t i = 0; i < args_cnt; _addcarry_u64(_addcarry_u64(0, lo, args[i++], &lo), hi, 0, &hi));
    *p_hi = (size_t) hi;
    return (size_t) lo;
}

size_t size_bit_scan_reverse(size_t x)
{
    unsigned long res;
    return _BitScanReverse64(&res, (unsigned __int64) x) ? res : SIZE_MAX;
}

size_t size_bit_scan_forward(size_t x)
{
    unsigned long res;
    return _BitScanForward64(&res, (unsigned __int64) x) ? res : SIZE_MAX;
}

#   elif defined _M_IX86

void size_inc_interlocked(volatile size_t *mem, const void *arg)
{
    (void) arg;
    _InterlockedIncrement((long *) mem);
}

void size_dec_interlocked(volatile size_t *mem, const void *arg)
{
    (void) arg;
    _InterlockedDecrement((long *) mem);
}

#   endif
#endif 

#if defined _M_IX86 || defined __i386__

size_t size_mul(size_t *p_hi, size_t x, size_t y)
{
    union { unsigned long long val; struct { size_t lo, hi; }; } res = { .val = (unsigned long long) x * (unsigned long long) y };
    *p_hi = res.hi;
    return res.lo;
}

size_t size_add(size_t *p_car, size_t x, size_t y)
{
    union { unsigned long long val; struct { size_t lo, hi; }; } res = { .val = (unsigned long long) x + (unsigned long long) y } ;
    *p_car = res.hi;
    return res.lo;
}

size_t size_sub(size_t *p_bor, size_t x, size_t y)
{
    union { unsigned long long val; struct { size_t lo, hi; }; } res = { .val = (unsigned long long) x - (unsigned long long) y };
    *p_bor = 0 - res.hi;
    return res.lo;
}

size_t size_sum(size_t *p_hi, size_t *args, size_t args_cnt)
{
    union { unsigned long long val; struct { size_t lo, hi; }; } res = { .val = 0 };
    for (size_t i = 0; i < args_cnt; res.val += args[i++]);
    *p_hi = res.hi;
    return res.lo;
}

size_t size_bit_scan_reverse(size_t x)
{
    return (size_t) uint32_bit_scan_reverse((uint32_t) x);
}

size_t size_bit_scan_forward(size_t x)
{
    return (size_t) uint32_bit_scan_forward((uint32_t) x);
}

#endif

DECLARE_LOAD_ACQUIRE(uint8_t, uint8)
DECLARE_LOAD_ACQUIRE(uint16_t, uint16)
DECLARE_LOAD_ACQUIRE(uint32_t, uint32)
DECLARE_LOAD_ACQUIRE(uint64_t, uint64)
DECLARE_LOAD_ACQUIRE(size_t, size)

void bit_set_interlocked_p(volatile uint8_t *arr, const size_t *p_bit)
{
    bit_set_interlocked(arr, *p_bit);
}

void bit_reset_interlocked_p(volatile uint8_t *arr, const size_t *p_bit)
{
    bit_reset_interlocked(arr, *p_bit);
}

void bit_set2_interlocked_p(volatile uint8_t *arr, const size_t *p_bit)
{
    bit_set2_interlocked(arr, *p_bit);
}

uint8_t bit_get2_acquire(volatile uint8_t *arr, size_t bit)
{
    uint8_t pos = bit & 7;
    if (pos < 7) return (uint8_load_acquire(arr + (bit >> 3)) >> pos) & 3;
    else return (uint16_load_acquire((uint16_t *) (arr + (bit >> 3))) >> 7) & 3;
}

bool bit_test2_acquire(volatile uint8_t *arr, size_t bit)
{
    return bit_get2_acquire(arr, bit) == 3;
}

bool bit_test2_acquire_p(volatile uint8_t *arr, const size_t *p_bit)
{
    return bit_test2_acquire(arr, *p_bit);
}

bool bit_test_range_acquire(volatile uint8_t *arr, size_t cnt)
{
    size_t div = cnt >> 3, rem = cnt & 7;
    for (size_t i = 0; i < div; i++) 
        if (uint8_load_acquire(arr + i) != 0xff) return 0;
    if (rem)
    {
        uint8_t msk = (1u << rem) - 1;
        return (uint8_load_acquire(arr + div) & msk) == msk;
    }
    return 1;
}

bool bit_test_range_acquire_p(volatile uint8_t *arr, const size_t *p_cnt)
{
    return bit_test_range_acquire(arr, *p_cnt);
}

bool bit_test2_range_acquire(volatile uint8_t *arr, size_t cnt)
{
    size_t div = cnt >> 3, rem = cnt & 7;
    for (size_t i = 0; i < div; i++) 
        if ((uint8_load_acquire(arr + i) & 0x55) != 0x55) return 0;
    if (rem)
    {
        uint8_t msk = ((1u << rem) - 1) & 0x55;
        return (uint8_load_acquire(arr + div) & msk) == msk;
    }
    return 1;
}

bool bit_test2_range_acquire_p(volatile uint8_t *arr, const size_t *p_cnt)
{
    return bit_test2_range_acquire(arr, *p_cnt);
}

bool size_test_acquire(volatile size_t *mem, const void *arg)
{
    (void) arg;
    return !!size_load_acquire(mem);
}

uint8_t uint8_bit_scan_reverse(uint8_t x)
{
    return (uint8_t) uint32_bit_scan_reverse((uint8_t) x);
}

uint8_t uint8_bit_scan_forward(uint8_t x)
{
    return (uint8_t) uint32_bit_scan_forward((uint8_t) x);
}

bool size_bit_test(size_t val, uint8_t bit)
{
    return !!(val & ((size_t) 1 << (bit & (SIZE_BIT - 1))));
}

size_t size_bit_set(size_t val, uint8_t bit)
{
    return val | ((size_t) 1 << (bit & (SIZE_BIT - 1)));
}

size_t size_bit_reset(size_t val, uint8_t bit)
{
    return val & ~((size_t) 1 << (bit & (SIZE_BIT - 1)));
}

bool bit_test(uint8_t *arr, size_t bit)
{
    return !!(arr[bit >> 3] & (1 << (bit & 7)));
}

void bit_set(uint8_t *arr, size_t bit)
{
    arr[bit >> 3] |= 1 << (bit & 7);
}

void bit_reset(uint8_t *arr, size_t bit)
{
    arr[bit >> 3] &= ~(1 << (bit & 7));
}

int flt64_stable_cmp_dsc(const double *p_a, const double *p_b, void *context)
{
    (void) context;
    __m128d ab = _mm_loadh_pd(_mm_load_sd(p_a), p_b);
    __m128i res = _mm_castpd_si128(_mm_cmplt_pd(ab, _mm_permute_pd(ab, 1)));
    return _mm_extract_epi32(res, 2) - _mm_cvtsi128_si32(res);
}

int flt64_stable_cmp_dsc_abs(const double *p_a, const double *p_b, void *context)
{
    (void) context;
    __m128d ab = _mm_and_pd(_mm_loadh_pd(_mm_load_sd(p_a), p_b), _mm_castsi128_pd(_mm_set1_epi64x(0x7fffffffffffffff)));
    __m128i res = _mm_castpd_si128(_mm_cmplt_pd(ab, _mm_permute_pd(ab, 1)));
    return _mm_extract_epi32(res, 2) - _mm_cvtsi128_si32(res);
}

int flt64_stable_cmp_dsc_nan(const double *p_a, const double *p_b, void *context)
{
    (void) context;
    __m128d ab = _mm_loadh_pd(_mm_load_sd(p_a), p_b);
    __m128i res = _mm_sub_epi32(_mm_castpd_si128(_mm_cmpunord_pd(ab, ab)), _mm_castpd_si128(_mm_cmp_pd(ab, _mm_permute_pd(ab, 1), _CMP_NLE_UQ)));
    return _mm_extract_epi32(res, 2) - _mm_cvtsi128_si32(res);
}
