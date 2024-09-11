#pragma once

// Set default name and version if not provided by CMake
#if !defined(NCINE_APP)
#	define NCINE_APP "jazz2"
#endif
#if !defined(NCINE_APP_NAME)
#	define NCINE_APP_NAME "Jazz² Resurrection"
#endif
#if !defined(NCINE_VERSION)
#	define NCINE_VERSION "2.8.0"
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

// Check platform-specific capabilities
#if defined(WITH_SDL) || defined(DEATH_TARGET_WINDOWS_RT)
#	define NCINE_HAS_GAMEPAD_RUMBLE
#endif
#if defined(DEATH_TARGET_ANDROID)
#	define NCINE_HAS_NATIVE_BACK_BUTTON
#endif
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
#	define NCINE_HAS_WINDOWS
#endif

// Return assert macros
#define RETURN_MSG(fmt, ...) do { LOGE(fmt, ##__VA_ARGS__); return; } while (false)
#define RETURN_ASSERT_MSG(x, fmt, ...) do { if DEATH_UNLIKELY(!(x)) { LOGE(fmt, ##__VA_ARGS__); return; } } while (false)
#define RETURN_ASSERT(x) do { if DEATH_UNLIKELY(!(x)) { LOGE("RETURN_ASSERT(" #x ")"); return; } } while (false)

// Return false assert macros
#define RETURNF_MSG(fmt, ...) do { LOGE(fmt, ##__VA_ARGS__); return false; } while (false)
#define RETURNF_ASSERT_MSG(x, fmt, ...) do { if DEATH_UNLIKELY(!(x)) { LOGE(fmt, ##__VA_ARGS__); return false; } } while (false)
#define RETURNF_ASSERT(x) do { if DEATH_UNLIKELY(!(x)) { LOGE("RETURNF_ASSERT(" #x ")"); return false; } } while (false)

// Fatal assert macros
#define FATAL_MSG(fmt, ...)					\
	do {									\
		LOGF(fmt, ##__VA_ARGS__);			\
		__DEATH_ASSERT_BREAK();				\
	} while (false)

#define FATAL_ASSERT_MSG(x, fmt, ...)		\
	do {									\
		if DEATH_UNLIKELY(!(x)) {			\
			LOGF(fmt, ##__VA_ARGS__);		\
			__DEATH_ASSERT_BREAK();			\
		}									\
	} while (false)

#define FATAL_ASSERT(x)						\
	do {									\
		if DEATH_UNLIKELY(!(x)) {			\
			LOGF("FATAL_ASSERT(" #x ")");	\
			__DEATH_ASSERT_BREAK();			\
		}									\
	} while (false)

// Non-fatal assert macros
#if defined(DEATH_TRACE)
#	define ASSERT_MSG(x, fmt, ...)			\
		do {								\
			if DEATH_UNLIKELY(!(x)) {		\
				__DEATH_ASSERT_TRACE(fmt, ##__VA_ARGS__);	\
				__DEATH_ASSERT_BREAK();		\
			}								\
		} while (false)

#	define ASSERT(x)						\
		do {								\
			if DEATH_UNLIKELY(!(x)) {		\
				__DEATH_ASSERT_TRACE("ASSERT(" #x ")");	\
				__DEATH_ASSERT_BREAK();		\
			}								\
		} while (false)
#else
#	define ASSERT_MSG(x, fmt, ...) do { } while (false)
#	define ASSERT(x) do { } while (false)
#endif