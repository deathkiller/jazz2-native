#include "PreferencesCache.h"
#include "ContentResolver.h"
#include "LevelHandler.h"
#include "Input/ControlScheme.h"
#include "UI/DiscordRpcClient.h"

#include "../nCine/Application.h"
#include "../nCine/I18n.h"
#include "../nCine/Base/Random.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringStl.h>
#include <Cpu.h>
#include <Environment.h>
#include <IO/FileSystem.h>
#include <IO/Compression/DeflateStream.h>
#include <Utf8.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "../nCine/Backends/Android/AndroidApplication.h"
#	include "../nCine/Backends/Android/AndroidJniHelper.h"
#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include <unistd.h>
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::IO::Compression;
using namespace nCine;

namespace Jazz2
{
	bool PreferencesCache::FirstRun = false;
#if defined(DEATH_TARGET_EMSCRIPTEN)
	bool PreferencesCache::IsStandalone = false;
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
	bool PreferencesCache::PreferVerticalSplitscreen = false;
	bool PreferencesCache::PreferZoomOut = false;
	bool PreferencesCache::BackgroundDithering = true;
	bool PreferencesCache::EnableReforgedGameplay = true;
	bool PreferencesCache::EnableReforgedHUD = true;
	bool PreferencesCache::EnableReforgedMainMenu = true;
#if defined(DEATH_TARGET_ANDROID)
	// Used to swap Android activity icons on exit/suspend
	bool PreferencesCache::EnableReforgedMainMenuInitial = true;
#endif
	bool PreferencesCache::EnableContinuousJump = true;
	bool PreferencesCache::EnableLedgeClimb = true;
	WeaponWheelStyle PreferencesCache::WeaponWheel = WeaponWheelStyle::Enabled;
	bool PreferencesCache::SwitchToNewWeapon = true;
#if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_EMSCRIPTEN) || defined(DEATH_TARGET_IOS) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WINDOWS_RT)
	bool PreferencesCache::EnableRgbLights = false;
#else
	bool PreferencesCache::EnableRgbLights = true;
#endif
	bool PreferencesCache::AllowUnsignedScripts = true;

	bool PreferencesCache::TutorialCompleted = false;
	bool PreferencesCache::ResumeOnStart = false;
	bool PreferencesCache::AllowCheats = false;
	bool PreferencesCache::AllowCheatsLives = false;
	bool PreferencesCache::AllowCheatsUnlock = false;
	EpisodeEndOverwriteMode PreferencesCache::OverwriteEpisodeEnd = EpisodeEndOverwriteMode::Always;
	char PreferencesCache::Language[6]{};
	bool PreferencesCache::BypassCache = false;
	float PreferencesCache::MasterVolume = 0.7f;
	float PreferencesCache::SfxVolume = 0.8f;
	float PreferencesCache::MusicVolume = 0.4f;
	bool PreferencesCache::ToggleRunAction = false;
#if defined(DEATH_TARGET_SWITCH)
	GamepadType PreferencesCache::GamepadButtonLabels = GamepadType::Switch;
#else
	GamepadType PreferencesCache::GamepadButtonLabels = GamepadType::Xbox;
#endif
	std::uint8_t PreferencesCache::GamepadRumble = 1;
	bool PreferencesCache::PlayStationExtendedSupport = false;
	bool PreferencesCache::UseNativeBackButton = false;
	Vector2f PreferencesCache::TouchLeftPadding;
	Vector2f PreferencesCache::TouchRightPadding;
	Uuid PreferencesCache::UniquePlayerID;
	Uuid PreferencesCache::UniqueServerID;
	String PreferencesCache::PlayerName;
	bool PreferencesCache::EnableDiscordIntegration = true;

	String PreferencesCache::_configPath;
	HashMap<String, EpisodeContinuationState> PreferencesCache::_episodeEnd;
	HashMap<String, EpisodeContinuationStateWithLevel> PreferencesCache::_episodeContinue;

