#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "ScriptLoader.h"
#include "JJ2PlusDefinitions.h"
#include "../ILevelHandler.h"

namespace Jazz2::Scripting
{
	class jjPLAYER;

	class LevelScriptLoader : public ScriptLoader
	{
		friend class jjPLAYER;

	public:
		LevelScriptLoader(LevelHandler* levelHandler, const StringView& scriptPath);

		const SmallVectorImpl<Actors::Player*>& GetPlayers() const;

		void OnLevelLoad();
		void OnLevelBegin();
		void OnLevelReload();
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

		// Global scripting variables
		static constexpr int FLAG_HFLIPPED_TILE = 0x1000;
		static constexpr int FLAG_VFLIPPED_TILE = 0x2000;
		static constexpr int FLAG_ANIMATED_TILE = 0x4000;

		jjPAL jjPalette;
		jjPAL jjBackupPalette;

		int jjObjectCount = 0;
		int jjObjectMax = 0;

		int32_t gameMode = 0;
		int32_t customMode = 0;
		int32_t partyMode = 0;

		uint32_t gameTicksSpentWhileActive = 0;
		int32_t renderFrame = 0;

		bool versionTSF = true;
		bool isServer = false;
		bool jjDeactivatingBecauseOfDeath = false;

		int32_t DifficultyForNextLevel = 0;
		int32_t DifficultyAtLevelStart = 0;

		uint32_t numberOfTiles = 0;

		bool parLowDetail = false;
		int32_t colorDepth = 0;
		int32_t checkedMaxSubVideoWidth = 0;
		int32_t checkedMaxSubVideoHeight = 0;
		int32_t realVideoW = 0;
		int32_t realVideoH = 0;
		int32_t subVideoW = 0;
		int32_t subVideoH = 0;

		bool snowing = false;
		bool snowingOutdoors = false;
		uint8_t snowingIntensity = 0;
		int32_t snowingType = 0;

		int32_t maxScore = 0;

		int32_t waterLightMode = 0;
		int32_t waterInteraction = 0;

		uint8_t ChatKey = 0;

		bool soundEnabled = false;
		bool soundFXActive = false;
		bool musicActive = false;
		int32_t soundFXVolume = false;
		int32_t musicVolume = false;
		int32_t levelEcho = 0;

		bool warpsTransmuteCoins = false;
		bool delayGeneratedCrateOrigins = false;

		bool g_levelHasFood = false;
		int32_t enforceAmbientLighting = 0;

		LevelScriptLoader(const LevelScriptLoader&) = delete;
		LevelScriptLoader& operator=(const LevelScriptLoader&) = delete;

		Actors::ActorBase* CreateActorInstance(const StringView& typeName);

		static void RegisterBuiltInFunctions(asIScriptEngine* engine);
		void RegisterLegacyFunctions(asIScriptEngine* engine);
		void RegisterStandardFunctions(asIScriptEngine* engine, asIScriptModule* module);

		void OnException(asIScriptContext* ctx);

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
		static void asShowLevelText(const String& text);
		static void asSetWeather(uint8_t weatherType, uint8_t intensity);

		static int32_t jjGameTicks();

		static String get_jjMusicFileName();

		static String get_jjHelpStrings(uint32_t index);
		static void set_jjHelpStrings(uint32_t index, const String& text);

		static void jjAlert(const String& text, bool sendToAll, uint32_t size);

		static bool get_jjTriggers(uint8_t id);
		static bool set_jjTriggers(uint8_t id, bool value);
		static bool jjSwitchTrigger(uint8_t id);

		static void jjNxt(bool warp, bool fast);
		static bool jjMusicLoad(const String& filename, bool forceReload, bool temporary);
		static void jjMusicStop();
		static void jjMusicPlay();
		static void jjMusicPause();
		static void jjMusicResume();

	};
}

#endif