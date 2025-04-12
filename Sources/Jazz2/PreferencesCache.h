#pragma once

#include "../Main.h"
#include "WeaponType.h"
#include "../nCine/AppConfiguration.h"
#include "../nCine/Base/HashMap.h"

#include <Containers/StaticArray.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2
{
	/** @brief Rescale mode */
	enum class RescaleMode {
		None,						/**< None/Pixel-perfect */
		HQ2x,						/**< HQ2× */
		_3xBrz,						/**< 3×BRZ */
		CrtScanlines,				/**< CRT Scanlines */
		CrtShadowMask,				/**< CRT Shadow Mask */
		CrtApertureGrille,			/**< CRT Aperture Grille */
		Monochrome,					/**< Monochrome */

		TypeMask = 0x0f,
		UseAntialiasing = 0x80
	};

	DEATH_ENUM_FLAGS(RescaleMode);

	/** @brief Weapon wheel style */
	enum class WeaponWheelStyle : std::uint8_t {
		Disabled,					/**< Disabled */
		Enabled,					/**< Enabled */
		EnabledWithAmmoCount		/**< Enabled with Ammo count */
	};

	/** @brief Gamepad button labels */
	enum class GamepadType : std::uint8_t {
		Xbox,						/**< Xbox */
		PlayStation,				/**< PlayStation */
		Steam,						/**< Steam */
		Switch						/**< Switch */
	};

	/** @brief Episode completion overwrite mode */
	enum class EpisodeEndOverwriteMode : std::uint8_t {
		Always,						/**< Always */
		NoCheatsOnly,				/**< No cheats only */
		HigherScoreOnly				/**< Higher score only */
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
		None = 0x00,				/**< None */

		IsCompleted = 0x01,			/**< Episode is complete */
		CheatsUsed = 0x02			/**< Cheats have been used */
	};

	DEATH_ENUM_FLAGS(EpisodeContinuationFlags);

#	pragma pack(push, 1)

	/** @brief Continuation state between two episodes */
	// These structures are aligned manually, because they are serialized and it should work cross-platform
	struct EpisodeContinuationState {
		/** @brief Flags */
		EpisodeContinuationFlags Flags;
		/** @brief Difficulty and player type */
		std::uint8_t DifficultyAndPlayerType;
		/** @brief Lives */
		std::uint8_t Lives;
		std::uint8_t Unused1;
		/** @brief Score */
		std::int32_t Score;
		std::uint16_t Unused2;
		/** @brief Elapsed game time in milliseconds */
		std::uint64_t ElapsedMilliseconds;
		/** @brief Gems collected */
		std::int32_t Gems[4];
		/** @brief Weapon ammo */
		StaticArray<(std::int32_t)WeaponType::Count, std::uint16_t> Ammo;
		/** @brief Weapon upgrades */
		StaticArray<(std::int32_t)WeaponType::Count, std::uint8_t> WeaponUpgrades;
	};

	/** @brief Continuation state between two levels in episode */
	struct EpisodeContinuationStateWithLevel {
		/** @brief Base continuation state */
		EpisodeContinuationState State;
		/** @brief Last level name */
		String LevelName;
	};

#	pragma pack(pop)

	/** @brief Provides access to a user preferences */
	class PreferencesCache
	{
	public:
		/** @{ @name Constants */

		/** @brief Value of @ref MaxFps that specifies unlimited frame rate */
		static constexpr std::int32_t UnlimitedFps = 0;
		/** @brief Value of @ref MaxFps that specifies the frame rate of the monitor being used */
		static constexpr std::int32_t UseVsync = -1;

		/** @} */

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
		/** @brief Currently unlocked episodes if compiled with `SHAREWARE_DEMO_ONLY` */
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

		// User Profile
		/** @brief Unique player ID */
		static StaticArray<16, std::uint8_t> UniquePlayerID;
		/** @brief Unique server ID */
		static StaticArray<16, std::uint8_t> UniqueServerID;
		/** @brief Player display name */
		static String PlayerName;
		/** @brief Whether Discord integration is enabled */
		static bool EnableDiscordIntegration;


		/** @brief Initializes preferences cache from a given application configuration */
		static void Initialize(const AppConfiguration& config);
		/** @brief Serializes current preferences to file */
		static void Save();
		/** @brief Returns directory path of the preferences file */
		static StringView GetDirectory();

		/** @brief Returns device ID of the device currently running this application */
		static String GetDeviceID();
		/** @brief Returns effective player name */
		static String GetEffectivePlayerName();

		/** @brief Returns information about episode completion */
		static EpisodeContinuationState* GetEpisodeEnd(StringView episodeName, bool createIfNotFound = false);
		/** @brief Returns information about episode continuation */
		static EpisodeContinuationStateWithLevel* GetEpisodeContinue(StringView episodeName, bool createIfNotFound = false);
		/** @brief Removes information about episode continuation (resets progress) */
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

		static constexpr std::uint8_t FileVersion = 11;

		static constexpr float TouchPaddingMultiplier = 0.003f;

		PreferencesCache(const PreferencesCache&) = delete;
		PreferencesCache& operator=(const PreferencesCache&) = delete;

		static String _configPath;
		static HashMap<String, EpisodeContinuationState> _episodeEnd;
		static HashMap<String, EpisodeContinuationStateWithLevel> _episodeContinue;

		static void TryLoadPreferredLanguage();
	};
}