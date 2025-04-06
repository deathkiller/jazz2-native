#pragma once

#include <mutex>

#include <Base/Move.h>

namespace nCine
{
	/**
		@brief Pointer wrapper that keeps the object locked for the duration of its existence.
	*/
	template<class T, class TLock>
	class LockedPtr
	{
	public:
		explicit LockedPtr(T* ptr, std::unique_lock<TLock>&& lock) noexcept
			: _pointer(ptr), _lock(Death::move(lock)) {}

		/** @brief Returns the pointer */
		DEATH_CONSTEXPR14 T* get() const noexcept { return _pointer; }

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