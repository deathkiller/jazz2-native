#pragma once

#if (defined(WITH_ANGELSCRIPT) && defined(WITH_IMGUI)) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	void RegisterImGuiBindings(asIScriptEngine* engine);
}

#endif