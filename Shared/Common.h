#pragma once

// Undefine min()/max() macros, use std::min()/std::max() instead
#if !defined(NOMINMAX)
#	define NOMINMAX
#endif

#include <cstdint>

#if defined(_WIN32)
#	include <windows.h>
#endif

// These attributes are not defined outside of MSVC
#if !defined(__in)
#	define __in
#endif
#if !defined(__in_opt)
#	define __in_opt
#endif
#if !defined(__out)
#	define __out
#endif
#if !defined(__out_opt)
#	define __out_opt
#endif
#if !defined(__success)
#	define __success(expr)
#endif

#if !defined(_countof)
#	define _countof(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

// Undefine "far" and "near" keywords, not used anymore
#ifdef far
#	undef far
#	ifdef FAR
#		undef FAR
#		define FAR
#	endif
#endif
#ifdef near
#	undef near
#	ifdef NEAR
#		undef NEAR
#		define NEAR
#	endif
#endif

#define ASSERT_UNREACHABLE() __assume(0)

#ifdef __cplusplus

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

#ifndef _ENUM_FLAG_CONSTEXPR
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
#	define DEFINE_ENUM_OPERATORS(ENUMTYPE) // NOP, C allows these operators.
#	define DEFINE_PRIVATE_ENUM_OPERATORS(ENUMTYPE)
#endif
