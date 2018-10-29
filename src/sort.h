#pragma once

#include "common.h"

typedef bool (*cmp_callback)(const void *, const void *, void *); // Functional type for compare callbacks
typedef int (*stable_cmp_callback)(const void *, const void *, void *); // Functional type for stable compare callbacks

uintptr_t *pointers_stable(const void *, size_t, size_t, stable_cmp_callback, void *);
void orders_from_pointers_inplace(uintptr_t *, uintptr_t, size_t, size_t);
uintptr_t *orders_stable(const void *, size_t, size_t, stable_cmp_callback, void *);
uintptr_t *orders_stable_unique(const void *, size_t *, size_t, stable_cmp_callback, void *);
void ranks_from_pointers_impl(size_t *, const uintptr_t *, uintptr_t, size_t, size_t);
size_t *ranks_from_pointers(const uintptr_t *, uintptr_t, size_t, size_t);
size_t *ranks_from_orders(const uintptr_t *, size_t);
void ranks_unique_from_pointers_impl(size_t *, const uintptr_t *, uintptr_t, size_t *, size_t, cmp_callback, void *);
size_t *ranks_unique_from_pointers(const uintptr_t *, uintptr_t, size_t *, size_t, cmp_callback, void *);
size_t *ranks_unique_from_orders(const uintptr_t *, const void *, size_t *, size_t, cmp_callback, void *);
void ranks_from_pointers_inplace_impl(uintptr_t *restrict, uintptr_t, size_t, size_t, uint8_t *restrict);
bool ranks_from_pointers_inplace(uintptr_t *restrict, uintptr_t, size_t, size_t);
bool ranks_from_orders_inplace(uintptr_t *restrict, size_t);
uintptr_t *ranks_stable(const void *, size_t, size_t, stable_cmp_callback, void *);
void orders_apply_impl(uintptr_t *restrict, size_t, size_t, void *restrict, uint8_t *restrict, void *restrict);
bool orders_apply(uintptr_t *restrict, size_t, size_t, void *restrict);

#define QUICK_SORT_CUTOFF 20 // The actual quick sort is applied only for arrays of counts, greater than this value
#define QUICK_SORT_CACHED // Enables the more optimal utilization of the CPU caches
void quick_sort(void *restrict, size_t, size_t, cmp_callback, void *);

enum binary_search_flags {
    BINARY_SEARCH_RIGHTMOST = 1, // Searches for the rightmost occurrence of the key
    BINARY_SEARCH_CRITICAL = 2, // Searches for the left/rightmost occurrence of the key
    BINARY_SEARCH_APPROX = 4, // Searches for the best approximation from left/right side
};

bool binary_search(size_t *, const void *restrict, const void *restrict, size_t, size_t, stable_cmp_callback, void *, enum binary_search_flags);
