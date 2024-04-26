#include "PreferencesCache.h"
#include "ContentResolver.h"
#include "LevelHandler.h"
#include "UI/ControlScheme.h"

#include <Containers/StringConcatenable.h>
#include <Environment.h>
#include <IO/DeflateStream.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2
{
	bool PreferencesCache::FirstRun = false;
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
	bool PreferencesCache::LowWaterQuality = false;
	bool PreferencesCache::UnalignedViewport = false;
	bool PreferencesCache::EnableReforgedGameplay = true;
	bool PreferencesCache::EnableReforgedHUD = true;
	bool PreferencesCache::EnableReforgedMainMenu = true;
#if defined(DEATH_TARGET_ANDROID)
	// Used to swap Android activity icons on exit/suspend
	bool PreferencesCache::EnableReforgedMainMenuInitial = true;
#endif
	bool PreferencesCache::EnableLedgeClimb = true;
	WeaponWheelStyle PreferencesCache::WeaponWheel = WeaponWheelStyle::Enabled;
#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_WINDOWS_RT)
	bool PreferencesCache::EnableRgbLights = true;
#else
	bool PreferencesCache::EnableRgbLights = false;
#endif
	bool PreferencesCache::AllowUnsignedScripts = true;
	bool PreferencesCache::ToggleRunAction = false;
	GamepadType PreferencesCache::GamepadButtonLabels = GamepadType::Xbox;
#if defined(DEATH_TARGET_ANDROID)
	bool PreferencesCache::UseNativeBackButton = true;
#else
	bool PreferencesCache::UseNativeBackButton = false;
#endif
	bool PreferencesCache::EnableDiscordIntegration = false;
	bool PreferencesCache::TutorialCompleted = false;
	bool PreferencesCache::ResumeOnStart = false;
	bool PreferencesCache::AllowCheats = false;
	bool PreferencesCache::AllowCheatsLives = false;
	bool PreferencesCache::AllowCheatsUnlock = false;
	Vector2f PreferencesCache::TouchLeftPadding;
	Vector2f PreferencesCache::TouchRightPadding;
	char PreferencesCache::Language[6] { };
	bool PreferencesCache::BypassCache = false;
	float PreferencesCache::MasterVolume = 0.7f;
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
		for (std::int32_t i = 0; i < config.argc(); i++) {
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
					if (version == 1) {
						// Version 1 included compressedSize and decompressedSize, it's not needed anymore
						/*int32_t compressedSize =*/ s->ReadValue<int32_t>();
						/*int32_t uncompressedSize =*/ s->ReadValue<int32_t>();
					}

					DeflateStream uc(*s);

					BoolOptions boolOptions = (BoolOptions)uc.ReadValue<uint64_t>();

#if !defined(DEATH_TARGET_EMSCRIPTEN)
					EnableFullscreen = ((boolOptions & BoolOptions::EnableFullscreen) == BoolOptions::EnableFullscreen);
#endif
					ShowPerformanceMetrics = ((boolOptions & BoolOptions::ShowPerformanceMetrics) == BoolOptions::ShowPerformanceMetrics);
					KeepAspectRatioInCinematics = ((boolOptions & BoolOptions::KeepAspectRatioInCinematics) == BoolOptions::KeepAspectRatioInCinematics);
					ShowPlayerTrails = ((boolOptions & BoolOptions::ShowPlayerTrails) == BoolOptions::ShowPlayerTrails);
					LowWaterQuality = ((boolOptions & BoolOptions::LowWaterQuality) == BoolOptions::LowWaterQuality);
					UnalignedViewport = ((boolOptions & BoolOptions::UnalignedViewport) == BoolOptions::UnalignedViewport);
					EnableReforgedGameplay = ((boolOptions & BoolOptions::EnableReforgedGameplay) == BoolOptions::EnableReforgedGameplay);
					EnableLedgeClimb = ((boolOptions & BoolOptions::EnableLedgeClimb) == BoolOptions::EnableLedgeClimb);
					WeaponWheel = ((boolOptions & BoolOptions::EnableWeaponWheel) == BoolOptions::EnableWeaponWheel ? WeaponWheelStyle::Enabled : WeaponWheelStyle::Disabled);
					EnableRgbLights = ((boolOptions & BoolOptions::EnableRgbLights) == BoolOptions::EnableRgbLights);
					AllowUnsignedScripts = ((boolOptions & BoolOptions::AllowUnsignedScripts) == BoolOptions::AllowUnsignedScripts);
					ToggleRunAction = ((boolOptions & BoolOptions::ToggleRunAction) == BoolOptions::ToggleRunAction);
					UseNativeBackButton = ((boolOptions & BoolOptions::UseNativeBackButton) == BoolOptions::UseNativeBackButton);
					EnableDiscordIntegration = ((boolOptions & BoolOptions::EnableDiscordIntegration) == BoolOptions::EnableDiscordIntegration);
					TutorialCompleted = ((boolOptions & BoolOptions::TutorialCompleted) == BoolOptions::TutorialCompleted);
					ResumeOnStart = ((boolOptions & BoolOptions::ResumeOnStart) == BoolOptions::ResumeOnStart);

					if (version >= 3) {
						// These 2 new options needs to be enabled by default
						EnableReforgedHUD = ((boolOptions & BoolOptions::EnableReforgedHUD) == BoolOptions::EnableReforgedHUD);
						EnableReforgedMainMenu = ((boolOptions & BoolOptions::EnableReforgedMainMenu) == BoolOptions::EnableReforgedMainMenu);
#if defined(DEATH_TARGET_ANDROID)
						EnableReforgedMainMenuInitial = EnableReforgedMainMenu;
#endif
					}

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

					if (version >= 5) {
						GamepadButtonLabels = (GamepadType)uc.ReadValue<uint8_t>();
					}

					// Controls
					if (version >= 4) {
						auto mappings = UI::ControlScheme::GetMappings();

						std::uint8_t playerCount = uc.ReadValue<std::uint8_t>();
						std::uint8_t controlMappingCount = uc.ReadValue<std::uint8_t>();
						for (std::uint32_t i = 0; i < playerCount; i++) {
							for (std::uint32_t j = 0; j < controlMappingCount; j++) {
								std::uint8_t targetCount = uc.ReadValue<std::uint8_t>();
								if (i < UI::ControlScheme::MaxSupportedPlayers && j < (std::uint32_t)PlayerActions::Count) {
									auto& mapping = mappings[i * (std::uint32_t)PlayerActions::Count + j];
									mapping.Targets.clear();

									for (std::uint32_t k = 0; k < targetCount; k++) {
										UI::MappingTarget target = { uc.ReadValue<std::uint32_t>() };
										mapping.Targets.push_back(target);
									}
								} else {
									uc.Seek(targetCount * sizeof(std::uint32_t), SeekOrigin::Current);
								}
							}
						}

						// Reset primary Menu action, because it's hardcoded
						auto& menuMapping = mappings[(std::uint32_t)PlayerActions::Menu];
						if (menuMapping.Targets.empty()) {
							mappings[(std::int32_t)PlayerActions::Menu].Targets.push_back(UI::ControlScheme::CreateTarget(KeySym::ESCAPE));
						}
					} else {
						// Skip old control mapping definitions
						uint8_t controlMappingCount = uc.ReadValue<std::uint8_t>();
						uc.Seek(controlMappingCount * sizeof(std::uint32_t), SeekOrigin::Current);
					}

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
			} else {
				FirstRun = true;
				TryLoadPreferredLanguage();

				auto configDir = fs::GetDirectoryName(_configPath);
				if (!configDir.empty()) {
					fs::CreateDirectories(configDir);
				}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
				// Create "Source" directory on the first launch
				auto& resolver = ContentResolver::Get();
				fs::CreateDirectories(resolver.GetSourcePath());
#endif
			}
		}