	static void ReadEpisodeContinuationState(Stream& s, EpisodeContinuationState& state)
	{
#if defined(DEATH_DEBUG)
		std::int64_t startPos = s.GetPosition();
#endif

		state.Flags = EpisodeContinuationFlags(s.ReadValue<std::uint8_t>());
		state.DifficultyAndPlayerType = s.ReadValue<std::uint8_t>();
		state.Lives = s.ReadValue<std::uint8_t>();
		state.Unused1 = s.ReadValue<std::uint8_t>();
		state.Score = s.ReadValueAsLE<std::int32_t>();
		state.Unused2 = s.ReadValueAsLE<std::uint16_t>();
		state.ElapsedMilliseconds = s.ReadValueAsLE<std::uint64_t>();
		for (std::size_t i = 0; i < arraySize(state.Gems); i++) {
			state.Gems[i] = s.ReadValueAsLE<std::int32_t>();
		}
		for (std::size_t i = 0; i < arraySize(state.Ammo); i++) {
			state.Ammo[i] = s.ReadValueAsLE<std::uint16_t>();
		}
		for (std::size_t i = 0; i < arraySize(state.WeaponUpgrades); i++) {
			state.WeaponUpgrades[i] = s.ReadValue<std::uint8_t>();
		}

#if defined(DEATH_DEBUG)
		std::int64_t endPos = s.GetPosition();
		DEATH_ASSERT(endPos - startPos == sizeof(EpisodeContinuationState),
			("EpisodeContinuationState struct mismatch (expected: {} bytes, written: {} bytes)", sizeof(EpisodeContinuationState), endPos - startPos));
#endif
	}

	static void WriteEpisodeContinuationState(Stream& s, const EpisodeContinuationState& state)
	{
#if defined(DEATH_DEBUG)
		std::int64_t startPos = s.GetPosition();
#endif

		s.WriteValue<std::uint8_t>(std::uint8_t(state.Flags));
		s.WriteValue<std::uint8_t>(state.DifficultyAndPlayerType);
		s.WriteValue<std::uint8_t>(state.Lives);
		s.WriteValue<std::uint8_t>(state.Unused1);
		s.WriteValueAsLE<std::int32_t>(state.Score);
		s.WriteValueAsLE<std::uint16_t>(state.Unused2);
		s.WriteValueAsLE<std::uint64_t>(state.ElapsedMilliseconds);
		for (std::size_t i = 0; i < arraySize(state.Gems); i++) {
			s.WriteValueAsLE<std::int32_t>(state.Gems[i]);
		}
		for (std::size_t i = 0; i < arraySize(state.Ammo); i++) {
			s.WriteValueAsLE<std::uint16_t>(state.Ammo[i]);
		}
		for (std::size_t i = 0; i < arraySize(state.WeaponUpgrades); i++) {
			s.WriteValue<std::uint8_t>(state.WeaponUpgrades[i]);
		}

#if defined(DEATH_DEBUG)
		std::int64_t endPos = s.GetPosition();
		DEATH_ASSERT(endPos - startPos == sizeof(EpisodeContinuationState),
			("EpisodeContinuationState struct mismatch (expected: {} bytes, written: {} bytes)", sizeof(EpisodeContinuationState), endPos - startPos));
#endif
	}

	void PreferencesCache::Initialize(AppConfiguration& config)
	{
		bool resetConfig = false;

#if defined(DEATH_TARGET_EMSCRIPTEN)
		auto configDir = "/Persistent"_s;
		fs::MountAsPersistent(configDir);
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
				auto configFileWritable = fs::Open(localConfigPath, FileAccess::ReadWrite);
				if (configFileWritable->IsValid()) {
					configFileWritable->Dispose();
					_configPath = localConfigPath;
				}
			}
#	endif
		}

		auto configDir = fs::GetDirectoryName(_configPath);

#	if defined(DEATH_TRACE)
#		if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_SWITCH)
		fs::CreateDirectories(configDir);
		theApplication().AttachTraceTarget(fs::CombinePath(configDir, "Jazz2.log"_s));
#		elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT))
		for (std::int32_t i = 0; i < config.argc(); i++) {
			auto arg = config.argv(i);
			if (arg == "/log:file"_s || arg.hasPrefix("/log:file:"_s)) {
				fs::CreateDirectories(configDir);
				if (arg.size() > "/log:file:"_s.size()) {
					theApplication().AttachTraceTarget(fs::CombinePath(configDir, arg.exceptPrefix("/log:file:"_s)));
				} else {
					theApplication().AttachTraceTarget(fs::CombinePath(configDir, "Jazz2.log"_s));
				}
			}
#			if defined(DEATH_TARGET_WINDOWS)
			else if (arg == "/log"_s) {
				theApplication().AttachTraceTarget(Application::ConsoleTarget);
			}
#			endif
		}
