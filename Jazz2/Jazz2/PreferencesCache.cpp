#include "PreferencesCache.h"
#include "ContentResolver.h"
#include "UI/ControlScheme.h"

#include "../nCine/IO/CompressionUtils.h"
#include "../nCine/IO/GrowableMemoryFile.h"
#include "../nCine/IO/MemoryFile.h"
#include "../nCine/IO/FileSystem.h"

using namespace Death::Containers::Literals;

namespace Jazz2
{
	UnlockableEpisodes PreferencesCache::UnlockedEpisodes = UnlockableEpisodes::None;
	RescaleMode PreferencesCache::ActiveRescaleMode = RescaleMode::None;
	bool PreferencesCache::EnableFullscreen = false;
	bool PreferencesCache::EnableVsync = true;
	bool PreferencesCache::ShowPerformanceMetrics = false;
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
		if (s->GetSize() > 18) {
			uint64_t signature = s->ReadValue<uint64_t>();
			uint8_t fileType = s->ReadValue<uint8_t>();
			uint8_t version = s->ReadValue<uint8_t>();
			if (signature == 0x2095A59FF0BFBBEF && fileType == ContentResolver::ConfigFile && version == FileVersion) {
				// Read compressed palette and mask
				int32_t compressedSize = s->ReadValue<int32_t>();
				int32_t uncompressedSize = s->ReadValue<int32_t>();
				std::unique_ptr<uint8_t[]> compressedBuffer = std::make_unique<uint8_t[]>(compressedSize);
				std::unique_ptr<uint8_t[]> uncompressedBuffer = std::make_unique<uint8_t[]>(uncompressedSize);
				s->Read(compressedBuffer.get(), compressedSize);

				auto result = CompressionUtils::Inflate(compressedBuffer.get(), compressedSize, uncompressedBuffer.get(), uncompressedSize);
				if (result == DecompressionResult::Success) {
					MemoryFile uc(uncompressedBuffer.get(), uncompressedSize);

					BoolOptions boolOptions = (BoolOptions)uc.ReadValue<uint64_t>();
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_IOS)
					EnableFullscreen = ((boolOptions & BoolOptions::EnableFullscreen) == BoolOptions::EnableFullscreen);
#endif
					ShowPerformanceMetrics = ((boolOptions & BoolOptions::ShowPerformanceMetrics) == BoolOptions::ShowPerformanceMetrics);
					ReduxMode = ((boolOptions & BoolOptions::ReduxMode) == BoolOptions::ReduxMode);
					EnableLedgeClimb = ((boolOptions & BoolOptions::EnableLedgeClimb) == BoolOptions::EnableLedgeClimb);
					EnableWeaponWheel = ((boolOptions & BoolOptions::EnableWeaponWheel) == BoolOptions::EnableWeaponWheel);
					EnableRgbLights = ((boolOptions & BoolOptions::EnableRgbLights) == BoolOptions::EnableRgbLights);
					TutorialCompleted = ((boolOptions & BoolOptions::TutorialCompleted) == BoolOptions::TutorialCompleted);

					UnlockedEpisodes = (UnlockableEpisodes)uc.ReadValue<uint32_t>();

					ActiveRescaleMode = (RescaleMode)uc.ReadValue<uint8_t>();

					MasterVolume = uc.ReadValue<float>();
					SfxVolume = uc.ReadValue<float>();
					MusicVolume = uc.ReadValue<float>();

					// Controls
					auto mappings = UI::ControlScheme::GetMappings();
					uint8_t controlMappingCount = uc.ReadValue<uint8_t>();
					for (int i = 0; i < controlMappingCount; i++) {
						KeySym key1 = (KeySym)uc.ReadValue<uint8_t>();
						KeySym key2 = (KeySym)uc.ReadValue<uint8_t>();
						uint8_t gamepadIndex = uc.ReadValue<uint8_t>();
						ButtonName gamepadButton = (ButtonName)uc.ReadValue<uint8_t>();

						if (i < mappings.size()) {
							auto& mapping = mappings[i];
							mapping.Key1 = key1;
							mapping.Key2 = key2;
							mapping.GamepadIndex = (gamepadIndex == 0xff ? -1 : gamepadIndex);
							mapping.GamepadButton = gamepadButton;
						}
					}

					// Episode End
					uint16_t episodeEndSize = uc.ReadValue<uint16_t>();
					uint16_t episodeEndCount = uc.ReadValue<uint16_t>();

					for (int i = 0; i < episodeEndCount; i++) {
						uint8_t nameLength = uc.ReadValue<uint8_t>();
						String episodeName = String(NoInit, nameLength);
						uc.Read(episodeName.data(), nameLength);

						EpisodeContinuationState state = { };
						if (episodeEndSize == sizeof(EpisodeContinuationState)) {
							uc.Read(&state, sizeof(EpisodeContinuationState));
						} else {
							// Struct has different size, so it's better to skip it
							uc.Seek(episodeEndSize, SeekOrigin::Current);
							state.Flags = EpisodeContinuationFlags::IsCompleted;
						}

						_episodeEnd.emplace(std::move(episodeName), std::move(state));
					}

					// Episode Continue
					uint16_t episodeContinueSize = uc.ReadValue<uint16_t>();
					uint16_t episodeContinueCount = uc.ReadValue<uint16_t>();

					for (int i = 0; i < episodeContinueCount; i++) {
						uint8_t nameLength = uc.ReadValue<uint8_t>();
						String episodeName = String(NoInit, nameLength);
						uc.Read(episodeName.data(), nameLength);

						if (episodeContinueSize == sizeof(EpisodeContinuationState)) {
							EpisodeContinuationStateWithLevel stateWithLevel = { };
							nameLength = uc.ReadValue<uint8_t>();
							stateWithLevel.LevelName = String(NoInit, nameLength);
							uc.Read(stateWithLevel.LevelName.data(), nameLength);

							uc.Read(&stateWithLevel.State, sizeof(EpisodeContinuationState));
							_episodeContinue.emplace(std::move(episodeName), std::move(stateWithLevel));
						} else {
							// Struct has different size, so it's better to skip it
							nameLength = uc.ReadValue<uint8_t>();
							uc.Seek(nameLength + episodeContinueSize, SeekOrigin::Current);
						}
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
				// V-Sync can be turned off only with command-line parameter
				EnableVsync = false;
			} else if (arg == "/no-rgb"_s) {
				EnableRgbLights = false;
			} else if (arg == "/no-rescale"_s) {
				ActiveRescaleMode = RescaleMode::None;
			} else if (arg == "/fps"_s) {
				ShowPerformanceMetrics = true;
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

		GrowableMemoryFile co(10 * 1024);

		BoolOptions boolOptions = BoolOptions::None;
		if (EnableFullscreen) boolOptions |= BoolOptions::EnableFullscreen;
		if (ShowPerformanceMetrics) boolOptions |= BoolOptions::ShowPerformanceMetrics;
		if (ReduxMode) boolOptions |= BoolOptions::ReduxMode;
		if (EnableLedgeClimb) boolOptions |= BoolOptions::EnableLedgeClimb;
		if (EnableWeaponWheel) boolOptions |= BoolOptions::EnableWeaponWheel;
		if (EnableRgbLights) boolOptions |= BoolOptions::EnableRgbLights;
		if (TutorialCompleted) boolOptions |= BoolOptions::TutorialCompleted;
		co.WriteValue<uint64_t>((uint64_t)boolOptions);

		co.WriteValue<uint32_t>((uint32_t)UnlockedEpisodes);

		co.WriteValue<uint8_t>((uint8_t)ActiveRescaleMode);

		co.WriteValue<float>(MasterVolume);
		co.WriteValue<float>(SfxVolume);
		co.WriteValue<float>(MusicVolume);

		// Controls
		auto mappings = UI::ControlScheme::GetMappings();
		co.WriteValue<uint8_t>((uint8_t)mappings.size());
		for (int i = 0; i < mappings.size(); i++) {
			auto& mapping = mappings[i];
			co.WriteValue<uint8_t>((uint8_t)mapping.Key1);
			co.WriteValue<uint8_t>((uint8_t)mapping.Key2);
			co.WriteValue<uint8_t>((uint8_t)(mapping.GamepadIndex == -1 ? 0xff : mapping.GamepadIndex));
			co.WriteValue<uint8_t>((uint8_t)mapping.GamepadButton);
		}

		// Episode End
		co.WriteValue<uint16_t>((uint16_t)sizeof(EpisodeContinuationState));
		co.WriteValue<uint16_t>((uint16_t)_episodeEnd.size());

		for (auto& pair : _episodeEnd) {
			String encryptedName = pair.first;
			for (char& c : encryptedName) {
				c = ~c;
			}

			co.WriteValue<uint8_t>((uint8_t)encryptedName.size());
			co.Write(encryptedName.data(), encryptedName.size());

			co.Write(&pair.second, sizeof(EpisodeContinuationState));
		}

		// Episode Continue
		co.WriteValue<uint16_t>((uint16_t)sizeof(EpisodeContinuationState));
		co.WriteValue<uint16_t>((uint16_t)_episodeContinue.size());

		for (auto& pair : _episodeContinue) {
			co.WriteValue<uint8_t>((uint8_t)pair.first.size());
			co.Write(pair.first.data(), pair.first.size());

			co.WriteValue<uint8_t>((uint8_t)pair.second.LevelName.size());
			co.Write(pair.second.LevelName.data(), pair.second.LevelName.size());

			co.Write(&pair.second.State, sizeof(EpisodeContinuationState));
		}

		// Compress content
		int32_t compressedSize = CompressionUtils::GetMaxDeflatedSize(co.GetSize());
		std::unique_ptr<uint8_t[]> compressedBuffer = std::make_unique<uint8_t[]>(compressedSize);
		compressedSize = CompressionUtils::Deflate(co.GetBuffer(), co.GetSize(), compressedBuffer.get(), compressedSize);
		ASSERT(compressedSize > 0);

		so->WriteValue<int32_t>(compressedSize);
		so->WriteValue<int32_t>(co.GetSize());
		so->Write(compressedBuffer.get(), compressedSize);

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