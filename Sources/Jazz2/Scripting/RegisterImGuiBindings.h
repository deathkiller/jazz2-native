#pragma once

#if defined(WITH_ANGELSCRIPT) && defined(WITH_IMGUI)

#include "FindAngelScript.h"

namespace Jazz2::Scripting
{
	void RegisterImGuiBindings(asIScriptEngine* engine);
}

#endif