#		endif
#	endif
#endif

		ControlScheme::Reset();

		// Try to read config file
		if (!resetConfig) {
			auto s = fs::Open(_configPath, FileAccess::Read);
			if (s->GetSize() > 18) {
				std::uint64_t signature = s->ReadValueAsLE<std::uint64_t>();
				std::uint8_t fileType = s->ReadValue<std::uint8_t>();
				std::uint8_t version = s->ReadValue<std::uint8_t>();

				if (signature == 0x2095A59FF0BFBBEF && fileType == ContentResolver::ConfigFile && version <= FileVersion) {
					if (version == 1) {
						// Version 1 included compressedSize and decompressedSize, it's not needed anymore
						/*std::int32_t compressedSize =*/ s->ReadValue<std::int32_t>();
						/*std::int32_t uncompressedSize =*/ s->ReadValue<std::int32_t>();
					}

					DeflateStream uc(*s);

					BoolOptions boolOptions = (BoolOptions)uc.ReadValueAsLE<std::uint64_t>();

#if !defined(DEATH_TARGET_EMSCRIPTEN)
					EnableFullscreen = ((boolOptions & BoolOptions::EnableFullscreen) == BoolOptions::EnableFullscreen);
#endif
					ShowPerformanceMetrics = ((boolOptions & BoolOptions::ShowPerformanceMetrics) == BoolOptions::ShowPerformanceMetrics);
					KeepAspectRatioInCinematics = ((boolOptions & BoolOptions::KeepAspectRatioInCinematics) == BoolOptions::KeepAspectRatioInCinematics);
					ShowPlayerTrails = ((boolOptions & BoolOptions::ShowPlayerTrails) == BoolOptions::ShowPlayerTrails);
					LowWaterQuality = ((boolOptions & BoolOptions::LowWaterQuality) == BoolOptions::LowWaterQuality);
					UnalignedViewport = ((boolOptions & BoolOptions::UnalignedViewport) == BoolOptions::UnalignedViewport);
					PreferVerticalSplitscreen = ((boolOptions & BoolOptions::PreferVerticalSplitscreen) == BoolOptions::PreferVerticalSplitscreen);
					PreferZoomOut = ((boolOptions & BoolOptions::PreferZoomOut) == BoolOptions::PreferZoomOut);
					BackgroundDithering = ((boolOptions & BoolOptions::BackgroundDithering) == BoolOptions::BackgroundDithering);
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
					UnlockedEpisodes = (UnlockableEpisodes)uc.ReadValueAsLE<std::uint32_t>();

					ActiveRescaleMode = (RescaleMode)uc.ReadValue<std::uint8_t>();

					MasterVolume = uc.ReadValue<std::uint8_t>() / 255.0f;
					SfxVolume = uc.ReadValue<std::uint8_t>() / 255.0f;
					MusicVolume = uc.ReadValue<std::uint8_t>() / 255.0f;

					TouchLeftPadding.X = std::round(uc.ReadValue<std::int8_t>() / (TouchPaddingMultiplier * INT8_MAX));
					TouchLeftPadding.Y = std::round(uc.ReadValue<std::int8_t>() / (TouchPaddingMultiplier * INT8_MAX));
					TouchRightPadding.X = std::round(uc.ReadValue<std::int8_t>() / (TouchPaddingMultiplier * INT8_MAX));
					TouchRightPadding.Y = std::round(uc.ReadValue<std::int8_t>() / (TouchPaddingMultiplier * INT8_MAX));

					if (version >= 5) {
						GamepadButtonLabels = (GamepadType)uc.ReadValue<std::uint8_t>();
					}

					if (version >= 6) {
						GamepadRumble = uc.ReadValue<std::uint8_t>();
					}

					if (version >= 7) {
						AllowCheats = ((boolOptions & BoolOptions::AllowCheats) == BoolOptions::AllowCheats);
						PlayStationExtendedSupport = ((boolOptions & BoolOptions::PlayStationExtendedSupport) == BoolOptions::PlayStationExtendedSupport);
						SwitchToNewWeapon = ((boolOptions & BoolOptions::SwitchToNewWeapon) == BoolOptions::SwitchToNewWeapon);
						OverwriteEpisodeEnd = (EpisodeEndOverwriteMode)uc.ReadValue<std::uint8_t>();
					}

					if (version >= 12) {
						EnableContinuousJump = ((boolOptions & BoolOptions::EnableContinuousJump) == BoolOptions::EnableContinuousJump);
					}

					if (version >= 10) {
						uc.Read(UniquePlayerID, sizeof(UniquePlayerID));
						std::uint32_t playerNameLength = uc.ReadVariableUint32();
						PlayerName = String(NoInit, playerNameLength);
						uc.Read(PlayerName.data(), playerNameLength);
					} else {
						// Generate a new UUID when upgrading from older version
						Random().Uuid(UniquePlayerID);
					}

					if (version >= 11) {
						uc.Read(UniqueServerID, sizeof(UniqueServerID));
					} else {
						// Generate a new UUID when upgrading from older version
						Random().Uuid(UniqueServerID);
					}

					// Controls
					if (version >= 4) {
						auto mappings = ControlScheme::GetAllMappings();

						bool shouldResetBecauseOfOldVersion = (version < 9);
						std::uint32_t playerCount = uc.ReadValue<std::uint8_t>();
						std::uint32_t controlMappingCount = uc.ReadValue<std::uint8_t>();
						for (std::uint32_t i = 0; i < playerCount; i++) {
							for (std::uint32_t j = 0; j < controlMappingCount; j++) {
								std::uint8_t targetCount = uc.ReadValue<std::uint8_t>();
								if (!shouldResetBecauseOfOldVersion && i < ControlScheme::MaxSupportedPlayers && j < (std::uint32_t)PlayerAction::Count) {
									auto& mapping = mappings[i * (std::uint32_t)PlayerAction::Count + j];
									mapping.Targets.clear();

									for (std::uint32_t k = 0; k < targetCount; k++) {
										MappingTarget target = { uc.ReadValueAsLE<std::uint32_t>() };
										mapping.Targets.push_back(target);
									}
								} else {
									uc.Seek(targetCount * sizeof(std::uint32_t), SeekOrigin::Current);
								}
							}
						}

						// Reset primary Menu action, because it's hardcoded
						auto& menuMapping = mappings[(std::uint32_t)PlayerAction::Menu];
						if (menuMapping.Targets.empty()) {
							mappings[(std::int32_t)PlayerAction::Menu].Targets.push_back(ControlScheme::CreateTarget(Keys::Escape));
						}
					} else {
						// Skip old control mapping definitions
						std::uint8_t controlMappingCount = uc.ReadValue<std::uint8_t>();
						uc.Seek(controlMappingCount * sizeof(std::uint32_t), SeekOrigin::Current);
					}

					// Episode End
					std::uint16_t episodeEndSize = uc.ReadValueAsLE<std::uint16_t>();
					std::uint16_t episodeEndCount = uc.ReadValueAsLE<std::uint16_t>();

					for (std::uint32_t i = 0; i < episodeEndCount; i++) {
						std::uint8_t nameLength = uc.ReadValue<std::uint8_t>();
						String episodeName{NoInit, nameLength};
						uc.Read(episodeName.data(), nameLength);

						EpisodeContinuationState state = {};
						if (episodeEndSize == sizeof(EpisodeContinuationState)) {
							ReadEpisodeContinuationState(uc, state);
						} else {
							// Struct has different size, so it's better to skip it
							uc.Seek(episodeEndSize, SeekOrigin::Current);
							state.Flags = EpisodeContinuationFlags::IsCompleted;
						}

						_episodeEnd.emplace(std::move(episodeName), std::move(state));
					}

					// Episode Continue
					std::uint16_t episodeContinueSize = uc.ReadValueAsLE<std::uint16_t>();
					std::uint16_t episodeContinueCount = uc.ReadValueAsLE<std::uint16_t>();

					for (std::uint32_t i = 0; i < episodeContinueCount; i++) {
						std::uint8_t nameLength = uc.ReadValue<std::uint8_t>();
						String episodeName{NoInit, nameLength};
						uc.Read(episodeName.data(), nameLength);

						if (episodeContinueSize == sizeof(EpisodeContinuationState)) {
							EpisodeContinuationStateWithLevel stateWithLevel = {};
							nameLength = uc.ReadValue<std::uint8_t>();
							stateWithLevel.LevelName = String(NoInit, nameLength);
							uc.Read(stateWithLevel.LevelName.data(), nameLength);

							ReadEpisodeContinuationState(uc, stateWithLevel.State);
							_episodeContinue.emplace(std::move(episodeName), std::move(stateWithLevel));
						} else {
							// Struct has different size, so it's better to skip it
							nameLength = uc.ReadValue<std::uint8_t>();
							uc.Seek(nameLength + episodeContinueSize, SeekOrigin::Current);
						}
					}
				} else {
					// The file is too new or corrupted
					resetConfig = true;
				}
			} else {
				// The file doesn't exist
				resetConfig = true;
			}
		}
		
		if (resetConfig) {
			// Config file doesn't exist or reset is requested
			FirstRun = true;
			Random().Uuid(UniquePlayerID);
			Random().Uuid(UniqueServerID);
			PlayerName = GetEffectivePlayerName();
			TryLoadPreferredLanguage();

			fs::CreateDirectories(configDir);

#if !defined(DEATH_TARGET_EMSCRIPTEN)
			// Create "Source" directory on the first launch
			auto& resolver = ContentResolver::Get();
			fs::CreateDirectories(resolver.GetSourcePath());

#	if defined(DEATH_TARGET_ANDROID)
			// Use native Back button as default on smart watches
			UseNativeBackButton = static_cast<AndroidApplication&>(theApplication()).IsScreenRound();
#	elif defined(DEATH_TARGET_UNIX)
			StringView isSteamDeck = ::getenv("SteamDeck");
			if (isSteamDeck == "1"_s) {
				GamepadButtonLabels = GamepadType::Steam;
			}
#	elif defined(DEATH_TARGET_WINDOWS)
			wchar_t envSteamDeck[2] = {};
			DWORD envLength = ::GetEnvironmentVariable(L"SteamDeck", envSteamDeck, 2);
			if (envLength == 1 && envSteamDeck[0] == L'1') {
				GamepadButtonLabels = GamepadType::Steam;
			}
#	endif
#endif
		}

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH)
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
			}
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
			else if (arg == "/gpu-workaround"_s) {
				if (i + 1 < config.argc() && config.argv(i + 1) == "fixed-batch-size"_s) {
					config.fixedBatchSize = 10;
				}
			}
