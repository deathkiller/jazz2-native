#pragma once

#include "CommonBase.h"

// Always define fixed width integer types (std::int8_t, std::int16_t, std::int32_t, std::int64_t, ...)
#include <cstddef>
#include <cstdint>

#ifdef DOXYGEN_GENERATING_OUTPUT
/**
	@brief Mark an enum as a set of flags

	Defines out-of-class operators (@cpp | @ce, @cpp & @ce, @cpp ~ @ce and @cpp ^ @ce)
	for a given @cpp enum class @ce type.
*/
#define DEATH_ENUM_FLAGS(type)
/**
	@brief Mark a private enum as a set of flags

	Defines out-of-class operators (@cpp | @ce, @cpp & @ce, @cpp ~ @ce and @cpp ^ @ce)
	for a given @cpp enum class @ce type as friends of encapsulating class. This variant
	should be used for @cpp enum class @ce types declared within classes.
*/
#define DEATH_PRIVATE_ENUM_FLAGS(type)
#else
namespace Death { namespace Implementation {
	template<std::size_t size> struct TypeFor {};
	template<> struct TypeFor<1> { typedef std::uint8_t  Type; };
	template<> struct TypeFor<2> { typedef std::uint16_t Type; };
	template<> struct TypeFor<4> { typedef std::uint32_t Type; };
	template<> struct TypeFor<8> { typedef std::uint64_t Type; };
	template<typename T> using EnumSizedInteger = typename TypeFor<sizeof(T)>::Type;
}}

/**
	@brief Mark an enum as a set of flags

	Defines out-of-class operators (@cpp | @ce, @cpp & @ce, @cpp ~ @ce and @cpp ^ @ce)
	for a given @cpp enum class @ce type.
*/
#define DEATH_ENUM_FLAGS(type)	\
	inline constexpr type operator|(type a, type b) noexcept { return type(((::Death::Implementation::EnumSizedInteger<type>)a) | ((::Death::Implementation::EnumSizedInteger<type>)b)); }	\
	inline type& operator|=(type& a, type b) noexcept { return (type&)(((::Death::Implementation::EnumSizedInteger<type>&)a) |= ((::Death::Implementation::EnumSizedInteger<type>)b)); }	\
	inline constexpr type operator&(type a, type b) noexcept { return type(((::Death::Implementation::EnumSizedInteger<type>)a) & ((::Death::Implementation::EnumSizedInteger<type>)b)); }	\
	inline type& operator&=(type& a, type b) noexcept { return (type&)(((::Death::Implementation::EnumSizedInteger<type>&)a) &= ((::Death::Implementation::EnumSizedInteger<type>)b)); }	\
	inline constexpr type operator~(type a) noexcept { return type(~((::Death::Implementation::EnumSizedInteger<type>)a)); }																\
	inline constexpr type operator^(type a, type b) noexcept { return type(((::Death::Implementation::EnumSizedInteger<type>)a) ^ ((::Death::Implementation::EnumSizedInteger<type>)b)); }	\
	inline type& operator^=(type& a, type b) noexcept { return (type&)(((::Death::Implementation::EnumSizedInteger<type>&)a) ^= ((::Death::Implementation::EnumSizedInteger<type>)b)); }

/**
	@brief Mark a private enum as a set of flags

	Defines out-of-class operators (@cpp | @ce, @cpp & @ce, @cpp ~ @ce and @cpp ^ @ce)
	for a given @cpp enum class @ce type as friends of encapsulating class. This variant
	should be used for @cpp enum class @ce types declared within classes.
*/
#define DEATH_PRIVATE_ENUM_FLAGS(type)	\
	friend inline constexpr type operator|(type a, type b) noexcept { return type(((::Death::Implementation::EnumSizedInteger<type>)a) | ((::Death::Implementation::EnumSizedInteger<type>)b)); }	\
	friend inline type& operator|=(type& a, type b) noexcept { return (type&)(((::Death::Implementation::EnumSizedInteger<type>&)a) |= ((::Death::Implementation::EnumSizedInteger<type>)b)); }		\
	friend inline constexpr type operator&(type a, type b) noexcept { return type(((::Death::Implementation::EnumSizedInteger<type>)a) & ((::Death::Implementation::EnumSizedInteger<type>)b)); }	\
	friend inline type& operator&=(type& a, type b) noexcept { return (type&)(((::Death::Implementation::EnumSizedInteger<type>&)a) &= ((::Death::Implementation::EnumSizedInteger<type>)b)); }		\
	friend inline constexpr type operator~(type a) noexcept { return type(~((::Death::Implementation::EnumSizedInteger<type>)a)); }																	\
	friend inline constexpr type operator^(type a, type b) noexcept { return type(((::Death::Implementation::EnumSizedInteger<type>)a) ^ ((::Death::Implementation::EnumSizedInteger<type>)b)); }	\
	friend inline type& operator^=(type& a, type b) noexcept { return (type&)(((::Death::Implementation::EnumSizedInteger<type>&)a) ^= ((::Death::Implementation::EnumSizedInteger<type>)b)); }
#endif

/** @brief Workaround for MSVC not being able to expand `__VA_ARGS__` correctly */
// Source: https://stackoverflow.com/a/5134656, would work with `/Zc:preprocessor`
#define DEATH_HELPER_EXPAND(...) __VA_ARGS__

/** @brief Pick a macro implementation based on how many arguments were passed */
// Source: https://stackoverflow.com/a/11763277
#ifdef DOXYGEN_GENERATING_OUTPUT
#define DEATH_HELPER_PICK(...)
#else
#define DEATH_HELPER_PICK(_0, _1, _2, _3, _4, _5, _6, _7, macroName, ...) macroName
#endif

/** @brief Get number of arguments in a variadic macro */
#define DEATH_HELPER_ARGS_COUNT(...) DEATH_HELPER_EXPAND(DEATH_HELPER_PICK(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0))

#ifndef DOXYGEN_GENERATING_OUTPUT
// Internal macro implementation
#define __DEATH_PASTE(x, y) x ## y
#define __DEATH_NOOP__DEATH_REMOVE_PARENS_EXTRACT
#define __DEATH_REMOVE_PARENS_EXTRACT(...) __DEATH_REMOVE_PARENS_EXTRACT __VA_ARGS__
#define __DEATH_REMOVE_PARENS_PASTE(x, ...) x ## __VA_ARGS__
#define __DEATH_REMOVE_PARENS_EVALUATE(x, ...) __DEATH_REMOVE_PARENS_PASTE(x, __VA_ARGS__)
#endif

/**
	@brief Paste two tokens together

	Concatenates preprocessor tokens to create a new one. However, two tokens
	that don't together form a valid token cannot be pasted together.
*/
#define DEATH_PASTE(x, y) __DEATH_PASTE(x, y)

/**
	@brief Remove optional parentheses from the specified argument

	Allows one or more arguments enclosed in parentheses to be passed to another macro or
	function. Parentheses are automatically removed before passing to the destination.
*/
#define DEATH_REMOVE_PARENS(x) __DEATH_REMOVE_PARENS_EVALUATE(__DEATH_NOOP, __DEATH_REMOVE_PARENS_EXTRACT x)

#ifndef DOXYGEN_GENERATING_OUTPUT
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
#endif