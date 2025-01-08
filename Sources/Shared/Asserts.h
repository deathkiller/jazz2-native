#pragma once

#include "Common.h"		// for DEATH_HELPER_EXPAND/DEATH_HELPER_PICK/DEATH_REMOVE_PARENS

#if !defined(DEATH_NO_ASSERT) && (!defined(DEATH_ASSERT) || !defined(DEATH_DEBUG_ASSERT) || !defined(DEATH_CONSTEXPR_ASSERT) || !defined(DEATH_DEBUG_CONSTEXPR_ASSERT) || !defined(DEATH_ASSERT_UNREACHABLE))
#	if defined(DEATH_STANDARD_ASSERT)
#		include <cassert>
#	elif defined(DEATH_TRACE)
#		include <cstdlib>
#	endif
#endif

// Event Tracing
#if defined(DEATH_TRACE)

// If DEATH_TRACE symbol is #defined with no value, supply internal function name
#	if DEATH_PASTE(DEATH_TRACE, 1) == 1 || DEATH_PASTE(DEATH_TRACE, 1) == 11
#		undef DEATH_TRACE
#		define DEATH_TRACE __WriteTrace
#	endif

/** @brief Trace level */
enum class TraceLevel {
	Unknown,	/**< Unspecified */
	Debug,		/**< Debug */
	Info,		/**< Info */
	Warning,	/**< Warning */
	Error,		/**< Error */
	Assert,		/**< Assert */
	Fatal		/**< Fatal */
};

/**
	@brief Callback function for writing to the event log

	This function needs to be provided by the target application to enable the event tracing.
	Alternatively, @relativeref{Death,ITraceSink} can be used instead.
*/
void DEATH_TRACE(TraceLevel level, const char* fmt, ...);

#ifndef DOXYGEN_GENERATING_OUTPUT
#	if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
#		define __DEATH_CURRENT_FUNCTION __PRETTY_FUNCTION__
#	elif defined(DEATH_TARGET_MSVC)
#		define __DEATH_CURRENT_FUNCTION __FUNCTION__ "()"
#	else
#		define __DEATH_CURRENT_FUNCTION __func__
#	endif
#endif

#	if defined(DEATH_DEBUG)
#		define LOGD(fmt, ...) DEATH_TRACE(TraceLevel::Debug, "%s ‡ " fmt, __DEATH_CURRENT_FUNCTION, ##__VA_ARGS__)
#	else
#		define LOGD(fmt, ...) do {} while (false)
#	endif
#	define LOGI(fmt, ...) DEATH_TRACE(TraceLevel::Info, "%s ‡ " fmt, __DEATH_CURRENT_FUNCTION, ##__VA_ARGS__)
#	define LOGW(fmt, ...) DEATH_TRACE(TraceLevel::Warning, "%s ‡ " fmt, __DEATH_CURRENT_FUNCTION, ##__VA_ARGS__)
#	define LOGE(fmt, ...) DEATH_TRACE(TraceLevel::Error, "%s ‡ " fmt, __DEATH_CURRENT_FUNCTION, ##__VA_ARGS__)
#	define LOGF(fmt, ...) DEATH_TRACE(TraceLevel::Fatal, "%s ‡ " fmt, __DEATH_CURRENT_FUNCTION, ##__VA_ARGS__)
#else
#	define LOGD(fmt, ...) do {} while (false)
#	define LOGI(fmt, ...) do {} while (false)
#	define LOGW(fmt, ...) do {} while (false)
#	define LOGE(fmt, ...) do {} while (false)
#	define LOGF(fmt, ...) do {} while (false)
#endif

/** @brief Breaks the execution if `DEATH_DEBUG` is defined */
#if !defined(DEATH_ASSERT_BREAK)
#	if !defined(DEATH_DEBUG)
#		define DEATH_ASSERT_BREAK() do {} while (false)
#	elif defined(DEATH_TARGET_WINDOWS)
#		define DEATH_ASSERT_BREAK() __debugbreak()
#	else
#		if defined(__has_builtin)
#			if __has_builtin(__builtin_debugtrap)
#				define DEATH_ASSERT_BREAK() __builtin_debugtrap()
#			elif __has_builtin(__builtin_trap)
#				define DEATH_ASSERT_BREAK() __builtin_trap()
#			endif
#		endif
#		if !defined(DEATH_ASSERT_BREAK)
#			if defined(DEATH_TARGET_GCC) && (defined(__i386__) || defined(__x86_64__))
#				define DEATH_ASSERT_BREAK() __asm__ volatile("int3;nop")
#			elif defined(DEATH_TARGET_GCC) && defined(__thumb__)
#				define DEATH_ASSERT_BREAK() __asm__ volatile(".inst 0xde01")
#			elif defined(DEATH_TARGET_GCC) && defined(__arm__) && !defined(__thumb__)
#				define DEATH_ASSERT_BREAK() __asm__ volatile(".inst 0xe7f001f0")
#			else
#				define DEATH_ASSERT_BREAK() ::abort()
#			endif
#		endif
#	endif
#endif

