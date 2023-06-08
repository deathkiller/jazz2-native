#pragma once

#include "CommonBase.h"

#if !defined(DEATH_NO_ASSERT) && (!defined(DEATH_ASSERT) || !defined(DEATH_CONSTEXPR_ASSERT) || !defined(DEATH_ASSERT_UNREACHABLE))
#	if !defined(DEATH_DEBUG) && defined(DEATH_LOGGING)
#		include <cstdlib>
#	else
#		include <cassert>
#	endif
#endif

// Logging
#if defined(DEATH_LOGGING)

// If symbol is #defined with no value, supply internal function name
#if DEATH_PASTE(DEATH_LOGGING, 1) == 1 || DEATH_PASTE(DEATH_LOGGING, 1) == 11
#	undef DEATH_LOGGING
#	define DEATH_LOGGING __WriteLog
#endif

enum class LogLevel {
	Unknown,
	Debug,
	Info,
	Warning,
	Error,
	Fatal
};

void DEATH_LOGGING(LogLevel level, const char* fmt, ...);

#	if defined(DEATH_TARGET_GCC)
#		define __DEATH_LOG_FUNCTION __PRETTY_FUNCTION__
#	elif defined(DEATH_TARGET_MSVC)
#		define __DEATH_LOG_FUNCTION __FUNCTION__
#	else
#		define __DEATH_LOG_FUNCTION __func__
#	endif

#	if defined(DEATH_DEBUG)
#		define LOGD(fmt, ...) DEATH_LOGGING(LogLevel::Debug, "%s -> " fmt, __DEATH_LOG_FUNCTION, ##__VA_ARGS__)
#	else
#		define LOGD(fmt, ...) do {} while (false)
#	endif
#	define LOGI(fmt, ...) DEATH_LOGGING(LogLevel::Info, "%s -> " fmt, __DEATH_LOG_FUNCTION, ##__VA_ARGS__)
#	define LOGW(fmt, ...) DEATH_LOGGING(LogLevel::Warning, "%s -> " fmt, __DEATH_LOG_FUNCTION, ##__VA_ARGS__)
#	define LOGE(fmt, ...) DEATH_LOGGING(LogLevel::Error, "%s -> " fmt, __DEATH_LOG_FUNCTION, ##__VA_ARGS__)
#	define LOGF(fmt, ...) DEATH_LOGGING(LogLevel::Fatal, "%s -> " fmt, __DEATH_LOG_FUNCTION, ##__VA_ARGS__)
#else
#	define LOGD(fmt, ...) do {} while (false)
#	define LOGI(fmt, ...) do {} while (false)
#	define LOGW(fmt, ...) do {} while (false)
#	define LOGE(fmt, ...) do {} while (false)
#	define LOGF(fmt, ...) do {} while (false)
#endif

// Assertions
/** @brief Assertion macro */
#if !defined(DEATH_ASSERT)
#	if defined(DEATH_NO_ASSERT) || (!defined(DEATH_DEBUG) && !defined(DEATH_LOGGING))
#		define DEATH_ASSERT(condition, returnValue, message, ...) do {} while (false)
#	elif !defined(DEATH_DEBUG) && defined(DEATH_LOGGING)
#		define DEATH_ASSERT(condition, returnValue, message, ...)	\
			do {													\
				if(!(condition)) {									\
					LOGF(message, ##__VA_ARGS__);					\
					std::abort();									\
					return returnValue;								\
				}													\
			} while(false)
#	else
#		define DEATH_ASSERT(condition, returnValue, message, ...) assert(condition)
#	endif
#endif

/** @brief Constexpr assertion macro */
#if !defined(DEATH_CONSTEXPR_ASSERT)
#	if defined(DEATH_NO_ASSERT) || (!defined(DEATH_DEBUG) && !defined(DEATH_LOGGING))
#		define DEATH_CONSTEXPR_ASSERT(condition, message, ...) static_cast<void>(0)
#	elif !defined(DEATH_DEBUG) && defined(DEATH_LOGGING)
#		define DEATH_CONSTEXPR_ASSERT(condition, message, ...)		\
			static_cast<void>((condition) ? 0 : ([&]() {			\
				LOGF(message, ##__VA_ARGS__);						\
				std::abort();										\
			}(), 0))
#	else
#		define DEATH_CONSTEXPR_ASSERT(condition, message, ...)		\
			static_cast<void>((condition) ? 0 : ([&]() {			\
				assert(!#condition);								\
			}(), 0))
#	endif
#endif

/** @brief Assert that the code is unreachable */
#if !defined(DEATH_ASSERT_UNREACHABLE)
#	if defined(DEATH_NO_ASSERT) || (!defined(DEATH_DEBUG) && !defined(DEATH_LOGGING))
#		if defined(DEATH_TARGET_GCC)
#			define DEATH_ASSERT_UNREACHABLE() __builtin_unreachable()
#		elif defined(DEATH_TARGET_MSVC)
#			define DEATH_ASSERT_UNREACHABLE() __assume(0)
#		else
#			define DEATH_ASSERT_UNREACHABLE() std::abort()
#		endif
#	elif !defined(DEATH_DEBUG) && defined(DEATH_LOGGING)
#		define DEATH_ASSERT_UNREACHABLE()							\
			do {													\
				LOGF("Reached unreachable code at " __FILE__ ":" DEATH_LINE_STRING);	\
				std::abort();										\
			} while (false)
#	else
#		define DEATH_ASSERT_UNREACHABLE() assert(!"Unreachable code")
#	endif
#endif
