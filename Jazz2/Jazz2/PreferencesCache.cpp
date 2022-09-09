#include "PreferencesCache.h"

#include "../nCine/IO/FileSystem.h"

using namespace Death::Containers::Literals;

namespace Jazz2
{
	RescaleMode PreferencesCache::ActiveRescaleMode = RescaleMode::None;
	bool PreferencesCache::EnableFullscreen = false;
	bool PreferencesCache::EnableVsync = true;
	bool PreferencesCache::ShowFps = false;
	bool PreferencesCache::ReduxMode = true;
	bool PreferencesCache::EnableLedgeClimb = true;
	bool PreferencesCache::AllowCheats = false;
	bool PreferencesCache::AllowCheatsWeapons = false;
	bool PreferencesCache::EnableWeaponWheel = false;
	bool PreferencesCache::EnableRgbLights = true;
	float PreferencesCache::MasterVolume = 0.8f;
	float PreferencesCache::SfxVolume = 0.8f;
	float PreferencesCache::MusicVolume = 0.4f;

	String PreferencesCache::_configPath;

	void PreferencesCache::Initialize(const AppConfiguration& config)
	{
#if defined(DEATH_TARGET_EMSCRIPTEN)
		fs::MountAsPersistent("/Persistent"_s);
		_configPath = "/Persistent/Jazz2.config"_s;
#else
		_configPath = "Jazz2.config"_s;
		bool overrideConfigPath = false;

#	if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS)
		for (int i = 0; i < config.argc() - 1; i++) {
			auto arg = config.argv(i);
			if (arg == "/config"_s) {
				_configPath = config.argv(i + 1);
				overrideConfigPath = true;
				i++;
			}
		}
#	endif

		// If config path is not overriden and portable config doesn't exist, use common path for current user
		if (!overrideConfigPath && !fs::IsReadableFile(_configPath)) {
			_configPath = fs::JoinPath(fs::GetSavePath("Jazz² Resurrection"_s), "Jazz2.config"_s);
		}
#endif

		// TODO: Read config from file

		// Override some settings by command-line arguments
		for (int i = 0; i < config.argc(); i++) {
			auto arg = config.argv(i);
			if (arg == "/cheats"_s) {
				AllowCheats = true;
			} else if (arg == "/cheats-weapons"_s) {
				AllowCheatsWeapons = true;
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
			} else if (arg == "/fps"_s) {
				ShowFps = true;
			} else if (arg == "/mute"_s) {
				MasterVolume = 0.0f;
			}
		}
	}

	void PreferencesCache::Save()
	{
		// TODO: Save to file

#if defined(DEATH_TARGET_EMSCRIPTEN)
		fs::SyncToPersistent();
#endif
	}
}