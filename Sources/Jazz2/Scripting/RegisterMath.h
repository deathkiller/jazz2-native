#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	/** @brief Registers `vec2`/`vec3`/`vec4`/`vec2i`/`vec3i`/`vec4i`/`Color` types to **AngelScript** engine */
	void RegisterMath(asIScriptEngine* engine);
}

#endif