#pragma once

#if defined(WITH_ANGELSCRIPT)

#if defined(_MSC_VER) && defined(__has_include)
#	if __has_include("../../../Libs/Includes/angelscript.h")
#		define __HAS_LOCAL_ANGELSCRIPT
#	endif
#endif
#ifdef __HAS_LOCAL_ANGELSCRIPT
#	include "../../../Libs/Includes/angelscript.h"
#else
#	include <angelscript.h>
#endif

#define AS_USE_ACCESSORS 1

#endif