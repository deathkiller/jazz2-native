// 64-bit atomic operations for the Wii U, which devkitPPC has no libatomic for.
//
// The Espresso is a 32-bit PowerPC: it has lwarx/stwcx. for 32-bit words, but no 64-bit load-reserve
// instruction, so GCC lowers every std::atomic<std::int64_t> operation to a __atomic_*_8 call that libatomic
// would normally provide. The byte counters WebRequest keeps per request are such atomics. These versions
// guard the operation with a single global coreinit spin lock; the uninterruptible variant also disables
// interrupts on the core while it is held, so a thread holding it cannot be preempted by one spinning on it.
//
// This file has to be compiled without LTO (see ncine_extra_sources.cmake): the calls only appear when the
// link-time code generation lowers the atomics, after the linker has already decided which symbols of the
// LTO objects are referenced, so a definition in an LTO object would be dropped.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <coreinit/spinlock.h>

static OSSpinLock _atomic64Lock;	// Zero-initialized, which is what OSInitSpinLock() sets too

uint64_t __atomic_load_8(const volatile void* mem, int model)
{
	(void)model;
	OSUninterruptibleSpinLock_Acquire(&_atomic64Lock);
	uint64_t result = *(const volatile uint64_t*)mem;
	OSUninterruptibleSpinLock_Release(&_atomic64Lock);
	return result;
}

void __atomic_store_8(volatile void* mem, uint64_t value, int model)
{
	(void)model;
	OSUninterruptibleSpinLock_Acquire(&_atomic64Lock);
	*(volatile uint64_t*)mem = value;
	OSUninterruptibleSpinLock_Release(&_atomic64Lock);
}

uint64_t __atomic_exchange_8(volatile void* mem, uint64_t value, int model)
{
	(void)model;
	OSUninterruptibleSpinLock_Acquire(&_atomic64Lock);
	uint64_t result = *(volatile uint64_t*)mem;
	*(volatile uint64_t*)mem = value;
	OSUninterruptibleSpinLock_Release(&_atomic64Lock);
	return result;
}

// The library call has no `weak` argument, unlike the __atomic_compare_exchange_n() built-in
bool __atomic_compare_exchange_8(volatile void* mem, void* expected, uint64_t desired, int success, int failure)
{
	(void)success; (void)failure;
	OSUninterruptibleSpinLock_Acquire(&_atomic64Lock);
	uint64_t current = *(volatile uint64_t*)mem;
	uint64_t expectedValue;
	memcpy(&expectedValue, expected, sizeof(expectedValue));
	bool result = (current == expectedValue);
	if (result) {
		*(volatile uint64_t*)mem = desired;
	} else {
		memcpy(expected, &current, sizeof(current));
	}
	OSUninterruptibleSpinLock_Release(&_atomic64Lock);
	return result;
}

#define DEFINE_FETCH_OP(name, op)															\
	uint64_t __atomic_fetch_##name##_8(volatile void* mem, uint64_t value, int model)		\
	{																						\
		(void)model;																		\
		OSUninterruptibleSpinLock_Acquire(&_atomic64Lock);									\
		uint64_t result = *(volatile uint64_t*)mem;											\
		*(volatile uint64_t*)mem = result op value;											\
		OSUninterruptibleSpinLock_Release(&_atomic64Lock);									\
		return result;																		\
	}																						\
	uint64_t __atomic_##name##_fetch_8(volatile void* mem, uint64_t value, int model)		\
	{																						\
		(void)model;																		\
		OSUninterruptibleSpinLock_Acquire(&_atomic64Lock);									\
		uint64_t result = *(volatile uint64_t*)mem op value;								\
		*(volatile uint64_t*)mem = result;													\
		OSUninterruptibleSpinLock_Release(&_atomic64Lock);									\
		return result;																		\
	}

DEFINE_FETCH_OP(add, +)
DEFINE_FETCH_OP(sub, -)
DEFINE_FETCH_OP(and, &)
DEFINE_FETCH_OP(or, |)
DEFINE_FETCH_OP(xor, ^)
