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
		/** @brief Value of @ref MaxFps that specifies unlimited frame rate */
		static constexpr std::int32_t UnlimitedFps = 0;
		/** @brief Value of @ref MaxFps that specifies the frame rate of the monitor being used */
		static constexpr std::int32_t UseVsync = -1;

		/** @brief Whether the application is running for the first time */
		static bool FirstRun;
#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Whether the application is running as progressive web app (PWA)
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_EMSCRIPTEN "Emscripten" platform.
		 */
		static bool IsStandalone;
#endif
#if defined(WITH_MULTIPLAYER)
		static String InitialState;
#endif
		/** @brief Currently unlocked episodes */
		static UnlockableEpisodes UnlockedEpisodes;

		// Graphics
		/** @brief Active rescale mode */
		static RescaleMode ActiveRescaleMode;
		/** @brief Whether the application is running in fullscreen */
		static bool EnableFullscreen;
		/** @brief Maximum frace rate */
		static std::int32_t MaxFps;
		/** @brief Whether performance metrics (FPS counter) are visible */
		static bool ShowPerformanceMetrics;
		/** @brief Whether cinematics should keep original aspect ratio */
		static bool KeepAspectRatioInCinematics;
		/** @brief Whether player trails are visible */
		static bool ShowPlayerTrails;
		/** @brief Whether low quality water effects are enabled */
		static bool LowWaterQuality;
		/** @brief Whether viewport should be unaligned */
		static bool UnalignedViewport;
		/** @brief Whether vertical splitscreen is preferred */
		static bool PreferVerticalSplitscreen;
		/** @brief Whether viewport zoom out is preferred */
		static bool PreferZoomOut;
		/** @brief Whether background dithering should be used */
		static bool BackgroundDithering;

		// Gameplay
		/** @brief Whether reforged gameplay is enabled */
		static bool EnableReforgedGameplay;
		/** @brief Whether reforged HUD is enabled */
		static bool EnableReforgedHUD;
		/** @brief Whether reforged main menu is enabled */
		static bool EnableReforgedMainMenu;
#if defined(DEATH_TARGET_ANDROID)
		// Used to swap Android activity icons on exit/suspend
		static bool EnableReforgedMainMenuInitial;
#endif
		/** @brief Whether ledge climbing is enabled */
		static bool EnableLedgeClimb;
		/** @brief Current weapon wheel style */
		static WeaponWheelStyle WeaponWheel;
		/** @brief Whether RGB light effects are enabled */
		static bool EnableRgbLights;
		/** @brief Whether unsigned scripts can be loaded */
		static bool AllowUnsignedScripts;
		/** @brief Whether Discord integration is enabled */
		static bool EnableDiscordIntegration;
		/** @brief Whether tutorial is completed */
		static bool TutorialCompleted;
		/** @brief Whether the last state should be resumed on start */
		static bool ResumeOnStart;
		/** @brief Whether cheats are enabled by user */
		static bool AllowCheats;
		/** @brief Whether unlimited lives are enabled */
		static bool AllowCheatsLives;
		/** @brief Whether all episodes are unlocked */
		static bool AllowCheatsUnlock;
		/** @brief Whether the last progress is overwritten on the end of episode */
		static EpisodeEndOverwriteMode OverwriteEpisodeEnd;
		/** @brief Current language */
		static char Language[6];
		/** @brief Whether the cache should be bypassed */
		static bool BypassCache;

		// Sounds
		/** @brief Master sound volume */
		static float MasterVolume;
		/** @brief SFX volume */
		static float SfxVolume;
		/** @brief Music volume */
		static float MusicVolume;

		// Controls
		/** @brief Whether toggle Run action is enabled */
		static bool ToggleRunAction;
		/** @brief Active gamepad button labels */
		static GamepadType GamepadButtonLabels;
		/** @brief Gamepad rumble intensity */
		static std::uint8_t GamepadRumble;
		/** @brief Whether PlayStation controller extended support is enabled */
		static bool PlayStationExtendedSupport;
		/**
		 * @brief Whether native Back button should be used
		 * 
		 * @partialsupport Available only on @ref DEATH_TARGET_ANDROID "Android" platform.
		 */
		static bool UseNativeBackButton;
		/** @brief Touch controls left padding */
		static Vector2f TouchLeftPadding;
		/** @brief Touch controls right padding */
		static Vector2f TouchRightPadding;

		/** @brief Initializes preferences cache from a given application configuration */
		static void Initialize(const AppConfiguration& config);
		/** @brief Serializes current preferences to file */
		static void Save();
		/** @brief Returns directory path of the preferences file */
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