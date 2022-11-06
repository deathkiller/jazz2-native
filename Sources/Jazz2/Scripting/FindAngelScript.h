#pragma once

#if defined(WITH_ANGELSCRIPT)

#if defined(_MSC_VER) && defined(__has_include)
#	if __has_include("../../../Libs/angelscript.h")
#		define __HAS_LOCAL_ANGELSCRIPT
#	endif
#endif
#ifdef __HAS_LOCAL_ANGELSCRIPT
#	include "../../../Libs/angelscript.h"
#else
#	include <angelscript.h>
#endif

#endif