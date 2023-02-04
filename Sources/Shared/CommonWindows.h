#pragma once

#include "Common.h"

#if defined(DEATH_TARGET_WINDOWS)

#if defined(_INC_WINDOWS)
#	error <windows.h> cannot be included directly, include <CommonWindows.h> instead
#endif

// Undefine `min`/`max` macros, use `std::min`/`std::max` instead
#if !defined(NOMINMAX)
#	define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Undefine `far` and `near` keywords, not used anymore
#if defined(far)
#	undef far
#	if defined(FAR)
#		undef FAR
#		define FAR
#	endif
#endif
#if defined(near)
#	undef near
#	if defined(NEAR)
#		undef NEAR
#		define NEAR
#	endif
#endif

#endif