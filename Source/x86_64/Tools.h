#pragma once

#include "../Common.h"

#if defined __x86_64 || defined _M_X64

static_assert(sizeof (uint64_t) == sizeof (size_t), "The size of 'size_t' type should be equal to the size of 'uint64_t'!");

#define sizeAddExchangeInterlocked ((size_t (*)(volatile size_t *, size_t)) uint64AddExchangeInterlocked)
#define sizeAddInterlockedMem ((void (*)(volatile size_t *, size_t *)) uint64AddInterlockedMem)
#define sizeAddInterlocked ((void (*)(volatile size_t *, size_t)) uint64AddInterlocked)
#define sizeIncInterlocked ((void (*)(volatile size_t *, void *)) uint64IncInterlocked)
#define sizeDecInterlocked ((void (*)(volatile size_t *, void *)) uint64DecInterlocked)
#define sizeCompareMem ((void (*)(volatile size_t *, size_t *)) uint64CompareMem)
#define sizeTest ((void (*)(volatile size_t *, void *)) uint64Test)
#define sizeBitScanReverse ((size_t (*)(size_t)) uint64BitScanReverse)
#define sizeFusedMulAdd ((bool (*)(size_t *, size_t, size_t)) uint64FusedMulAdd)
#define sizeFusedMul ((bool (*)(size_t *, size_t)) uint64FusedMul)

uint64_t uint64AddExchangeInterlocked(volatile uint64_t *, uint64_t);
void uint64AddInterlockedMem(volatile uint64_t *, uint64_t *);
void uint64AddInterlocked(volatile uint64_t *, uint64_t);
void uint64IncInterlocked(volatile uint64_t *, void *); // 2nd argument is unused
void uint64DecInterlocked(volatile uint64_t *, void *); // 2nd argument is unused
bool uint64CompareMem(uint64_t *, uint64_t *);
bool uint64Test(uint64_t *, void *); // 2nd argument is unused

void bitSetInterlockedMem(volatile uint8_t *, size_t *);
void bitSetInterlocked(volatile uint8_t *, size_t);
void bitSet(uint8_t *, size_t);
void bitResetInterlocked(volatile uint8_t *, size_t);
void bitReset(uint8_t *, size_t);
bool bitTestMem(uint8_t *, size_t *);
bool bitTest(uint8_t *, size_t);

// Sets two bits starting from the selected position (2nd argument)
void bitSet2InterlockedMem(volatile uint8_t *, size_t *);
void bitSet2Interlocked(volatile uint8_t *, size_t);
// Tests if two bits starting from the selected position (2nd argument) are set
bool bitTest2Mem(uint8_t *, size_t *);
bool bitTest2(uint8_t *, size_t);
// Gets two bits starting from the selected position (2nd argument)
uint8_t bitGet2(uint8_t *, size_t);

// Tests if first N bits are set, N specified by 2nd argument
bool bitTestRangeMem(uint8_t *, size_t *);
bool bitTestRange(uint8_t *, size_t);

// Tests if first N pairs of bits are '01' or '11', N specified by 2nd argument
bool bitTestRange2Mem(uint8_t *, size_t *);
bool bitTestRange2(uint8_t *, size_t);

// Functions search for the first bit set (either from left to right, or vice versa). Return '-1' if argument is zero
uint8_t uint8BitScanForward(uint8_t);
uint8_t uint8BitScanReverse(uint8_t);
uint32_t uint32BitScanReverse(uint32_t);
uint64_t uint64BitScanReverse(uint64_t);

// Functions stores the number (1st) * (2nd) + (3rd) in the 1st argument. Return '1' if no overflow occurs
bool uint32FusedMulAdd(uint32_t *, uint32_t, uint32_t);
bool uint64FusedMulAdd(uint64_t *, uint64_t, uint64_t);

// Functions stores the number (1st) * (2nd) in the 1st argument. Return '1' if no overflow occurs
bool uint64FusedMul(uint64_t *, uint64_t);

#endif
