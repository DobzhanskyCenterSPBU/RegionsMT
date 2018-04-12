#pragma once

///////////////////////////////////////////////////////////////////////////////
//
//  Functions providing low level facilities
//

#include "common.h"

#if defined __GNUC__ || defined __clang__
typedef volatile unsigned spinlock_handle;
#elif defined _MSC_BUILD
typedef volatile long spinlock_handle;
#endif

#define SPINLOCK_INIT 0
void spinlock_acquire(spinlock_handle *);
void spinlock_release(spinlock_handle *);

typedef void *(*double_lock_callback)(void *);
void *double_lock_execute(spinlock_handle *, double_lock_callback, double_lock_callback, void *, void *);

uint8_t uint8_load_acquire(volatile uint8_t *);
uint16_t uint16_load_acquire(volatile uint16_t *);
uint32_t uint32_load_acquire(volatile uint32_t *);
uint64_t uint64_load_acquire(volatile uint64_t *);
size_t size_load_acquire(volatile size_t *);

void bit_set_interlocked(volatile uint8_t *, size_t);
void bit_set_interlocked_p(volatile void *, const void *);
void bit_reset_interlocked(volatile uint8_t *, size_t);
void bit_reset_interlocked_p(volatile void *, const void *);

// Sets two bits starting from the selected position specified by 2nd argument
void bit_set2_interlocked(volatile uint8_t *, size_t);
void bit_set2_interlocked_p(volatile void *, const void *);

void size_inc_interlocked(volatile size_t *);
void size_inc_interlocked_p(volatile void *, const void *);

void size_dec_interlocked(volatile size_t *);
void size_dec_interlocked_p(volatile void *, const void *);

bool size_test_acquire(volatile size_t *);
bool size_test_acquire_p(volatile void *, const void *);

// Gets two bits starting from the position specified by 2nd argument 
uint8_t bit_get2_acquire(volatile uint8_t *, size_t);

// Tests if two bits starting from the position specified by 2nd argument are set 
bool bit_test2_acquire(volatile uint8_t *, size_t);
bool bit_test2_acquire_p(volatile void *, const void *);

// Tests if first N bits are set where N specified by 2nd argument 
bool bit_test_range_acquire(volatile uint8_t *, size_t);
bool bit_test_range_acquire_p(volatile void *, const void *);

// Tests if first N pairs of bits are '01' or '11' where N specified by 2nd argument 
bool bit_test2_range_acquire(volatile uint8_t *, size_t);
bool bit_test2_range_acquire_p(volatile void *, const void *);

size_t size_add(size_t *, size_t, size_t);
size_t size_sub(size_t *, size_t, size_t);
size_t size_sum(size_t *, size_t *, size_t);
size_t size_mul(size_t *, size_t, size_t);
uint8_t uint8_bit_scan_reverse(uint8_t);
uint8_t uint8_bit_scan_forward(uint8_t);
uint32_t uint32_bit_scan_reverse(uint32_t);
uint32_t uint32_bit_scan_forward(uint32_t);
size_t size_bit_scan_reverse(size_t);
size_t size_bit_scan_forward(size_t);

#define SIZE_BIT (sizeof(size_t) << 3)
bool size_bit_test(size_t, uint8_t);
void size_bit_set(size_t *, uint8_t);
void size_bit_reset(size_t *, uint8_t);

size_t size_add_sat(size_t, size_t);
size_t size_sub_sat(size_t, size_t);

#define BYTE_CNT(BIT) (((BIT) + 7) >> 3)
bool bit_test(uint8_t *, size_t);
void bit_set(uint8_t *, size_t);
void bit_reset(uint8_t *, size_t);

#define NaN (((union { double val; uint64_t bin; }) { .bin = UINT64_MAX }).val)
int flt64_stable_cmp_dsc(const void *, const void *, void *);
int flt64_stable_cmp_dsc_abs(const void *, const void *, void *);
int flt64_stable_cmp_dsc_nan(const void *, const void *, void *);

uint32_t uint32_fused_mul_add(uint32_t *, uint32_t, uint32_t);