#	endif
			else if (arg == "/no-rgb"_s) {
				EnableRgbLights = false;
			} else if (arg == "/no-rescale"_s) {
				ActiveRescaleMode = RescaleMode::None;
			} else if (arg == "/mute"_s) {
				MasterVolume = 0.0f;
			} else if (arg == "/reset-controls"_s) {
				ControlScheme::Reset();
			}
#	if defined(DEATH_TARGET_EMSCRIPTEN)
			else if (arg == "/standalone"_s) {
				IsStandalone = true;
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

		auto so = fs::Open(_configPath, FileAccess::Write);
		if (!so->IsValid()) {
			return;
		}

		so->WriteValueAsLE<std::uint64_t>(0x2095A59FF0BFBBEF);
		so->WriteValue<std::uint8_t>(ContentResolver::ConfigFile);
		so->WriteValue<std::uint8_t>(FileVersion);

		DeflateWriter co(*so);

		BoolOptions boolOptions = BoolOptions::None;
		if (EnableFullscreen) boolOptions |= BoolOptions::EnableFullscreen;
		if (ShowPerformanceMetrics) boolOptions |= BoolOptions::ShowPerformanceMetrics;
		if (KeepAspectRatioInCinematics) boolOptions |= BoolOptions::KeepAspectRatioInCinematics;
		if (ShowPlayerTrails) boolOptions |= BoolOptions::ShowPlayerTrails;
		if (LowWaterQuality) boolOptions |= BoolOptions::LowWaterQuality;
		if (UnalignedViewport) boolOptions |= BoolOptions::UnalignedViewport;
		if (PreferVerticalSplitscreen) boolOptions |= BoolOptions::PreferVerticalSplitscreen;
		if (PreferZoomOut) boolOptions |= BoolOptions::PreferZoomOut;
		if (BackgroundDithering) boolOptions |= BoolOptions::BackgroundDithering;
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
		if (AllowCheats) boolOptions |= BoolOptions::AllowCheats;
		if (PlayStationExtendedSupport) boolOptions |= BoolOptions::PlayStationExtendedSupport;
		if (SwitchToNewWeapon) boolOptions |= BoolOptions::SwitchToNewWeapon;
		co.WriteValueAsLE<std::uint64_t>(std::uint64_t(boolOptions));

		if (Language[0] != '\0') {
			co.Write(Language, sizeof(Language));
		}

		// Bitmask of unlocked episodes, used only if compiled with SHAREWARE_DEMO_ONLY
		co.WriteValueAsLE<std::uint32_t>(std::uint32_t(UnlockedEpisodes));

		co.WriteValue<std::uint8_t>(std::uint8_t(ActiveRescaleMode));

		co.WriteValue<std::uint8_t>(std::uint8_t(MasterVolume * 255.0f));
		co.WriteValue<std::uint8_t>(std::uint8_t(SfxVolume * 255.0f));
		co.WriteValue<std::uint8_t>(std::uint8_t(MusicVolume * 255.0f));

		co.WriteValue<std::int8_t>(std::int8_t(TouchLeftPadding.X * INT8_MAX * TouchPaddingMultiplier));
		co.WriteValue<std::int8_t>(std::int8_t(TouchLeftPadding.Y * INT8_MAX * TouchPaddingMultiplier));
		co.WriteValue<std::int8_t>(std::int8_t(TouchRightPadding.X * INT8_MAX * TouchPaddingMultiplier));
		co.WriteValue<std::int8_t>(std::int8_t(TouchRightPadding.Y * INT8_MAX * TouchPaddingMultiplier));

		co.WriteValue<std::uint8_t>(std::uint8_t(GamepadButtonLabels));
		co.WriteValue<std::uint8_t>(GamepadRumble);
		co.WriteValue<std::uint8_t>(std::uint8_t(OverwriteEpisodeEnd));

		co.Write(UniquePlayerID, sizeof(UniquePlayerID));
		co.WriteVariableUint32(std::uint32_t(PlayerName.size()));
		co.Write(PlayerName.data(), std::int64_t(PlayerName.size()));

		co.Write(UniqueServerID, sizeof(UniqueServerID));

		// Controls
		co.WriteValue<std::uint8_t>(std::uint8_t(ControlScheme::MaxSupportedPlayers));
		co.WriteValue<std::uint8_t>(std::uint8_t(PlayerAction::Count));
		for (std::int32_t i = 0; i < ControlScheme::MaxSupportedPlayers; i++) {
			auto mappings = ControlScheme::GetMappings(i);
			for (std::uint32_t j = 0; j < mappings.size(); j++) {
				const auto& mapping = mappings[j];
				std::uint8_t targetCount = (std::uint8_t)mapping.Targets.size();
				co.WriteValue<std::uint8_t>(targetCount);
				for (std::uint32_t k = 0; k < targetCount; k++) {
					co.WriteValueAsLE<std::uint32_t>(mapping.Targets[k].Data);
				}
			}
		}

		// Episode End
		co.WriteValueAsLE<std::uint16_t>(sizeof(EpisodeContinuationState));
		co.WriteValueAsLE<std::uint16_t>(std::uint16_t(_episodeEnd.size()));

		for (auto& pair : _episodeEnd) {
			co.WriteValue<std::uint8_t>((std::uint8_t)pair.first.size());
			co.Write(pair.first.data(), (std::int64_t)pair.first.size());
			WriteEpisodeContinuationState(co, pair.second);
		}

		// Episode Continue
		co.WriteValueAsLE<std::uint16_t>(sizeof(EpisodeContinuationState));
		co.WriteValueAsLE<std::uint16_t>(std::uint16_t(_episodeContinue.size()));

		for (auto& pair : _episodeContinue) {
			co.WriteValue<std::uint8_t>((std::uint8_t)pair.first.size());
			co.Write(pair.first.data(), (std::int64_t)pair.first.size());

			co.WriteValue<std::uint8_t>((std::uint8_t)pair.second.LevelName.size());
			co.Write(pair.second.LevelName.data(), (std::int64_t)pair.second.LevelName.size());
			WriteEpisodeContinuationState(co, pair.second.State);
		}

		co.Dispose();
		so->Dispose();

#if defined(DEATH_TARGET_EMSCRIPTEN)
		fs::SyncToPersistent();
#endif
	}

	StringView PreferencesCache::GetDirectory()
	{
		return fs::GetDirectoryName(_configPath);
	}

	String PreferencesCache::GetDeviceID()
	{
#if defined(DEATH_TARGET_X86)
		std::int32_t arch = 1;
		Cpu::Features cpuFeatures = Cpu::runtimeFeatures();
		if (cpuFeatures & Cpu::Avx) {
			arch |= 0x400;
		}
		if (cpuFeatures & Cpu::Avx2) {
			arch |= 0x800;
		}
		if (cpuFeatures & Cpu::Avx512f) {
			arch |= 0x1000;
		}
#elif defined(DEATH_TARGET_ARM)
		std::int32_t arch = 2;
		Cpu::Features cpuFeatures = Cpu::runtimeFeatures();
		if (cpuFeatures & Cpu::Neon) {
			arch |= 0x2000;
		}
#elif defined(DEATH_TARGET_POWERPC)
		std::int32_t arch = 3;
#elif defined(DEATH_TARGET_RISCV)
		std::int32_t arch = 5;
#elif defined(DEATH_TARGET_WASM)
		std::int32_t arch = 4;
		Cpu::Features cpuFeatures = Cpu::runtimeFeatures();
		if (cpuFeatures & Cpu::Simd128) {
			arch |= 0x4000;
		}
#else
		std::int32_t arch = 0;
#endif
#if defined(DEATH_TARGET_32BIT)
		arch |= 0x100;
#endif
#if defined(DEATH_TARGET_BIG_ENDIAN)
		arch |= 0x200;
#endif
#if defined(DEATH_TARGET_CYGWIN)
		arch |= 0x200000;
#endif
#if defined(DEATH_TARGET_MINGW)
		arch |= 0x400000;
#endif

#if defined(DEATH_TARGET_ANDROID)
		auto sanitizeName = [](char* dst, std::size_t dstMaxLength, std::size_t& dstLength, StringView name, bool isBrand) {
			bool wasSpace = true;
			std::size_t lowercaseLength = 0;

			if (isBrand) {
				for (char c : name) {
					if (c == '\0' || c == ' ') {
						break;
					}
					lowercaseLength++;
				}
				if (lowercaseLength < 5 || name[0] < 'A' || name[0] > 'Z' || name[lowercaseLength - 1] < 'A' || name[lowercaseLength - 1] > 'Z') {
					lowercaseLength = 0;
				}
			}

			for (char c : name) {
				if (c == '\0' || dstLength >= dstMaxLength) {
					break;
				}
				if (isalnum(c) || c == ' ' || c == '.' || c == ',' || c == ':' || c == '_' || c == '-' || c == '+' || c == '/' || c == '*' ||
					c == '!' || c == '(' || c == ')' || c == '[' || c == ']' || c == '@' || c == '&' || c == '#' || c == '\'' || c == '"') {
					if (wasSpace && c >= 'a' && c <= 'z') {
						c &= ~0x20;
						if (lowercaseLength > 0) {
							lowercaseLength--;
						}
					} else if (lowercaseLength > 0) {
						if (c >= 'A' && c <= 'Z') {
							c |= 0x20;
						}
						lowercaseLength--;
					}
					dst[dstLength++] = c;
				}
				wasSpace = (c == ' ');
			}
		};

		auto sdkVersion = Backends::AndroidJniHelper::SdkVersion();
		auto androidId = Backends::AndroidJniWrap_Secure::getAndroidId();
		auto deviceBrand = Backends::AndroidJniClass_Version::deviceBrand();
		auto deviceModel = Backends::AndroidJniClass_Version::deviceModel();

		char deviceName[64];
		std::size_t deviceNameLength = 0;
		if (deviceModel.empty()) {
			sanitizeName(deviceName, arraySize(deviceName) - 1, deviceNameLength, deviceBrand, false);
		} else if (deviceModel.hasPrefix(deviceBrand)) {
			sanitizeName(deviceName, arraySize(deviceName) - 1, deviceNameLength, deviceModel, true);
		} else {
			if (!deviceBrand.empty()) {
				sanitizeName(deviceName, arraySize(deviceName) - 8, deviceNameLength, deviceBrand, true);
				deviceName[deviceNameLength++] = ' ';
			}
			sanitizeName(deviceName, arraySize(deviceName) - 1, deviceNameLength, deviceModel, false);
		}
		deviceName[deviceNameLength] = '\0';

		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "{}|Android {}|{}|2|{}", androidId, sdkVersion, deviceName, arch);
