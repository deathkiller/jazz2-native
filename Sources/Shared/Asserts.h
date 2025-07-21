#pragma once

#include "Common.h"		// for DEATH_HELPER_EXPAND/DEATH_HELPER_PICK/DEATH_REMOVE_PARENS
#include "Base/Format.h"

#if !defined(DEATH_NO_ASSERT) && (!defined(DEATH_ASSERT) || !defined(DEATH_DEBUG_ASSERT) || !defined(DEATH_CONSTEXPR_ASSERT) || !defined(DEATH_DEBUG_CONSTEXPR_ASSERT) || !defined(DEATH_ASSERT_UNREACHABLE))
#	if defined(DEATH_STANDARD_ASSERT)
#		include <cassert>
#	elif defined(DEATH_TRACE)
#		include <cstdlib>
#	endif
#endif

// Event Tracing
#if defined(DEATH_TRACE)
#	include <cstring>	// for strlen()

// If DEATH_TRACE symbol is #defined with no value, supply internal function name
#	if DEATH_PASTE(DEATH_TRACE, 1) == 1 || DEATH_PASTE(DEATH_TRACE, 1) == 11
#		undef DEATH_TRACE
#		define DEATH_TRACE __WriteTrace
#	endif

/** @brief Trace severity level */
enum class TraceLevel {
	Unknown,		/**< Unspecified */
	Debug,			/**< Debug */
	Deferred,		/**< Deferred (usually not written immediately) */
	Info,			/**< Info */
	Warning,		/**< Warning */
	Error,			/**< Error */
	Assert,			/**< Assert */
	Fatal			/**< Fatal */
};

/**
	@brief Callback function for writing to the event log

	This function needs to be provided by the target application to enable the event tracing.
	Alternatively, @relativeref{Death,ITraceSink} interface can be used instead.
*/
void DEATH_TRACE(TraceLevel level, const char* functionName, const char* message, std::uint32_t messageLength);

#ifndef DOXYGEN_GENERATING_OUTPUT
inline void __DEATH_TRACE(TraceLevel level, const char* functionName, const char* format)
{
	DEATH_TRACE(level, functionName, format, std::uint32_t(std::strlen(format)));
}

template<class ...Args>
inline typename std::enable_if<(sizeof...(Args) > 0), void>::type 
	__DEATH_TRACE(TraceLevel level, const char* functionName, const char* format, const Args&... args)
{
	char formattedMessage[8192];
	std::size_t length = Death::formatInto(formattedMessage, format, args...);
	DEATH_TRACE(level, functionName, formattedMessage, std::uint32_t(length));
}

#	if defined(DEATH_TARGET_GCC) || defined(DEATH_TARGET_CLANG)
#		define __DEATH_CURRENT_FUNCTION __PRETTY_FUNCTION__
#	elif defined(DEATH_TARGET_MSVC)
#		define __DEATH_CURRENT_FUNCTION __FUNCTION__ "()"
#	else
#		define __DEATH_CURRENT_FUNCTION __func__
#	endif
#endif

/** @brief Print a formatted message with @ref TraceLevel::Debug to the event log */
#	if defined(DEATH_DEBUG)
#		define LOGD(fmt, ...) __DEATH_TRACE(TraceLevel::Debug, __DEATH_CURRENT_FUNCTION, fmt, ##__VA_ARGS__)
#	else
#		define LOGD(fmt, ...) do {} while (false)
#	endif
/** @brief Print a deferred formatted message with @ref TraceLevel::Deferred to the event log */
#	define LOGB(fmt, ...) __DEATH_TRACE(TraceLevel::Deferred, __DEATH_CURRENT_FUNCTION, fmt, ##__VA_ARGS__)
/** @brief Print a formatted message with @ref TraceLevel::Info to the event log */
#	define LOGI(fmt, ...) __DEATH_TRACE(TraceLevel::Info, __DEATH_CURRENT_FUNCTION, fmt, ##__VA_ARGS__)
/** @brief Print a formatted message with @ref TraceLevel::Warning to the event log */
#	define LOGW(fmt, ...) __DEATH_TRACE(TraceLevel::Warning, __DEATH_CURRENT_FUNCTION, fmt, ##__VA_ARGS__)
/** @brief Print a formatted message with @ref TraceLevel::Error to the event log */
#	define LOGE(fmt, ...) __DEATH_TRACE(TraceLevel::Error, __DEATH_CURRENT_FUNCTION, fmt, ##__VA_ARGS__)
/** @brief Print a formatted message with @ref TraceLevel::Fatal to the event log */
#	define LOGF(fmt, ...) __DEATH_TRACE(TraceLevel::Fatal, __DEATH_CURRENT_FUNCTION, fmt, ##__VA_ARGS__)
#else
/** @brief Print a formatted message with @ref TraceLevel::Debug to the event log */
#	define LOGD(fmt, ...) do {} while (false)
/** @brief Print a deferred formatted message with @ref TraceLevel::Deferred to the event log */
#	define LOGB(fmt, ...) do {} while (false)
/** @brief Print a formatted message with @ref TraceLevel::Info to the event log */
#	define LOGI(fmt, ...) do {} while (false)
/** @brief Print a formatted message with @ref TraceLevel::Warning to the event log */
#	define LOGW(fmt, ...) do {} while (false)
/** @brief Print a formatted message with @ref TraceLevel::Error to the event log */
#	define LOGE(fmt, ...) do {} while (false)
/** @brief Print a formatted message with @ref TraceLevel::Fatal to the event log */
#	define LOGF(fmt, ...) do {} while (false)
#endif

