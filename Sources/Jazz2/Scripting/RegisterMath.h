#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "FindAngelScript.h"

namespace Jazz2::Scripting
{
	void RegisterMath(asIScriptEngine* engine);
}

#endif