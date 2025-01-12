#pragma once

#include "../Main.h"
#include "WeaponType.h"
#include "../nCine/AppConfiguration.h"
#include "../nCine/Base/HashMap.h"

using namespace nCine;

namespace Jazz2
{
	/** @brief Rescale mode */
	enum class RescaleMode {
		None,
		HQ2x,
		_3xBrz,
		CrtScanlines,
		CrtShadowMask,
		CrtApertureGrille,
		Monochrome,

		TypeMask = 0x0f,
		UseAntialiasing = 0x80
	};

	DEATH_ENUM_FLAGS(RescaleMode);

	/** @brief Weapon wheel style */
	enum class WeaponWheelStyle : std::uint8_t {
		Disabled,
		Enabled,
		EnabledWithAmmoCount
	};

	/** @brief Gamepad button labels */
	enum class GamepadType : std::uint8_t {
		Xbox,
		PlayStation,
		Steam,
		Switch
	};

	/** @brief Episode completion overwrite mode */
	enum class EpisodeEndOverwriteMode : std::uint8_t {
		Always,
		NoCheatsOnly,
		HigherScoreOnly
	};

	/** @brief Unlockable episodes, mainly used if compiled with `SHAREWARE_DEMO_ONLY` */
	enum class UnlockableEpisodes : std::uint32_t {
		None = 0x00,

		FormerlyAPrince = 0x01,
		JazzInTime = 0x02,
		Flashback = 0x04,
		FunkyMonkeys = 0x08,
		ChristmasChronicles = 0x10,
		TheSecretFiles = 0x20,
	};

	DEATH_ENUM_FLAGS(UnlockableEpisodes);

	/** @brief Episode continuation flags, supports a bitwise combination of its member values */
	enum class EpisodeContinuationFlags : std::uint8_t {
		None = 0x00,

		IsCompleted = 0x01,
		CheatsUsed = 0x02
	};

	DEATH_ENUM_FLAGS(EpisodeContinuationFlags);

#	pragma pack(push, 1)

	/** @brief Continuation state between two episodes */
	// These structures are aligned manually, because they are serialized and it should work cross-platform
	struct EpisodeContinuationState {
		EpisodeContinuationFlags Flags;
		std::uint8_t DifficultyAndPlayerType;
		std::uint8_t Lives;
		std::uint8_t Unused1;
		std::int32_t Score;
		std::uint16_t Unused2;
		std::uint64_t ElapsedMilliseconds;
		std::int32_t Gems[4];
		std::uint16_t Ammo[(std::int32_t)WeaponType::Count];
		std::uint8_t WeaponUpgrades[(std::int32_t)WeaponType::Count];
	};

	/** @brief Continuation state between two levels in episode */
	struct EpisodeContinuationStateWithLevel {
		EpisodeContinuationState State;
		String LevelName;
	};

#	pragma pack(pop)

	/** @brief Provides access to a user preferences */
	class PreferencesCache
	{
	public:
		static constexpr std::int32_t UnlimitedFps = 0;
		static constexpr std::int32_t UseVsync = -1;

		static bool FirstRun;
#if defined(DEATH_TARGET_EMSCRIPTEN)
		static bool IsStandalone;
#endif
#if defined(WITH_MULTIPLAYER)
		static String InitialState;
#endif
		static UnlockableEpisodes UnlockedEpisodes;

		// Graphics
		static RescaleMode ActiveRescaleMode;
		static bool EnableFullscreen;
		static std::int32_t MaxFps;
		static bool ShowPerformanceMetrics;
		static bool KeepAspectRatioInCinematics;
		static bool ShowPlayerTrails;
		static bool LowWaterQuality;
		static bool UnalignedViewport;
		static bool PreferVerticalSplitscreen;
		static bool PreferZoomOut;
		static bool BackgroundDithering;

		// Gameplay
		static bool EnableReforgedGameplay;
		static bool EnableReforgedHUD;
		static bool EnableReforgedMainMenu;
#if defined(DEATH_TARGET_ANDROID)
		// Used to swap Android activity icons on exit/suspend
		static bool EnableReforgedMainMenuInitial;
#endif
		static bool EnableLedgeClimb;
		static WeaponWheelStyle WeaponWheel;
		static bool EnableRgbLights;
		static bool AllowUnsignedScripts;
		static bool EnableDiscordIntegration;
		static bool TutorialCompleted;
		static bool ResumeOnStart;
		static bool AllowCheats;
		static bool AllowCheatsLives;
		static bool AllowCheatsUnlock;
		static EpisodeEndOverwriteMode OverwriteEpisodeEnd;
		static char Language[6];
		static bool BypassCache;

		// Sounds
		static float MasterVolume;
		static float SfxVolume;
		static float MusicVolume;

		// Controls
		static bool ToggleRunAction;
		static GamepadType GamepadButtonLabels;
		static std::uint8_t GamepadRumble;
		static bool PlayStationExtendedSupport;
		static bool UseNativeBackButton;
		static Vector2f TouchLeftPadding;
		static Vector2f TouchRightPadding;

		static void Initialize(const AppConfiguration& config);
		static void Save();
		static StringView GetDirectory();

		static EpisodeContinuationState* GetEpisodeEnd(StringView episodeName, bool createIfNotFound = false);
		static EpisodeContinuationStateWithLevel* GetEpisodeContinue(StringView episodeName, bool createIfNotFound = false);
		static void RemoveEpisodeContinue(StringView episodeName);

	private:
		enum class BoolOptions : uint64_t {
			None = 0x00,

			EnableFullscreen = 0x01,
			ShowPerformanceMetrics = 0x02,
			KeepAspectRatioInCinematics = 0x04,
			ShowPlayerTrails = 0x08,
			LowWaterQuality = 0x10,
			UnalignedViewport = 0x20,
			PreferVerticalSplitscreen = 0x40,
			PreferZoomOut = 0x80,

			EnableReforgedGameplay = 0x100,
			EnableLedgeClimb = 0x200,
			EnableWeaponWheel = 0x400,
			EnableRgbLights = 0x800,
			AllowUnsignedScripts = 0x1000,
			UseNativeBackButton = 0x2000,
			EnableDiscordIntegration = 0x4000,
			ShowWeaponWheelAmmoCount = 0x8000,

			TutorialCompleted = 0x10000,
			SetLanguage = 0x20000,
			ResumeOnStart = 0x40000,

			EnableReforgedHUD = 0x100000,
			EnableReforgedMainMenu = 0x200000,
			ToggleRunAction = 0x400000,
			AllowCheats = 0x1000000,
			BackgroundDithering = 0x2000000,

			PlayStationExtendedSupport = 0x4000000,
		};

		DEATH_PRIVATE_ENUM_FLAGS(BoolOptions);

		static constexpr std::uint8_t FileVersion = 9;

		static constexpr float TouchPaddingMultiplier = 0.003f;

		PreferencesCache(const PreferencesCache&) = delete;
		PreferencesCache& operator=(const PreferencesCache&) = delete;

		static String _configPath;
		static HashMap<String, EpisodeContinuationState> _episodeEnd;
		static HashMap<String, EpisodeContinuationStateWithLevel> _episodeContinue;

		static void TryLoadPreferredLanguage();
	};
}