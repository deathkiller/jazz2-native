#pragma once

#include "CommonBase.h"

// Always define fixed width integer types (std::int8_t, std::int16_t, std::int32_t, std::int64_t, ...)
#include <cstddef>
#include <cstdint>

#if !defined(countof)
#	if defined(__cplusplus)
		namespace Death::Implementation { template<typename T, std::size_t N> char(*__ArrayCountOfHelper(T(&)[N]))[N]; }
#		define countof(a) (sizeof(*Death::Implementation::__ArrayCountOfHelper(a)))
#	else
#		define countof(a) (sizeof(a) / sizeof(a[0]))
#	endif
#endif

#if defined(__cplusplus)
namespace Death::Implementation
{
	// Used as an approximation of std::underlying_type<T>
	template<std::int32_t S> struct __EnumTypeForSize;
	template<> struct __EnumTypeForSize<1> { typedef std::int8_t Type; };
	template<> struct __EnumTypeForSize<2> { typedef std::int16_t Type; };
	template<> struct __EnumTypeForSize<4> { typedef std::int32_t Type; };
	template<> struct __EnumTypeForSize<8> { typedef std::int64_t Type; };
	template<class T> struct __EnumSizedInteger { typedef typename __EnumTypeForSize<sizeof(T)>::Type Type; };
}

#	define DEFINE_ENUM_OPERATORS(type)	\
		inline DEATH_CONSTEXPR14 type operator|(type a, type b) { return type(((Death::Implementation::__EnumSizedInteger<type>::Type)a) | ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }	\
		inline type& operator|=(type& a, type b) { return (type&)(((Death::Implementation::__EnumSizedInteger<type>::Type&)a) |= ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }				\
		inline DEATH_CONSTEXPR14 type operator&(type a, type b) { return type(((Death::Implementation::__EnumSizedInteger<type>::Type)a) & ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }	\
		inline type& operator&=(type& a, type b) { return (type&)(((Death::Implementation::__EnumSizedInteger<type>::Type&)a) &= ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }				\
		inline DEATH_CONSTEXPR14 type operator~(type a) { return type(~((Death::Implementation::__EnumSizedInteger<type>::Type)a)); }																		\
		inline DEATH_CONSTEXPR14 type operator^(type a, type b) { return type(((Death::Implementation::__EnumSizedInteger<type>::Type)a) ^ ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }	\
		inline type& operator^=(type& a, type b) { return (type&)(((Death::Implementation::__EnumSizedInteger<type>::Type&)a) ^= ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }

#	define DEFINE_PRIVATE_ENUM_OPERATORS(type)	\
		friend inline DEATH_CONSTEXPR14 type operator|(type a, type b) { return type(((Death::Implementation::__EnumSizedInteger<type>::Type)a) | ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }	\
		friend inline type& operator|=(type& a, type b) { return (type&)(((Death::Implementation::__EnumSizedInteger<type>::Type&)a) |= ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }				\
		friend inline DEATH_CONSTEXPR14 type operator&(type a, type b) { return type(((Death::Implementation::__EnumSizedInteger<type>::Type)a) & ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }	\
		friend inline type& operator&=(type& a, type b) { return (type&)(((Death::Implementation::__EnumSizedInteger<type>::Type&)a) &= ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }				\
		friend inline DEATH_CONSTEXPR14 type operator~(type a) { return type(~((Death::Implementation::__EnumSizedInteger<type>::Type)a)); }																		\
		friend inline DEATH_CONSTEXPR14 type operator^(type a, type b) { return type(((Death::Implementation::__EnumSizedInteger<type>::Type)a) ^ ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }	\
		friend inline type& operator^=(type& a, type b) { return (type&)(((Death::Implementation::__EnumSizedInteger<type>::Type&)a) ^= ((Death::Implementation::__EnumSizedInteger<type>::Type)b)); }
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