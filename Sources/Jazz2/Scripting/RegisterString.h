#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "FindAngelScript.h"

namespace Jazz2::Scripting
{
	void RegisterString(asIScriptEngine* engine);
}

#endif