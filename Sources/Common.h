#pragma once

// Set default version if not provided by CMake
#if !defined(NCINE_VERSION)
#	define NCINE_VERSION "1.1.0"
#endif

#if !defined(_MSC_VER) && defined(__has_include)
#   if __has_include("../Shared/Common.h")
#       define __HAS_LOCAL_COMMON
#   endif
#endif
#ifdef __HAS_LOCAL_COMMON
#   include "../Shared/Common.h"
#else
#   include <Common.h>
#endif

#include <stdlib.h>

// Logging
#if defined(NCINE_LOG)

enum class LogLevel {
	Unknown,
	Verbose,
	Info,
	Warning,
	Error,
	Fatal
};

void __WriteLog(LogLevel level, const char* fmt, ...);

#	ifdef __GNUC__
#		define FUNCTION __PRETTY_FUNCTION__
#	elif _MSC_VER
#		define FUNCTION __FUNCTION__
#	else
#		define FUNCTION __func__
#	endif
#
#	if defined(NCINE_DEBUG)
#		define LOGV_X(fmt, ...) __WriteLog(LogLevel::Verbose, static_cast<const char *>("%s -> " fmt), FUNCTION, ##__VA_ARGS__)
#	else
#		define LOGV_X(fmt, ...) do { } while (false)
#	endif
#	define LOGI_X(fmt, ...) __WriteLog(LogLevel::Info, static_cast<const char *>("%s -> " fmt), FUNCTION, ##__VA_ARGS__)
#	define LOGW_X(fmt, ...) __WriteLog(LogLevel::Warning, static_cast<const char *>("%s -> " fmt), FUNCTION, ##__VA_ARGS__)
#	define LOGE_X(fmt, ...) __WriteLog(LogLevel::Error, static_cast<const char *>("%s -> " fmt), FUNCTION, ##__VA_ARGS__)
#	define LOGF_X(fmt, ...) __WriteLog(LogLevel::Fatal, static_cast<const char *>("%s -> " fmt), FUNCTION, ##__VA_ARGS__)
#
#	if defined(NCINE_DEBUG)
#		define LOGV(fmt) __WriteLog(LogLevel::Verbose, static_cast<const char *>("%s -> " fmt), FUNCTION)
#	else
#		define LOGV(fmt) do { } while (false)
#	endif
#	define LOGI(fmt) __WriteLog(LogLevel::Info, static_cast<const char *>("%s -> " fmt), FUNCTION)
#	define LOGW(fmt) __WriteLog(LogLevel::Warning, static_cast<const char *>("%s -> " fmt), FUNCTION)
#	define LOGE(fmt) __WriteLog(LogLevel::Error, static_cast<const char *>("%s -> " fmt), FUNCTION)
#	define LOGF(fmt) __WriteLog(LogLevel::Fatal, static_cast<const char *>("%s -> " fmt), FUNCTION)
#else
#	define LOGV_X(fmt, ...) do { } while (false)
#	define LOGI_X(fmt, ...) do { } while (false)
#	define LOGW_X(fmt, ...) do { } while (false)
#	define LOGE_X(fmt, ...) do { } while (false)
#	define LOGF_X(fmt, ...) do { } while (false)
#
#	define LOGV(fmt) do { } while (false)
#	define LOGI(fmt) do { } while (false)
#	define LOGW(fmt) do { } while (false)
#	define LOGE(fmt) do { } while (false)
#	define LOGF(fmt) do { } while (false)
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
#	ifdef _MSC_VER
#		define BREAK() __debugbreak()
#	else
#		ifndef __has_builtin
#			define __has_builtin(x) 0
#		endif
#		if __has_builtin(__builtin_trap)
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