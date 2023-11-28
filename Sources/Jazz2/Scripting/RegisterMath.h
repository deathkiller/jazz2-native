#pragma once

#if defined(WITH_ANGELSCRIPT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	void RegisterMath(asIScriptEngine* engine);
}

#endif