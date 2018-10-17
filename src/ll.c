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
    __atomic_or_fetch(arr + bit / CHAR_BIT, 1 << bit % CHAR_BIT, __ATOMIC_ACQ_REL);
}

void bit_reset_interlocked(volatile uint8_t *arr, size_t bit)
{
    __atomic_and_fetch(arr + bit / CHAR_BIT, ~(1 << bit % CHAR_BIT), __ATOMIC_ACQ_REL);
}

void bit_set2_interlocked(volatile uint8_t *arr, size_t bit)
{
    uint8_t pos = bit % CHAR_BIT;
    if (pos < CHAR_BIT - 1) __atomic_or_fetch(arr + bit / CHAR_BIT, 3u << pos, __ATOMIC_ACQ_REL);
    else __atomic_or_fetch((volatile uint16_t *) (arr + bit / CHAR_BIT), 3u << (CHAR_BIT - 1), __ATOMIC_ACQ_REL);
}

void size_inc_interlocked(volatile size_t *mem)
{
    __atomic_add_fetch(mem, 1, __ATOMIC_ACQ_REL);
}

void size_dec_interlocked(volatile size_t *mem)
{
    __atomic_sub_fetch(mem, 1, __ATOMIC_ACQ_REL);
}

uint32_t uint32_bit_scan_reverse(uint32_t x)
{
    return x ? ((sizeof(unsigned) * CHAR_BIT) - __builtin_clz((unsigned) x) - 1) : UINT_MAX;
}

uint32_t uint32_bit_scan_forward(uint32_t x)
{
    return x ? __builtin_ctz((unsigned) x) : UINT_MAX;
}

uint32_t uint32_pop_cnt(uint32_t x)
{
    return __builtin_popcount(x);
}

#   ifdef __x86_64__

size_t size_bit_scan_reverse(size_t x)
{
    return x ? SIZE_BIT - __builtin_clzll(x) - 1 : SIZE_MAX;
}

size_t size_bit_scan_forward(size_t x)
{
    return x ? __builtin_ctzll(x) : SIZE_MAX;
}

size_t size_pop_cnt(size_t x)
{
    return __builtin_popcountll(x);
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
    _InterlockedOr8((volatile char *) (arr + bit / CHAR_BIT), 1 << bit % CHAR_BIT);
}

void bit_reset_interlocked(volatile uint8_t *arr, size_t bit)
{
    _InterlockedAnd8((volatile char *) (arr + bit / CHAR_BIT), ~(1 << bit % CHAR_BIT));
}