/** @brief Break the execution if `DEATH_DEBUG` is defined */
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
#			define DEATH_ASSERT_BREAK() ::abort()
#		endif
#	endif
#endif

// Assertions
#if !defined(DEATH_NO_ASSERT) && !defined(DEATH_STANDARD_ASSERT) && defined(DEATH_TRACE) && !defined(DOXYGEN_GENERATING_OUTPUT)
#	define __DEATH_TRACE_ASSERT_(fmt, ...) __DEATH_TRACE(TraceLevel::Assert, __DEATH_CURRENT_FUNCTION, fmt, ##__VA_ARGS__)
#	define __DEATH_TRACE_ASSERT(...) DEATH_HELPER_EXPAND(__DEATH_TRACE_ASSERT_(__VA_ARGS__))
#endif

/**
	@brief Assertion macro

	Usable for sanity checks on user input, as it prints explanational message on error.

	By default, if assertion fails, @p message is printed with @ref TraceLevel::Assert to the event log,
	the function returns with @p returnValue instead and the execution is break (if @cpp DEATH_DEBUG @ce is defined).
	If @cpp DEATH_STANDARD_ASSERT @ce is defined, this macro expands to @cpp assert(condition) @ce, ignoring @p message.
	If @cpp DEATH_NO_ASSERT @ce is defined (or if both @cpp DEATH_TRACE @ce and @cpp DEATH_STANDARD_ASSERT @ce are
	not defined), this macro expands to @cpp do{}while(false) @ce. It also allows to specify only the condition,
	and the message will be generated automatically without @cpp return @ce statement.

	You can override this implementation by placing your own @cpp #define DEATH_ASSERT @ce before including
	the @ref Asserts.h header.
*/
#if !defined(DEATH_ASSERT)
#	if defined(DEATH_NO_ASSERT) || (!defined(DEATH_STANDARD_ASSERT) && !defined(DEATH_TRACE)) || defined(DOXYGEN_GENERATING_OUTPUT)
#		define DEATH_ASSERT(condition, ...) do {} while (false)
#	elif defined(DEATH_STANDARD_ASSERT)
#		define DEATH_ASSERT(condition, ...) assert(condition)
#	else
#		define __DEATH_ASSERT1(condition)							\
			do {														\
				if DEATH_UNLIKELY(!(condition)) {						\
					__DEATH_TRACE_ASSERT("Assertion ({}) failed at \"{}:{}\"", #condition, __FILE__, __LINE__);	\
					DEATH_ASSERT_BREAK();								\
				}														\
			} while(false)
#		define __DEATH_ASSERT3(condition, message, returnValue)	\
			do {														\
				if DEATH_UNLIKELY(!(condition)) {						\
					__DEATH_TRACE_ASSERT(DEATH_REMOVE_PARENS(message));	\
					DEATH_ASSERT_BREAK();								\
					return returnValue;									\
				}														\
			} while(false)
#		define DEATH_ASSERT(...) DEATH_HELPER_EXPAND(DEATH_HELPER_PICK(__VA_ARGS__, __DEATH_ASSERT3, __DEATH_ASSERT3, __DEATH_ASSERT3, __DEATH_ASSERT3, __DEATH_ASSERT3, __DEATH_ASSERT3, __DEATH_ASSERT1, __DEATH_ASSERT1, )(__VA_ARGS__))
#	endif
#endif

/**
	@brief Debug-only assertion macro

	Unlike @ref DEATH_ASSERT() this macro expands to @cpp do{}while(false) @ce if @cpp DEATH_DEBUG @ce
	is not defined (i.e., in release configuration). It also allows to specify only the condition,
	and the message will be generated automatically without @cpp return @ce statement.

	You can override this implementation by placing your own @cpp #define DEATH_DEBUG_ASSERT @ce before including
	the @ref Asserts.h header.
*/
#if !defined(DEATH_DEBUG_ASSERT)
#	if defined(DEATH_NO_ASSERT) || !defined(DEATH_DEBUG) || (!defined(DEATH_STANDARD_ASSERT) && !defined(DEATH_TRACE)) || defined(DOXYGEN_GENERATING_OUTPUT)
#		define DEATH_DEBUG_ASSERT(condition, ...) do {} while (false)
#	elif defined(DEATH_STANDARD_ASSERT)
#		define DEATH_DEBUG_ASSERT(condition, ...) assert(condition)
#	else
#		define DEATH_DEBUG_ASSERT(...) DEATH_ASSERT(__VA_ARGS__)
#	endif
#endif

/**
	@brief Constexpr assertion macro

	Unlike @ref DEATH_ASSERT() this macro can be used in C++11 @cpp constexpr @ce functions. In a @cpp constexpr @ce
	context, if assertion fails, the code fails to compile. In a non-@cpp constexpr @ce context, if assertion fails,
	@p message is printed with @ref TraceLevel::Assert to the event log and the execution is break.
	If @cpp DEATH_STANDARD_ASSERT @ce is defined, @p message is ignored and the standard @cpp assert() @ce is called
	if condition fails. If @cpp DEATH_NO_ASSERT @ce is defined (or if both @cpp DEATH_TRACE @ce and
	@cpp DEATH_STANDARD_ASSERT @ce are not defined), this macro expands to @cpp static_cast<void>(0) @ce.

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
				__DEATH_TRACE_ASSERT(DEATH_REMOVE_PARENS(message));		\
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
	is defined, the standard @cpp assert(!"Unreachable code") @ce is called. If @cpp DEATH_NO_ASSERT @ce is defined
	(or if both @cpp DEATH_TRACE @ce and @cpp DEATH_STANDARD_ASSERT @ce are not defined), this macro hints to
	the compiler that given code is not reachable, possibly helping the optimizer (using a compiler builtin on GCC,
	Clang and MSVC; calling @ref std::abort() otherwise).

	You can override this implementation by placing your own @cpp #define DEATH_ASSERT_UNREACHABLE @ce before
	including the @ref Asserts.h header.
*/
#if !defined(DEATH_ASSERT_UNREACHABLE)
#	if defined(DEATH_NO_ASSERT) || (!defined(DEATH_STANDARD_ASSERT) && !defined(DEATH_TRACE)) || defined(DOXYGEN_GENERATING_OUTPUT)
#		if defined(DEATH_TARGET_GCC)
#			define DEATH_ASSERT_UNREACHABLE(...) __builtin_unreachable()
#		elif defined(DEATH_TARGET_MSVC)
#			define DEATH_ASSERT_UNREACHABLE(...) __assume(0)
#		else
#			define DEATH_ASSERT_UNREACHABLE(...) std::abort()
#		endif
#	elif defined(DEATH_STANDARD_ASSERT)
#		define DEATH_ASSERT_UNREACHABLE(...) assert(!"Unreachable code")
#	else
#		define __DEATH_ASSERT_UNREACHABLE0()							\
			do {														\
				__DEATH_TRACE_ASSERT("Reached unreachable code at \"{}:{}\"", __FILE__, __LINE__);	\
				DEATH_ASSERT_BREAK();									\
			} while(false)
#		define __DEATH_ASSERT_UNREACHABLE2(message, returnValue)		\
			do {														\
				__DEATH_TRACE_ASSERT(DEATH_REMOVE_PARENS(message));		\
				DEATH_ASSERT_BREAK();									\
				return returnValue;										\
			} while(false)
#		define DEATH_ASSERT_UNREACHABLE(...) DEATH_HELPER_EXPAND(DEATH_HELPER_PICK(__VA_ARGS__, __DEATH_ASSERT_UNREACHABLE2, __DEATH_ASSERT_UNREACHABLE2, __DEATH_ASSERT_UNREACHABLE2, __DEATH_ASSERT_UNREACHABLE2, __DEATH_ASSERT_UNREACHABLE2, __DEATH_ASSERT_UNREACHABLE2, __DEATH_ASSERT_UNREACHABLE2, __DEATH_ASSERT_UNREACHABLE0, __DEATH_ASSERT_UNREACHABLE0)(__VA_ARGS__))
#	endif
#endif
