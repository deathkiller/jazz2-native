#pragma once

#include "CommonBase.h"

// Always define fixed width integer types (std::int8_t, std::int16_t, std::int32_t, std::int64_t, ...)
#include <cstddef>
#include <cstdint>

#if !defined(countof)
#	if defined(__cplusplus)
		namespace Death { namespace Implementation { 
			template<typename T, std::size_t N> char(*__ArrayCountOfHelper(T(&)[N]))[N];
		}}
#		define countof(a) (sizeof(*Death::Implementation::__ArrayCountOfHelper(a)))
#	else
#		define countof(a) (sizeof(a) / sizeof(a[0]))
#	endif
#endif

#if defined(__cplusplus)
namespace Death { namespace Implementation {
	// Used as an approximation of std::underlying_type<T>
	template<std::int32_t S> struct __EnumTypeForSize;
	template<> struct __EnumTypeForSize<1> { using Type = std::int8_t; };
	template<> struct __EnumTypeForSize<2> { using Type = std::int16_t; };
	template<> struct __EnumTypeForSize<4> { using Type = std::int32_t; };
	template<> struct __EnumTypeForSize<8> { using Type = std::int64_t; };
	template<class T> struct __EnumSizedInteger { using Type = typename __EnumTypeForSize<sizeof(T)>::Type; };
}}

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

/** @brief Workaround for MSVC not being able to expand __VA_ARGS__ correctly. Would work with /Zc:preprocessor. Source: https://stackoverflow.com/a/5134656 */
#define DEATH_HELPER_EXPAND(...) __VA_ARGS__
/** @brief Pick a macro implementation based on how many arguments were passed. Source: https://stackoverflow.com/a/11763277 */
#define DEATH_HELPER_PICK(_0, _1, _2, _3, _4, _5, _6, _7, macroName, ...) macroName
/** @brief Get number of arguments in a variadic macro */
#define DEATH_HELPER_ARGS_COUNT(...) DEATH_HELPER_EXPAND(DEATH_HELPER_PICK(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0))

#define __DEATH_NOOP__DEATH_REMOVE_PARENS_EXTRACT
#define __DEATH_REMOVE_PARENS_EXTRACT(...) __DEATH_REMOVE_PARENS_EXTRACT __VA_ARGS__
#define __DEATH_REMOVE_PARENS_PASTE(x, ...) x ## __VA_ARGS__
#define __DEATH_REMOVE_PARENS_EVALUATE(x, ...) __DEATH_REMOVE_PARENS_PASTE(x, __VA_ARGS__)
/** @brief Remove optional parentheses from argument */
#define DEATH_REMOVE_PARENS(x) __DEATH_REMOVE_PARENS_EVALUATE(__DEATH_NOOP, __DEATH_REMOVE_PARENS_EXTRACT x)

// Compile-time and runtime CPU instruction set dispatch
namespace Death { namespace Cpu {
	class Features;
}}

#if defined(DEATH_CPU_USE_RUNTIME_DISPATCH) && !defined(DEATH_CPU_USE_IFUNC)
#	define DEATH_CPU_DISPATCHER_DECLARATION(name) decltype(name) name ## Implementation(Cpu::Features);
#	define DEATH_CPU_DISPATCHER(...) __DEATH_CPU_DISPATCHER(__VA_ARGS__)
#	define DEATH_CPU_DISPATCHER_BASE(...) __DEATH_CPU_DISPATCHER_BASE(__VA_ARGS__)
#	define DEATH_CPU_DISPATCHED_DECLARATION(name) (*name)
#	define DEATH_CPU_DISPATCHED(dispatcher, ...) DEATH_CPU_DISPATCHED_POINTER(dispatcher, __VA_ARGS__) DEATH_NOOP
#	define DEATH_CPU_MAYBE_UNUSED
#else
#	define DEATH_CPU_DISPATCHER_DECLARATION(name)
#	define DEATH_CPU_DISPATCHED_DECLARATION(name) (name)
#	if defined(DEATH_CPU_USE_RUNTIME_DISPATCH) && defined(DEATH_CPU_USE_IFUNC)
#		define DEATH_CPU_DISPATCHER(...) namespace { __DEATH_CPU_DISPATCHER(__VA_ARGS__) }
#		define DEATH_CPU_DISPATCHER_BASE(...) namespace { __DEATH_CPU_DISPATCHER_BASE(__VA_ARGS__) }
#		define DEATH_CPU_DISPATCHED(dispatcher, ...) DEATH_CPU_DISPATCHED_IFUNC(dispatcher, __VA_ARGS__) DEATH_NOOP
#		define DEATH_CPU_MAYBE_UNUSED
#	else
#		define DEATH_CPU_DISPATCHER(...)
#		define DEATH_CPU_DISPATCHER_BASE(...)
#		define DEATH_CPU_DISPATCHED(dispatcher, ...) __VA_ARGS__ DEATH_PASSTHROUGH
#		define DEATH_CPU_MAYBE_UNUSED DEATH_UNUSED
#	endif
#endif