#elif defined(DEATH_TARGET_APPLE)
		char DeviceDesc[256] {}; std::int32_t DeviceDescLength;
		if (::gethostname(DeviceDesc, arraySize(DeviceDesc)) == 0) {
			DeviceDesc[arraySize(DeviceDesc) - 1] = '\0';
			DeviceDescLength = std::strlen(DeviceDesc);
		} else {
			DeviceDescLength = 0;
		}
		String appleVersion = Environment::GetAppleVersion();
		DeviceDescLength += formatInto({ DeviceDesc + DeviceDescLength, arraySize(DeviceDesc) - DeviceDescLength },
			"|macOS {}||5|{}", appleVersion, arch);
#elif defined(DEATH_TARGET_SWITCH)
		std::uint32_t switchVersion = Environment::GetSwitchVersion();
		bool isAtmosphere = Environment::HasSwitchAtmosphere();

		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|Nintendo Switch {}.{}.{}{}||9|{}",
			((switchVersion >> 16) & 0xFF), ((switchVersion >> 8) & 0xFF), (switchVersion & 0xFF), isAtmosphere ? " (Atmosphère)"_s : ""_s, arch);
#elif defined(DEATH_TARGET_UNIX)
#	if defined(DEATH_TARGET_CLANG)
		arch |= 0x100000;
