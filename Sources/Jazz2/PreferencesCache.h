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
	/**
		@brief Universally unique identifier (16 bytes)
		
		Fixed 16-byte array used as a stable identity, such as the unique player and server IDs persisted in
		@ref PreferencesCache.
	*/
	using Uuid = StaticArray<16, std::uint8_t>;

	/**
		@brief Rescale mode
		
		Selects the post-processing filter applied when the internal viewport is scaled to the output
		resolution, ranging from pixel-perfect to upscalers (HQ2×, 3×BRZ, SABR, CleanEdge) and CRT
		emulation. @ref TypeMask isolates the filter while @ref UseAntialiasing is an independent flag.
	*/
	enum class RescaleMode {
		None,						/**< None/Pixel-perfect */
		HQ2x,						/**< HQ2× */
		_3xBrz,						/**< 3×BRZ */
		CrtScanlines,				/**< CRT Scanlines */
		CrtShadowMask,				/**< CRT Shadow Mask */
		CrtApertureGrille,			/**< CRT Aperture Grille */
		Monochrome,					/**< Monochrome */
		Sabr,						/**< SABR */
		CleanEdge,					/**< CleanEdge */

		Count,						/**< Count of rescale modes */

		TypeMask = 0x0f,			/**< Mask for the rescale mode */
		UseAntialiasing = 0x80		/**< Antialiasing should be applied */
	};

	DEATH_ENUM_FLAGS(RescaleMode);

	/**
		@brief Weapon wheel style
		
		Controls whether the radial weapon selection wheel is available and, when enabled, whether it
		also displays the remaining ammo count for each weapon.
	*/
	enum class WeaponWheelStyle : std::uint8_t {
		Disabled,					/**< Disabled */
		Enabled,					/**< Enabled */
		EnabledWithAmmoCount		/**< Enabled with Ammo count */
	};

	/**
		@brief When the custom player character color is applied
		
		Determines the scope in which the user's custom fur recolor takes effect: only for remote
		players in online multiplayer, additionally for the first local player, or for every local
		player.
	*/
	enum class PlayerColorMode : std::uint8_t {
		OnlineOnly,					/**< Only in online multiplayer */
		FirstLocalPlayer,			/**< Online, and the first player in local games */
		AllLocalPlayers				/**< Online, and all players in local games */
	};

	/**
		@brief Gamepad button labels
		
		Selects which controller family's glyphs and button names are shown in the user interface, so
		that on-screen prompts match the physical gamepad being used.
	*/
	enum class GamepadType : std::uint8_t {
		Xbox,						/**< Xbox */
		PlayStation,				/**< PlayStation */
		Steam,						/**< Steam */
		Switch						/**< Switch */
	};

	/**
		@brief Episode completion overwrite mode
		
		Decides under which conditions a newly recorded episode-completion result replaces the
		previously stored one: always, only when no cheats were used, or only when the new score is
		higher.
	*/
	enum class EpisodeEndOverwriteMode : std::uint8_t {
		Always,						/**< Always */
		NoCheatsOnly,				/**< No cheats only */
		HigherScoreOnly				/**< Higher score only */
	};

	/**
		@brief Unlockable episodes, supports a bitwise combination of its member values
		
		Bit flags identifying which episodes have been unlocked, mainly used when the application is
		compiled with `SHAREWARE_DEMO_ONLY` to gate access to the full episode list. Stored in
		@ref PreferencesCache::UnlockedEpisodes.
	*/
	enum class UnlockableEpisodes : std::uint32_t {
		None = 0x00,						/**< None */

		FormerlyAPrince = 0x01,				/**< Formerly a Prince */
		JazzInTime = 0x02,					/**< Jazz in Time */
		Flashback = 0x04,					/**< Flashback */
		FunkyMonkeys = 0x08,				/**< Funky Monkeys */
		ChristmasChronicles = 0x10,			/**< Christmas Chronicles */
		TheSecretFiles = 0x20,				/**< The Secret Files */
	};

	DEATH_ENUM_FLAGS(UnlockableEpisodes);

	/**
		@brief Episode continuation flags, supports a bitwise combination of its member values
		
		Bit flags recorded alongside a stored @ref EpisodeContinuationState, marking whether the
		episode has been completed and whether cheats were used during the saved playthrough.
	*/
	enum class EpisodeContinuationFlags : std::uint8_t {
		None = 0x00,				/**< None */

		IsCompleted = 0x01,			/**< Episode is complete */
		CheatsUsed = 0x02			/**< Cheats have been used */
	};

	/**
		@brief Anchor edge for a configurable touch button
		
		Identifies which screen corner (or top-center, used for round screens) a touch button's
		position is measured from, so that its @ref TouchButtonLayout::EdgeOffset stays consistent
		across different screen sizes and aspect ratios.
	*/
	enum class TouchButtonAnchor : std::uint8_t {
		BottomLeft = 0,				/**< Bottom-left corner */
		BottomRight = 1,			/**< Bottom-right corner */
		TopLeft = 2,				/**< Top-left corner */
		TopRight = 3,				/**< Top-right corner */
		TopCenter = 4				/**< Top-center (used for round screens) */
	};

	/**
		@brief Slot index for each independently configurable touch button
		
		Enumerates the on-screen touch controls (D-pad, fire, jump, run, change weapon, menu and
		console) that can each be repositioned and resized independently. Used to index the
		@ref PreferencesCache::TouchButtons array, with @ref Count giving the number of slots.
	*/
	enum class TouchButtonSlot : std::uint8_t {
		Dpad = 0,					/**< D-pad / Joystick (moves and resizes as a unit with its sub-zones) */
		Fire = 1,					/**< Fire button */
		Jump = 2,					/**< Jump button */
		Run = 3,					/**< Run button */
		ChangeWeapon = 4,			/**< Change weapon button */
		Menu = 5,					/**< Pause / Menu button */
		Console = 6,				/**< Console button */
		Count = 7					/**< Number of configurable slots */
	};

	/**
		@brief Per-button layout stored in user preferences
		
		Holds the persisted on-screen placement of a single touch button: its anchor edge, the offset
		from that edge in reference pixels and a size scale factor. One entry is stored per
		@ref TouchButtonSlot in @ref PreferencesCache::TouchButtons.
	*/
	struct TouchButtonLayout {
		/** @brief Offset from the anchor edge in reference pixels (DefaultWidth * 0.5 = 360) */
		Vector2f EdgeOffset;
		/** @brief Size scale factor (0.5 = half size, 1.0 = default, 3.0 = triple size) */
		float Scale;
		/** @brief Which screen corner this button is anchored to */
		TouchButtonAnchor Anchor;
	};

	DEATH_ENUM_FLAGS(EpisodeContinuationFlags);

