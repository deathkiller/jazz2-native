#pragma once

#if defined(WITH_ANGELSCRIPT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	void RegisterString(asIScriptEngine* engine);
}

#endif