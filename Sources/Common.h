#pragma once

// Set default name and version if not provided by CMake
#if !defined(NCINE_APP)
#	define NCINE_APP "jazz2"
#endif
#if !defined(NCINE_APP_NAME)
#	define NCINE_APP_NAME "Jazz² Resurrection"
#endif
#if !defined(NCINE_VERSION)
#	define NCINE_VERSION "1.9.0"
#endif

// Prefer local version of shared libraries in CMake build
#if defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("../Shared/Common.h")
#		define __HAS_LOCAL_COMMON
#	endif
#endif
#ifdef __HAS_LOCAL_COMMON
#	include "../Shared/Common.h"
#else
#	include <Common.h>
#endif

#include <stdlib.h>

// Logging
#if defined(NCINE_LOG)

enum class LogLevel {
	Unknown,
	Debug,
	Info,
	Warning,
	Error,
	Fatal
};

void __WriteLog(LogLevel level, const char* fmt, ...);

#	if defined(DEATH_TARGET_GCC)
#		define __LOG_FUNCTION __PRETTY_FUNCTION__
#	elif defined(DEATH_TARGET_MSVC)
#		define __LOG_FUNCTION __FUNCTION__
#	else
#		define __LOG_FUNCTION __func__
#	endif
#
#	if defined(NCINE_DEBUG)
#		define LOGD(fmt) __WriteLog(LogLevel::Debug, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION)
#	else
#		define LOGD(fmt) do { } while (false)
#	endif
#	define LOGI(fmt) __WriteLog(LogLevel::Info, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION)
#	define LOGW(fmt) __WriteLog(LogLevel::Warning, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION)
#	define LOGE(fmt) __WriteLog(LogLevel::Error, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION)
#	define LOGF(fmt) __WriteLog(LogLevel::Fatal, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION)
#
#	if defined(NCINE_DEBUG)
#		define LOGD_X(fmt, ...) __WriteLog(LogLevel::Debug, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION, ##__VA_ARGS__)
#	else
#		define LOGD_X(fmt, ...) do { } while (false)
#	endif
#	define LOGI_X(fmt, ...) __WriteLog(LogLevel::Info, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION, ##__VA_ARGS__)
#	define LOGW_X(fmt, ...) __WriteLog(LogLevel::Warning, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION, ##__VA_ARGS__)
#	define LOGE_X(fmt, ...) __WriteLog(LogLevel::Error, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION, ##__VA_ARGS__)
#	define LOGF_X(fmt, ...) __WriteLog(LogLevel::Fatal, static_cast<const char *>("%s -> " fmt), __LOG_FUNCTION, ##__VA_ARGS__)
#else
#	define LOGD(fmt) do { } while (false)
#	define LOGI(fmt) do { } while (false)
#	define LOGW(fmt) do { } while (false)
#	define LOGE(fmt) do { } while (false)
#	define LOGF(fmt) do { } while (false)
#
#	define LOGD_X(fmt, ...) do { } while (false)
#	define LOGI_X(fmt, ...) do { } while (false)
#	define LOGW_X(fmt, ...) do { } while (false)
#	define LOGE_X(fmt, ...) do { } while (false)
#	define LOGF_X(fmt, ...) do { } while (false)
#endif

// Return assert macros
#define RETURN_ASSERT_MSG_X(x, fmt, ...) do { if (!(x)) { LOGE_X(fmt, ##__VA_ARGS__); return; } } while (false)
#define RETURN_ASSERT_MSG(x, fmt) do { if (!(x)) { LOGE(fmt); return; } } while (false)
#define RETURN_ASSERT(x) do { if (!(x)) { LOGE("RETURN_ASSERT(" #x ")"); return; } } while (false)

// Return macros
#define RETURN_MSG_X(fmt, ...) do { LOGE_X(fmt, ##__VA_ARGS__); return; } while (false)
#define RETURN_MSG(fmt) do { LOGE(fmt); return; } while (false)

// Return false assert macros
#define RETURNF_ASSERT_MSG_X(x, fmt, ...) do { if (!(x)) { LOGE_X(fmt, ##__VA_ARGS__); return false; } } while (false)
#define RETURNF_ASSERT_MSG(x, fmt) do { if (!(x)) { LOGE(fmt); return false; } } while (false)
#define RETURNF_ASSERT(x) do { if (!(x)) { LOGE("RETURNF_ASSERT(" #x ")"); return false; } } while (false)

// Return false macros
#define RETURNF_MSG_X(fmt, ...) do { LOGE_X(fmt, ##__VA_ARGS__); return false; } while (false)
#define RETURNF_MSG(fmt) do { LOGE(fmt); return false; } while (false)

#if defined(NCINE_LOG)
#	if defined(_MSC_VER)
#		define BREAK() __debugbreak()
#	else
#		if defined(__has_builtin)
#			if __has_builtin(__builtin_trap)
#				define __HAS_BUILTIN_TRAP
#			endif
#		endif
#		if defined(__HAS_BUILTIN_TRAP)
#			define BREAK() __builtin_trap()
#		else
#			define BREAK() ::abort()
#		endif
#	endif
#else
#	define BREAK() do { } while (false)
#endif

// Fatal assert macros
#define FATAL_ASSERT_MSG_X(x, fmt, ...) \
	do \
	{ \
		if (!(x)) \
		{ \
			LOGF_X(fmt, ##__VA_ARGS__); \
			BREAK(); \
		} \
	} while (false)

#define FATAL_ASSERT_MSG(x, fmt) \
	do \
	{ \
		if (!(x)) \
		{ \
			LOGF(fmt); \
			BREAK(); \
		} \
	} while (false)

#define FATAL_ASSERT(x) \
	do \
	{ \
		if (!(x)) \
		{ \
			LOGF("FATAL_ASSERT(" #x ")"); \
			BREAK(); \
		} \
	} while (false)

// Fatal macros
#define FATAL_MSG_X(fmt, ...) \
	do \
	{ \
		LOGF_X(fmt, ##__VA_ARGS__); \
		BREAK(); \
	} while (false)

#define FATAL_MSG(fmt) \
	do \
	{ \
		LOGF(fmt); \
		BREAK(); \
	} while (false)

#define FATAL() \
	do \
	{ \
		BREAK(); \
	} while (false)

// Non-fatal assert macros
#if defined(NCINE_LOG)
#	define ASSERT_MSG_X(x, fmt, ...) \
		do \
		{ \
			if (!(x)) \
			{ \
				LOGE_X(fmt, ##__VA_ARGS__); \
				BREAK(); \
			} \
		} while (false)

#	define ASSERT_MSG(x, fmt) \
		do \
		{ \
			if (!(x)) \
			{ \
				LOGE(fmt); \
				BREAK(); \
			} \
		} while (false)

#	define ASSERT(x) \
		do \
		{ \
			if (!(x)) \
			{ \
				LOGE("ASSERT(" #x ")"); \
				BREAK(); \
			} \
		} while (false)
#else
#	define ASSERT_MSG_X(x, fmt, ...) do { } while (false)
#	define ASSERT_MSG(x, fmt) do { } while (false)
#	define ASSERT(x) do { } while (false)
#endif