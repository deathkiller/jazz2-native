#include "PreferencesCache.h"

#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace Jazz2
{
	RescaleMode PreferencesCache::ActiveRescaleMode = RescaleMode::None;
	bool PreferencesCache::EnableFullscreen = false;
	bool PreferencesCache::EnableVsync = true;
	bool PreferencesCache::ReduxMode = true;
	bool PreferencesCache::EnableLedgeClimb = true;
	bool PreferencesCache::AllowCheats = false;
	bool PreferencesCache::EnableWeaponWheel = false;
	bool PreferencesCache::EnableRgbLights = true;
	float PreferencesCache::MasterVolume = 0.8f;
	float PreferencesCache::SfxVolume = 0.8f;
	float PreferencesCache::MusicVolume = 0.4f;

	void PreferencesCache::Initialize(const AppConfiguration& config)
	{
		// TODO: Load preferences from file

		// Override some settings by command-line arguments
		for (int i = 0; i < config.argc(); i++) {
			auto arg = config.argv(i);
			if (arg == "/cheats"_s) {
				AllowCheats = true;
			} else if (arg == "/fullscreen"_s) {
				EnableFullscreen = true;
			} else if (arg == "/no-vsync"_s) {
				EnableVsync = false;
			} else if (arg == "/no-rgb"_s) {
				EnableRgbLights = false;
			} else if (arg == "/3xbrz"_s) {
				ActiveRescaleMode = RescaleMode::_3xBrz;
			} else if (arg == "/monochrome"_s) {
				ActiveRescaleMode = RescaleMode::Monochrome;
			} else if (arg == "/mute"_s) {
				MasterVolume = 0.0f;
			}
		}
	}

	void PreferencesCache::Save()
	{
		// TODO
	}
}