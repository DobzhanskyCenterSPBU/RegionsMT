#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef bool (*cmp_callback)(const void *, const void *, void *); // Functional type for compare callbacks
typedef int (*stable_cmp_callback)(const void *, const void *, void *); // Functional type for stable compare callbacks

uintptr_t *orders_stable(void *, size_t, size_t, stable_cmp_callback, void *);
uintptr_t *orders_stable_unique(void *, size_t *, size_t, stable_cmp_callback, void *);
uintptr_t *ranks_from_orders(const uintptr_t *restrict, size_t);
bool ranks_from_orders_inplace(uintptr_t *restrict, uintptr_t, size_t, size_t);
uintptr_t *ranks_stable(void *, size_t, size_t, stable_cmp_callback, void *);
void quick_sort(void *restrict, size_t, size_t, cmp_callback, void *);
size_t binary_search(const void *restrict, const void *restrict, size_t, size_t, stable_cmp_callback, void *);
