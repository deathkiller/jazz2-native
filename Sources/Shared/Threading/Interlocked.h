#pragma once

#include "../Common.h"

#if defined(DEATH_TARGET_MSVC)
#	include <intrin.h>
#endif

namespace Death {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides atomic operations for variables that are shared by multiple threads
	*/
	class Interlocked
	{
	public:
		/**
		 * @brief Increments the value of the specified 32-bit variable as an atomic operation
		 * @param addend    Variable to be incremented
		 * @param size      Size of the C string, excluding the null terminator
		 * @param flags     Flags describing additional string properties
		 * @return          The resulting incremented value
		 */
		template<typename T>
		static T Increment(T volatile* addend);

		/**
		 * @brief Decrements the value of the specified 32-bit variable as an atomic operation
		 * @param addend    Variable to be decremented
		 * @return          The resulting decremented value
		 */
		template<typename T>
		static T Decrement(T volatile* addend);

		/**
		 * @brief Performs an atomic AND operation on the values of the specified 32-bit variables
		 * @param destination   The first operand and the destination
		 * @param value         The second operand
		 */
		template<typename T>
		static void And(T volatile* destination, T value);

		/**
		 * @brief Performs an atomic OR operation on the values of the specified 32-bit variables
		 * @param destination   The first operand and the destination
		 * @param value         The second operand
		 */
		template<typename T>
		static void Or(T volatile* destination, T value);

		/**
		 * @brief Sets a 32-bit variable to the specified value as an atomic operation
		 * @param destination   Value to be exchanged
		 * @param value         Value to set the destination to
		 * @return              The previous value of the destination
		 */
		template<typename T>
		static T Exchange(T volatile* destination, T value);

		/**
		 * @brief Sets a pointer variable to the specified value as an atomic operation
		 * @param destination   Value to be exchanged
		 * @param value         Value to set the destination to
		 * @return              The previous value of the destination
		 */
		template<typename T>
		static T ExchangePointer(T volatile* destination, T value);

		/** @overload */
		template<typename T>
		static T ExchangePointer(T volatile* destination, std::nullptr_t value);

		/**
		 * @brief Performs an atomic addition of two 32-bit values
		 * @param addend    Variable to be added to
		 * @param value     Value to add
		 * @return          The previous value of the destination
		 */
		template<typename T>
		static T ExchangeAdd(T volatile* addend, T value);

		/**
		 * @brief Performs an atomic addition of two 64-bit values
		 * @param addend    Variable to be added to
		 * @param value     Value to add
		 * @return          The previous value of the destination
		 */
		template<typename T>
		static T ExchangeAdd64(T volatile* addend, T value);

		/**
		 * @brief Performs an atomic addition of two pointer values
		 * @param addend    Variable to be added to
		 * @param value     Value to add
		 * @return          The previous value of the destination
		 */
		template<typename T>
		static T ExchangeAddPointer(T volatile* addend, T value);

		/**
		 * @brief Performs an atomic compare-and-exchange operation on the specified 32-bit variables
		 * @param destination   Value to be exchanged
		 * @param exchange      Value to set the destination to
		 * @param comparand     Value to compare the destination to before setting it to the exchange, the destination is set only if the destination is equal to the comparand
		 * @return              The original value of the destination
		 */
		template<typename T>
		static T CompareExchange(T volatile* destination, T exchange, T comparand);

		/**
		 * @brief Performs an atomic compare-and-exchange operation on the specified pointer variables
		 * @param destination   Value to be exchanged
		 * @param exchange      Value to set the destination to
		 * @param comparand     Value to compare the destination to before setting it to the exchange, the destination is set only if the destination is equal to the comparand
		 * @return              The original value of the destination
		 */
		template<typename T>
		static T CompareExchangePointer(T volatile* destination, T exchange, T comparand);

		/** @overload */
		template<typename T>
		static T CompareExchangePointer(T volatile* destination, T exchange, std::nullptr_t comparand);