#	endif

		char DeviceDesc[256] {}; std::int32_t DeviceDescLength;
		if (::gethostname(DeviceDesc, arraySize(DeviceDesc)) == 0) {
			DeviceDesc[arraySize(DeviceDesc) - 1] = '\0';
			DeviceDescLength = std::strlen(DeviceDesc);
		} else {
			DeviceDescLength = 0;
		}
		String unixFlavor = Environment::GetUnixFlavor();
		DeviceDescLength += formatInto({ DeviceDesc + DeviceDescLength, arraySize(DeviceDesc) - DeviceDescLength }, "|{}||4|{}",
			unixFlavor.empty() ? "Unix"_s : StringView(unixFlavor), arch);
#elif defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_WINDOWS_RT)
#	if defined(DEATH_TARGET_CLANG)
		arch |= 0x100000;
#	endif

		auto osVersion = Environment::WindowsVersion;
		wchar_t deviceNameW[128]; DWORD DeviceDescLength = DWORD(arraySize(deviceNameW));
		if (!::GetComputerNameW(deviceNameW, &DeviceDescLength)) {
			DeviceDescLength = 0;
		}

		char DeviceDesc[256];
		DeviceDescLength = Utf8::FromUtf16(DeviceDesc, deviceNameW, DeviceDescLength);

