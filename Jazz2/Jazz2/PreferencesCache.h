#pragma once

#include "../nCine/AppConfiguration.h"

using namespace nCine;

namespace Jazz2
{
	enum class RescaleMode {
		None,
		_3xBrz,
		Monochrome
	};

	class PreferencesCache
	{
	public:
		// Graphics
		static RescaleMode ActiveRescaleMode;
		static bool EnableFullscreen;
		static bool EnableVsync;
		static bool ShowFps;

		// Gameplay
		static bool ReduxMode;
		static bool EnableLedgeClimb;
		static bool AllowCheats;
		static bool AllowCheatsWeapons;

		// Controls
		static bool EnableWeaponWheel;
		static bool EnableRgbLights;

		// Sounds
		static float MasterVolume;
		static float SfxVolume;
		static float MusicVolume;

		static void Initialize(const AppConfiguration& config);
		static void Save();

	private:
		/// Deleted copy constructor
		PreferencesCache(const PreferencesCache&) = delete;
		/// Deleted assignment operator
		PreferencesCache& operator=(const PreferencesCache&) = delete;
	};
}