// Assertions
#if !defined(DEATH_NO_ASSERT) && !defined(DEATH_STANDARD_ASSERT) && defined(DEATH_TRACE) && !defined(DOXYGEN_GENERATING_OUTPUT)
#	define __DEATH_ASSERT_BASE(fmt, ...) DEATH_TRACE(TraceLevel::Assert, "%s ‡ " fmt, __DEATH_CURRENT_FUNCTION, ##__VA_ARGS__)
#	define __DEATH_ASSERT_TRACE(...) DEATH_HELPER_EXPAND(__DEATH_ASSERT_BASE(__VA_ARGS__))
#endif

/**
	@brief Assertion macro

	Usable for sanity checks on user input, as it prints explanational message on error.

	By default, if assertion fails, @p message is printed with @ref TraceLevel::Assert to the event log,
	the function returns with @p returnValue instead and the execution is break (if @cpp DEATH_DEBUG @ce is defined).
	If @cpp DEATH_STANDARD_ASSERT @ce is defined, this macro expands to @cpp assert(condition) @ce, ignoring @p message.
	If @cpp DEATH_NO_ASSERT @ce is defined (or if both @cpp DEATH_TRACE @ce and @cpp DEATH_STANDARD_ASSERT @ce are
	not defined), this macro expands to @cpp do {} while (false) @ce.

	You can override this implementation by placing your own @cpp #define DEATH_ASSERT @ce before including
	the @ref Asserts.h header.
*/
#if !defined(DEATH_ASSERT)
#	if defined(DEATH_NO_ASSERT) || (!defined(DEATH_STANDARD_ASSERT) && !defined(DEATH_TRACE))
#		define DEATH_ASSERT(condition, message, returnValue) do {} while (false)
#	elif defined(DEATH_STANDARD_ASSERT)
#		define DEATH_ASSERT(condition, message, returnValue) assert(condition)
#	else
#		define DEATH_ASSERT(condition, message, returnValue)			\
			do {														\
				if DEATH_UNLIKELY(!(condition)) {						\
					__DEATH_ASSERT_TRACE(DEATH_REMOVE_PARENS(message));	\
					DEATH_ASSERT_BREAK();								\
					return returnValue;									\
				}														\
			} while(false)
#	endif
#endif

