#pragma once

#include "CommonBase.h"

// Always define fixed width integer types (int8_t, int16_t, int32_t, int64_t, ...)
#include <cstddef>
#include <cstdint>

#if !defined(_countof)
#	if defined(__cplusplus)
		namespace Death::Implementation { template<typename T, std::size_t N> char(*_ArrayCountOfHelper(T(&)[N]))[N]; }
#		define _countof(a) (sizeof(*Death::Implementation::_ArrayCountOfHelper(a)))
#	else
#		define _countof(a) (sizeof(a) / sizeof(a[0]))
#	endif
#endif

#if defined(__cplusplus)
namespace Death::Implementation
{
	// Used as an approximation of std::underlying_type<T>
	template<int S> struct _EnumTypeForSize;
	template<> struct _EnumTypeForSize<1> { typedef int8_t Type; };
	template<> struct _EnumTypeForSize<2> { typedef int16_t Type; };
	template<> struct _EnumTypeForSize<4> { typedef int32_t Type; };
	template<> struct _EnumTypeForSize<8> { typedef int64_t Type; };
	template<class T> struct _EnumSizedInteger { typedef typename _EnumTypeForSize<sizeof(T)>::Type Type; };
}

#	define DEFINE_ENUM_OPERATORS(type)	\
		inline DEATH_CONSTEXPR14 type operator|(type a, type b) { return type(((Death::Implementation::_EnumSizedInteger<type>::Type)a) | ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }	\
		inline type& operator|=(type& a, type b) { return (type&)(((Death::Implementation::_EnumSizedInteger<type>::Type&)a) |= ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }			\
		inline DEATH_CONSTEXPR14 type operator&(type a, type b) { return type(((Death::Implementation::_EnumSizedInteger<type>::Type)a) & ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }	\
		inline type& operator&=(type& a, type b) { return (type&)(((Death::Implementation::_EnumSizedInteger<type>::Type&)a) &= ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }			\
		inline DEATH_CONSTEXPR14 type operator~(type a) { return type(~((Death::Implementation::_EnumSizedInteger<type>::Type)a)); }																	\
		inline DEATH_CONSTEXPR14 type operator^(type a, type b) { return type(((Death::Implementation::_EnumSizedInteger<type>::Type)a) ^ ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }	\
		inline type& operator^=(type& a, type b) { return (type&)(((Death::Implementation::_EnumSizedInteger<type>::Type&)a) ^= ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }

#	define DEFINE_PRIVATE_ENUM_OPERATORS(type)	\
		friend inline DEATH_CONSTEXPR14 type operator|(type a, type b) { return type(((Death::Implementation::_EnumSizedInteger<type>::Type)a) | ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }	\
		friend inline type& operator|=(type& a, type b) { return (type&)(((Death::Implementation::_EnumSizedInteger<type>::Type&)a) |= ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }			\
		friend inline DEATH_CONSTEXPR14 type operator&(type a, type b) { return type(((Death::Implementation::_EnumSizedInteger<type>::Type)a) & ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }	\
		friend inline type& operator&=(type& a, type b) { return (type&)(((Death::Implementation::_EnumSizedInteger<type>::Type&)a) &= ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }			\
		friend inline DEATH_CONSTEXPR14 type operator~(type a) { return type(~((Death::Implementation::_EnumSizedInteger<type>::Type)a)); }																		\
		friend inline DEATH_CONSTEXPR14 type operator^(type a, type b) { return type(((Death::Implementation::_EnumSizedInteger<type>::Type)a) ^ ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }	\
		friend inline type& operator^=(type& a, type b) { return (type&)(((Death::Implementation::_EnumSizedInteger<type>::Type&)a) ^= ((Death::Implementation::_EnumSizedInteger<type>::Type)b)); }
#else
#	define DEFINE_ENUM_OPERATORS(type)
#	define DEFINE_PRIVATE_ENUM_OPERATORS(type)
#endif

// Compile-time and runtime CPU instruction set dispatch
namespace Death::Cpu {
	class Features;
}

#if defined(DEATH_CPU_USE_RUNTIME_DISPATCH) && !defined(DEATH_CPU_USE_IFUNC)
#	define DEATH_CPU_DISPATCHER_DECLARATION(name) decltype(name) name ## Implementation(Cpu::Features);
#	define DEATH_CPU_DISPATCHER(...) _DEATH_CPU_DISPATCHER(__VA_ARGS__)
#	define DEATH_CPU_DISPATCHED_DECLARATION(name) (*name)
#	define DEATH_CPU_DISPATCHED(dispatcher, ...) DEATH_CPU_DISPATCHED_POINTER(dispatcher, __VA_ARGS__) DEATH_NOOP
#	define DEATH_CPU_MAYBE_UNUSED
#else
#	define DEATH_CPU_DISPATCHER_DECLARATION(name)
#	define DEATH_CPU_DISPATCHED_DECLARATION(name) (name)
#	if defined(DEATH_CPU_USE_RUNTIME_DISPATCH) && defined(DEATH_CPU_USE_IFUNC)
#		define DEATH_CPU_DISPATCHER(...) namespace { _DEATH_CPU_DISPATCHER(__VA_ARGS__) }
#		define DEATH_CPU_DISPATCHED(dispatcher, ...) DEATH_CPU_DISPATCHED_IFUNC(dispatcher, __VA_ARGS__) DEATH_NOOP
#		define DEATH_CPU_MAYBE_UNUSED
#	else
#		define DEATH_CPU_DISPATCHER(...)
#		define DEATH_CPU_DISPATCHED(dispatcher, ...) __VA_ARGS__ DEATH_PASSTHROUGH
#		define DEATH_CPU_MAYBE_UNUSED DEATH_UNUSED
#	endif
#endif