#	if defined(DEATH_TARGET_WINDOWS_RT)
		const char* deviceType;
		switch (Environment::CurrentDeviceType) {
			case DeviceType::Desktop: deviceType = "Desktop"; break;
			case DeviceType::Mobile: deviceType = "Mobile"; break;
			case DeviceType::Iot: deviceType = "Iot"; break;
			case DeviceType::Xbox: deviceType = "Xbox"; break;
			default: deviceType = "Unknown"; break;
		}
		DeviceDescLength += DWORD(formatInto(MutableStringView(DeviceDesc + DeviceDescLength, arraySize(DeviceDesc) - DeviceDescLength), "|Windows {}.{}.{} ({})||7|{}",
			std::int32_t((osVersion >> 48) & 0xffffu), std::int32_t((osVersion >> 32) & 0xffffu), std::int32_t(osVersion & 0xffffffffu), deviceType, arch));
#	else
		bool isWine = Environment::IsWine();
		DeviceDescLength += DWORD(formatInto({ DeviceDesc + DeviceDescLength, arraySize(DeviceDesc) - DeviceDescLength },
			isWine ? "|Windows {}.{}.{} (Wine)||3|{}" : "|Windows {}.{}.{}||3|{}",
			std::int32_t((osVersion >> 48) & 0xffffu), std::int32_t((osVersion >> 32) & 0xffffu), std::int32_t(osVersion & 0xffffffffu), arch));
