#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	/** @brief Registers @ref Death::Containers::String as `string` type to **AngelScript** engine */
	void RegisterString(asIScriptEngine* engine);
}

#endif