#	pragma pack(push, 1)

	/**
		@brief Continuation state between two episodes
		
		Persisted per-episode progress (flags, difficulty/player type, lives, score, elapsed time, gems, ammo and
		weapon upgrades) carried over when starting the next episode. Stored in @ref PreferencesCache.
	*/
	// These structures are aligned manually, because they are serialized and it should work cross-platform
	struct EpisodeContinuationState {
		/** @brief Flags */
		EpisodeContinuationFlags Flags;
		/** @brief Difficulty and player type */
		std::uint8_t DifficultyAndPlayerType;
		/** @brief Lives */
		std::uint8_t Lives;
		/** @brief Reserved for alignment */
		std::uint8_t Unused1;
		/** @brief Score */
		std::int32_t Score;
		/** @brief Reserved for alignment */
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

	/**
		@brief Continuation state between two levels in episode
		
		Wraps an @ref EpisodeContinuationState with the name of the last level played, so that an
		in-progress episode can be resumed at the correct level. Stored per-episode in
		@ref PreferencesCache.
	*/
	struct EpisodeContinuationStateWithLevel {
		/** @brief Base continuation state */
		EpisodeContinuationState State;
		/** @brief Last level name */
		String LevelName;
	};

#	pragma pack(pop)

	/**
		@brief Provides access to a user preferences
		
		Static, process-wide store of all persisted settings (graphics, gameplay, input, language) plus per-episode
		continuation states. Loaded on startup and written back to the configuration file, it is queried throughout
		the engine to read the current configuration.
	*/
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
		/** @brief Whether blur effects are allowed */
		static bool BlurEffects;
		/** @brief Lighting resolution percent */
		static std::uint8_t LightingResolutionPercent;

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
		/** @brief Whether continuous jump is enabled */
		static bool EnableContinuousJump;
		/** @brief Whether ledge climbing is enabled */
		static bool EnableLedgeClimb;
		/** @brief Current weapon wheel style */
		static WeaponWheelStyle WeaponWheel;
		/** @brief Whether a newly acquired weapon is automatically selected */
		static bool SwitchToNewWeapon;
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
		/** @brief Whether D-pad is replaced by a floating analog joystick */
		static bool EnableTouchJoystick;
		/** @brief Whether device vibration is used for touch button press feedback */
		static bool EnableTouchVibration;
		/** @brief Per-button layout configuration for all configurable touch buttons */
		static TouchButtonLayout TouchButtons[(std::size_t)TouchButtonSlot::Count];

		// User Profile
		/** @brief Unique player ID */
		static Uuid UniquePlayerID;
		/** @brief Unique server ID */
		static Uuid UniqueServerID;
		/** @brief Player display name */
		static String PlayerName;
		/**
		 * @brief Player character recolor as 4 packed bytes (one per fur section)
		 *
		 * Each byte is a sprite-palette gradient start; `0x00000000` means "use the original colors" (see @ref
		 * ContentResolver::BuildPlayerColorPalette).
		 */
		static std::uint32_t PlayerFurColor;
		/** @brief When the custom player character color is applied (see @ref PlayerColorMode) */
		static PlayerColorMode PlayerColors;
		/** @brief Whether Discord integration is enabled */
		static bool EnableDiscordIntegration;


		/** @brief Initializes preferences cache from a given application configuration */
		static void Initialize(AppConfiguration& config);
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
		/** @brief Resets all touch button layouts to their default positions and sizes */
		static void ResetTouchButtons();

	private:
		enum class BoolOptions : std::uint64_t {
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
			SwitchToNewWeapon = 0x8000000,
			EnableContinuousJump = 0x10000000,

			BlurEffects = 0x20000000,
			EnableTouchJoystick = 0x40000000,
			EnableTouchVibration = 0x80000000
		};

		DEATH_PRIVATE_ENUM_FLAGS(BoolOptions);

		static constexpr std::uint8_t FileVersion = 15;

		PreferencesCache(const PreferencesCache&) = delete;
		PreferencesCache& operator=(const PreferencesCache&) = delete;

		static String _configPath;
		static HashMap<String, EpisodeContinuationState> _episodeEnd;
		static HashMap<String, EpisodeContinuationStateWithLevel> _episodeContinue;

		static void TryLoadPreferredLanguage();
	};
}