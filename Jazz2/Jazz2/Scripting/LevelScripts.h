#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "../ILevelHandler.h"

#if defined(_MSC_VER) && defined(__has_include)
#	if __has_include("../../../Libs/angelscript.h")
#		define __HAS_LOCAL_ANGELSCRIPT
#	endif
#endif
#ifdef __HAS_LOCAL_ANGELSCRIPT
#	include "../../../Libs/angelscript.h"
#else
#	include <angelscript.h>
#endif

namespace Jazz2::Scripting
{
	class LevelScripts
	{
	public:
		LevelScripts(LevelHandler* levelHandler, const StringView& scriptPath);
		~LevelScripts();

	private:
		LevelHandler* _levelHandler;
		asIScriptEngine* _engine;
		asIScriptModule* _module;
		asIScriptContext* _ctx;
		HashMap<String, bool> _definedWords;

		bool AddScriptFromFile(const StringView& path);
		int ExcludeCode(String& scriptContent, int pos);
		int SkipStatement(String& scriptContent, int pos);

		void Status(const asSMessageInfo& msg);
	};
}

#endif