#pragma once

#include "Common.h"

#if defined(DEATH_TARGET_WINDOWS)

// Undefine `min`/`max` macros, use `std::min`/`std::max` instead
#if !defined(NOMINMAX)
#	define NOMINMAX
#endif

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