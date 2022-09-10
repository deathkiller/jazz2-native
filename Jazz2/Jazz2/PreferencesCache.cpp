#include "PreferencesCache.h"
#include "ContentResolver.h"
#include "UI/ControlScheme.h"

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
	bool PreferencesCache::AllowCheatsUnlock = false;
	bool PreferencesCache::AllowCheatsWeapons = false;
	bool PreferencesCache::EnableWeaponWheel = false;
	bool PreferencesCache::EnableRgbLights = true;
	float PreferencesCache::MasterVolume = 0.8f;
	float PreferencesCache::SfxVolume = 0.8f;
	float PreferencesCache::MusicVolume = 0.4f;

	String PreferencesCache::_configPath;
	HashMap<String, EpisodeContinuationState> PreferencesCache::_episodeEnd;

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

		UI::ControlScheme::Initialize();

		// Try to read config file
		auto s = fs::Open(_configPath, FileAccessMode::Read);
		if (s->GetSize() > 10) {
			uint64_t signature = s->ReadValue<uint64_t>();
			uint8_t fileType = s->ReadValue<uint8_t>();
			uint8_t version = s->ReadValue<uint8_t>();
			if (signature == 0x2095A59FF0BFBBEF && fileType == ContentResolver::ConfigFile && version == FileVersion) {
				BoolOptions boolOptions = (BoolOptions)s->ReadValue<uint64_t>();
				EnableFullscreen = ((boolOptions & BoolOptions::EnableFullscreen) == BoolOptions::EnableFullscreen);
				EnableVsync = ((boolOptions & BoolOptions::EnableVsync) == BoolOptions::EnableVsync);
				ShowFps = ((boolOptions & BoolOptions::ShowFps) == BoolOptions::ShowFps);
				ReduxMode = ((boolOptions & BoolOptions::ReduxMode) == BoolOptions::ReduxMode);
				EnableLedgeClimb = ((boolOptions & BoolOptions::EnableLedgeClimb) == BoolOptions::EnableLedgeClimb);
				EnableWeaponWheel = ((boolOptions & BoolOptions::EnableWeaponWheel) == BoolOptions::EnableWeaponWheel);
				EnableRgbLights = ((boolOptions & BoolOptions::EnableRgbLights) == BoolOptions::EnableRgbLights);

				ActiveRescaleMode = (RescaleMode)s->ReadValue<uint8_t>();

				MasterVolume = s->ReadValue<float>();
				SfxVolume = s->ReadValue<float>();
				MusicVolume = s->ReadValue<float>();

				// Controls
				uint8_t controlMappingCount = s->ReadValue<uint8_t>();
				for (int i = 0; i < controlMappingCount; i++) {
					KeySym key1 = (KeySym)s->ReadValue<uint8_t>();
					KeySym key2 = (KeySym)s->ReadValue<uint8_t>();
					uint8_t gamepadIndex = s->ReadValue<uint8_t>();
					ButtonName gamepadButton = (ButtonName)s->ReadValue<uint8_t>();

					if (i < _countof(UI::ControlScheme::_mappings)) {
						auto& mapping = UI::ControlScheme::_mappings[i];
						mapping.Key1 = key1;
						mapping.Key2 = key2;
						mapping.GamepadIndex = (gamepadIndex == 0xff ? -1 : gamepadIndex);
						mapping.GamepadButton = gamepadButton;
					}
				}

				// Episode End
				uint16_t episodeContinuationSize = s->ReadValue<uint16_t>();
				uint16_t episodeContinuationCount = s->ReadValue<uint16_t>();

				for (int i = 0; i < episodeContinuationCount; i++) {
					uint8_t nameLength = s->ReadValue<uint8_t>();
					String episodeName = String(NoInit, nameLength);
					s->Read(episodeName.data(), nameLength);

					for (char& c : episodeName) {
						c = ~c;
					}

					EpisodeContinuationState state = { };
					if (episodeContinuationSize == sizeof(EpisodeContinuationState)) {
						s->Read(&state, sizeof(EpisodeContinuationState));
					} else {
						// Struct has different size, so it's better to skip it
						s->Seek(episodeContinuationSize, SeekOrigin::Current);
						state.Flags = EpisodeContinuationFlags::Completed;
					}

					_episodeEnd.emplace(std::move(episodeName), std::move(state));
				}
			}
		}

		// Override some settings by command-line arguments
		for (int i = 0; i < config.argc(); i++) {
			auto arg = config.argv(i);
			if (arg == "/cheats"_s) {
				AllowCheats = true;
			} else if (arg == "/cheats-unlock"_s) {
				AllowCheatsUnlock = true;
			} else if (arg == "/cheats-weapons"_s) {
				AllowCheatsWeapons = true;
			} else if (arg == "/fullscreen"_s) {
				EnableFullscreen = true;
			} else if (arg == "/no-vsync"_s) {
				EnableVsync = false;
			} else if (arg == "/no-rgb"_s) {
				EnableRgbLights = false;
			} else if (arg == "/no-rescale"_s) {
				ActiveRescaleMode = RescaleMode::None;
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
		auto so = fs::Open(_configPath, FileAccessMode::Write);
		if (!so->IsOpened()) {
			return;
		}

		so->WriteValue<uint64_t>(0x2095A59FF0BFBBEF);
		so->WriteValue<uint8_t>(ContentResolver::ConfigFile);
		so->WriteValue<uint8_t>(FileVersion);

		BoolOptions boolOptions = BoolOptions::None;
		if (EnableFullscreen) boolOptions |= BoolOptions::EnableFullscreen;
		if (EnableVsync) boolOptions |= BoolOptions::EnableVsync;
		if (ShowFps) boolOptions |= BoolOptions::ShowFps;
		if (ReduxMode) boolOptions |= BoolOptions::ReduxMode;
		if (EnableLedgeClimb) boolOptions |= BoolOptions::EnableLedgeClimb;
		if (EnableWeaponWheel) boolOptions |= BoolOptions::EnableWeaponWheel;
		if (EnableRgbLights) boolOptions |= BoolOptions::EnableRgbLights;
		so->WriteValue<uint64_t>((uint64_t)boolOptions);

		so->WriteValue<uint8_t>((uint8_t)ActiveRescaleMode);

		so->WriteValue<float>(MasterVolume);
		so->WriteValue<float>(SfxVolume);
		so->WriteValue<float>(MusicVolume);

		// Controls
		so->WriteValue<uint8_t>((uint8_t)_countof(UI::ControlScheme::_mappings));
		for (int i = 0; i < _countof(UI::ControlScheme::_mappings); i++) {
			auto& mapping = UI::ControlScheme::_mappings[i];
			so->WriteValue<uint8_t>((uint8_t)mapping.Key1);
			so->WriteValue<uint8_t>((uint8_t)mapping.Key2);
			so->WriteValue<uint8_t>((uint8_t)(mapping.GamepadIndex == -1 ? 0xff : mapping.GamepadIndex));
			so->WriteValue<uint8_t>((uint8_t)mapping.GamepadButton);
		}

		// Episode End
		so->WriteValue<uint16_t>((uint16_t)sizeof(EpisodeContinuationState));
		so->WriteValue<uint16_t>((uint16_t)_episodeEnd.size());

		for (auto& pair : _episodeEnd) {
			String episodeNameEncrypted = pair.first;
			for (char& c : episodeNameEncrypted) {
				c = ~c;
			}

			so->WriteValue<uint8_t>((uint8_t)episodeNameEncrypted.size());
			so->Write(episodeNameEncrypted.data(), episodeNameEncrypted.size());

			so->Write(&pair.second, sizeof(EpisodeContinuationState));
		}

#if defined(DEATH_TARGET_EMSCRIPTEN)
		fs::SyncToPersistent();
#endif
	}

	EpisodeContinuationState* PreferencesCache::GetEpisodeEnd(const StringView& episodeName, bool createIfNotFound)
	{
		auto it = _episodeEnd.find(String::nullTerminatedView(episodeName));
		if (it == _episodeEnd.end()) {
			if (createIfNotFound) {
				return &_episodeEnd.emplace(String(episodeName), EpisodeContinuationState()).first->second;
			} else {
				return nullptr;
			}
		}

		return &it->second;
	}
}