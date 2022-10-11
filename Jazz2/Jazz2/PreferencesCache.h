#pragma once

#include "LevelInitialization.h"
#include "../Common.h"
#include "../nCine/AppConfiguration.h"
#include "../nCine/Base/HashMap.h"

#include <Containers/Reference.h>

using namespace nCine;

namespace Jazz2
{
	enum class RescaleMode {
		None,
		HQ2x,
		_3xBrz,
		Crt,
		Monochrome,

		TypeMask = 0x0f,
		UseAntialiasing = 0x80
	};

	DEFINE_ENUM_OPERATORS(RescaleMode);

	enum class UnlockableEpisodes : uint32_t {
		None = 0x00,

		FormerlyAPrince = 0x01,
		JazzInTime = 0x02,
		Flashback = 0x04,
		FunkyMonkeys = 0x08,
		HolidayHare98 = 0x10,
		TheSecretFiles = 0x20,
	};

	DEFINE_ENUM_OPERATORS(UnlockableEpisodes);

	enum class EpisodeContinuationFlags : uint8_t {
		None = 0x00,

		IsCompleted = 0x01,
		CheatsUsed = 0x02
	};

	DEFINE_ENUM_OPERATORS(EpisodeContinuationFlags);

#	pragma pack(push, 1)

	// These structures are aligned manually, because they are serialized and it should work cross-platform
	struct EpisodeContinuationState {
		EpisodeContinuationFlags Flags;
		uint8_t DifficultyAndPlayerType;
		uint8_t Lives;
		uint8_t Unused1;
		int32_t Score;
		uint16_t Unused2;
		uint16_t Ammo[PlayerCarryOver::WeaponCount];
		uint8_t WeaponUpgrades[PlayerCarryOver::WeaponCount];
	};

	struct EpisodeContinuationStateWithLevel {
		EpisodeContinuationState State;
		String LevelName;
	};

#	pragma pack(pop)

	class PreferencesCache
	{
	public:
		static UnlockableEpisodes UnlockedEpisodes;

		// Graphics
		static RescaleMode ActiveRescaleMode;
		static bool EnableFullscreen;
		static bool EnableVsync;
		static bool ShowPerformanceMetrics;

		// Gameplay
		static bool EnableReforged;
		static bool EnableLedgeClimb;
		static bool EnableWeaponWheel;
		static bool EnableRgbLights;
		static bool AllowUnsignedScripts;
		static bool UseNativeBackButton;
		static bool TutorialCompleted;
		static bool AllowCheats;
		static bool AllowCheatsUnlock;
		static bool AllowCheatsWeapons;
		static Vector2f TouchLeftPadding;
		static Vector2f TouchRightPadding;
		static uint8_t Language[4];

		// Sounds
		static float MasterVolume;
		static float SfxVolume;
		static float MusicVolume;

		static void Initialize(const AppConfiguration& config);
		static void Save();

		static EpisodeContinuationState* GetEpisodeEnd(const StringView& episodeName, bool createIfNotFound = false);
		static EpisodeContinuationStateWithLevel* GetEpisodeContinue(const StringView& episodeName, bool createIfNotFound = false);
		static void RemoveEpisodeContinue(const StringView& episodeName);

	private:
		enum class BoolOptions : uint64_t {
			None = 0x00,

			EnableFullscreen = 0x01,
			ShowPerformanceMetrics = 0x02,

			EnableReforged = 0x100,
			EnableLedgeClimb = 0x200,
			EnableWeaponWheel = 0x400,
			EnableRgbLights = 0x800,
			AllowUnsignedScripts = 0x1000,
			UseNativeBackButton = 0x2000,

			TutorialCompleted = 0x10000,
			SetLanguage = 0x20000
		};

		DEFINE_PRIVATE_ENUM_OPERATORS(BoolOptions);

		static constexpr uint8_t FileVersion = 1;

		static constexpr float TouchPaddingMultiplier = 0.003f;

		/// Deleted copy constructor
		PreferencesCache(const PreferencesCache&) = delete;
		/// Deleted assignment operator
		PreferencesCache& operator=(const PreferencesCache&) = delete;

		static String _configPath;
		static HashMap<String, EpisodeContinuationState> _episodeEnd;
		static HashMap<String, EpisodeContinuationStateWithLevel> _episodeContinue;
	};
}