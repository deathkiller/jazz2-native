#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	void RegisterMath(asIScriptEngine* engine);
}

#endif