void bit_set2_interlocked(volatile uint8_t *arr, size_t bit)
{
    uint8_t pos = bit % CHAR_BIT;
    if (pos < CHAR_BIT - 1) _InterlockedOr8((volatile char *) (arr + bit / CHAR_BIT), 3u << pos);
    else _InterlockedOr16((volatile short *) (arr + bit / CHAR_BIT), 3u << (CHAR_BIT - 1));
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

uint32_t uint32_pop_cnt(uint32_t x)
{
    return __popcnt((unsigned) x);
}

#   ifdef _M_X64

void size_inc_interlocked(volatile size_t *mem)
{
    _InterlockedIncrement64((volatile __int64 *) mem);
}

void size_dec_interlocked(volatile size_t *mem)
{
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
    return (size_t) res;
}

size_t size_sub(size_t *p_bor, size_t x, size_t y)
{
    unsigned __int64 res;
    *p_bor = _subborrow_u64(0, x, y, &res);
    return (size_t) res;
}

size_t size_sum(size_t *p_hi, size_t *args, size_t args_cnt)
{
    if (!args_cnt)
    {
        *p_hi = 0;
        return 0;
    }
    unsigned __int64 lo = args[0], hi = 0;
    for (size_t i = 1; i < args_cnt; _addcarry_u64(_addcarry_u64(0, lo, args[i++], &lo), hi, 0, &hi));
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

size_t size_pop_cnt(size_t x)
{
    return __popcnt64((__int64) x);
}

#   elif defined _M_IX86

void size_inc_interlocked(volatile size_t *mem)
{
    _InterlockedIncrement((volatile long *) mem);
}

void size_dec_interlocked(volatile size_t *mem)
{
    _InterlockedDecrement((volatile long *) mem);
}

#   endif
#endif 

#if defined __GNUC__ || defined __clang__ || defined _M_IX86 || defined __i386__
#   if defined _M_X64 || defined __x86_64__
typedef unsigned __int128 double_size;
#   elif defined _M_IX86 || defined __i386__
typedef unsigned long long double_size;
#   endif

size_t size_mul(size_t *p_hi, size_t x, size_t y)
{
    union { double_size val; struct { size_t lo, hi; }; } res = { .val = (double_size) x * (double_size) y };
    *p_hi = res.hi;
    return res.lo;
}

size_t size_add(size_t *p_car, size_t x, size_t y)
{
    union { double_size val; struct { size_t lo, hi; }; } res = { .val = (double_size) x + (double_size) y } ;
    *p_car = res.hi;
    return res.lo;
}

size_t size_sub(size_t *p_bor, size_t x, size_t y)
{
    union { double_size val; struct { size_t lo, hi; }; } res = { .val = (double_size) x - (double_size) y };
    *p_bor = 0 - res.hi;
    return res.lo;
}

size_t size_sum(size_t *p_hi, size_t *args, size_t args_cnt)
{
    if (!args_cnt)
    {
        *p_hi = 0;
        return 0;
    }
    union { double_size val; struct { size_t lo, hi; }; } res = { .val = (double_size) args[0] };
    for (size_t i = 1; i < args_cnt; res.val += args[i++]);
    *p_hi = res.hi;
    return res.lo;
}

#endif

#if defined _M_IX86 || defined __i386__

size_t size_bit_scan_reverse(size_t x)
{
    return (size_t) uint32_bit_scan_reverse((uint32_t) x);
}

size_t size_bit_scan_forward(size_t x)
{
    return (size_t) uint32_bit_scan_forward((uint32_t) x);
}

size_t size_pop_cnt(size_t x)
{
    return (size_t) uint32_pop_cnt((uint32_t) x);
}

#endif

DECLARE_LOAD_ACQUIRE(uint8_t, uint8)
DECLARE_LOAD_ACQUIRE(uint16_t, uint16)
DECLARE_LOAD_ACQUIRE(uint32_t, uint32)
DECLARE_LOAD_ACQUIRE(uint64_t, uint64)
DECLARE_LOAD_ACQUIRE(size_t, size)

void bit_set_interlocked_p(volatile void *arr, const void *p_bit)
{
    bit_set_interlocked(arr, *(const size_t *) p_bit);
}

void bit_reset_interlocked_p(volatile void *arr, const void *p_bit)
{
    bit_reset_interlocked(arr, *(const size_t *) p_bit);
}

void bit_set2_interlocked_p(volatile void *arr, const void *p_bit)
{
    bit_set2_interlocked(arr, *(const size_t *) p_bit);
}

void size_inc_interlocked_p(volatile void *mem, const void *arg)
{
    (void) arg;
    size_inc_interlocked(mem);
}

void size_dec_interlocked_p(volatile void *mem, const void *arg)
{
    (void) arg;
    size_dec_interlocked(mem);
}

uint8_t bit_get2_acquire(volatile uint8_t *arr, size_t bit)
{
    uint8_t pos = bit % CHAR_BIT;
    if (pos < CHAR_BIT - 1) return (uint8_load_acquire(arr + bit / CHAR_BIT) >> pos) & 3;
    else return (uint16_load_acquire((uint16_t *) (arr + bit / CHAR_BIT)) >> (CHAR_BIT - 1)) & 3;
}

bool bit_test2_acquire(volatile uint8_t *arr, size_t bit)
{
    return bit_get2_acquire(arr, bit) == 3;
}

bool bit_test2_acquire_p(volatile void *arr, const void *p_bit)
{
    return bit_test2_acquire(arr, *(const size_t *) p_bit);
}

bool bit_test_range_acquire(volatile uint8_t *arr, size_t cnt)
{
    size_t div = cnt / CHAR_BIT, rem = cnt % CHAR_BIT;
    for (size_t i = 0; i < div; i++) 
        if (uint8_load_acquire(arr + i) != UINT8_MAX) return 0;
    if (rem)
    {
        uint8_t msk = (1u << rem) - 1;
        return (uint8_load_acquire(arr + div) & msk) == msk;
    }
    return 1;
}

bool bit_test_range_acquire_p(volatile void *arr, const void *p_cnt)
{
    return bit_test_range_acquire(arr, *(const size_t *) p_cnt);
}

bool bit_test2_range_acquire(volatile uint8_t *arr, size_t cnt)
{
    size_t div = cnt / CHAR_BIT, rem = cnt % CHAR_BIT;
    for (size_t i = 0; i < div; i++) 
        if ((uint8_load_acquire(arr + i) & BIT_TEST2_MASK) != BIT_TEST2_MASK) return 0;
    if (rem)
    {
        uint8_t msk = ((1u << rem) - 1) & BIT_TEST2_MASK;
        return (uint8_load_acquire(arr + div) & msk) == msk;
    }
    return 1;
}

bool bit_test2_range_acquire_p(volatile void *arr, const void *p_cnt)
{
    return bit_test2_range_acquire(arr, *(const size_t *) p_cnt);
}

bool size_test_acquire(volatile size_t *mem)
{
    return !!size_load_acquire(mem);
}

bool size_test_acquire_p(volatile void *mem, const void *arg)
{
    (void) arg;
    return size_test_acquire(mem);
}

uint8_t uint8_bit_scan_reverse(uint8_t x)
{
    return (uint8_t) uint32_bit_scan_reverse((uint8_t) x);
}

uint8_t uint8_bit_scan_forward(uint8_t x)
{
    return (uint8_t) uint32_bit_scan_forward((uint8_t) x);
}

size_t size_add_sat(size_t a, size_t b)
{
    size_t car, res = size_add(&car, a, b);
    return car ? SIZE_MAX : res;
}

size_t size_sub_sat(size_t a, size_t b)
{
    size_t bor, res = size_sub(&bor, a, b);
    return bor ? 0 : res;
}

int size_cmp_stable_dsc(const void *A, const void *B, void *thunk)
{
    (void) thunk;
    size_t bor, diff = size_sub(&bor, *(size_t *) B, *(size_t *) A);
    return diff ? 1 - (int) (bor << 1) : 0;
}

int size_cmp_stable_asc(const void *A, const void *B, void *thunk)
{
    (void) thunk;
    return -size_cmp_stable_dsc(A, B, thunk);
}

bool size_cmp_dsc(const void *A, const void *B, void *thunk)
{
    (void) thunk;
    return *(size_t *) B > *(size_t *) A;
}

bool size_cmp_asc(const void *A, const void *B, void *thunk)
{
    (void) thunk;
    return *(size_t *) A > *(size_t *) B;
}

int flt64_stable_cmp_dsc(const void *a, const void *b, void *thunk)
{
    (void) thunk;
    __m128d ab = _mm_loadh_pd(_mm_load_sd(a), b);
    __m128i res = _mm_castpd_si128(_mm_cmplt_pd(ab, _mm_permute_pd(ab, 1)));
    return _mm_extract_epi32(res, 2) - _mm_cvtsi128_si32(res);
}

int flt64_stable_cmp_dsc_abs(const void *a, const void *b, void *thunk)
{
    (void) thunk;
    __m128d ab = _mm_and_pd(_mm_loadh_pd(_mm_load_sd(a), b), _mm_castsi128_pd(_mm_set1_epi64x(0x7fffffffffffffff)));
    __m128i res = _mm_castpd_si128(_mm_cmplt_pd(ab, _mm_permute_pd(ab, 1)));
    return _mm_extract_epi32(res, 2) - _mm_cvtsi128_si32(res);
}

int flt64_stable_cmp_dsc_nan(const void *a, const void *b, void *thunk)
{
    (void) thunk;
    __m128d ab = _mm_loadh_pd(_mm_load_sd(a), b);
    __m128i res = _mm_sub_epi32(_mm_castpd_si128(_mm_cmpunord_pd(ab, ab)), _mm_castpd_si128(_mm_cmp_pd(ab, _mm_permute_pd(ab, 1), _CMP_NLE_UQ)));
    return _mm_extract_epi32(res, 2) - _mm_cvtsi128_si32(res);
}

int flt64_sign(double x)
{
    __m128i res = _mm_castpd_si128(_mm_cmpgt_pd(_mm_set_sd(x), _mm_set_pd(x, 0)));
    return _mm_extract_epi32(res, 2) - _mm_cvtsi128_si32(res);
}

uint32_t uint32_fused_mul_add(uint32_t *p_res, uint32_t m, uint32_t a)
{
    union { uint64_t val; struct { uint32_t lo, hi; }; } res = { .val = (uint64_t) *p_res * (uint64_t) m + (uint64_t) a };
    *p_res = res.lo;
    return res.hi;
}

#define DECLARE_BIT_TEST(TYPE, PREFIX) \
    bool PREFIX ## _bit_test(TYPE *arr, size_t bit) \
    { \
        return !!(arr[bit / (CHAR_BIT * sizeof(TYPE))] & ((TYPE) 1 << bit % (CHAR_BIT * sizeof(TYPE)))); \
    }

#define DECLARE_BIT_TEST_SET(TYPE, PREFIX) \
    bool PREFIX ## _bit_test_set(TYPE *arr, size_t bit) \
    { \
        size_t ind = bit / (CHAR_BIT * sizeof(TYPE)); \
        uint8_t msk = (TYPE) 1 << bit % (CHAR_BIT * sizeof(TYPE)); \
        if (arr[ind] & msk) return 1; \
        arr[ind] |= msk; \
        return 0; \
    }

#define DECLARE_BIT_TEST_RESET(TYPE, PREFIX) \
    bool PREFIX ## _bit_test_reset(TYPE *arr, size_t bit) \
    { \
        size_t ind = bit / (CHAR_BIT * sizeof(TYPE)); \
        uint8_t msk = (TYPE) 1 << bit % (CHAR_BIT * sizeof(TYPE)); \
        if (arr[ind] & msk) \
        { \
            arr[ind] &= ~msk; \
            return 1; \
        } \
        return 0; \
    }

#define DECLARE_BIT_SET(TYPE, PREFIX) \
    void PREFIX ## _bit_set(TYPE *arr, size_t bit) \
    { \
        arr[bit / (CHAR_BIT * sizeof(TYPE))] |= (TYPE) 1 << bit % (CHAR_BIT * sizeof(TYPE)); \
    }

#define DECLARE_BIT_RESET(TYPE, PREFIX) \
    void PREFIX ## _bit_reset(TYPE *arr, size_t bit) \
    { \
        arr[bit / (CHAR_BIT * sizeof(TYPE))] &= ~((TYPE) 1 << bit % (CHAR_BIT * sizeof(TYPE))); \
    }

DECLARE_BIT_TEST(uint8_t, uint8)
DECLARE_BIT_TEST_SET(uint8_t, uint8)
DECLARE_BIT_TEST_RESET(uint8_t, uint8)
DECLARE_BIT_SET(uint8_t, uint8)
DECLARE_BIT_RESET(uint8_t, uint8)
