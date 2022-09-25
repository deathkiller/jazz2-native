#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "FindAngelScript.h"
#include "../ILevelHandler.h"

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

		const SmallVectorImpl<Actors::Player*>& GetPlayers() const;

		void OnLevelBegin();
		void OnLevelUpdate(float timeMult);
		void OnLevelCallback(Actors::ActorBase* initiator, uint8_t* eventParams);

	private:
		LevelHandler* _levelHandler;
		asIScriptEngine* _engine;
		asIScriptModule* _module;
		SmallVector<asIScriptContext*, 4> _contextPool;

		asIScriptFunction* _onLevelUpdate;

		HashMap<int, asITypeInfo*> _eventTypeToTypeInfo;

		bool AddScriptFromFile(const StringView& path, const HashMap<String, bool>& definedSymbols);
		int ExcludeCode(String& scriptContent, int pos);
		int SkipStatement(String& scriptContent, int pos);
		void ProcessPragma(const StringView& content);

		static asIScriptContext* RequestContextCallback(asIScriptEngine* engine, void* param);
		static void ReturnContextCallback(asIScriptEngine* engine, asIScriptContext* ctx, void* param);

		void Message(const asSMessageInfo& msg);
		Actors::ActorBase* CreateActorInstance(const StringView& typeName);

		static uint8_t asGetDifficulty();
		static bool asIsReforged();
		static int asGetLevelWidth();
		static int asGetLevelHeight();
		static float asGetElapsedFrames();
		static float asGetAmbientLight();
		static void asSetAmbientLight(float value);
		static float asGetWaterLevel();
		static void asSetWaterLevel(float value);

		static void asPreloadMetadata(const String& path);
		static void asRegisterSpawnable(int eventType, const String& typeName);
		static std::shared_ptr<Actors::ActorBase> asRegisterSpawnableCallback(const Actors::ActorActivationDetails& details);
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