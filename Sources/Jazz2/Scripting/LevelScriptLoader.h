#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "ScriptLoader.h"
#include "../ILevelHandler.h"

namespace Jazz2::Scripting
{
	class LevelScriptLoader : public ScriptLoader
	{
	public:
		LevelScriptLoader(LevelHandler* levelHandler, const StringView& scriptPath);

		const SmallVectorImpl<Actors::Player*>& GetPlayers() const;

		void OnLevelBegin();
		void OnLevelUpdate(float timeMult);
		void OnLevelCallback(Actors::ActorBase* initiator, uint8_t* eventParams);

	protected:
		String OnProcessInclude(const StringView& includePath, const StringView& scriptPath) override;
		void OnProcessPragma(const StringView& content, ScriptContextType& contextType) override;

	private:
		LevelHandler* _levelHandler;
		asIScriptFunction* _onLevelUpdate;
		int32_t _onLevelUpdateLastFrame;
		HashMap<int, asITypeInfo*> _eventTypeToTypeInfo;

		Actors::ActorBase* CreateActorInstance(const StringView& typeName);

		static void RegisterBuiltInFunctions(asIScriptEngine* engine);
		void RegisterLegacyFunctions(asIScriptEngine* engine);
		void RegisterStandardFunctions(asIScriptEngine* engine, asIScriptModule* module);

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

		static void asChangeLevel(int exitType, const String& path);
		static void asMusicPlay(const String& path);
		static void asShowLevelText(const String& text);
		static void asSetWeather(uint8_t weatherType, uint8_t intensity);

		static int32_t jjGameTicks();

		static void jjAlert(const String& text, bool sendToAll, uint32_t size);

		static void jjNxt(bool warp, bool fast);
	};
}

#endif