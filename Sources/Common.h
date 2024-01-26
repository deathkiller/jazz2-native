#pragma once

// Set default name and version if not provided by CMake
#if !defined(NCINE_APP)
#	define NCINE_APP "jazz2"
#endif
#if !defined(NCINE_APP_NAME)
#	define NCINE_APP_NAME "Jazz² Resurrection"
#endif
#if !defined(NCINE_VERSION)
#	define NCINE_VERSION "2.5.0"
#endif
#if !defined(NCINE_BUILD_YEAR)
#	define NCINE_BUILD_YEAR "2024"
#endif
#if !defined(NCINE_LINUX_PACKAGE)
#	define NCINE_LINUX_PACKAGE NCINE_APP_NAME
#endif

// Prefer local version of shared libraries in CMake build
#if defined(CMAKE_BUILD) && defined(__has_include)
#	if __has_include("../Shared/Common.h")
#		define __HAS_LOCAL_COMMON
#	endif
#endif
#ifdef __HAS_LOCAL_COMMON
#	include "../Shared/Common.h"
#	include "../Shared/Asserts.h"
#else
#	include <Common.h>
#	include <Asserts.h>
#endif

#include <stdlib.h>

// Return assert macros
#define RETURN_ASSERT_MSG(x, fmt, ...) do { if (!(x)) { LOGE(fmt, ##__VA_ARGS__); return; } } while (false)
#define RETURN_ASSERT(x) do { if (!(x)) { LOGE("RETURN_ASSERT(" #x ")"); return; } } while (false)

// Return macros
#define RETURN_MSG(fmt, ...) do { LOGE(fmt, ##__VA_ARGS__); return; } while (false)

// Return false assert macros
#define RETURNF_ASSERT_MSG(x, fmt, ...) do { if (!(x)) { LOGE(fmt, ##__VA_ARGS__); return false; } } while (false)
#define RETURNF_ASSERT(x) do { if (!(x)) { LOGE("RETURNF_ASSERT(" #x ")"); return false; } } while (false)

// Return false macros
#define RETURNF_MSG(fmt, ...) do { LOGE(fmt, ##__VA_ARGS__); return false; } while (false)

#if defined(DEATH_TRACE)
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
#define FATAL_ASSERT_MSG(x, fmt, ...) \
	do \
	{ \
		if (!(x)) \
		{ \
			LOGF(fmt, ##__VA_ARGS__); \
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
#define FATAL_MSG(fmt, ...) \
	do \
	{ \
		LOGF(fmt, ##__VA_ARGS__); \
		BREAK(); \
	} while (false)

#define FATAL() \
	do \
	{ \
		BREAK(); \
	} while (false)

// Non-fatal assert macros
#if defined(DEATH_TRACE)
#	define ASSERT_MSG(x, fmt, ...) \
		do \
		{ \
			if (!(x)) \
			{ \
				LOGE(fmt, ##__VA_ARGS__); \
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
#	define ASSERT_MSG(x, fmt, ...) do { } while (false)
#	define ASSERT(x) do { } while (false)
#endif