	private:
		Interlocked() = delete;

#if !defined(DEATH_TARGET_MSVC)
		static void InterlockedOperationBarrier();
#endif
	};

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::Increment(T volatile* addend)
	{
#if defined(DEATH_TARGET_MSVC)
		static_assert(sizeof(T) == sizeof(long), "Size of T must be the same as size of long");
		return _InterlockedIncrement((long*)addend);
#else
		T result = __sync_add_and_fetch(addend, 1);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::Decrement(T volatile* addend)
	{
#if defined(DEATH_TARGET_MSVC)
		static_assert(sizeof(T) == sizeof(long), "Size of T must be the same as size of long");
		return _InterlockedDecrement((long*)addend);
#else
		T result = __sync_sub_and_fetch(addend, 1);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::Exchange(T volatile* destination, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		static_assert(sizeof(T) == sizeof(long), "Size of T must be the same as size of long");
		return _InterlockedExchange((long*)destination, value);
#else
		T result = __atomic_exchange_n(destination, value, __ATOMIC_ACQ_REL);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::CompareExchange(T volatile* destination, T exchange, T comparand)
	{
#if defined(DEATH_TARGET_MSVC)
		static_assert(sizeof(T) == sizeof(long), "Size of T must be the same as size of long");
		return _InterlockedCompareExchange((long*)destination, exchange, comparand);
#else
		T result = __sync_val_compare_and_swap(destination, comparand, exchange);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::ExchangeAdd(T volatile* addend, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		static_assert(sizeof(T) == sizeof(long), "Size of T must be the same as size of long");
		return _InterlockedExchangeAdd((long*)addend, value);
#else
		T result = __sync_fetch_and_add(addend, value);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::ExchangeAdd64(T volatile* addend, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		static_assert(sizeof(T) == sizeof(std::int64_t), "Size of T must be the same as size of std::int64_t");
		return _InterlockedExchangeAdd64((std::int64_t*)addend, value);
#else
		T result = __sync_fetch_and_add(addend, value);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::ExchangeAddPointer(T volatile* addend, T value)
	{
#if defined(DEATH_TARGET_MSVC)
#	if !defined(DEATH_TARGET_32BIT)
		static_assert(sizeof(T) == sizeof(std::int64_t), "Size of T must be the same as size of std::int64_t");
		return _InterlockedExchangeAdd64((std::int64_t*)addend, value);
#	else
		static_assert(sizeof(T) == sizeof(long), "Size of T must be the same as size of long");
		return _InterlockedExchangeAdd((long*)addend, value);
#	endif
#else
		T result = __sync_fetch_and_add(addend, value);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE void Interlocked::And(T volatile* destination, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		static_assert(sizeof(T) == sizeof(long), "Size of T must be the same as size of long");
		_InterlockedAnd((long*)destination, value);
#else
		__sync_and_and_fetch(destination, value);
		InterlockedOperationBarrier();
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE void Interlocked::Or(T volatile* destination, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		static_assert(sizeof(T) == sizeof(long), "Size of T must be the same as size of long");
		_InterlockedOr((long*)destination, value);
#else
		__sync_or_and_fetch(destination, value);
		InterlockedOperationBarrier();
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::ExchangePointer(T volatile* destination, T value)
	{
#if defined(DEATH_TARGET_MSVC)
#	if !defined(DEATH_TARGET_32BIT)
		return (T)(std::uintptr_t)_InterlockedExchangePointer((void* volatile*)destination, value);
#	else
		return (T)(std::uintptr_t)_InterlockedExchange((long volatile*)(void* volatile*)destination, (long)(void*)value);
#	endif
#else
		T result = (T)(std::uintptr_t)__atomic_exchange_n((void* volatile*)destination, value, __ATOMIC_ACQ_REL);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::ExchangePointer(T volatile* destination, std::nullptr_t value)
	{
#if defined(DEATH_TARGET_MSVC)
#	if !defined(DEATH_TARGET_32BIT)
		return (T)(std::uintptr_t)_InterlockedExchangePointer((void* volatile*)destination, value);
#	else
		return (T)(std::uintptr_t)_InterlockedExchange((long volatile*)(void* volatile*)destination, (long)(void*)value);
#	endif
#else
		T result = (T)(std::uintptr_t)__atomic_exchange_n((void* volatile*)destination, value, __ATOMIC_ACQ_REL);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::CompareExchangePointer(T volatile* destination, T exchange, T comparand)
	{
#if defined(DEATH_TARGET_MSVC)
#	if !defined(DEATH_TARGET_32BIT)
		return (T)(std::uintptr_t)_InterlockedCompareExchangePointer((void* volatile*)destination, exchange, comparand);
#	else
		return (T)(std::uintptr_t)_InterlockedCompareExchange((long volatile*)(void* volatile*)destination, (long)(void*)exchange, (long)(void*)comparand);
#	endif
#else
		T result = (T)(std::uintptr_t)__sync_val_compare_and_swap((void* volatile*)destination, comparand, exchange);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<typename T>
	DEATH_ALWAYS_INLINE T Interlocked::CompareExchangePointer(T volatile* destination, T exchange, std::nullptr_t comparand)
	{
#if defined(DEATH_TARGET_MSVC)
#	if !defined(DEATH_TARGET_32BIT)
		return (T)(std::uintptr_t)_InterlockedCompareExchangePointer((void* volatile*)destination, (void*)exchange, (void*)comparand);
#	else
		return (T)(std::uintptr_t)_InterlockedCompareExchange((long volatile*)(void* volatile*)destination, (long)(void*)exchange, (long)(void*)comparand);
#	endif
#else
		T result = (T)(std::uintptr_t)__sync_val_compare_and_swap((void* volatile*)destination, (void*)comparand, (void*)exchange);
		InterlockedOperationBarrier();
		return result;
#endif
	}

#if !defined(DEATH_TARGET_MSVC)
	DEATH_ALWAYS_INLINE void Interlocked::InterlockedOperationBarrier()
	{
#	if defined(DEATH_TARGET_ARM) || defined(DEATH_TARGET_RISCV)
		__sync_synchronize();
#	endif
	}
#endif
}