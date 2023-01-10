#pragma once

#include "CommonInternal.h"

// Undefine min()/max() macros, use std::min()/std::max() instead
#if !defined(NOMINMAX)
#	define NOMINMAX
#endif

#include <cstdint>

#if defined(_WIN32)
#	include <windows.h>
#endif

#if !defined(_countof)
#	define _countof(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

// Undefine "far" and "near" keywords, not used anymore
#if defined(far)
#	undef far
#	if defined(FAR)
#		undef FAR
#		define FAR
#	endif
#endif
#if defined(near)
#	undef near
#	if defined(NEAR)
#		undef NEAR
#		define NEAR
#	endif
#endif

#if defined(__cplusplus)

namespace Death
{
	// Used as an approximation of std::underlying_type<T>
	template<int S>
	struct _enum_type_for_size;

	template<>
	struct _enum_type_for_size<1> {
		typedef int8_t Type;
	};

	template<>
	struct _enum_type_for_size<2> {
		typedef int16_t Type;
	};

	template<>
	struct _enum_type_for_size<4> {
		typedef int32_t Type;
	};

	template<>
	struct _enum_type_for_size<8> {
		typedef int64_t Type;
	};

	template<class T>
	struct EnumSizedInteger {
		typedef typename _enum_type_for_size<sizeof(T)>::Type Type;
	};
}

#if !defined(_ENUM_FLAG_CONSTEXPR)
#	define _ENUM_FLAG_CONSTEXPR constexpr
#endif

#define DEFINE_ENUM_OPERATORS(ENUMTYPE) \
inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator | (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((Death::EnumSizedInteger<ENUMTYPE>::Type)a) | ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
inline ENUMTYPE &operator |= (ENUMTYPE &a, ENUMTYPE b) throw() { return (ENUMTYPE &)(((Death::EnumSizedInteger<ENUMTYPE>::Type &)a) |= ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator & (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((Death::EnumSizedInteger<ENUMTYPE>::Type)a) & ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
inline ENUMTYPE &operator &= (ENUMTYPE &a, ENUMTYPE b) throw() { return (ENUMTYPE &)(((Death::EnumSizedInteger<ENUMTYPE>::Type &)a) &= ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator ~ (ENUMTYPE a) throw() { return ENUMTYPE(~((Death::EnumSizedInteger<ENUMTYPE>::Type)a)); } \
inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator ^ (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((Death::EnumSizedInteger<ENUMTYPE>::Type)a) ^ ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
inline ENUMTYPE &operator ^= (ENUMTYPE &a, ENUMTYPE b) throw() { return (ENUMTYPE &)(((Death::EnumSizedInteger<ENUMTYPE>::Type &)a) ^= ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); }

// Special version of DEFINE_ENUM_OPERATORS that can be used in classes for private enums
#define DEFINE_PRIVATE_ENUM_OPERATORS(ENUMTYPE) \
friend inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator | (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((Death::EnumSizedInteger<ENUMTYPE>::Type)a) | ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
friend inline ENUMTYPE &operator |= (ENUMTYPE &a, ENUMTYPE b) throw() { return (ENUMTYPE &)(((Death::EnumSizedInteger<ENUMTYPE>::Type &)a) |= ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
friend inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator & (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((Death::EnumSizedInteger<ENUMTYPE>::Type)a) & ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
friend inline ENUMTYPE &operator &= (ENUMTYPE &a, ENUMTYPE b) throw() { return (ENUMTYPE &)(((Death::EnumSizedInteger<ENUMTYPE>::Type &)a) &= ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
friend inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator ~ (ENUMTYPE a) throw() { return ENUMTYPE(~((Death::EnumSizedInteger<ENUMTYPE>::Type)a)); } \
friend inline _ENUM_FLAG_CONSTEXPR ENUMTYPE operator ^ (ENUMTYPE a, ENUMTYPE b) throw() { return ENUMTYPE(((Death::EnumSizedInteger<ENUMTYPE>::Type)a) ^ ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); } \
friend inline ENUMTYPE &operator ^= (ENUMTYPE &a, ENUMTYPE b) throw() { return (ENUMTYPE &)(((Death::EnumSizedInteger<ENUMTYPE>::Type &)a) ^= ((Death::EnumSizedInteger<ENUMTYPE>::Type)b)); }

#else
#	define DEFINE_ENUM_OPERATORS(ENUMTYPE) // C allows these operators
#	define DEFINE_PRIVATE_ENUM_OPERATORS(ENUMTYPE)
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