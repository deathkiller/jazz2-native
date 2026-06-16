#pragma once

#include <mutex>

#include <Base/Move.h>

namespace nCine
{
	/**
		@brief Pointer wrapper that keeps the object locked for the duration of its existence
		
		Holds a raw pointer together with a `std::unique_lock` over a lockable of type `TLock`.
		The lock is acquired before the wrapper is constructed and released when it is destroyed,
		so the pointed-to object can be accessed safely through @ref operator*() and
		@ref operator->() for the lifetime of the wrapper.
	*/
	template<class T, class TLock>
	class LockedPtr
	{
	public:
		explicit LockedPtr(T* ptr, std::unique_lock<TLock>&& lock) noexcept
			: _pointer(ptr), _lock(Death::move(lock)) {}

		/** @brief Returns the pointer */
		DEATH_CONSTEXPR14 T* get() const noexcept { return _pointer; }

		/** @brief Unlocks the locked pointer */
		void unlock() noexcept {
			_lock.unlock();
			_pointer = nullptr;
		}

		/** @brief Dereferences the pointer */
		DEATH_CONSTEXPR14 T& operator*() const noexcept { return *_pointer; }
		/** @brief Allows direct calls against the pointer */
		DEATH_CONSTEXPR14 T* operator->() const noexcept { return _pointer; }

		/** @brief Whether the pointer is assigned */
		explicit operator bool() const noexcept {
			return (_pointer != nullptr);
		}

	private:
		T* _pointer;
		std::unique_lock<TLock> _lock;
	};
}