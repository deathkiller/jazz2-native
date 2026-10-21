#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	/** @brief Registers @ref Death::Containers::String as `string` type to **AngelScript** engine */
	void RegisterString(asIScriptEngine* engine);

	/** @brief Registers `string::split()` and `join()`, which need the `array` type registered first */
	void RegisterStringUtils(asIScriptEngine* engine);
}

#endif