#	endif
#else
		static const char DeviceDesc[] = "||||"; std::int32_t DeviceDescLength = sizeof(DeviceDesc) - 1;
#endif
		return toBase64Url(DeviceDesc, DeviceDesc + DeviceDescLength);
	}

	String PreferencesCache::GetEffectivePlayerName()
	{
		// Discord display name has the highest priority, then the player name set in the preferences, and finally the system username

		String playerName;
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		if (PreferencesCache::EnableDiscordIntegration && UI::DiscordRpcClient::Get().IsSupported()) {
			playerName = UI::DiscordRpcClient::Get().GetUserDisplayName();
		}
#endif
		if (playerName.empty()) {
			playerName = PreferencesCache::PlayerName;
			if (playerName.empty()) {
				playerName = theApplication().GetUserName();
			}
		}

		return playerName;
	}

	EpisodeContinuationState* PreferencesCache::GetEpisodeEnd(StringView episodeName, bool createIfNotFound)
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

	EpisodeContinuationStateWithLevel* PreferencesCache::GetEpisodeContinue(StringView episodeName, bool createIfNotFound)
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

	void PreferencesCache::RemoveEpisodeContinue(StringView episodeName)
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
				if (i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, String(language + ".mo"_s) })) ||
					i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, String(language + ".mo"_s) }))) {
					std::memcpy(PreferencesCache::Language, language.data(), language.size());
					std::memset(PreferencesCache::Language + language.size(), '\0', sizeof(PreferencesCache::Language) - language.size());
					break;
				}
			}

			StringView baseLanguage = I18n::TryRemoveLanguageSpecifiers(language);
			if (baseLanguage != language && !baseLanguage.empty() && baseLanguage.size() < sizeof(PreferencesCache::Language)) {
				if (baseLanguage == "en"_s) {
					break;
				}
				if (i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, String(baseLanguage + ".mo"_s) })) ||
					i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, String(baseLanguage + ".mo"_s) }))) {
					std::memcpy(PreferencesCache::Language, baseLanguage.data(), baseLanguage.size());
					std::memset(PreferencesCache::Language + baseLanguage.size(), '\0', sizeof(PreferencesCache::Language) - baseLanguage.size());
					break;
				}
			}
		}
	}
}