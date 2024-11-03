#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "ScriptLoader.h"
#include "JJ2PlusDefinitions.h"
#include "../ILevelHandler.h"
#include "../../nCine/Base/BitArray.h"

namespace Jazz2::UI
{
	class HUD;
}

namespace Jazz2::Scripting
{
	class jjPLAYER;

	enum class DrawType
	{
		WeaponAmmo,
		Health,
		Lives,
		PlayerTimer,
		Score,
		GameModeHUD
	};

	struct PlayerBackingStore
	{
		void* TimerCallback;
		std::uint32_t TimerState;
		float TimerLeft;
		bool TimerPersists;

		PlayerBackingStore();
	};

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
		void OnLevelCallback(Actors::ActorBase* initiator, std::uint8_t* eventParams);
		bool OnDraw(UI::HUD* hud, Actors::Player* player, const Rectf& view, DrawType type);
		void OnPlayerDied(Actors::Player* player, Actors::ActorBase* collider);

	protected:
		String OnProcessInclude(const StringView& includePath, const StringView& scriptPath) override;
		void OnProcessPragma(const StringView& content, ScriptContextType& contextType) override;

	private:
		LevelHandler* _levelHandler;
		asIScriptFunction* _onLevelUpdate;
		std::int32_t _onLevelUpdateLastFrame;
		asIScriptFunction* _onDrawAmmo;
		asIScriptFunction* _onDrawHealth;
		asIScriptFunction* _onDrawLives;
		asIScriptFunction* _onDrawPlayerTimer;
		asIScriptFunction* _onDrawScore;
		asIScriptFunction* _onDrawGameModeHUD;
		HashMap<std::int32_t, asITypeInfo*> _eventTypeToTypeInfo;
		BitArray _enabledCallbacks;
		HashMap<std::int32_t, PlayerBackingStore> _playerBackingStore;

		// Global scripting variables
		static constexpr std::int32_t FLAG_HFLIPPED_TILE = 0x1000;
		static constexpr std::int32_t FLAG_VFLIPPED_TILE = 0x2000;
		static constexpr std::int32_t FLAG_ANIMATED_TILE = 0x4000;

		jjPAL jjPalette;
		jjPAL jjBackupPalette;

		std::int32_t jjObjectCount = 0;
		std::int32_t jjObjectMax = 0;

		std::int32_t gameMode = 0;
		std::int32_t customMode = 0;
		std::int32_t partyMode = 0;

		std::uint32_t gameTicksSpentWhileActive = 0;
		std::int32_t renderFrame = 0;

		bool versionTSF = true;
		bool isServer = false;
		bool jjDeactivatingBecauseOfDeath = false;

		std::int32_t DifficultyForNextLevel = 0;
		std::int32_t DifficultyAtLevelStart = 0;

		std::uint32_t numberOfTiles = 0;

		bool parLowDetail = false;
		std::int32_t colorDepth = 0;
		std::int32_t checkedMaxSubVideoWidth = 0;
		std::int32_t checkedMaxSubVideoHeight = 0;
		std::int32_t realVideoW = 0;
		std::int32_t realVideoH = 0;
		std::int32_t subVideoW = 0;
		std::int32_t subVideoH = 0;

		bool snowing = false;
		bool snowingOutdoors = false;
		std::uint8_t snowingIntensity = 0;
		std::int32_t snowingType = 0;

		std::int32_t maxScore = 0;

		std::int32_t waterLightMode = 0;
		std::int32_t waterInteraction = 0;

		std::uint8_t ChatKey = 0;

		bool soundEnabled = false;
		bool soundFXActive = false;
		bool musicActive = false;
		std::int32_t soundFXVolume = false;
		std::int32_t musicVolume = false;
		std::int32_t levelEcho = 0;

		bool warpsTransmuteCoins = false;
		bool delayGeneratedCrateOrigins = false;

		bool g_levelHasFood = false;
		std::int32_t enforceAmbientLighting = 0;

		LevelScriptLoader(const LevelScriptLoader&) = delete;
		LevelScriptLoader& operator=(const LevelScriptLoader&) = delete;

		Actors::ActorBase* CreateActorInstance(const StringView& typeName);

		PlayerBackingStore& GetPlayerBackingStore(std::int32_t playerIdx);

		static void RegisterBuiltInFunctions(asIScriptEngine* engine);
		void RegisterLegacyFunctions(asIScriptEngine* engine);
		void RegisterStandardFunctions(asIScriptEngine* engine, asIScriptModule* module);

		void OnException(asIScriptContext* ctx);

		static std::uint8_t asGetDifficulty();
		static bool asIsReforged();
		static std::int32_t asGetLevelWidth();
		static std::int32_t asGetLevelHeight();
		static float asGetElapsedFrames();
		static float asGetAmbientLight();
		static void asSetAmbientLight(float value);
		static float asGetWaterLevel();
		static void asSetWaterLevel(float value);

		static void asPreloadMetadata(const String& path);
		static void asRegisterSpawnable(std::int32_t eventType, const String& typeName);
		static std::shared_ptr<Actors::ActorBase> asRegisterSpawnableCallback(const Actors::ActorActivationDetails& details);
		static void asSpawnEvent(std::int32_t eventType, std::int32_t x, std::int32_t y);
		static void asSpawnEventParams(std::int32_t eventType, std::int32_t x, std::int32_t y, const CScriptArray& eventParams);
		static void asSpawnType(const String& typeName, std::int32_t x, std::int32_t y);
		static void asSpawnTypeParams(const String& typeName, std::int32_t x, std::int32_t y, const CScriptArray& eventParams);

		static void asChangeLevel(std::int32_t exitType, const String& path);
		static void asShowLevelText(const String& text);
		static void asSetWeather(std::uint8_t weatherType, std::uint8_t intensity);

		static std::int32_t jjGameTicks();

		static String get_jjMusicFileName();

		static String get_jjHelpStrings(std::uint32_t index);
		static void set_jjHelpStrings(std::uint32_t index, const String& text);

		static void jjAlert(const String& text, bool sendToAll, std::uint32_t size);

		static bool get_jjTriggers(std::uint8_t id);
		static bool set_jjTriggers(std::uint8_t id, bool value);
		static bool jjSwitchTrigger(std::uint8_t id);

		static bool isNumberedASFunctionEnabled(std::uint8_t id);
		static bool setNumberedASFunctionEnabled(std::uint8_t id, bool value);
		static void reenableAllNumberedASFunctions();

		static void jjNxt(bool warp, bool fast);
		static bool jjMusicLoad(const String& filename, bool forceReload, bool temporary);
		static void jjMusicStop();
		static void jjMusicPlay();
		static void jjMusicPause();
		static void jjMusicResume();

	};
}

#endif