#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	/** @brief Registers `string` type to **AngelScript** engine */
	void RegisterString(asIScriptEngine* engine);
}

#endif