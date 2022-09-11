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
	bool PreferencesCache::TutorialCompleted = false;
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
	HashMap<String, EpisodeContinuationStateWithLevel> PreferencesCache::_episodeContinue;

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

		UI::ControlScheme::Reset();

		// Try to read config file
		auto s = fs::Open(_configPath, FileAccessMode::Read);
		if (s->GetSize() > 32) {
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
				TutorialCompleted = ((boolOptions & BoolOptions::TutorialCompleted) == BoolOptions::TutorialCompleted);
				EnableWeaponWheel = ((boolOptions & BoolOptions::EnableWeaponWheel) == BoolOptions::EnableWeaponWheel);
				EnableRgbLights = ((boolOptions & BoolOptions::EnableRgbLights) == BoolOptions::EnableRgbLights);

				ActiveRescaleMode = (RescaleMode)s->ReadValue<uint8_t>();

				MasterVolume = s->ReadValue<float>();
				SfxVolume = s->ReadValue<float>();
				MusicVolume = s->ReadValue<float>();

				// Controls
				auto mappings = UI::ControlScheme::GetMappings();
				uint8_t controlMappingCount = s->ReadValue<uint8_t>();
				for (int i = 0; i < controlMappingCount; i++) {
					KeySym key1 = (KeySym)s->ReadValue<uint8_t>();
					KeySym key2 = (KeySym)s->ReadValue<uint8_t>();
					uint8_t gamepadIndex = s->ReadValue<uint8_t>();
					ButtonName gamepadButton = (ButtonName)s->ReadValue<uint8_t>();


					if (i < mappings.size()) {
						auto& mapping = mappings[i];
						mapping.Key1 = key1;
						mapping.Key2 = key2;
						mapping.GamepadIndex = (gamepadIndex == 0xff ? -1 : gamepadIndex);
						mapping.GamepadButton = gamepadButton;
					}
				}

				// Episode End
				uint16_t episodeEndSize = s->ReadValue<uint16_t>();
				uint16_t episodeEndCount = s->ReadValue<uint16_t>();

				for (int i = 0; i < episodeEndCount; i++) {
					uint8_t nameLength = s->ReadValue<uint8_t>();
					String episodeName = String(NoInit, nameLength);
					s->Read(episodeName.data(), nameLength);

					for (char& c : episodeName) {
						c = ~c;
					}

					EpisodeContinuationState state = { };
					if (episodeEndSize == sizeof(EpisodeContinuationState)) {
						s->Read(&state, sizeof(EpisodeContinuationState));
					} else {
						// Struct has different size, so it's better to skip it
						s->Seek(episodeEndSize, SeekOrigin::Current);
						state.Flags = EpisodeContinuationFlags::IsCompleted;
					}

					_episodeEnd.emplace(std::move(episodeName), std::move(state));
				}

				// Episode Continue
				uint16_t episodeContinueSize = s->ReadValue<uint16_t>();
				uint16_t episodeContinueCount = s->ReadValue<uint16_t>();

				for (int i = 0; i < episodeContinueCount; i++) {
					uint8_t nameLength = s->ReadValue<uint8_t>();
					String episodeName = String(NoInit, nameLength);
					s->Read(episodeName.data(), nameLength);

					for (char& c : episodeName) {
						c = ~c;
					}

					if (episodeContinueSize == sizeof(EpisodeContinuationState)) {
						EpisodeContinuationStateWithLevel stateWithLevel = { };
						nameLength = s->ReadValue<uint8_t>();
						stateWithLevel.LevelName = String(NoInit, nameLength);
						s->Read(stateWithLevel.LevelName.data(), nameLength);

						for (char& c : stateWithLevel.LevelName) {
							c = ~c;
						}

						s->Read(&stateWithLevel.State, sizeof(EpisodeContinuationState));
						_episodeContinue.emplace(std::move(episodeName), std::move(stateWithLevel));
					} else {
						// Struct has different size, so it's better to skip it
						nameLength = s->ReadValue<uint8_t>();
						s->Seek(nameLength + episodeContinueSize, SeekOrigin::Current);
					}
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
		fs::CreateDirectories(fs::GetDirectoryName(_configPath));

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
		if (TutorialCompleted) boolOptions |= BoolOptions::TutorialCompleted;
		if (EnableWeaponWheel) boolOptions |= BoolOptions::EnableWeaponWheel;
		if (EnableRgbLights) boolOptions |= BoolOptions::EnableRgbLights;
		so->WriteValue<uint64_t>((uint64_t)boolOptions);

		so->WriteValue<uint8_t>((uint8_t)ActiveRescaleMode);

		so->WriteValue<float>(MasterVolume);
		so->WriteValue<float>(SfxVolume);
		so->WriteValue<float>(MusicVolume);

		// Controls
		auto mappings = UI::ControlScheme::GetMappings();
		so->WriteValue<uint8_t>((uint8_t)mappings.size());
		for (int i = 0; i < mappings.size(); i++) {
			auto& mapping = mappings[i];
			so->WriteValue<uint8_t>((uint8_t)mapping.Key1);
			so->WriteValue<uint8_t>((uint8_t)mapping.Key2);
			so->WriteValue<uint8_t>((uint8_t)(mapping.GamepadIndex == -1 ? 0xff : mapping.GamepadIndex));
			so->WriteValue<uint8_t>((uint8_t)mapping.GamepadButton);
		}

		// Episode End
		so->WriteValue<uint16_t>((uint16_t)sizeof(EpisodeContinuationState));
		so->WriteValue<uint16_t>((uint16_t)_episodeEnd.size());

		for (auto& pair : _episodeEnd) {
			String encryptedName = pair.first;
			for (char& c : encryptedName) {
				c = ~c;
			}

			so->WriteValue<uint8_t>((uint8_t)encryptedName.size());
			so->Write(encryptedName.data(), encryptedName.size());

			so->Write(&pair.second, sizeof(EpisodeContinuationState));
		}

		// Episode Continue
		so->WriteValue<uint16_t>((uint16_t)sizeof(EpisodeContinuationState));
		so->WriteValue<uint16_t>((uint16_t)_episodeContinue.size());

		for (auto& pair : _episodeContinue) {
			String encryptedName = pair.first;
			for (char& c : encryptedName) {
				c = ~c;
			}

			so->WriteValue<uint8_t>((uint8_t)encryptedName.size());
			so->Write(encryptedName.data(), encryptedName.size());

			encryptedName = pair.second.LevelName;
			for (char& c : encryptedName) {
				c = ~c;
			}

			so->WriteValue<uint8_t>((uint8_t)encryptedName.size());
			so->Write(encryptedName.data(), encryptedName.size());

			so->Write(&pair.second.State, sizeof(EpisodeContinuationState));
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

	EpisodeContinuationStateWithLevel* PreferencesCache::GetEpisodeContinue(const StringView& episodeName, bool createIfNotFound)
	{
		auto it = _episodeContinue.find(String::nullTerminatedView(episodeName));
		if (it == _episodeContinue.end()) {
			if (createIfNotFound) {
				return &_episodeContinue.emplace(String(episodeName), EpisodeContinuationStateWithLevel()).first->second;
			} else {
				return nullptr;
			}
		}

		return &it->second;
	}

	void PreferencesCache::RemoveEpisodeContinue(const StringView& episodeName)
	{
		_episodeContinue.erase(String::nullTerminatedView(episodeName));
	}
}