/**
	@brief Debug-only assertion macro

	Unlike @ref DEATH_ASSERT() this macro expands to @cpp do {} while (false) @ce if @cpp DEATH_DEBUG @ce
	is not defined (i.e., in release configuration).

	You can override this implementation by placing your own @cpp #define DEATH_DEBUG_ASSERT @ce before including
	the @ref Asserts.h header.
*/
#if !defined(DEATH_DEBUG_ASSERT)
#	if defined(DEATH_NO_ASSERT) || !defined(DEATH_DEBUG) || (!defined(DEATH_STANDARD_ASSERT) && !defined(DEATH_TRACE))
#		define DEATH_DEBUG_ASSERT(condition, ...) do {} while (false)
#	elif defined(DEATH_STANDARD_ASSERT)
#		define DEATH_DEBUG_ASSERT(condition, ...) assert(condition)
#	else
#		define __DEATH_DEBUG_ASSERT1(condition)							\
			do {														\
				if DEATH_UNLIKELY(!(condition)) {						\
					__DEATH_ASSERT_TRACE("Assertion (%s) failed at \"%s:%i\"", #condition, __FILE__, __LINE__);	\
					DEATH_ASSERT_BREAK();								\
				}														\
			} while(false)
#		define __DEATH_DEBUG_ASSERT3(condition, message, returnValue)	\
			do {														\
				if DEATH_UNLIKELY(!(condition)) {						\
					__DEATH_ASSERT_TRACE(DEATH_REMOVE_PARENS(message));	\
					DEATH_ASSERT_BREAK();								\
					return returnValue;									\
				}														\
			} while(false)
#		define DEATH_DEBUG_ASSERT(...) DEATH_HELPER_EXPAND(DEATH_HELPER_PICK(__VA_ARGS__, __DEATH_DEBUG_ASSERT3, __DEATH_DEBUG_ASSERT3, __DEATH_DEBUG_ASSERT3, __DEATH_DEBUG_ASSERT3, __DEATH_DEBUG_ASSERT3, __DEATH_DEBUG_ASSERT3, __DEATH_DEBUG_ASSERT1, __DEATH_DEBUG_ASSERT1, )(__VA_ARGS__))
#	endif
#endif

/**
	@brief Constexpr assertion macro

	Unlike @ref DEATH_ASSERT() this macro can be used in C++11 @cpp constexpr @ce functions. In a @cpp constexpr @ce
	context, if assertion fails, the code fails to compile. In a non-@cpp constexpr @ce context, if assertion fails,
	@p message is printed with @ref TraceLevel::Assert to the event log and the execution is break. If @cpp DEATH_STANDARD_ASSERT @ce
	is defined, @p message is ignored and the standard @cpp assert() @ce is called if condition fails. If
	@cpp DEATH_NO_ASSERT @ce is defined (or if both @cpp DEATH_TRACE @ce and @cpp DEATH_STANDARD_ASSERT @ce are
	not defined), this macro expands to @cpp static_cast<void>(0) @ce.

	You can override this implementation by placing your own @cpp #define DEATH_CONSTEXPR_ASSERT @ce before including
	the @ref Asserts.h header.
*/
#if !defined(DEATH_CONSTEXPR_ASSERT)
#	if defined(DEATH_NO_ASSERT) || (!defined(DEATH_STANDARD_ASSERT) && !defined(DEATH_TRACE))
#		define DEATH_CONSTEXPR_ASSERT(condition, message) static_cast<void>(0)
#	elif defined(DEATH_STANDARD_ASSERT)
#		define DEATH_CONSTEXPR_ASSERT(condition, message)				\
			static_cast<void>((condition) ? 0 : ([&]() {				\
				assert(!#condition);									\
			}(), 0))
#	else
#		define DEATH_CONSTEXPR_ASSERT(condition, message)				\
			static_cast<void>((condition) ? 0 : ([&]() {				\
				__DEATH_ASSERT_TRACE(DEATH_REMOVE_PARENS(message));		\
				DEATH_ASSERT_BREAK();									\
			}(), 0))
#	endif
#endif

/**
	@brief Debug-only constexpr assertion macro

	Unlike @ref DEATH_CONSTEXPR_ASSERT() this macro expands to @cpp static_cast<void>(0) @ce if @cpp DEATH_DEBUG @ce
	is not defined (i.e., in release configuration).

	You can override this implementation by placing your own @cpp #define DEATH_DEBUG_CONSTEXPR_ASSERT @ce before
	including the @ref Asserts.h header.
*/
#if !defined(DEATH_DEBUG_CONSTEXPR_ASSERT)
#	if defined(DEATH_DEBUG)
#		define DEATH_DEBUG_CONSTEXPR_ASSERT(condition, message) DEATH_CONSTEXPR_ASSERT(condition, message)
#	else
#		define DEATH_DEBUG_CONSTEXPR_ASSERT(condition, message) static_cast<void>(0)
#	endif
#endif

/**
	@brief Assert that the code is unreachable

	By default, if code marked with this macro is reached, a hint message is printed with @ref TraceLevel::Assert
	to the event log. and the execution is break (if @cpp DEATH_DEBUG @ce is defined). If @cpp DEATH_STANDARD_ASSERT @ce
	is defined, the standard @cpp assert(!"Unreachable code") @ce is called. If @cpp DEATH_NO_ASSERT @ce is defined (or
	if both @cpp DEATH_TRACE @ce and @cpp DEATH_STANDARD_ASSERT @ce are not defined), this macro hints to the compiler
	that given code is not reachable, possibly helping the optimizer (using a compiler builtin on GCC, Clang and
	MSVC; calling @ref std::abort() otherwise).

	You can override this implementation by placing your own @cpp #define DEATH_ASSERT_UNREACHABLE @ce before
	including the @ref Asserts.h header.
*/
#if !defined(DEATH_ASSERT_UNREACHABLE)
#	if defined(DEATH_NO_ASSERT) || (!defined(DEATH_STANDARD_ASSERT) && !defined(DEATH_TRACE))
#		if defined(DEATH_TARGET_GCC)
#			define DEATH_ASSERT_UNREACHABLE() __builtin_unreachable()
#		elif defined(DEATH_TARGET_MSVC)
#			define DEATH_ASSERT_UNREACHABLE() __assume(0)
#		else
#			define DEATH_ASSERT_UNREACHABLE() std::abort()
#		endif
#	elif defined(DEATH_STANDARD_ASSERT)
#		define DEATH_ASSERT_UNREACHABLE() assert(!"Unreachable code")
#	else
#		define DEATH_ASSERT_UNREACHABLE()								\
			do {														\
				__DEATH_ASSERT_TRACE("Reached unreachable code at \"%s:%i\"", __FILE__, __LINE__);	\
				DEATH_ASSERT_BREAK();									\
			} while (false)
#	endif
#endif
