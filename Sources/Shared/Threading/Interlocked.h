#pragma once

#include "../Common.h"

#if defined(DEATH_TARGET_MSVC)
#	include <intrin.h>
#endif

namespace Death { namespace Threading {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Provides atomic operations for variables that are shared by multiple threads
	*/
	class Interlocked
	{
	public:
		Interlocked() = delete;
		~Interlocked() = delete;

		/**
		 * @brief Reads the value of the specified 32/64-bit variable as an atomic operation with acquire semantics
		 * @param source    Variable to be read
		 * @return          The resulting value
		 */
		template<class T>
		static T ReadAcquire(T volatile* source);

		/**
		 * @brief Writes the value to the specified 32/64-bit variable as an atomic operation with release semantics
		 * @param destination    Variable to be written
		 * @param value          New value
		 */
		template<class T>
		static void WriteRelease(T volatile* destination, T value);

		/**
		 * @brief Increments the value of the specified 32/64-bit variable as an atomic operation
		 * @param addend    Variable to be incremented
		 * @return          The resulting incremented value
		 */
		template<class T>
		static T Increment(T volatile* addend);

		/**
		 * @brief Decrements the value of the specified 32/64-bit variable as an atomic operation
		 * @param addend    Variable to be decremented
		 * @return          The resulting decremented value
		 */
		template<class T>
		static T Decrement(T volatile* addend);

		/**
		 * @brief Performs an atomic AND operation on the values of the specified 32/64-bit variables
		 * @param destination   The first operand and the destination
		 * @param value         The second operand
		 */
		template<class T>
		static void And(T volatile* destination, T value);

		/**
		 * @brief Performs an atomic OR operation on the values of the specified 32/64-bit variables
		 * @param destination   The first operand and the destination
		 * @param value         The second operand
		 */
		template<class T>
		static void Or(T volatile* destination, T value);

		/**
		 * @brief Exchanges a 32/64-bit variable to the specified value as an atomic operation
		 * @param destination   Value to be exchanged
		 * @param value         Value to set the destination to
		 * @return              The previous value of the destination
		 */
		template<class T>
		static T Exchange(T volatile* destination, T value);

		/** @overload */
		template<class T, class U, class = typename std::enable_if<std::is_same<std::nullptr_t, U>::value>::type>
		static T Exchange(T volatile* destination, U);

		/**
		 * @brief Performs an atomic exchange and addition of two 32/64-bit values
		 * @param addend    Variable to be added to
		 * @param value     Value to add
		 * @return          The previous value of the destination
		 */
		template<class T>
		static T ExchangeAdd(T volatile* addend, T value);

		/**
		 * @brief Performs an atomic compare-and-exchange operation on the specified 32/64-bit variables
		 * @param destination   Value to be exchanged
		 * @param exchange      Value to set the destination to
		 * @param comparand     Value to compare the destination to before setting it to the exchange, the destination is set only if the destination is equal to the comparand
		 * @return              The original value of the destination
		 */
		template<class T>
		static T CompareExchange(T volatile* destination, T exchange, T comparand);

	private:
#if !defined(DEATH_TARGET_MSVC)
		static void InterlockedOperationBarrier();
#endif
	};

	template<class T>
	DEATH_ALWAYS_INLINE T Interlocked::ReadAcquire(T volatile* source)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			return (T)::ReadAcquire((LONG*)source);
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			return (T)::ReadAcquire64((LONG64*)source);
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		return __atomic_load_n(source, __ATOMIC_ACQUIRE);
#endif
	}

	template<class T>
	DEATH_ALWAYS_INLINE void Interlocked::WriteRelease(T volatile* destination, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			::WriteRelease((LONG*)destination, (LONG)value);
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			::WriteRelease((LONG64*)destination, (LONG64)value);
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		__atomic_store_n(destination, value, __ATOMIC_RELEASE);
#endif
	}

	template<class T>
	DEATH_ALWAYS_INLINE T Interlocked::Increment(T volatile* addend)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			return (T)_InterlockedIncrement((LONG*)addend);
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			return (T)_InterlockedIncrement64((LONG64*)addend);
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		T result = __sync_add_and_fetch(addend, 1);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<class T>
	DEATH_ALWAYS_INLINE T Interlocked::Decrement(T volatile* addend)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			return (T)_InterlockedDecrement((LONG*)addend);
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			return (T)_InterlockedDecrement64((LONG64*)addend);
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		T result = __sync_sub_and_fetch(addend, 1);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<class T>
	DEATH_ALWAYS_INLINE T Interlocked::Exchange(T volatile* destination, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			return (T)_InterlockedExchange((LONG*)destination, (LONG)value);
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			return (T)_InterlockedExchange64((LONG64*)destination, (LONG64)value);
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		T result = __atomic_exchange_n(destination, value, __ATOMIC_ACQ_REL);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<class T, class U, class>
	DEATH_ALWAYS_INLINE T Interlocked::Exchange(T volatile* destination, U)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			return (T)_InterlockedExchange((LONG*)destination, {});
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			return (T)_InterlockedExchange64((LONG64*)destination, {});
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		T result = __atomic_exchange_n(destination, {}, __ATOMIC_ACQ_REL);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<class T>
	DEATH_ALWAYS_INLINE T Interlocked::CompareExchange(T volatile* destination, T exchange, T comparand)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			return (T)_InterlockedCompareExchange((LONG*)destination, (LONG)exchange, (LONG)comparand);
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			return (T)_InterlockedCompareExchange64((LONG64*)destination, (LONG64)exchange, (LONG64)comparand);
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		T result = __sync_val_compare_and_swap(destination, comparand, exchange);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<class T>
	DEATH_ALWAYS_INLINE T Interlocked::ExchangeAdd(T volatile* addend, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			return (T)_InterlockedExchangeAdd((LONG*)addend, (LONG)value);
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			return (T)_InterlockedExchangeAdd64((LONG64*)addend, (LONG64)value);
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		T result = __sync_fetch_and_add(addend, value);
		InterlockedOperationBarrier();
		return result;
#endif
	}

	template<class T>
	DEATH_ALWAYS_INLINE void Interlocked::And(T volatile* destination, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			return (T)_InterlockedAnd((LONG*)destination, (LONG)value);
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			return (T)_InterlockedAnd64((LONG64*)destination, (LONG64)value);
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		__sync_and_and_fetch(destination, value);
		InterlockedOperationBarrier();
#endif
	}

	template<class T>
	DEATH_ALWAYS_INLINE void Interlocked::Or(T volatile* destination, T value)
	{
#if defined(DEATH_TARGET_MSVC)
		if constexpr (sizeof(T) == sizeof(LONG)) {
			return (T)_InterlockedOr((LONG*)destination, (LONG)value);
		} else if constexpr (sizeof(T) == sizeof(LONG64)) {
			return (T)_InterlockedOr64((LONG64*)destination, (LONG64)value);
		} else {
			static_assert(sizeof(T) == sizeof(LONG) || sizeof(T) == sizeof(LONG64), "Size of T must be 32-bit or 64-bit");
		}
#else
		__sync_or_and_fetch(destination, value);
		InterlockedOperationBarrier();
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

}}