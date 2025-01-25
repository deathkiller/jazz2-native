#pragma once

#include "../Common.h"

#include <memory>

#if defined(DEATH_TARGET_MSVC)
#	include <malloc.h>
#elif !defined(__FreeBSD__) && !defined(__DragonFly__) // It's part of <stdlib.h> on FreeBSD
#	include <alloca.h>
#endif

namespace Death { namespace Implementation {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	/**
		@brief Array allocated on the stack. Don't use this class directly, use placeholder type specifier instead.
	*/
	template<typename T>
	class stack_alloc
	{
	public:
		stack_alloc(T* data, std::size_t size)
			: _data(data), _size((std::uint32_t)size)
		{
		}

		~stack_alloc()
		{
			if (_size == UINT32_MAX) {
				delete[] _data;
			} else if constexpr (!std::is_trivially_destructible<T>::value) {
				for (std::int32_t i = 0; i < _size; i++) {
					_data[i].~T();
				}
			}
		}

		stack_alloc(stack_alloc&& o) noexcept
			: _data(o._data), _size(o._size)
		{
			o._size = 0;
		}

		constexpr T& operator*() const { return *_data; }
		constexpr operator T*() const { return _data; }

	private:
		T* _data;
		std::uint32_t _size;
	};

}}

/**
	@brief Allocates an array of the given type on the stack if possible

	If @p size is greater than 4024 bytes, the standard heap allocation is used. Otherwise, the stack
	allocation is used, which must be used with caution as there is no check on how much space is left in
	the stack. Using the following syntax is recommended, as the type itself is an implementation detail:
	
	@code{.cpp}
	auto array = stack_alloc(std::int32_t, 1024);
	array[0] = …;
	@endcode
*/
#if defined(DEATH_TARGET_MSVC)
#	define stack_alloc(type, size)																					\
		__pragma(warning(suppress: 6255 6386))																		\
		(((size) * sizeof(type)) < (4024)																			\
			? Death::Implementation::stack_alloc<type>(new (_alloca((size) * sizeof(type))) type[(size)], (size))	\
			: Death::Implementation::stack_alloc<type>(new type[(size)], UINT32_MAX))
#else
#	define stack_alloc(type, size)																					\
		(((size) * sizeof(type)) < (4024)																			\
			? Death::Implementation::stack_alloc<type>(new (alloca((size) * sizeof(type))) type[(size)], (size))	\
			: Death::Implementation::stack_alloc<type>(new type[(size)], UINT32_MAX))
#endif
