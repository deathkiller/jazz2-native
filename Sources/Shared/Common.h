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

//namespace Death { namespace Implementation {
//	template<typename T/*, typename = std::enable_if_t<std::is_invocable_v<T>>*/>
//	class ScopeExit
//	{
//	public:
//		constexpr explicit ScopeExit(T deferredCallback) noexcept : _deferredCallback(std::move(deferredCallback)) {}
//		ScopeExit& operator=(const ScopeExit&) = delete;
//		~ScopeExit() noexcept { _deferredCallback(); }
//
//	private:
//		T _deferredCallback;
//	};
//
//	struct ScopeExitHelper {};
//
//	template<typename T>
//	ScopeExit<T> operator%(const ScopeExitHelper&, T deferredCallback) {
//		return ScopeExit<T>{std::move(deferredCallback)};
//	}
//}}
//
///** @brief Defer execution of following block to the end of current scope */
//#define DEATH_DEFER DEATH_UNUSED auto const& DEATH_PASTE(_deferred_, __LINE__) = Death::Implementation::ScopeExitHelper{} % [&]()

// Compile-time and runtime CPU instruction set dispatch
namespace Death { namespace Cpu {
	class Features;
}}

#if defined(DEATH_CPU_USE_RUNTIME_DISPATCH) && !defined(DEATH_CPU_USE_IFUNC)
#	define DEATH_CPU_DISPATCHER_DECLARATION(name) decltype(name) name ## Implementation(Cpu::Features);
#	define DEATH_CPU_DISPATCHER(...) _DEATH_CPU_DISPATCHER(__VA_ARGS__)
#	define DEATH_CPU_DISPATCHER_BASE(...) _DEATH_CPU_DISPATCHER_BASE(__VA_ARGS__)
#	define DEATH_CPU_DISPATCHED_DECLARATION(name) (*name)
#	define DEATH_CPU_DISPATCHED(dispatcher, ...) DEATH_CPU_DISPATCHED_POINTER(dispatcher, __VA_ARGS__) DEATH_NOOP
#	define DEATH_CPU_MAYBE_UNUSED
#else
#	define DEATH_CPU_DISPATCHER_DECLARATION(name)
#	define DEATH_CPU_DISPATCHED_DECLARATION(name) (name)
#	if defined(DEATH_CPU_USE_RUNTIME_DISPATCH) && defined(DEATH_CPU_USE_IFUNC)
#		define DEATH_CPU_DISPATCHER(...) namespace { _DEATH_CPU_DISPATCHER(__VA_ARGS__) }
#		define DEATH_CPU_DISPATCHER_BASE(...) namespace { _DEATH_CPU_DISPATCHER_BASE(__VA_ARGS__) }
#		define DEATH_CPU_DISPATCHED(dispatcher, ...) DEATH_CPU_DISPATCHED_IFUNC(dispatcher, __VA_ARGS__) DEATH_NOOP
#		define DEATH_CPU_MAYBE_UNUSED
#	else
#		define DEATH_CPU_DISPATCHER(...)
#		define DEATH_CPU_DISPATCHER_BASE(...)
#		define DEATH_CPU_DISPATCHED(dispatcher, ...) __VA_ARGS__ DEATH_PASSTHROUGH
#		define DEATH_CPU_MAYBE_UNUSED DEATH_UNUSED
#	endif
#endif