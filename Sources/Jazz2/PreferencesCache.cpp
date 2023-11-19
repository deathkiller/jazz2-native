#include "PreferencesCache.h"
#include "ContentResolver.h"
#include "LevelHandler.h"
#include "UI/ControlScheme.h"

#include "../nCine/IO/CompressionUtils.h"

#include <Environment.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2
{
#if defined(WITH_MULTIPLAYER)
	String PreferencesCache::InitialState;
#endif
	UnlockableEpisodes PreferencesCache::UnlockedEpisodes = UnlockableEpisodes::None;
	RescaleMode PreferencesCache::ActiveRescaleMode = RescaleMode::None;
	bool PreferencesCache::EnableFullscreen = false;
	std::int32_t PreferencesCache::MaxFps = PreferencesCache::UseVsync;
	bool PreferencesCache::ShowPerformanceMetrics = false;
	bool PreferencesCache::KeepAspectRatioInCinematics = false;
	bool PreferencesCache::ShowPlayerTrails = true;
	bool PreferencesCache::LowGraphicsQuality = false;
	bool PreferencesCache::UnalignedViewport = true;
	bool PreferencesCache::EnableReforged = true;
	bool PreferencesCache::EnableLedgeClimb = true;
	WeaponWheelStyle PreferencesCache::WeaponWheel = WeaponWheelStyle::Enabled;
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
	bool PreferencesCache::EnableRgbLights = true;
#else
	bool PreferencesCache::EnableRgbLights = false;
#endif
	bool PreferencesCache::AllowUnsignedScripts = true;
#if defined(DEATH_TARGET_ANDROID)
	bool PreferencesCache::UseNativeBackButton = true;
#else
	bool PreferencesCache::UseNativeBackButton = false;
#endif
	bool PreferencesCache::EnableDiscordIntegration = false;
	bool PreferencesCache::TutorialCompleted = false;
	bool PreferencesCache::AllowCheats = false;
	bool PreferencesCache::AllowCheatsLives = false;
	bool PreferencesCache::AllowCheatsUnlock = false;
	Vector2f PreferencesCache::TouchLeftPadding;
	Vector2f PreferencesCache::TouchRightPadding;
	char PreferencesCache::Language[6] { };
	bool PreferencesCache::BypassCache = false;
	float PreferencesCache::MasterVolume = 0.8f;
	float PreferencesCache::SfxVolume = 0.8f;
	float PreferencesCache::MusicVolume = 0.4f;

	String PreferencesCache::_configPath;
	HashMap<String, EpisodeContinuationState> PreferencesCache::_episodeEnd;
	HashMap<String, EpisodeContinuationStateWithLevel> PreferencesCache::_episodeContinue;

	void PreferencesCache::Initialize(const AppConfiguration& config)
	{
		bool resetConfig = false;

#if defined(DEATH_TARGET_EMSCRIPTEN)
		fs::MountAsPersistent("/Persistent"_s);
		_configPath = "/Persistent/Jazz2.config"_s;

		for (int32_t i = 0; i < config.argc(); i++) {
			auto arg = config.argv(i);
			if (arg == "/reset-config"_s) {
				resetConfig = true;
			}
		}
#else
		_configPath = "Jazz2.config"_s;
		bool overrideConfigPath = false;

#	if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
		for (int32_t i = 0; i < config.argc(); i++) {
			auto arg = config.argv(i);
			if (arg == "/config"_s) {
				if (i + 1 < config.argc()) {
					_configPath = config.argv(i + 1);
					overrideConfigPath = true;
					i++;
				}
			} else if (arg == "/reset-config"_s) {
				resetConfig = true;
			}
		}
#	endif

		// If config path is not overriden and portable config doesn't exist, use common path for current user
		if (!overrideConfigPath && !fs::IsReadableFile(_configPath)) {
#	if defined(DEATH_TARGET_SWITCH)
			// Save config file next to `Source` directory
			auto& resolver = ContentResolver::Get();
			_configPath = fs::CombinePath(fs::GetDirectoryName(resolver.GetSourcePath()), "Jazz2.config"_s);
#	elif defined(DEATH_TARGET_UNIX) && defined(NCINE_PACKAGED_CONTENT_PATH)
			_configPath = fs::CombinePath(fs::GetSavePath(NCINE_LINUX_PACKAGE), "Jazz2.config"_s);
#	else
			_configPath = fs::CombinePath(fs::GetSavePath("Jazz² Resurrection"_s), "Jazz2.config"_s);
#	endif

#	if defined(DEATH_TARGET_ANDROID)
			// Save config file to external path if possible
			auto& resolver = ContentResolver::Get();
			auto externalConfigPath = fs::CombinePath(fs::GetDirectoryName(resolver.GetSourcePath()), "Jazz2.config"_s);
			if (!fs::IsReadableFile(_configPath) || fs::IsReadableFile(externalConfigPath)) {
				_configPath = externalConfigPath;
			}
#	elif defined(DEATH_TARGET_WINDOWS_RT)
			// Save config file next to `Source` directory (e.g., on external drive) if possible
			auto& resolver = ContentResolver::Get();
			auto localConfigPath = fs::CombinePath(fs::GetDirectoryName(resolver.GetSourcePath()), "Jazz2.config"_s);
			if (_configPath != localConfigPath) {
				auto configFileWritable = fs::Open(localConfigPath, FileAccessMode::Read | FileAccessMode::Write);
				if (configFileWritable->IsValid()) {
					configFileWritable->Close();
					_configPath = localConfigPath;
				}
			}
#	endif
		}
#endif

		UI::ControlScheme::Reset();

		// Try to read config file
		if (!resetConfig) {
			auto s = fs::Open(_configPath, FileAccessMode::Read);
			if (s->GetSize() > 18) {
				uint64_t signature = s->ReadValue<uint64_t>();
				uint8_t fileType = s->ReadValue<uint8_t>();
				uint8_t version = s->ReadValue<uint8_t>();
				if (signature == 0x2095A59FF0BFBBEF && fileType == ContentResolver::ConfigFile && version <= FileVersion) {
					// Read compressed palette and mask
					int32_t compressedSize = s->ReadValue<int32_t>();
					int32_t uncompressedSize = s->ReadValue<int32_t>();
					std::unique_ptr<uint8_t[]> compressedBuffer = std::make_unique<uint8_t[]>(compressedSize);
					std::unique_ptr<uint8_t[]> uncompressedBuffer = std::make_unique<uint8_t[]>(uncompressedSize);
					s->Read(compressedBuffer.get(), compressedSize);

					auto result = CompressionUtils::Inflate(compressedBuffer.get(), compressedSize, uncompressedBuffer.get(), uncompressedSize);
					if (result == DecompressionResult::Success) {
						MemoryStream uc(uncompressedBuffer.get(), uncompressedSize);

						BoolOptions boolOptions = (BoolOptions)uc.ReadValue<uint64_t>();

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
						EnableFullscreen = ((boolOptions & BoolOptions::EnableFullscreen) == BoolOptions::EnableFullscreen);
#endif
						ShowPerformanceMetrics = ((boolOptions & BoolOptions::ShowPerformanceMetrics) == BoolOptions::ShowPerformanceMetrics);
						KeepAspectRatioInCinematics = ((boolOptions & BoolOptions::KeepAspectRatioInCinematics) == BoolOptions::KeepAspectRatioInCinematics);
						ShowPlayerTrails = ((boolOptions & BoolOptions::ShowPlayerTrails) == BoolOptions::ShowPlayerTrails);
						LowGraphicsQuality = ((boolOptions & BoolOptions::LowGraphicsQuality) == BoolOptions::LowGraphicsQuality);
						UnalignedViewport = ((boolOptions & BoolOptions::UnalignedViewport) == BoolOptions::UnalignedViewport);
						EnableReforged = ((boolOptions & BoolOptions::EnableReforged) == BoolOptions::EnableReforged);
						EnableLedgeClimb = ((boolOptions & BoolOptions::EnableLedgeClimb) == BoolOptions::EnableLedgeClimb);
						WeaponWheel = ((boolOptions & BoolOptions::EnableWeaponWheel) == BoolOptions::EnableWeaponWheel ? WeaponWheelStyle::Enabled : WeaponWheelStyle::Disabled);
						EnableRgbLights = ((boolOptions & BoolOptions::EnableRgbLights) == BoolOptions::EnableRgbLights);
						AllowUnsignedScripts = ((boolOptions & BoolOptions::AllowUnsignedScripts) == BoolOptions::AllowUnsignedScripts);
#if defined(DEATH_TARGET_ANDROID)
						UseNativeBackButton = ((boolOptions & BoolOptions::UseNativeBackButton) == BoolOptions::UseNativeBackButton);
#endif
						EnableDiscordIntegration = ((boolOptions & BoolOptions::EnableDiscordIntegration) == BoolOptions::EnableDiscordIntegration);
						TutorialCompleted = ((boolOptions & BoolOptions::TutorialCompleted) == BoolOptions::TutorialCompleted);

						if (WeaponWheel != WeaponWheelStyle::Disabled && (boolOptions & BoolOptions::ShowWeaponWheelAmmoCount) == BoolOptions::ShowWeaponWheelAmmoCount) {
							WeaponWheel = WeaponWheelStyle::EnabledWithAmmoCount;
						}

						if ((boolOptions & BoolOptions::SetLanguage) == BoolOptions::SetLanguage) {
							uc.Read(Language, sizeof(Language));
						} else {
							std::memset(Language, 0, sizeof(Language));
						}

						// Bitmask of unlocked episodes, used only if compiled with SHAREWARE_DEMO_ONLY
						UnlockedEpisodes = (UnlockableEpisodes)uc.ReadValue<uint32_t>();

						ActiveRescaleMode = (RescaleMode)uc.ReadValue<uint8_t>();

						MasterVolume = uc.ReadValue<uint8_t>() / 255.0f;
						SfxVolume = uc.ReadValue<uint8_t>() / 255.0f;
						MusicVolume = uc.ReadValue<uint8_t>() / 255.0f;

						TouchLeftPadding.X = std::round(uc.ReadValue<int8_t>() / (TouchPaddingMultiplier * INT8_MAX));
						TouchLeftPadding.Y = std::round(uc.ReadValue<int8_t>() / (TouchPaddingMultiplier * INT8_MAX));
						TouchRightPadding.X = std::round(uc.ReadValue<int8_t>() / (TouchPaddingMultiplier * INT8_MAX));
						TouchRightPadding.Y = std::round(uc.ReadValue<int8_t>() / (TouchPaddingMultiplier * INT8_MAX));

						// Controls
						auto mappings = UI::ControlScheme::GetMappings();
						uint8_t controlMappingCount = uc.ReadValue<uint8_t>();
						for (uint32_t i = 0; i < controlMappingCount; i++) {
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
						// Reset primary Menu action, because it's hardcoded
						mappings[(int32_t)PlayerActions::Menu].Key1 = KeySym::ESCAPE;

						// Episode End
						uint16_t episodeEndSize = uc.ReadValue<uint16_t>();
						uint16_t episodeEndCount = uc.ReadValue<uint16_t>();

						for (uint32_t i = 0; i < episodeEndCount; i++) {
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

						for (uint32_t i = 0; i < episodeContinueCount; i++) {
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
			} else {
				TryLoadPreferredLanguage();

#if !defined(DEATH_TARGET_EMSCRIPTEN)
				// Create "Source" directory on the first launch
				auto& resolver = ContentResolver::Get();
				fs::CreateDirectories(resolver.GetSourcePath());
#endif
			}
		}

		// Override some settings by command-line arguments
		for (int32_t i = 0; i < config.argc(); i++) {
			auto arg = config.argv(i);
			if (arg == "/bypass-cache"_s) {
				BypassCache = true;
			} else if (arg == "/cheats"_s) {
				AllowCheats = true;
			} else if (arg == "/cheats-lives"_s) {
				AllowCheatsLives = true;
			} else if (arg == "/cheats-unlock"_s) {
				AllowCheatsUnlock = true;
			} else if (arg == "/fullscreen"_s) {
				EnableFullscreen = true;
			} else if (arg == "/windowed"_s) {
				EnableFullscreen = false;
			} else if (arg == "/no-vsync"_s) {
				// V-Sync can be turned off only with command-line parameter
				if (MaxFps == UseVsync) {
					MaxFps = UnlimitedFps;
				}
			} else if (arg.hasPrefix("/max-fps:"_s)) {
				// Max. FPS can be set only with command-line parameter
				char* end;
				unsigned long paramValue = strtoul(arg.exceptPrefix("/max-fps:"_s).data(), &end, 10);
				if (paramValue > 0) {
					MaxFps = std::max(paramValue, 30ul);
				}
			} else if (arg == "/no-rgb"_s) {
				EnableRgbLights = false;
			} else if (arg == "/no-rescale"_s) {
				ActiveRescaleMode = RescaleMode::None;
			} else if (arg == "/mute"_s) {
				MasterVolume = 0.0f;
			}
#if defined(WITH_MULTIPLAYER)
			else if (InitialState.empty() && (arg == "/server"_s || arg.hasPrefix("/connect:"_s))) {
				InitialState = arg;
			}
#endif
		}
	}

	void PreferencesCache::Save()
	{
		fs::CreateDirectories(fs::GetDirectoryName(_configPath));

		auto so = fs::Open(_configPath, FileAccessMode::Write);
		if (!so->IsValid()) {
			return;
		}

		so->WriteValue<uint64_t>(0x2095A59FF0BFBBEF);
		so->WriteValue<uint8_t>(ContentResolver::ConfigFile);
		so->WriteValue<uint8_t>(FileVersion);

		MemoryStream co(10 * 1024);

		BoolOptions boolOptions = BoolOptions::None;
		if (EnableFullscreen) boolOptions |= BoolOptions::EnableFullscreen;
		if (ShowPerformanceMetrics) boolOptions |= BoolOptions::ShowPerformanceMetrics;
		if (KeepAspectRatioInCinematics) boolOptions |= BoolOptions::KeepAspectRatioInCinematics;
		if (ShowPlayerTrails) boolOptions |= BoolOptions::ShowPlayerTrails;
		if (LowGraphicsQuality) boolOptions |= BoolOptions::LowGraphicsQuality;
		if (UnalignedViewport) boolOptions |= BoolOptions::UnalignedViewport;
		if (EnableReforged) boolOptions |= BoolOptions::EnableReforged;
		if (EnableLedgeClimb) boolOptions |= BoolOptions::EnableLedgeClimb;
		if (WeaponWheel != WeaponWheelStyle::Disabled) boolOptions |= BoolOptions::EnableWeaponWheel;
		if (WeaponWheel == WeaponWheelStyle::EnabledWithAmmoCount) boolOptions |= BoolOptions::ShowWeaponWheelAmmoCount;
		if (EnableRgbLights) boolOptions |= BoolOptions::EnableRgbLights;
		if (AllowUnsignedScripts) boolOptions |= BoolOptions::AllowUnsignedScripts;
		if (UseNativeBackButton) boolOptions |= BoolOptions::UseNativeBackButton;
		if (EnableDiscordIntegration) boolOptions |= BoolOptions::EnableDiscordIntegration;
		if (TutorialCompleted) boolOptions |= BoolOptions::TutorialCompleted;
		if (Language[0] != '\0') boolOptions |= BoolOptions::SetLanguage;
		co.WriteValue<uint64_t>((uint64_t)boolOptions);

		if (Language[0] != '\0') {
			co.Write(Language, sizeof(Language));
		}

		// Bitmask of unlocked episodes, used only if compiled with SHAREWARE_DEMO_ONLY
		co.WriteValue<uint32_t>((uint32_t)UnlockedEpisodes);

		co.WriteValue<uint8_t>((uint8_t)ActiveRescaleMode);

		co.WriteValue<uint8_t>((uint8_t)(MasterVolume * 255.0f));
		co.WriteValue<uint8_t>((uint8_t)(SfxVolume * 255.0f));
		co.WriteValue<uint8_t>((uint8_t)(MusicVolume * 255.0f));

		co.WriteValue<int8_t>((int8_t)(TouchLeftPadding.X * INT8_MAX * TouchPaddingMultiplier));
		co.WriteValue<int8_t>((int8_t)(TouchLeftPadding.Y * INT8_MAX * TouchPaddingMultiplier));
		co.WriteValue<int8_t>((int8_t)(TouchRightPadding.X * INT8_MAX * TouchPaddingMultiplier));
		co.WriteValue<int8_t>((int8_t)(TouchRightPadding.Y * INT8_MAX * TouchPaddingMultiplier));

		// Controls
		auto mappings = UI::ControlScheme::GetMappings();
		co.WriteValue<uint8_t>((uint8_t)mappings.size());
		for (std::size_t i = 0; i < mappings.size(); i++) {
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
			co.WriteValue<uint8_t>((uint8_t)pair.first.size());
			co.Write(pair.first.data(), (uint32_t)pair.first.size());

			co.Write(&pair.second, sizeof(EpisodeContinuationState));
		}

		// Episode Continue
		co.WriteValue<uint16_t>((uint16_t)sizeof(EpisodeContinuationState));
		co.WriteValue<uint16_t>((uint16_t)_episodeContinue.size());

		for (auto& pair : _episodeContinue) {
			co.WriteValue<uint8_t>((uint8_t)pair.first.size());
			co.Write(pair.first.data(), (uint32_t)pair.first.size());

			co.WriteValue<uint8_t>((uint8_t)pair.second.LevelName.size());
			co.Write(pair.second.LevelName.data(), (uint32_t)pair.second.LevelName.size());

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

		so->Close();

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

	void PreferencesCache::TryLoadPreferredLanguage()
	{
		auto& i18n = I18n::Get();
		auto& resolver = ContentResolver::Get();

		Array<String> languages = I18n::GetPreferredLanguages();
		for (String language : languages) {
			if (!language.empty() && language.size() < sizeof(PreferencesCache::Language)) {
				if (language == "en"_s) {
					break;
				}
				if (i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, language + ".mo"_s })) ||
					i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, language + ".mo"_s }))) {
					std::memcpy(PreferencesCache::Language, language.data(), language.size());
					std::memset(PreferencesCache::Language + language.size(), 0, sizeof(PreferencesCache::Language) - language.size());
					break;
				}
			}

			StringView baseLanguage = I18n::TryRemoveLanguageSpecifiers(language);
			if (baseLanguage != language && !baseLanguage.empty() && baseLanguage.size() < sizeof(PreferencesCache::Language)) {
				if (baseLanguage == "en"_s) {
					break;
				}
				if (i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, baseLanguage + ".mo"_s })) ||
					i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, baseLanguage + ".mo"_s }))) {
					std::memcpy(PreferencesCache::Language, baseLanguage.data(), baseLanguage.size());
					std::memset(PreferencesCache::Language + baseLanguage.size(), 0, sizeof(PreferencesCache::Language) - baseLanguage.size());
					break;
				}
			}
		}
	}
}