#	if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
		// Override some settings by command-line arguments
		for (std::int32_t i = 0; i < config.argc(); i++) {
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
			} else if (arg == "/reset-controls"_s) {
				UI::ControlScheme::Reset();
			}
#	if defined(WITH_MULTIPLAYER)
			else if (InitialState.empty() && (arg == "/server"_s || arg.hasPrefix("/connect:"_s))) {
				InitialState = arg;
			}
#	endif
		}
#endif
	}

	void PreferencesCache::Save()
	{
		// `FirstRun` is true only if config file doesn't exist yet
		FirstRun = false;

		fs::CreateDirectories(fs::GetDirectoryName(_configPath));

		auto so = fs::Open(_configPath, FileAccessMode::Write);
		if (!so->IsValid()) {
			return;
		}

		so->WriteValue<uint64_t>(0x2095A59FF0BFBBEF);
		so->WriteValue<uint8_t>(ContentResolver::ConfigFile);
		so->WriteValue<uint8_t>(FileVersion);

		DeflateWriter co(*so);

		BoolOptions boolOptions = BoolOptions::None;
		if (EnableFullscreen) boolOptions |= BoolOptions::EnableFullscreen;
		if (ShowPerformanceMetrics) boolOptions |= BoolOptions::ShowPerformanceMetrics;
		if (KeepAspectRatioInCinematics) boolOptions |= BoolOptions::KeepAspectRatioInCinematics;
		if (ShowPlayerTrails) boolOptions |= BoolOptions::ShowPlayerTrails;
		if (LowWaterQuality) boolOptions |= BoolOptions::LowWaterQuality;
		if (UnalignedViewport) boolOptions |= BoolOptions::UnalignedViewport;
		if (EnableReforgedGameplay) boolOptions |= BoolOptions::EnableReforgedGameplay;
		if (EnableLedgeClimb) boolOptions |= BoolOptions::EnableLedgeClimb;
		if (WeaponWheel != WeaponWheelStyle::Disabled) boolOptions |= BoolOptions::EnableWeaponWheel;
		if (WeaponWheel == WeaponWheelStyle::EnabledWithAmmoCount) boolOptions |= BoolOptions::ShowWeaponWheelAmmoCount;
		if (EnableRgbLights) boolOptions |= BoolOptions::EnableRgbLights;
		if (AllowUnsignedScripts) boolOptions |= BoolOptions::AllowUnsignedScripts;
		if (ToggleRunAction) boolOptions |= BoolOptions::ToggleRunAction;
		if (UseNativeBackButton) boolOptions |= BoolOptions::UseNativeBackButton;
		if (EnableDiscordIntegration) boolOptions |= BoolOptions::EnableDiscordIntegration;
		if (TutorialCompleted) boolOptions |= BoolOptions::TutorialCompleted;
		if (Language[0] != '\0') boolOptions |= BoolOptions::SetLanguage;
		if (ResumeOnStart) boolOptions |= BoolOptions::ResumeOnStart;
		if (EnableReforgedHUD) boolOptions |= BoolOptions::EnableReforgedHUD;
		if (EnableReforgedMainMenu) boolOptions |= BoolOptions::EnableReforgedMainMenu;
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

		co.WriteValue<uint8_t>((uint8_t)GamepadButtonLabels);

		// Controls
		auto mappings = UI::ControlScheme::GetMappings();
		co.WriteValue<std::uint8_t>((std::uint8_t)UI::ControlScheme::MaxSupportedPlayers);
		co.WriteValue<std::uint8_t>((std::uint8_t)mappings.size());
		for (std::uint32_t i = 0; i < mappings.size(); i++) {
			const auto& mapping = mappings[i];

			std::uint8_t targetCount = (std::uint8_t)mapping.Targets.size();
			co.WriteValue<std::uint8_t>(targetCount);
			for (std::uint32_t k = 0; k < targetCount; k++) {
				co.WriteValue<std::uint32_t>(mapping.Targets[k].Data);
			}
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

		co.Close();
		so->Close();

#if defined(DEATH_TARGET_EMSCRIPTEN)
		fs::SyncToPersistent();
#endif
	}

	StringView PreferencesCache::GetDirectory()
	{
		return fs::GetDirectoryName(_configPath);
	}

	EpisodeContinuationState* PreferencesCache::GetEpisodeEnd(const StringView episodeName, bool createIfNotFound)
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

	EpisodeContinuationStateWithLevel* PreferencesCache::GetEpisodeContinue(const StringView episodeName, bool createIfNotFound)
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

	void PreferencesCache::RemoveEpisodeContinue(const StringView episodeName)
	{
		if (episodeName.empty() || episodeName == "unknown"_s) {
			return;
		}

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
				if (i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, String(language + ".mo"_s) })) ||
					i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, String(language + ".mo"_s) }))) {
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
				if (i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, String(baseLanguage + ".mo"_s) })) ||
					i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, String(baseLanguage + ".mo"_s) }))) {
					std::memcpy(PreferencesCache::Language, baseLanguage.data(), baseLanguage.size());
					std::memset(PreferencesCache::Language + baseLanguage.size(), 0, sizeof(PreferencesCache::Language) - baseLanguage.size());
					break;
				}
			}
		}
	}
}