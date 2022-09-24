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
	class CScriptArray;
	class ScriptActorWrapper;

	class LevelScripts
	{
	public:
		static constexpr asPWORD EngineToOwner = 0;

		LevelScripts(LevelHandler* levelHandler, const StringView& scriptPath);
		~LevelScripts();

		asIScriptEngine* GetEngine() const {
			return _engine;
		}

		asIScriptModule* GetMainModule() const {
			return _module;
		}

		void OnLevelBegin();
		void OnLevelCallback(uint8_t* eventParams);

	private:
		LevelHandler* _levelHandler;
		asIScriptEngine* _engine;
		asIScriptModule* _module;
		SmallVector<asIScriptContext*, 4> _contextPool;
		HashMap<String, bool> _definedWords;

		HashMap<int, String> _eventTypeToTypeName;

		bool AddScriptFromFile(const StringView& path);
		int ExcludeCode(String& scriptContent, int pos);
		int SkipStatement(String& scriptContent, int pos);

		static asIScriptContext* RequestContextCallback(asIScriptEngine* engine, void* param);
		static void ReturnContextCallback(asIScriptEngine* engine, asIScriptContext* ctx, void* param);

		void Message(const asSMessageInfo& msg);
		Actors::ActorBase* CreateActorInstance(const StringView& typeName);

		static uint8_t asGetDifficulty();
		static int asGetLevelWidth();
		static int asGetLevelHeight();
		static float asGetElapsedFrames();
		static float asGetAmbientLight();
		static void asSetAmbientLight(float value);
		static float asGetWaterLevel();
		static void asSetWaterLevel(float value);

		static void asPreloadMetadata(const String& path);
		static void asRegisterSpawnable(int eventType, const String& typeName);
		static void asSpawnEvent(int eventType, int x, int y);
		static void asSpawnEventParams(int eventType, int x, int y, const CScriptArray& eventParams);
		static void asSpawnType(const String& typeName, int x, int y);
		static void asSpawnTypeParams(const String& typeName, int x, int y, const CScriptArray& eventParams);

		static void asMusicPlay(const String& path);
		static void asShowLevelText(const String& text);
		static void asSetWeather(uint8_t weatherType, uint8_t intensity);
	};
}

#endif