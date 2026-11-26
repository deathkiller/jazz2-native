#include "PreferencesCache.h"
#include "ContentResolver.h"
#include "LevelHandler.h"
#include "Input/ControlScheme.h"
#include "UI/DiscordRpcClient.h"

#include "../nCine/Application.h"
#include "../nCine/I18n.h"
#include "../nCine/Base/FrameStatistics.h"
#include "../nCine/Base/FrameTimer.h"
#include "../nCine/Base/Random.h"
#include "../nCine/Graphics/RHI/RhiFwd.h"	// RHI_LOW_POWER_GPU (a header macro, not a build define)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <Containers/SmallVector.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringStl.h>
#include <Containers/StringUtils.h>
#include <Cpu.h>
#include <Environment.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "../nCine/Backends/Android/AndroidApplication.h"
#	include "../nCine/Backends/Android/AndroidJniHelper.h"
#elif defined(DEATH_TARGET_DREAMCAST)
#	include "DreamcastIcon.h"	// Generated from "Sources/Icons/Dreamcast/Icon.ico" at configure time

#	include <dc/maple.h>
#	include <dc/maple/vmu.h>
#	include <dc/fs_vmu.h>
#	include <dc/vmu_pkg.h>
#elif defined(DEATH_TARGET_N64)
#	include <cerrno>
#	include <fcntl.h>
#	include <unistd.h>
#	include <eeprom.h>
#	include <eepromfs.h>
#	include <system.h>	// for attach_filesystem()
#elif defined(DEATH_TARGET_GAMECUBE) && defined(WITH_OGC)
#	include "../nCine/Backends/Ogc/OgcMemoryCard.h"
#	include "../nCine/Backends/Ogc/OgcStorage.h"
#elif defined(DEATH_TARGET_PS2)
#	include <cmath>
extern "C" {
#	include <libmc.h>
}
#endif
#if defined(WITH_LIBRETRO)
#	include "../nCine/Backends/Libretro/LibretroApplication.h"
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
#if defined(WITH_PHYSICS_PROBE)
	bool PreferencesCache::PhysicsProbe = false;
#endif
	PerformanceMetricsLevel PreferencesCache::PerformanceMetrics = PerformanceMetricsLevel::Off;
	bool PreferencesCache::KeepAspectRatioInCinematics = false;
	bool PreferencesCache::ShowPlayerTrails = true;
	bool PreferencesCache::ShowMinimap = true;
	bool PreferencesCache::LowWaterQuality = false;
	bool PreferencesCache::UnalignedViewport = false;
	bool PreferencesCache::PreferVerticalSplitscreen = false;
	bool PreferencesCache::PreferZoomOut = true;
#if defined(RHI_LOW_POWER_GPU)
	// Forced off (no menu item), the shader compiles dithering out on low-power GPUs anyway
	DitheringScope PreferencesCache::Dithering = DitheringScope::None;
#else
	DitheringScope PreferencesCache::Dithering = DitheringScope::Layers;
#endif
	bool PreferencesCache::BlurEffects = true;
#if defined(DEATH_TARGET_VITA)
	// Half-resolution lighting is a quarter of the cost and barely visible
	std::uint8_t PreferencesCache::LightingResolutionPercent = 50;
	// 480x272, fewer fragments and a clean 2x upscale
	std::uint8_t PreferencesCache::RenderingResolutionPercent = 50;
#else
	std::uint8_t PreferencesCache::LightingResolutionPercent = 100;
	std::uint8_t PreferencesCache::RenderingResolutionPercent = 100;
#endif
#if defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_GAMECUBE) || \
		defined(DEATH_TARGET_N64)
	// Particles are fill-rate heavy on these consoles, can be raised in Options > Graphics
	static constexpr ParticleQuality DefaultParticleQuality = ParticleQuality::Low;
#else
	static constexpr ParticleQuality DefaultParticleQuality = ParticleQuality::High;
#endif
	// Load() falls back to the same constant for out-of-range values
	ParticleQuality PreferencesCache::Particles = DefaultParticleQuality;
	bool PreferencesCache::EnableReforgedGameplay = false;
	bool PreferencesCache::EnableReforgedCamera = false;
	bool PreferencesCache::EnableReforgedHUD = false;
	bool PreferencesCache::EnableReforgedMainMenu = false;
#if defined(DEATH_TARGET_ANDROID)
	// Used to swap Android activity icons on exit/suspend
	bool PreferencesCache::EnableReforgedMainMenuInitial = false;
#endif
	bool PreferencesCache::EnableContinuousJump = true;
	bool PreferencesCache::EnableLedgeClimb = true;
	WeaponWheelStyle PreferencesCache::WeaponWheel = WeaponWheelStyle::Enabled;
	bool PreferencesCache::ShowAllWeaponWheelSlots = false;
	bool PreferencesCache::SwitchToNewWeapon = true;
	// Off on the web, it needs a local bridge
#if defined(NCINE_HAS_RGB_LIGHTS) && !defined(DEATH_TARGET_EMSCRIPTEN)
	bool PreferencesCache::EnableRgbLights = true;
#else
	bool PreferencesCache::EnableRgbLights = false;
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
#if defined(NCINE_HAS_LOG_FILE)
	bool PreferencesCache::EnableLogFile = true;
	bool PreferencesCache::_enableLogFileInitial = false;
	bool PreferencesCache::_logFileOverridden = false;
#endif
	float PreferencesCache::MasterVolume = 0.7f;
	float PreferencesCache::SfxVolume = 0.8f;
	float PreferencesCache::MusicVolume = 0.4f;
#if defined(DEATH_TARGET_3DS)
	// Half the mixing cost, samples are 11-22 kHz anyway and the DSP resamples in hardware
	std::int32_t PreferencesCache::AudioSampleRate = 22050;
#elif defined(DEATH_TARGET_PSP)
	// Native rate, still 60 fps on hardware
	std::int32_t PreferencesCache::AudioSampleRate = 44100;
#else
	std::int32_t PreferencesCache::AudioSampleRate = 0;
#endif
	bool PreferencesCache::ToggleRunAction = false;
	bool PreferencesCache::SwapJumpAndChangeWeaponInMenu = false;
#if defined(DEATH_TARGET_N64)
	GamepadType PreferencesCache::GamepadButtonLabels = GamepadType::Nintendo64;
#elif defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || \
		defined(DEATH_TARGET_WIIU) || defined(DEATH_TARGET_3DS)
	GamepadType PreferencesCache::GamepadButtonLabels = GamepadType::Switch;
#elif defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA) || \
		defined(DEATH_TARGET_PS3)
	GamepadType PreferencesCache::GamepadButtonLabels = GamepadType::PlayStationLegacy;
#else
	GamepadType PreferencesCache::GamepadButtonLabels = GamepadType::Xbox;
#endif
	std::uint8_t PreferencesCache::GamepadRumble = 1;
	bool PreferencesCache::PlayStationExtendedSupport = false;
	bool PreferencesCache::UseNativeBackButton = false;
	bool PreferencesCache::EnableTouchJoystick = false;
	bool PreferencesCache::EnableTouchVibration = true;
	TouchButtonLayout PreferencesCache::TouchButtons[(std::size_t)TouchButtonSlot::Count] = {};
	std::uint16_t PreferencesCache::SafeArea[(std::size_t)SafeAreaEdge::Count] = {};
	Uuid PreferencesCache::UniquePlayerID;
	Uuid PreferencesCache::UniqueServerID;
	String PreferencesCache::PlayerName;
	std::uint32_t PreferencesCache::PlayerFurColor = 0;
	PlayerColorMode PreferencesCache::PlayerColors = PlayerColorMode::AllLocalPlayers;
	bool PreferencesCache::EnableDiscordIntegration = true;

	String PreferencesCache::_configPath;
	HashMap<String, EpisodeContinuationState> PreferencesCache::_episodeEnd;
	HashMap<String, EpisodeContinuationStateWithLevel> PreferencesCache::_episodeContinue;

	void PreferencesCache::ResetTouchButtons()
	{
		// Default positions derived from HUD constants × (HUD::DefaultRef = 360):
		//	DpadSize=0.37 → 133px, DpadLeft=0.02 → 7px, DpadBottom=0.1 → 36px
		//	ButtonSize=0.172 → 62px, SmallButtonSize=0.098 → 35px
		TouchButtons[(std::size_t)TouchButtonSlot::Dpad]			= { Vector2f(  7.0f,  36.0f), 1.0f, TouchButtonAnchor::BottomLeft  };
		TouchButtons[(std::size_t)TouchButtonSlot::Fire]			= { Vector2f(138.0f,  14.0f), 1.0f, TouchButtonAnchor::BottomRight };
		TouchButtons[(std::size_t)TouchButtonSlot::Jump]			= { Vector2f( 69.0f,  43.0f), 1.0f, TouchButtonAnchor::BottomRight };
		TouchButtons[(std::size_t)TouchButtonSlot::Run]				= { Vector2f(  0.0f,  58.0f), 1.0f, TouchButtonAnchor::BottomRight };
		TouchButtons[(std::size_t)TouchButtonSlot::ChangeWeapon]	= { Vector2f( 66.0f, 115.0f), 1.0f, TouchButtonAnchor::BottomRight };
		TouchButtons[(std::size_t)TouchButtonSlot::Menu]			= { Vector2f(  7.0f,   7.0f), 1.0f, TouchButtonAnchor::TopRight    };
		TouchButtons[(std::size_t)TouchButtonSlot::Console]			= { Vector2f(  7.0f,   7.0f), 1.0f, TouchButtonAnchor::TopLeft     };
	}

	void PreferencesCache::ResetSafeArea()
	{
		for (std::size_t i = 0; i < (std::size_t)SafeAreaEdge::Count; i++) {
			SafeArea[i] = 0;
		}
	}

	bool PreferencesCache::IsSafeAreaEnabled()
	{
		for (std::size_t i = 0; i < (std::size_t)SafeAreaEdge::Count; i++) {
			if (SafeArea[i] != 0) {
				return true;
			}
		}
		return false;
	}

	float PreferencesCache::GetSafeAreaInset(SafeAreaEdge edge, Vector2i viewSize)
	{
		std::uint16_t value = SafeArea[(std::size_t)edge];
		if (value == 0) {
			return 0.0f;
		}

		// Left/right are relative to the width, top/bottom to the height
		float extent = (edge == SafeAreaEdge::Left || edge == SafeAreaEdge::Right ? (float)viewSize.X : (float)viewSize.Y);
		return std::round(extent * (float)value * 0.001f);
	}

	Rectf PreferencesCache::ApplySafeArea(const Rectf& bounds, Vector2i viewSize)
	{
		if (!IsSafeAreaEnabled()) {
			return bounds;
		}

		float left = GetSafeAreaInset(SafeAreaEdge::Left, viewSize);
		float top = GetSafeAreaInset(SafeAreaEdge::Top, viewSize);
		float right = GetSafeAreaInset(SafeAreaEdge::Right, viewSize);
		float bottom = GetSafeAreaInset(SafeAreaEdge::Bottom, viewSize);

		Rectf result = Rectf(bounds.X + left, bounds.Y + top, bounds.W - left - right, bounds.H - top - bottom);

		// Don't allow negative size on narrow rectangles
		if (result.W < 0.0f) {
			result.X = bounds.X + bounds.W * 0.5f;
			result.W = 0.0f;
		}
		if (result.H < 0.0f) {
			result.Y = bounds.Y + bounds.H * 0.5f;
			result.H = 0.0f;
		}
		return result;
	}

	static void ReadEpisodeContinuationState(Stream& s, EpisodeContinuationState& state)
	{
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
	}

	static void WriteEpisodeContinuationState(Stream& s, const EpisodeContinuationState& state)
	{
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
	}

#if defined(DEATH_TARGET_PS2)
namespace
{
	/**
		@brief Writes the browser metadata the console's own save browser needs into @p saveDir

		Without `icon.sys` the browser shows the save as "Corrupted Data". Failures are ignored, the save
		works either way.
	*/
	void WritePs2BrowserMetadata(StringView saveDir)
	{
		static constexpr StringView IconFileName = "Jazz2.icn"_s;
		// Slices around the body, and rings from the tip to the top
		static constexpr std::int32_t Segments = 20;
		// Lit per vertex, so the shoulder needs dense rings to avoid visible facets
		static constexpr std::int32_t BodyRings = 8;		// Tip up to the widest ring
		static constexpr std::int32_t ShoulderRings = 5;	// Widest ring to the top
		static constexpr std::int32_t Rings = BodyRings + ShoulderRings;
		static constexpr std::int32_t Leaves = 5;
		static constexpr std::int32_t BladeStations = 4;
		// Fans at both poles, quads between rings, double-sided blades ending in a triangle
		static constexpr std::int32_t IconVertexCount = (Segments * (2 * Rings - 4) +
			Leaves * 2 * ((BladeStations - 2) * 2 + 1)) * 3;
		// 128x128 texture, 16 bits per texel, RLE compressed
		static constexpr std::int32_t IconTextureRows = 128;
		static constexpr std::int32_t IconTextureRunLength = 4096;
		// Root rows, a gap against filtering bleed, then leaf rows
		static constexpr std::int32_t IconTextureRootRows = 104;
		static constexpr std::int32_t IconTextureLeafRow = 112;
		// Header, background corners, lighting, title, 3 file names, reserved
		static constexpr std::int64_t IconSysSize = 16 + 4 * 16 + 7 * 16 + 68 + 3 * 64 + 512;
		// Header, vertices (position, normal, UV, color), animation block; texture size varies
		static constexpr std::int64_t IconSizeWithoutTexture = 20 + std::int64_t(IconVertexCount) * (8 + 8 + 4 + 4) +
			20 + 8 + 8;

		// Built in memory and written only if the content differs (same size doesn't mean same content)
		MemoryStream iconData(IconSizeWithoutTexture + 1024);

		// The icon model, a carrot. The texture is required: type 0x07 without one renders black,
		// 0x0F with uncompressed data freezes the browser.
		{
			// s16 fixed point (4096 = 1.0), Y grows downward. Sized and positioned like the Open PS2 Loader
			// and HDLGameInstaller icons; the browser looks above the origin, so the model hangs at `Hang`.
			constexpr float Scale = 1.5f;
			constexpr float Hang = -11000.0f;
			constexpr float TipY = Hang + 6400.0f * Scale;	// The point of the carrot, at the bottom
			constexpr float RimY = Hang - 2200.0f * Scale;	// Where the body meets the leaves
			constexpr float LeafY = Hang - 7000.0f * Scale;	// Leaf tips, at the top
			constexpr float Radius = 2500.0f * Scale;		// At the rim, the widest the body gets
			constexpr float Pi = 3.14159265f;

			float rim[Segments][2], mid[Segments][2];
			for (std::int32_t i = 0; i < Segments; i++) {
				const float step = 2.0f * Pi / float(Segments);
				rim[i][0] = std::cos(step * float(i));
				rim[i][1] = std::sin(step * float(i));
				// Facet normal direction
				mid[i][0] = std::cos(step * (float(i) + 0.5f));
				mid[i][1] = std::sin(step * (float(i) + 0.5f));
			}

			// Root profile: power curve for the sides (not a cone), flattened quarter circle for the shoulder
			constexpr float ShoulderHeight = 900.0f * Scale;
			float ringR[Rings], ringY[Rings];
			for (std::int32_t k = 0; k < BodyRings; k++) {
				const float t = float(k) / float(BodyRings - 1);
				ringR[k] = Radius * std::pow(t, 0.7f);
				ringY[k] = TipY + (RimY - TipY) * t;
			}
			for (std::int32_t k = BodyRings; k < Rings; k++) {
				const float a = 0.5f * Pi * float(k - BodyRings + 1) / float(ShoulderRings);
				ringR[k] = Radius * std::cos(a);
				ringY[k] = RimY - ShoulderHeight * std::sin(a);
			}

			// Smooth shading, a ring's normal is the average of the two adjacent bands
			std::int16_t ringNr[Rings], ringNy[Rings];
			for (std::int32_t k = 0; k < Rings; k++) {
				const std::int32_t below = (k > 0 ? k - 1 : 0);
				const std::int32_t above = (k < Rings - 1 ? k : Rings - 2);
				const auto bandNormal = [&](std::int32_t b, float& nr, float& ny) {
					const float dR = ringR[b + 1] - ringR[b], dY = ringY[b + 1] - ringY[b];
					const float scale = 1.0f / std::sqrt(dR * dR + dY * dY);
					nr = -dY * scale;
					ny = dR * scale;
				};
				float nrBelow, nyBelow, nrAbove, nyAbove;
				bandNormal(below, nrBelow, nyBelow);
				bandNormal(above, nrAbove, nyAbove);
				const float nr = nrBelow + nrAbove, ny = nyBelow + nyAbove;
				const float scale = 4096.0f / std::sqrt(nr * nr + ny * ny);
				ringNr[k] = std::int16_t(nr * scale);
				ringNy[k] = std::int16_t(ny * scale);
			}

			// The leaves, evenly spaced around the rim
			float leaf[Leaves][2];
			for (std::int32_t i = 0; i < Leaves; i++) {
				leaf[i][0] = std::cos(2.0f * Pi * float(i) / float(Leaves));
				leaf[i][1] = std::sin(2.0f * Pi * float(i) / float(Leaves));
			}

			// Blade centreline and width at each station, arcing upward and out
			float bladeR[BladeStations], bladeY[BladeStations];
			static const float BladeW[BladeStations] = { 520.0f * Scale, 900.0f * Scale, 620.0f * Scale, 0.0f };
			// Start slightly inside the shoulder, so blades don't hover above it
			const float bladeBase = ringY[Rings - 1] + 150.0f * Scale;
			for (std::int32_t k = 0; k < BladeStations; k++) {
				const float s = float(k) / float(BladeStations - 1);
				bladeR[k] = Radius * 1.35f * std::pow(s, 1.75f);
				bladeY[k] = bladeBase + (LeafY - bladeBase) * std::pow(s, 0.75f);
			}

			// RGBA, kept low enough that lighting can't clip them
			static const std::uint8_t OrangeDeep[4] = { 0x96, 0x4A, 0x0C, 0xFF };	// At the point
			static const std::uint8_t OrangeLit[4] = { 0xC4, 0x6E, 0x12, 0xFF };	// At the widest ring
			static const std::uint8_t Green[4]     = { 0x2D, 0x87, 0x2D, 0xFF };
			static const std::uint8_t GreenDark[4] = { 0x1E, 0x5A, 0x1E, 0xFF };

			// Per-ring gradient (avoids seams), brightest at the widest ring
			std::uint8_t ringRgba[Rings][4];
			for (std::int32_t k = 0; k < Rings; k++) {
				const float up = float(k) / float(BodyRings - 1);
				const float lit = (k < BodyRings ? up
					: 1.0f - 0.18f * float(k - BodyRings + 1) / float(ShoulderRings));
				for (std::int32_t c = 0; c < 4; c++) {
					ringRgba[k][c] = std::uint8_t(float(OrangeDeep[c]) +
						(float(OrangeLit[c]) - float(OrangeDeep[c])) * lit);
				}
			}

			Stream* so = &iconData;
			so->WriteValueAsLE<std::uint32_t>(0x00010000);	// File identifier
			so->WriteValueAsLE<std::uint32_t>(1);			// Animation shapes
			so->WriteValueAsLE<std::uint32_t>(0x0E);		// Texture type: present, and RLE compressed
			so->WriteValueAsLE<std::uint32_t>(0x3F800000);	// Reserved, 1.0f as the format writes it
			so->WriteValueAsLE<std::uint32_t>(IconVertexCount);	// Vertices, always a multiple of three

			// Position, normal, UV (4096 = whole image) and color
			const auto vertex = [&so](float x, float y, float z,
				std::int16_t nx, std::int16_t ny, std::int16_t nz, std::int16_t u, std::int16_t v,
				const std::uint8_t* color) {
				so->WriteValueAsLE<std::int16_t>(std::int16_t(x));
				so->WriteValueAsLE<std::int16_t>(std::int16_t(y));
				so->WriteValueAsLE<std::int16_t>(std::int16_t(z));
				so->WriteValueAsLE<std::int16_t>(0);
				so->WriteValueAsLE<std::int16_t>(nx);
				so->WriteValueAsLE<std::int16_t>(ny);
				so->WriteValueAsLE<std::int16_t>(nz);
				so->WriteValueAsLE<std::int16_t>(0);
				so->WriteValueAsLE<std::int16_t>(u);
				so->WriteValueAsLE<std::int16_t>(v);
				so->Write(color, 4);
			};
			// Center of texel @p at, so filtering doesn't mix neighbors
			const auto texel = [](float at) {
				return std::int16_t((at + 0.5f) * (4096.0f / float(IconTextureRows)));
			};

			// V along the root by height (not ring index), U around it
			const auto ringV = [&](std::int32_t k) {
				const float along = (TipY - ringY[k]) / (TipY - ringY[Rings - 1]);
				return texel(along * float(IconTextureRootRows - 1));
			};
			// Not wrapped, so the seam slice doesn't map the whole image backwards
			const auto sliceU = [&](float s) {
				return texel(s / float(Segments) * float(IconTextureRows - 1));
			};
			// Vertex on ring @p k at slice @p s
			const auto ringVertex = [&](std::int32_t k, std::int32_t s) {
				const std::int32_t w = s % Segments;
				vertex(rim[w][0] * ringR[k], ringY[k], rim[w][1] * ringR[k],
					std::int16_t(rim[w][0] * ringNr[k]), ringNy[k], std::int16_t(rim[w][1] * ringNr[k]),
					sliceU(float(s)), ringV(k), ringRgba[k]);
			};
			// Pole vertex, uses the facet's normal direction
			const auto poleVertex = [&](std::int32_t k, std::int32_t i) {
				vertex(0.0f, ringY[k], 0.0f, std::int16_t(mid[i][0] * ringNr[k]), ringNy[k],
					std::int16_t(mid[i][1] * ringNr[k]),
					sliceU(float(i) + 0.5f), ringV(k), ringRgba[k]);
			};

			// Tip
			for (std::int32_t i = 0; i < Segments; i++) {
				poleVertex(0, i);
				ringVertex(1, i);
				ringVertex(1, i + 1);
			}
			// Body
			for (std::int32_t k = 1; k < Rings - 2; k++) {
				for (std::int32_t i = 0; i < Segments; i++) {
					ringVertex(k, i); ringVertex(k + 1, i); ringVertex(k + 1, i + 1);
					ringVertex(k, i); ringVertex(k + 1, i + 1); ringVertex(k, i + 1);
				}
			}
			// Top, opposite winding
			for (std::int32_t i = 0; i < Segments; i++) {
				poleVertex(Rings - 1, i);
				ringVertex(Rings - 2, i + 1);
				ringVertex(Rings - 2, i);
			}
			for (std::int32_t i = 0; i < Leaves; i++) {
				const float dx = leaf[i][0], dz = leaf[i][1];

				// Normal per station, negated because Y grows downward (same as the body)
				std::int16_t bladeNr[BladeStations], bladeNy[BladeStations];
				for (std::int32_t k = 0; k < BladeStations; k++) {
					const std::int32_t from = (k > 0 ? k - 1 : 0);
					const std::int32_t to = (k < BladeStations - 1 ? k + 1 : k);
					const float dR = bladeR[to] - bladeR[from], dY = bladeY[to] - bladeY[from];
					const float scale = 4096.0f / std::sqrt(dR * dR + dY * dY);
					bladeNr[k] = std::int16_t(-dY * scale);
					bladeNy[k] = std::int16_t(dR * scale);
				}

				// Both sides, slightly offset along the normal to avoid z-fighting
				constexpr float Thickness = 16.0f * Scale / 4096.0f;
				for (std::int32_t side = 0; side < 2; side++) {
					const float f = (side == 0 ? 1.0f : -1.0f);
					// Corner of station @p k, @p edge selects the side
					const auto blade = [&](std::int32_t k, float edge) {
						const float across = BladeW[k] * edge * f;
						const float nx = dx * float(bladeNr[k]) * f, ny = float(bladeNy[k]) * f;
						const float nz = dz * float(bladeNr[k]) * f;
						// Leaf rows of the texture, V across and U along the blade
						const float row = float(IconTextureLeafRow) +
							(edge + 1.0f) * 0.5f * float(IconTextureRows - 1 - IconTextureLeafRow);
						const float along = float(k) / float(BladeStations - 1) * float(IconTextureRows - 1);
						vertex(dx * bladeR[k] - dz * across + nx * Thickness, bladeY[k] + ny * Thickness,
							dz * bladeR[k] + dx * across + nz * Thickness,
							std::int16_t(nx), std::int16_t(ny), std::int16_t(nz),
							texel(along), texel(row), (k == 0 ? GreenDark : Green));
					};
					// Quads between stations, a triangle at the tip
					for (std::int32_t k = 0; k < BladeStations - 2; k++) {
						blade(k, 1.0f); blade(k, -1.0f); blade(k + 1, -1.0f);
						blade(k, 1.0f); blade(k + 1, -1.0f); blade(k + 1, 1.0f);
					}
					blade(BladeStations - 2, 1.0f);
					blade(BladeStations - 2, -1.0f);
					blade(BladeStations - 1, 0.0f);
				}
			}

			// Single frame with a single key (no animation)
			so->WriteValueAsLE<std::uint32_t>(1);			// Tag
			so->WriteValueAsLE<std::uint32_t>(1);			// Frame length
			so->WriteValueAsLE<std::uint32_t>(0x3F800000);	// Animation speed, 1.0f
			so->WriteValueAsLE<std::uint32_t>(0);			// Play offset
			so->WriteValueAsLE<std::uint32_t>(1);			// Frames
			so->WriteValueAsLE<std::uint32_t>(0);			// Shape of frame 0
			so->WriteValueAsLE<std::uint32_t>(1);			// Keys in frame 0
			so->WriteValueAsLE<std::uint32_t>(0);			// Key time, 0.0f
			so->WriteValueAsLE<std::uint32_t>(0);			// Key value, 0.0f

			// 128x128 BGR555 detail map multiplied with the vertex colors. Speckles instead of bands, because
			// affine texture mapping bends straight edges. The base is below max, so specks can be lighter too.
			constexpr std::uint16_t SpeckleBase = 29, SpeckleDark = 27, SpeckleLight = 31;
			constexpr std::int32_t SpeckleSize = 4;			// Texels per speck
			// Deterministic hash, so the icon isn't rewritten on every launch
			const auto speckle = [](std::int32_t x, std::int32_t y) {
				std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u;
				h = (h ^ (h >> 13)) * 1274126177u;
				h ^= (h >> 16);
				const std::uint32_t pick = h & 0xFF;
				const std::uint16_t g = (pick < 30 ? SpeckleDark : (pick < 55 ? SpeckleLight : SpeckleBase));
				return std::uint16_t(g | (g << 5) | (g << 10));
			};
			Array<std::uint16_t> texels(NoInit, std::size_t(IconTextureRows) * IconTextureRows);
			for (std::int32_t y = 0; y < IconTextureRows; y++) {
				for (std::int32_t x = 0; x < IconTextureRows; x++) {
					texels[y * IconTextureRows + x] = speckle(x / SpeckleSize, y / SpeckleSize);
				}
			}

			// RLE: code < 0x8000 repeats the next word, otherwise a literal block of 0x10000 - code words
			// (not 0xFFFF - code as documented, verified with the Open PS2 Loader icon)
			MemoryStream texData(8192);
			const std::int32_t TexelCount = IconTextureRows * IconTextureRows;
			for (std::int32_t at = 0; at < TexelCount; ) {
				std::int32_t run = 1;
				while (at + run < TexelCount && texels[at + run] == texels[at] && run < IconTextureRunLength) {
					run++;
				}
				if (run >= 2) {
					texData.WriteValueAsLE<std::uint16_t>(std::uint16_t(run));
					texData.WriteValueAsLE<std::uint16_t>(texels[at]);
					at += run;
					continue;
				}
				// Literal block until a run of 3+ (a run of 2 isn't worth breaking out)
				const std::int32_t from = at;
				std::int32_t count = 0;
				while (at < TexelCount && count < 255) {
					std::int32_t ahead = 1;
					while (at + ahead < TexelCount && texels[at + ahead] == texels[at]) {
						ahead++;
					}
					if (ahead >= 3 || count + ahead > 255) {
						break;
					}
					at += ahead;
					count += ahead;
				}
				texData.WriteValueAsLE<std::uint16_t>(std::uint16_t(0x10000 - count));
				texData.Write(&texels[from], count * 2);
			}
			so->WriteValueAsLE<std::uint32_t>(std::uint32_t(texData.GetSize()));
			so->Write(texData.GetBuffer(), texData.GetSize());
		}

		MemoryStream sysData(IconSysSize);
		Stream* so = &sysData;

		const auto writeVector = [&so](float x, float y, float z, float w) {
			so->WriteValueAsLE<float>(x); so->WriteValueAsLE<float>(y);
			so->WriteValueAsLE<float>(z); so->WriteValueAsLE<float>(w);
		};
		const auto writeName = [&so](StringView name) {
			// Fixed 64 bytes, zero padded
			std::uint8_t field[64] = {};
			std::memcpy(field, name.data(), (name.size() < sizeof(field) ? name.size() : sizeof(field) - 1));
			so->Write(field, sizeof(field));
		};

		so->Write("PS2D", 4);
		so->WriteValueAsLE<std::uint16_t>(0);			// Saved data
		// Line break offset in bytes (2 per character), puts "Resurrection" on the second line
		so->WriteValueAsLE<std::uint16_t>(12);
		so->WriteValueAsLE<std::uint32_t>(0);			// Unused
		so->WriteValueAsLE<std::uint32_t>(0);			// Opaque background
		for (std::int32_t corner = 0; corner < 4; corner++) {
			// Same color in all corners
			so->WriteValueAsLE<std::uint32_t>(0x10);
			so->WriteValueAsLE<std::uint32_t>(0x20);
			so->WriteValueAsLE<std::uint32_t>(0x50);
			so->WriteValueAsLE<std::uint32_t>(0x80);
		}
		// Usual light directions, colors scaled so ambient + all lights never clip
		writeVector(0.5f, 0.5f, 0.5f, 0.0f);			// Three light directions...
		writeVector(0.0f, -0.4f, -0.1f, 0.0f);
		writeVector(-0.5f, -0.5f, 0.5f, 0.0f);
		writeVector(0.45f, 0.41f, 0.31f, 1.0f);			// ...their colours, a warm key...
		writeVector(0.26f, 0.25f, 0.21f, 1.0f);			// ...a softer fill...
		writeVector(0.12f, 0.13f, 0.22f, 1.0f);			// ...and a cool one from behind...
		writeVector(0.45f, 0.45f, 0.48f, 1.0f);			// ...and the ambient term

		// Shift-JIS full-width characters (half-width ones aren't displayed), anything else becomes a space
		const auto toFullWidth = [](char c) -> std::uint16_t {
			if (c >= '0' && c <= '9') return std::uint16_t(0x824F + (c - '0'));
			if (c >= 'A' && c <= 'Z') return std::uint16_t(0x8260 + (c - 'A'));
			if (c >= 'a' && c <= 'z') return std::uint16_t(0x8281 + (c - 'a'));
			return 0x8140;
		};
		std::uint8_t title[68] = {};
		static constexpr StringView TitleText = "Jazz2 Resurrection"_s;
		std::size_t titleLength = 0;
		for (char c : TitleText) {
			if (titleLength + 2 > sizeof(title)) {
				break;
			}
			const std::uint16_t wide = toFullWidth(c);
			// Big-endian
			title[titleLength++] = std::uint8_t(wide >> 8);
			title[titleLength++] = std::uint8_t(wide & 0xFF);
		}
		so->Write(title, sizeof(title));

		writeName(IconFileName);						// Listed
		writeName(IconFileName);						// Being copied
		writeName(IconFileName);						// Being deleted

		std::uint8_t reserved[512] = {};
		so->Write(reserved, sizeof(reserved));

		// Write each file only if it differs from the one on the card
		const auto replaceIfDifferent = [&saveDir](StringView name, const MemoryStream& built) {
			const std::int64_t size = built.GetSize();
			String path = fs::CombinePath(saveDir, name);
			{
				auto onCard = fs::Open(path, FileAccess::Read);
				if (onCard->IsValid() && onCard->GetSize() == size) {
					Array<std::uint8_t> previous(NoInit, std::size_t(size));
					if (onCard->Read(previous.data(), size) == size &&
						std::memcmp(previous.data(), built.GetBuffer(), std::size_t(size)) == 0) {
						return;
					}
				}
			}
			auto out = fs::Open(path, FileAccess::Write);
			if (out->IsValid()) {
				out->Write(built.GetBuffer(), size);
			}
		};
		replaceIfDifferent(IconFileName, iconData);
		replaceIfDifferent("icon.sys"_s, sysData);
	}
}
#endif

#if defined(DEATH_TARGET_N64)
namespace
{
	// The only file in the EEPROM filesystem
	static constexpr char EepromConfigFile[] = "/JAZZ2CFG";
	// All free space, set in Initialize() (the EEPROM can be 4 or 16 kbit)
	static std::size_t EepromConfigFileSize = 0;

	/**
		@brief One open handle of the "eeprom:/" bridge below

		eepromfs works only with whole files, so a handle is a RAM copy with a cursor, committed on close.
	*/
	struct EepromOpenFile
	{
		std::unique_ptr<std::uint8_t[]> Buffer;
		/** @brief Cursor, in bytes from the start of the file */
		std::size_t Position;
		/** @brief Size of valid data */
		std::size_t Size;
		bool Writable;
		/** @brief A write didn't fit, so the file must not be committed on close */
		bool Overflowed;
	};

	void* EepromFsOpen(char* name, int flags)
	{
		// The prefix is already stripped, a leading slash may remain
		const char* fileName = (name[0] == '/' ? name + 1 : name);
		if (std::strcmp(fileName, EepromConfigFile + 1) != 0) {
			errno = ENOENT;
			return nullptr;
		}

		auto file = std::make_unique<EepromOpenFile>();
		file->Buffer = std::make_unique<std::uint8_t[]>(EepromConfigFileSize);
		file->Position = 0;
		file->Writable = ((flags & O_ACCMODE) != O_RDONLY);
		file->Overflowed = false;
		if ((flags & O_TRUNC) != 0) {
			file->Size = 0;
		} else {
			if (eepfs_read(EepromConfigFile, file->Buffer.get(), EepromConfigFileSize) != EEPFS_ESUCCESS) {
				errno = EIO;
				return nullptr;
			}
			file->Size = EepromConfigFileSize;
		}
		return file.release();
	}

	int EepromFsFstat(void* file, struct stat* st)
	{
		auto* f = static_cast<EepromOpenFile*>(file);
		std::memset(st, 0, sizeof(*st));
		st->st_mode = S_IFREG;
		st->st_nlink = 1;
		st->st_size = off_t(f->Size);
		return 0;
	}

	int EepromFsStat(char* name, struct stat* st)
	{
		const char* fileName = (name[0] == '/' ? name + 1 : name);
		if (std::strcmp(fileName, EepromConfigFile + 1) != 0) {
			errno = ENOENT;
			return -1;
		}
		std::memset(st, 0, sizeof(*st));
		st->st_mode = S_IFREG;
		st->st_nlink = 1;
		st->st_size = off_t(EepromConfigFileSize);
		return 0;
	}

	int EepromFsLseek(void* file, int ptr, int dir)
	{
		auto* f = static_cast<EepromOpenFile*>(file);
		std::int64_t target;
		switch (dir) {
			case SEEK_SET: target = ptr; break;
			case SEEK_CUR: target = std::int64_t(f->Position) + ptr; break;
			case SEEK_END: target = std::int64_t(f->Size) + ptr; break;
			default: errno = EINVAL; return -1;
		}
		if (target < 0 || target > std::int64_t(EepromConfigFileSize)) {
			errno = EINVAL;
			return -1;
		}
		f->Position = std::size_t(target);
		return int(target);
	}

	int EepromFsRead(void* file, std::uint8_t* ptr, int len)
	{
		auto* f = static_cast<EepromOpenFile*>(file);
		if (len < 0) {
			errno = EINVAL;
			return -1;
		}
		std::size_t available = (f->Position < f->Size ? f->Size - f->Position : 0);
		std::size_t n = (std::size_t(len) < available ? std::size_t(len) : available);
		std::memcpy(ptr, &f->Buffer[f->Position], n);
		f->Position += n;
		return int(n);
	}

	int EepromFsWrite(void* file, std::uint8_t* ptr, int len)
	{
		auto* f = static_cast<EepromOpenFile*>(file);
		if (len < 0) {
			errno = EINVAL;
			return -1;
		}
		if (!f->Writable) {
			errno = EBADF;
			return -1;
		}
		std::size_t capacity = (f->Position < EepromConfigFileSize ? EepromConfigFileSize - f->Position : 0);
		if (std::size_t(len) > capacity) {
			// Doesn't fit, refuse the whole write
			f->Overflowed = true;
			errno = ENOSPC;
			return -1;
		}
		std::memcpy(&f->Buffer[f->Position], ptr, std::size_t(len));
		f->Position += std::size_t(len);
		if (f->Position > f->Size) {
			f->Size = f->Position;
		}
		return len;
	}

	int EepromFsClose(void* file)
	{
		std::unique_ptr<EepromOpenFile> f(static_cast<EepromOpenFile*>(file));
		int result = 0;
		if (f->Writable) {
			// Commit on close, but never after an overflow (a partial config would replace a good one)
			if (f->Overflowed) {
				LOGE("Cannot save settings, the configuration does not fit into the {}-byte EEPROM file", EepromConfigFileSize);
				errno = ENOSPC;
				result = -1;
			} else if (eepfs_write(EepromConfigFile, f->Buffer.get(), EepromConfigFileSize) != EEPFS_ESUCCESS) {
				errno = EIO;
				result = -1;
			}
			// eepfs_write() returns immediately, the data is flushed in the background (~1.5 s)
		}
		return result;
	}

	filesystem_t CreateEepromFilesystem()
	{
		filesystem_t fs = {};
		fs.open = EepromFsOpen;
		fs.fstat = EepromFsFstat;
		fs.stat = EepromFsStat;
		fs.lseek = EepromFsLseek;
		fs.read = EepromFsRead;
		fs.write = EepromFsWrite;
		fs.close = EepromFsClose;
		return fs;
	}
}
#endif

	void PreferencesCache::Initialize(AppConfiguration& config)
	{
		bool resetConfig = false;

#if defined(DEATH_TARGET_EMSCRIPTEN)
		auto configDir = "/Persistent"_s;
		fs::MountAsPersistent(configDir);
		_configPath = "/Persistent/Jazz2.config"_s;

		for (int32_t i = 0; i < config.GetArgc(); i++) {
			auto arg = config.GetArgv(i);
			if (arg == "/reset-config"_s) {
				resetConfig = true;
			}
		}
#else
		_configPath = "Jazz2.config"_s;
		bool overrideConfigPath = false;

#	if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && \
		!defined(DEATH_TARGET_N64) && !defined(DEATH_TARGET_WII) && !defined(DEATH_TARGET_WIIU) && !defined(DEATH_TARGET_GAMECUBE) && \
		!defined(DEATH_TARGET_3DS) && !defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_PS2) && !defined(DEATH_TARGET_PS3) && \
		!defined(DEATH_TARGET_PSP) && !defined(DEATH_TARGET_VITA)
		for (std::int32_t i = 0; i < config.GetArgc(); i++) {
			auto arg = config.GetArgv(i);
			if (arg == "/config"_s) {
				if (i + 1 < config.GetArgc()) {
					_configPath = config.GetArgv(i + 1);
					overrideConfigPath = true;
					i++;
				}
			} else if (arg == "/reset-config"_s) {
				resetConfig = true;
			}
		}
#	endif

#	if defined(WITH_LIBRETRO)
		// Use the frontend's save directory instead of the user or portable path
		if (!overrideConfigPath) {
			StringView saveDir = Backends::theLibretroApplication().GetHostPaths().Save;
			if (!saveDir.empty()) {
				// Own subdirectory, the directory is shared by all cores
				_configPath = fs::CombinePath({saveDir, NCINE_APP, "Jazz2.config"_s});
				fs::CreateDirectories(fs::GetDirectoryName(_configPath));
				overrideConfigPath = true;
			}
		}
#	endif

		// A portable config wins, except on read-only disc/ROM platforms (a lookup there costs a seek)
#	if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_PS2)
		constexpr bool hasPortableConfig = false;
#	else
		const bool hasPortableConfig = fs::IsReadableFile(_configPath);
#	endif

		// If config path is not overriden and portable config doesn't exist, use common path for current user
		if (!overrideConfigPath && !hasPortableConfig) {
#	if defined(DEATH_TARGET_DREAMCAST)
			// First VMU found (names are max. 12 characters), otherwise the path stays on the disc and saving fails
			if (maple_device_t* memoryCard = maple_enum_type(0, MAPLE_FUNC_MEMCARD)) {
				char vmuPath[] = "/vmu/a1/Jazz2";
				vmuPath[5] = char('a' + memoryCard->port);
				vmuPath[6] = char('0' + memoryCard->unit);
				_configPath = String(vmuPath, sizeof(vmuPath) - 1);
			} else {
				LOGW("No memory card found, settings and progress cannot be saved");
				auto& resolver = ContentResolver::Get();
				_configPath = fs::CombinePath(fs::GetDirectoryName(resolver.GetSourcePath()), "Jazz2.config"_s);
			}
#	elif defined(DEATH_TARGET_PS2)
			// Removable storage (USB, MX4SIO) the game booted from, otherwise the first usable memory card
			// ("mc0:"/"mc1:", must be probed first), otherwise the path stays on the disc and saving fails
			StringView writablePath = ContentResolver::Get().GetWritablePath();
			if (!writablePath.empty()) {
				_configPath = fs::CombinePath(writablePath, "Jazz2.config"_s);
			} else {
				bool memoryCardFound = false;
				// Plain `int`, `std::int32_t` is `long` on the EE
				for (int port = 0; port < 2 && !memoryCardFound; port++) {
					int type = 0, freeClusters = 0, formatted = 0;
					mcGetInfo(port, 0, &type, &freeClusters, &formatted);
					int probe = -1;
					mcSync(MC_WAIT, nullptr, &probe);

					if (type != MC_TYPE_PS2) {
						continue;	// Empty slot or a PS1 card
					}

					// Try to use the card, the probe result isn't reliable on the first call
					char saveDir[] = "mc0:/Jazz2";
					saveDir[2] = char('0' + port);
					if (fs::CreateDirectories(saveDir)) {
						_configPath = fs::CombinePath(StringView(saveDir, sizeof(saveDir) - 1), "Jazz2.config"_s);
						memoryCardFound = true;
						// Otherwise the browser shows "Corrupted Data"
						WritePs2BrowserMetadata(StringView(saveDir, sizeof(saveDir) - 1));
					} else if (probe == -2 || formatted != MC_FORMATTED) {
						// Never format it here, that would erase all saves on the card
						LOGW("The memory card in slot {} is not formatted, so nothing can be saved to it", port);
					}
				}
				if (!memoryCardFound) {
					LOGW("No usable memory card found, settings and progress cannot be saved");
					auto& resolver = ContentResolver::Get();
					_configPath = fs::CombinePath(fs::GetDirectoryName(resolver.GetSourcePath()), "Jazz2.config"_s);
				}
			}
#	elif defined(DEATH_TARGET_N64)
			// Save EEPROM through the "eeprom:/" bridge above, otherwise the path stays on the ROM and saving fails
			bool eepromFound = false;
			if (eeprom_present() != EEPROM_NONE) {
				// The first block holds the signature
				EepromConfigFileSize = (eeprom_total_blocks() - 1) * EEPROM_BLOCK_SIZE;
				const eepfs_entry_t eepfsEntries[] = {
					{ EepromConfigFile, EepromConfigFileSize, false, false }
				};
				if (eepfs_init(eepfsEntries, arraySize(eepfsEntries)) == EEPFS_ESUCCESS) {
					if (!eepfs_verify_signature()) {
						// Empty or used by another game, wipe it (takes a few seconds, only once)
						LOGI("EEPROM signature mismatch, erasing the whole EEPROM");
						eepfs_wipe();
						eeprom_wait_idle();
					}
					static filesystem_t eepromFilesystem = CreateEepromFilesystem();
					if (attach_filesystem("eeprom:/", &eepromFilesystem) == 0) {
						_configPath = "eeprom:/JAZZ2CFG"_s;
						eepromFound = true;
					}
				}
			}
			if (!eepromFound) {
				LOGW("No usable EEPROM found, settings and progress cannot be saved");
				auto& resolver = ContentResolver::Get();
				_configPath = fs::CombinePath(fs::GetDirectoryName(resolver.GetSourcePath()), "Jazz2.config"_s);
			}
#	elif defined(DEATH_TARGET_GAMECUBE) && defined(WITH_OGC)
			// SD card with the content, otherwise the first usable memory card, otherwise saving fails
			auto& resolver = ContentResolver::Get();
			StringView writablePath = resolver.GetWritablePath();
			if (!writablePath.empty()) {
				_configPath = fs::CombinePath(writablePath, "Jazz2.config"_s);
			} else {
				Backends::OgcMemoryCard::Initialize(Backends::OgcStorage::GetGameId());
				if (Backends::OgcMemoryCard::IsUsable(0)) {
					_configPath = "mca:/Jazz2.config"_s;
				} else if (Backends::OgcMemoryCard::IsUsable(1)) {
					_configPath = "mcb:/Jazz2.config"_s;
				} else {
					LOGW("No usable memory card found, settings and progress cannot be saved");
					_configPath = fs::CombinePath(fs::GetDirectoryName(resolver.GetSourcePath()), "Jazz2.config"_s);
				}
			}
#	elif defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_WIIU) || \
				defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA) || defined(DEATH_TARGET_AMIGAOS)
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
#endif

#if defined(NCINE_HAS_LOG_FILE)
		// The command line takes precedence over EnableLogFile, which is applied once the config is read
#	if defined(DEATH_TRACE_LOG_PATH)
		StringView logFileName = DEATH_TRACE_LOG_PATH;
#	else
		StringView logFileName = "Jazz2.log"_s;
#	endif
#	if defined(DEATH_TARGET_IOS)
		// "Documents" is accessible through the Files app
		String logDir = fs::GetDirectoryName(ContentResolver::Get().GetSourcePath());
#	else
		String logDir = configDir;
#	endif
#	if (defined(DEATH_TARGET_GAMECUBE) && defined(WITH_OGC)) || defined(DEATH_TARGET_PS2)
		// Only on an SD card or removable storage, a memory card is too small for logs
		const bool canWriteLogFile = !ContentResolver::Get().GetWritablePath().empty();
#	else
		constexpr bool canWriteLogFile = true;
#	endif

		DEATH_UNUSED bool logFileSpecified = false;
		for (std::int32_t i = 0; i < config.GetArgc(); i++) {
			auto arg = config.GetArgv(i);
			if (arg == "/log:file"_s || arg.hasPrefix("/log:file:"_s)) {
				_logFileOverridden = true;
				logFileSpecified = true;
				if (canWriteLogFile) {
					fs::CreateDirectories(logDir);
					if (arg.size() > "/log:file:"_s.size()) {
						// Custom log path, don't archive the previous one
						theApplication().AttachTraceTarget(fs::CombinePath(logDir, arg.exceptPrefix("/log:file:"_s)), false);
					} else {
						theApplication().AttachTraceTarget(fs::CombinePath(logDir, logFileName));
					}
				}
			}
#	if defined(DEATH_TARGET_WINDOWS)
			else if (arg == "/log"_s) {
				_logFileOverridden = true;
				theApplication().AttachTraceTarget(Application::ConsoleTarget);
			}
#	endif
		}
#endif

		ControlScheme::Reset();

		// Version of an older config file, so a backup can be made before it's overwritten
		DEATH_UNUSED std::uint8_t upgradedFromVersion = 0;

		// Try to read config file
		if (!resetConfig) {
			auto s = fs::Open(_configPath, FileAccess::Read);
			if (s->GetSize() > 18) {
				std::uint64_t signature = s->ReadValueAsLE<std::uint64_t>();
				std::uint8_t fileType = s->ReadValue<std::uint8_t>();
				std::uint8_t version = s->ReadValue<std::uint8_t>();

				if (signature == 0x2095A59FF0BFBBEF && fileType == ContentFileType::Config && version <= FileVersion) {
					if (version < FileVersion) {
						upgradedFromVersion = version;
					}

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
					if ((boolOptions & BoolOptions::ShowPerformanceMetrics) == BoolOptions::ShowPerformanceMetrics) {
						PerformanceMetrics = ((boolOptions & BoolOptions::ShowDetailedPerformanceMetrics) == BoolOptions::ShowDetailedPerformanceMetrics
							? PerformanceMetricsLevel::Detailed : PerformanceMetricsLevel::Basic);
					} else {
						PerformanceMetrics = PerformanceMetricsLevel::Off;
					}
					KeepAspectRatioInCinematics = ((boolOptions & BoolOptions::KeepAspectRatioInCinematics) == BoolOptions::KeepAspectRatioInCinematics);
					ShowPlayerTrails = ((boolOptions & BoolOptions::ShowPlayerTrails) == BoolOptions::ShowPlayerTrails);
					LowWaterQuality = ((boolOptions & BoolOptions::LowWaterQuality) == BoolOptions::LowWaterQuality);
					UnalignedViewport = ((boolOptions & BoolOptions::UnalignedViewport) == BoolOptions::UnalignedViewport);
					PreferVerticalSplitscreen = ((boolOptions & BoolOptions::PreferVerticalSplitscreen) == BoolOptions::PreferVerticalSplitscreen);
					PreferZoomOut = ((boolOptions & BoolOptions::PreferZoomOut) == BoolOptions::PreferZoomOut);
					Dithering = DitheringScope((std::uint64_t(boolOptions) >> DitheringShift) & 0x03);
					if (Dithering == DitheringScope::None && (boolOptions & BoolOptions::Reserved) == BoolOptions::Reserved) {
						// Dithering of the background layers of older versions
						Dithering = DitheringScope::Layers;
					}
					EnableReforgedGameplay = ((boolOptions & BoolOptions::EnableReforgedGameplay) == BoolOptions::EnableReforgedGameplay);
					EnableLedgeClimb = ((boolOptions & BoolOptions::EnableLedgeClimb) == BoolOptions::EnableLedgeClimb);
					WeaponWheel = ((boolOptions & BoolOptions::EnableWeaponWheel) == BoolOptions::EnableWeaponWheel ? WeaponWheelStyle::Enabled : WeaponWheelStyle::Disabled);
					EnableRgbLights = ((boolOptions & BoolOptions::EnableRgbLights) == BoolOptions::EnableRgbLights);
					AllowUnsignedScripts = ((boolOptions & BoolOptions::AllowUnsignedScripts) == BoolOptions::AllowUnsignedScripts);
					ToggleRunAction = ((boolOptions & BoolOptions::ToggleRunAction) == BoolOptions::ToggleRunAction);
					SwapJumpAndChangeWeaponInMenu = ((boolOptions & BoolOptions::SwapJumpAndChangeWeaponInMenu) == BoolOptions::SwapJumpAndChangeWeaponInMenu);
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

					if (version >= 17) {
						EnableReforgedCamera = ((boolOptions & BoolOptions::EnableReforgedCamera) == BoolOptions::EnableReforgedCamera);
					} else {
						// The camera used to follow the gameplay setting
						EnableReforgedCamera = EnableReforgedGameplay;
					}

					if (WeaponWheel != WeaponWheelStyle::Disabled && (boolOptions & BoolOptions::ShowWeaponWheelAmmoCount) == BoolOptions::ShowWeaponWheelAmmoCount) {
						WeaponWheel = WeaponWheelStyle::EnabledWithAmmoCount;
					}
					ShowAllWeaponWheelSlots = ((boolOptions & BoolOptions::ShowAllWeaponWheelSlots) == BoolOptions::ShowAllWeaponWheelSlots);

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

					// v14+: Removed the 4 legacy TouchPadding bytes - convert old format on load
					/*Vector2f legacyLeftPadding, legacyRightPadding;*/
					if (version < 14) {
						/*constexpr float TouchPaddingMultiplier = 0.003f;
						legacyLeftPadding.X  = std::round(uc.ReadValue<std::int8_t>() / (TouchPaddingMultiplier * INT8_MAX));
						legacyLeftPadding.Y  = std::round(uc.ReadValue<std::int8_t>() / (TouchPaddingMultiplier * INT8_MAX));
						legacyRightPadding.X = std::round(uc.ReadValue<std::int8_t>() / (TouchPaddingMultiplier * INT8_MAX));
						legacyRightPadding.Y = std::round(uc.ReadValue<std::int8_t>() / (TouchPaddingMultiplier * INT8_MAX));*/
						uc.ReadValue<std::int32_t>();
					}

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

					if (version >= 12) {
						EnableContinuousJump = ((boolOptions & BoolOptions::EnableContinuousJump) == BoolOptions::EnableContinuousJump);
					}

					if (version >= 13) {
						// These 2 new options needs to be enabled by default
						BlurEffects = ((boolOptions & BoolOptions::BlurEffects) == BoolOptions::BlurEffects);
						LightingResolutionPercent = std::clamp(uc.ReadValue<std::uint8_t>(), std::uint8_t(10), std::uint8_t(100));
					}

					// Forced off for any version, there's no menu item to turn it off again on these GPUs
#if defined(RHI_LOW_POWER_GPU)
					Dithering = DitheringScope::None;
#endif

					// Touch button per-slot configuration (v14+)
					EnableTouchJoystick = ((boolOptions & BoolOptions::EnableTouchJoystick) == BoolOptions::EnableTouchJoystick);
					EnableTouchVibration = ((boolOptions & BoolOptions::EnableTouchVibration) == BoolOptions::EnableTouchVibration);
					if (version >= 14) {
						for (std::size_t i = 0; i < (std::size_t)TouchButtonSlot::Count; i++) {
							TouchButtons[i].EdgeOffset.X = (float)uc.ReadValueAsLE<std::int16_t>();
							TouchButtons[i].EdgeOffset.Y = (float)uc.ReadValueAsLE<std::int16_t>();
							std::uint8_t scaleByte = uc.ReadValue<std::uint8_t>();
							TouchButtons[i].Scale = 0.5f + (scaleByte / 255.0f) * 2.5f;
							TouchButtons[i].Anchor = (TouchButtonAnchor)uc.ReadValue<std::uint8_t>();
						}
					} else {
						// Convert from legacy left/right padding to per-button layout
						ResetTouchButtons();
						/*TouchButtons[(std::size_t)TouchButtonSlot::Dpad].EdgeOffset.X += legacyLeftPadding.X;
						TouchButtons[(std::size_t)TouchButtonSlot::Dpad].EdgeOffset.Y -= legacyLeftPadding.Y;
						for (std::size_t i = (std::size_t)TouchButtonSlot::Fire; i <= (std::size_t)TouchButtonSlot::ChangeWeapon; i++) {
							TouchButtons[i].EdgeOffset.X -= legacyRightPadding.X;
							TouchButtons[i].EdgeOffset.Y -= legacyRightPadding.Y;
						}*/
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

						_episodeEnd.emplace(Death::move(episodeName), Death::move(state));
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
							_episodeContinue.emplace(Death::move(episodeName), Death::move(stateWithLevel));
						} else {
							// Struct has different size, so it's better to skip it
							nameLength = uc.ReadValue<std::uint8_t>();
							uc.Seek(nameLength + episodeContinueSize, SeekOrigin::Current);
						}
					}

					if (version >= 15) {
						ShowMinimap = ((boolOptions & BoolOptions::ShowMinimap) == BoolOptions::ShowMinimap);
						PlayerFurColor = uc.ReadValueAsLE<std::uint32_t>();
						PlayerColors = (PlayerColorMode)uc.ReadValue<std::uint8_t>();
					}

					if (version >= 16) {
#if defined(WITH_RHI_GXM)
						// The Vita's menu stops at 75% (see GraphicsOptionsSection), so a file cannot put it above that
						RenderingResolutionPercent = std::clamp(uc.ReadValue<std::uint8_t>(), std::uint8_t(25), std::uint8_t(75));
#else
						RenderingResolutionPercent = std::clamp(uc.ReadValue<std::uint8_t>(), std::uint8_t(25), std::uint8_t(100));
#endif
						std::uint8_t particles = uc.ReadValue<std::uint8_t>();
						Particles = (particles <= (std::uint8_t)ParticleQuality::Ultra ? (ParticleQuality)particles : DefaultParticleQuality);
						// `0` means platform default, out-of-range values are ignored
						std::int32_t sampleRate = uc.ReadValueAsLE<std::uint16_t>();
						if (sampleRate == 0 || (sampleRate >= 8000 && sampleRate <= 48000)) {
							AudioSampleRate = sampleRate;
						}
					}

					if (version >= 19) {
						for (std::size_t i = 0; i < (std::size_t)SafeAreaEdge::Count; i++) {
							SafeArea[i] = std::min<std::uint16_t>(uc.ReadValueAsLE<std::uint16_t>(), MaxSafeArea);
						}
					}

#if defined(NCINE_HAS_LOG_FILE)
					EnableLogFile = ((boolOptions & BoolOptions::EnableLogFile) == BoolOptions::EnableLogFile);
#endif
				} else {
					// The file is too new or corrupted
					resetConfig = true;
				}
			} else {
				// The file doesn't exist
				resetConfig = true;
			}
		}

#if defined(NCINE_HAS_LOG_FILE)
		// Attached only now that the preference is known, entries logged until then are kept in the startup history
		_enableLogFileInitial = EnableLogFile;
#	if defined(DEATH_TRACE_LOG_PATH)
		// Such a build writes nothing to the standard output or the console, so the file can't be turned off
		const bool writeLogFile = !logFileSpecified;
#	else
		const bool writeLogFile = (EnableLogFile && !_logFileOverridden);
#	endif
		if (writeLogFile && canWriteLogFile) {
			fs::CreateDirectories(logDir);
			theApplication().AttachTraceTarget(fs::CombinePath(logDir, logFileName));
		}
#endif

#if !defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_N64)
		// Back up the old config (never overwritten), except on memory cards
		if (upgradedFromVersion != 0) {
			StringView configPath = _configPath;
			bool isMemoryCard = configPath.hasPrefix("mc0:"_s) || configPath.hasPrefix("mc1:"_s) ||
				configPath.hasPrefix("mca:"_s) || configPath.hasPrefix("mcb:"_s);
			if (!isMemoryCard) {
				String backupPath = format("{}.v{}.bak", configPath, std::int32_t(upgradedFromVersion));
				if (fs::Copy(configPath, backupPath, false)) {
					LOGI("Config file upgraded from version {}, backup saved to \"{}\"", std::int32_t(upgradedFromVersion), backupPath);
				}

				// Keep only the two newest backups
				constexpr std::size_t MaxConfigBackups = 2;
				String backupPrefix = fs::GetFileName(configPath) + ".v"_s;
				StringView backupDir = fs::GetDirectoryName(backupPath);
				SmallVector<std::pair<std::int32_t, String>, 4> backups;
				for (auto item : fs::Directory(backupDir.empty() ? "."_s : backupDir, fs::EnumerationOptions::SkipDirectories)) {
					StringView fileName = fs::GetFileName(item);
					if (fileName.size() <= backupPrefix.size() + ".bak"_s.size() ||
						!fileName.hasPrefix(backupPrefix) || !fileName.hasSuffix(".bak"_s)) {
						continue;
					}
					StringView number = fileName.exceptPrefix(backupPrefix).exceptSuffix(".bak"_s);
					if (number.size() > 3) {
						continue;
					}
					std::int32_t backupVersion = 0;
					for (char c : number) {
						if (c < '0' || c > '9') {
							backupVersion = -1;
							break;
						}
						backupVersion = backupVersion * 10 + (c - '0');
					}
					if (backupVersion > 0) {
						backups.emplace_back(backupVersion, String(item));
					}
				}
				if (backups.size() > MaxConfigBackups) {
					std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b) {
						return a.first > b.first;
					});
					for (std::size_t i = MaxConfigBackups; i < backups.size(); i++) {
						if (fs::RemoveFile(backups[i].second)) {
							LOGI("Removed old config backup \"{}\"", backups[i].second);
						}
					}
				}
			}
		}
#endif

		if (resetConfig) {
			// Config file doesn't exist or reset is requested
			FirstRun = true;
			ResetTouchButtons();
			Random().Uuid(UniquePlayerID);
			Random().Uuid(UniqueServerID);
			PlayerName = GetEffectivePlayerName();
			TryLoadPreferredLanguage();

			fs::CreateDirectories(configDir);

#if defined(NCINE_HAS_WRITABLE_CACHE)
			// Create "Source" directory on the first launch for the original files
			auto& resolver = ContentResolver::Get();
			fs::CreateDirectories(resolver.GetSourcePath());
#endif

#if defined(DEATH_TARGET_ANDROID)
			// Use native Back button as default on smart watches
			UseNativeBackButton = static_cast<AndroidApplication&>(theApplication()).IsScreenRound();
#elif defined(DEATH_TARGET_N64)
			// Use Nintendo 64 button labels
			GamepadButtonLabels = GamepadType::Nintendo64;
#elif defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WIIU) || defined(DEATH_TARGET_3DS)
			// Use Switch button labels
			GamepadButtonLabels = GamepadType::Switch;
#elif defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA) || \
			defined(DEATH_TARGET_PS3)
			// Use PlayStation legacy button labels on the older PlayStation consoles
			GamepadButtonLabels = GamepadType::PlayStationLegacy;
#elif defined(DEATH_TARGET_UNIX)
			StringView isSteamDeck = ::getenv("SteamDeck");
			if (isSteamDeck == "1"_s) {
				GamepadButtonLabels = GamepadType::Steam;
			}
#elif defined(DEATH_TARGET_WINDOWS)
			wchar_t envSteamDeck[2] = {};
			DWORD envLength = ::GetEnvironmentVariable(L"SteamDeck", envSteamDeck, 2);
			if (envLength == 1 && envSteamDeck[0] == L'1') {
				GamepadButtonLabels = GamepadType::Steam;
			}
#endif
		}

#if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && \
		!defined(DEATH_TARGET_N64) && !defined(DEATH_TARGET_WII) && !defined(DEATH_TARGET_WIIU) && !defined(DEATH_TARGET_GAMECUBE) && \
		!defined(DEATH_TARGET_3DS) && !defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_PS2) && !defined(DEATH_TARGET_PS3) && \
		!defined(DEATH_TARGET_PSP) && !defined(DEATH_TARGET_VITA)
		// Override some settings by command-line arguments
		for (std::int32_t i = 0; i < config.GetArgc(); i++) {
			auto arg = config.GetArgv(i);
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
					// Below LowestFrameRate the game would run in slow motion
					MaxFps = std::max<unsigned long>(paramValue, (unsigned long)FrameTimer::LowestFrameRate);
				}
			}
#	if defined(WITH_PHYSICS_PROBE)
			else if (arg == "/physics-probe"_s) {
				// Runs movement scenarios and logs each tick (see Tests/PhysicsProbe.h), use with /log:file: and /max-fps:
				PhysicsProbe = true;
			}
#	endif
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
			else if (arg == "/gpu-workaround"_s) {
				if (i + 1 < config.GetArgc() && config.GetArgv(i + 1) == "fixed-batch-size"_s) {
					config.FixedBatchSize = 10;
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

#if defined(DEATH_TARGET_GAMECUBE)
	void PreferencesCache::DescribeNextMemoryCardFile(StringView shortDescription, StringView longDescription)
	{
#	if defined(WITH_OGC)
		StringView configPath = _configPath;
		if (configPath.hasPrefix("mca:"_s) || configPath.hasPrefix("mcb:"_s)) {
			Backends::OgcMemoryCard::SetNextFileDescription(shortDescription, longDescription);
		}
#	else
		static_cast<void>(shortDescription);
		static_cast<void>(longDescription);
#	endif
	}
#endif

#if defined(DEATH_TARGET_DREAMCAST)
	void PreferencesCache::DescribeNextMemoryCardFile(StringView shortDescription, StringView longDescription)
	{
		// Only files on a memory card have a header
		if (!StringView(_configPath).hasPrefix("/vmu/"_s)) {
			return;
		}

		// The real fields are smaller than `vmu_pkg_t` declares (16 and 32 bytes), so clamp them
		const auto copyField = [](char* dst, std::size_t capacity, StringView src) {
			std::size_t length = (src.size() < capacity ? src.size() : capacity);
			std::memcpy(dst, src.data(), length);
			dst[length] = '\0';
		};

		vmu_pkg_t pkg = {};
		copyField(pkg.desc_short, 16, shortDescription);
		copyField(pkg.desc_long, 32, longDescription);
		copyField(pkg.app_id, 15, "Jazz2"_s);
		pkg.icon_cnt = 1;
		pkg.icon_anim_speed = 0;
		// No eyecatch, it would cost 4 blocks
		pkg.eyecatch_type = VMUPKG_EC_NONE;
		std::memcpy(pkg.icon_pal, DreamcastIconPalette, sizeof(pkg.icon_pal));
		// Copied before returning
		pkg.icon_data = const_cast<std::uint8_t*>(DreamcastIconData);

		if (fs_vmu_set_default_header(&pkg) != 0) {
			LOGW("Failed to describe the next file written to the memory card");
		}
	}
#endif

	void PreferencesCache::Save()
	{
		// `FirstRun` is true only if config file doesn't exist yet
		FirstRun = false;

#if !defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_N64)
		// Not needed for memory cards or the EEPROM
		fs::CreateDirectories(fs::GetDirectoryName(_configPath));
#endif

#if defined(DEATH_TARGET_DREAMCAST)
		DescribeNextMemoryCardFile("Jazz2 Settings"_s, "Jazz2 Resurrection - Settings"_s);
#elif defined(DEATH_TARGET_GAMECUBE)
		DescribeNextMemoryCardFile("Jazz² Resurrection"_s, "Settings and progress"_s);
#endif

		auto so = fs::Open(_configPath, FileAccess::Write);
		if (!so->IsValid()) {
			return;
		}

		so->WriteValueAsLE<std::uint64_t>(0x2095A59FF0BFBBEF);
		so->WriteValue<std::uint8_t>(ContentFileType::Config);
		so->WriteValue<std::uint8_t>(FileVersion);

		DeflateWriter co(*so);

		BoolOptions boolOptions = BoolOptions::None;
		if (EnableFullscreen) boolOptions |= BoolOptions::EnableFullscreen;
		if (PerformanceMetrics != PerformanceMetricsLevel::Off) boolOptions |= BoolOptions::ShowPerformanceMetrics;
		if (PerformanceMetrics == PerformanceMetricsLevel::Detailed) boolOptions |= BoolOptions::ShowDetailedPerformanceMetrics;
		if (KeepAspectRatioInCinematics) boolOptions |= BoolOptions::KeepAspectRatioInCinematics;
		if (ShowPlayerTrails) boolOptions |= BoolOptions::ShowPlayerTrails;
		if (LowWaterQuality) boolOptions |= BoolOptions::LowWaterQuality;
		if (UnalignedViewport) boolOptions |= BoolOptions::UnalignedViewport;
		if (PreferVerticalSplitscreen) boolOptions |= BoolOptions::PreferVerticalSplitscreen;
		if (PreferZoomOut) boolOptions |= BoolOptions::PreferZoomOut;
		boolOptions |= BoolOptions((std::uint64_t(Dithering) & 0x03) << DitheringShift);
		if (EnableReforgedGameplay) boolOptions |= BoolOptions::EnableReforgedGameplay;
		if (EnableReforgedCamera) boolOptions |= BoolOptions::EnableReforgedCamera;
		if (EnableLedgeClimb) boolOptions |= BoolOptions::EnableLedgeClimb;
		if (WeaponWheel != WeaponWheelStyle::Disabled) boolOptions |= BoolOptions::EnableWeaponWheel;
		if (WeaponWheel == WeaponWheelStyle::EnabledWithAmmoCount) boolOptions |= BoolOptions::ShowWeaponWheelAmmoCount;
		if (ShowAllWeaponWheelSlots) boolOptions |= BoolOptions::ShowAllWeaponWheelSlots;
		if (EnableRgbLights) boolOptions |= BoolOptions::EnableRgbLights;
		if (AllowUnsignedScripts) boolOptions |= BoolOptions::AllowUnsignedScripts;
		if (ToggleRunAction) boolOptions |= BoolOptions::ToggleRunAction;
		if (SwapJumpAndChangeWeaponInMenu) boolOptions |= BoolOptions::SwapJumpAndChangeWeaponInMenu;
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
		if (EnableContinuousJump) boolOptions |= BoolOptions::EnableContinuousJump;
		if (BlurEffects) boolOptions |= BoolOptions::BlurEffects;
		if (EnableTouchJoystick) boolOptions |= BoolOptions::EnableTouchJoystick;
		if (EnableTouchVibration) boolOptions |= BoolOptions::EnableTouchVibration;
		if (ShowMinimap) boolOptions |= BoolOptions::ShowMinimap;
#if defined(NCINE_HAS_LOG_FILE)
		if (EnableLogFile) boolOptions |= BoolOptions::EnableLogFile;
#endif
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

		// v14+: No legacy TouchPadding bytes here

		co.WriteValue<std::uint8_t>(std::uint8_t(GamepadButtonLabels));
		co.WriteValue<std::uint8_t>(GamepadRumble);
		co.WriteValue<std::uint8_t>(std::uint8_t(OverwriteEpisodeEnd));

		co.Write(UniquePlayerID, sizeof(UniquePlayerID));
		co.WriteVariableUint32(std::uint32_t(PlayerName.size()));
		co.Write(PlayerName.data(), std::int64_t(PlayerName.size()));

		co.Write(UniqueServerID, sizeof(UniqueServerID));

		co.WriteValue<std::uint8_t>(LightingResolutionPercent);

		// Per-button touch layout (v14+)
		for (std::size_t i = 0; i < (std::size_t)TouchButtonSlot::Count; i++) {
			co.WriteValueAsLE<std::int16_t>(std::int16_t(TouchButtons[i].EdgeOffset.X));
			co.WriteValueAsLE<std::int16_t>(std::int16_t(TouchButtons[i].EdgeOffset.Y));
			co.WriteValue<std::uint8_t>(std::uint8_t(std::clamp((TouchButtons[i].Scale - 0.5f) / 2.5f, 0.0f, 1.0f) * 255.0f));
			co.WriteValue<std::uint8_t>(std::uint8_t(TouchButtons[i].Anchor));
		}

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

		// Player character recolor and recolor scope (v15+)
		co.WriteValueAsLE<std::uint32_t>(PlayerFurColor);
		co.WriteValue<std::uint8_t>((std::uint8_t)PlayerColors);

		// Rendering resolution, particle quality and audio sample rate (v16+)
		co.WriteValue<std::uint8_t>(RenderingResolutionPercent);
		co.WriteValue<std::uint8_t>((std::uint8_t)Particles);
		co.WriteValueAsLE<std::uint16_t>((std::uint16_t)AudioSampleRate);

		// Safe area insets (v19+), 16 bits per edge
		for (std::size_t i = 0; i < (std::size_t)SafeAreaEdge::Count; i++) {
			co.WriteValueAsLE<std::uint16_t>(SafeArea[i]);
		}

		co.Dispose();
		so->Dispose();

#if defined(DEATH_TARGET_EMSCRIPTEN)
		fs::SyncToPersistent();
#endif
	}

	void PreferencesCache::ApplyRenderingResolution()
	{
#if defined(WITH_RHI_GXM)
		// The native Vita backend renders into a smaller surface and upscales it on present, so resize that surface
		auto& gfxDevice = theApplication().GetGfxDevice();
		const auto& videoMode = gfxDevice.GetCurrentVideoMode();
		Vector2i panel((std::int32_t)videoMode.Width, (std::int32_t)videoMode.Height);
		if (panel.X <= 0 || panel.Y <= 0) {
			panel = gfxDevice.GetResolution();
		}
		gfxDevice.SetDrawableSize(panel.X * RenderingResolutionPercent / 100, panel.Y * RenderingResolutionPercent / 100);
#endif
	}

	void PreferencesCache::ApplyPerformanceMetrics()
	{
		// Only the detailed level needs per-phase timings
		FrameStatistics::SetEnabled(PerformanceMetrics == PerformanceMetricsLevel::Detailed);
	}

#if defined(NCINE_HAS_LOG_FILE)
	bool PreferencesCache::IsLogFileOverridden()
	{
		return _logFileOverridden;
	}

	bool PreferencesCache::IsLogFileRestartPending()
	{
		return (EnableLogFile != _enableLogFileInitial);
	}
#endif

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
#elif defined(DEATH_TARGET_MIPS)
		std::int32_t arch = 6;
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

		String hostName = Application::GetDeviceHostname();
#if defined(WITH_LIBRETRO)
		// Appended to the system name, like Wine or Atmosphère
		StringView systemSuffix = " (Libretro)"_s;
#else
		StringView systemSuffix = {};
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
		auto deviceBrand = Backends::AndroidJniClass_Version::GetDeviceBrand();
		auto deviceModel = Backends::AndroidJniClass_Version::GetDeviceModel();

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

		String deviceDesc = format("{}|Android {}{}|{}|2|{}", hostName, sdkVersion, systemSuffix, deviceName, arch);
#elif defined(DEATH_TARGET_IOS)
		String appleVersion = Environment::GetAppleVersion();
		String deviceModel = Environment::GetAppleDeviceModel();
		String deviceDesc = format("{}|iOS {}{}|{}|6|{}", hostName, appleVersion, systemSuffix, deviceModel, arch);
#elif defined(DEATH_TARGET_APPLE)
		String appleVersion = Environment::GetAppleVersion();
		String deviceDesc = format("{}|macOS {}{}||5|{}", hostName, appleVersion, systemSuffix, arch);
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		String deviceDesc = format("{}|WASM{}||8|{}", hostName, systemSuffix, arch);
#elif defined(DEATH_TARGET_SWITCH)
		std::uint32_t switchVersion = Environment::GetSwitchVersion();
		bool isAtmosphere = Environment::HasSwitchAtmosphere();
		String deviceDesc = format("{}|Nintendo Switch {}.{}.{}{}{}||9|{}", hostName,
			((switchVersion >> 16) & 0xFF), ((switchVersion >> 8) & 0xFF), (switchVersion & 0xFF), isAtmosphere ? " (Atmosphère)"_s : ""_s, systemSuffix, arch);
#elif defined(DEATH_TARGET_WII)
		String deviceDesc = format("{}|Nintendo Wii{}||14|{}", hostName, systemSuffix, arch);
#elif defined(DEATH_TARGET_WIIU)
		String deviceDesc = format("{}|Nintendo Wii U{}||20|{}", hostName, systemSuffix, arch);
#elif defined(DEATH_TARGET_GAMECUBE)
		String deviceDesc = format("{}|Nintendo GameCube{}||15|{}", hostName, systemSuffix, arch);
#elif defined(DEATH_TARGET_3DS)
		String deviceDesc = format("{}|Nintendo 3DS{}||19|{}", hostName, systemSuffix, arch);
#elif defined(DEATH_TARGET_PS2)
		String deviceDesc = format("{}|PlayStation 2{}||11|{}", hostName, systemSuffix, arch);
#elif defined(DEATH_TARGET_PS3)
		String deviceDesc = format("{}|PlayStation 3{}||12|{}", hostName, systemSuffix, arch);
#elif defined(DEATH_TARGET_VITA)
		String deviceDesc = format("{}|PlayStation Vita{}||10|{}", hostName, systemSuffix, arch);
#elif defined(DEATH_TARGET_PSP)
		String deviceDesc = format("{}|PlayStation Portable{}||16|{}", hostName, systemSuffix, arch);
#elif defined(DEATH_TARGET_AMIGAOS) || defined(DEATH_TARGET_AMIGAOS4) || defined(DEATH_TARGET_MORPHOS)
#	if defined(DEATH_TARGET_AMIGAOS)
		StringView systemName = "AmigaOS 3.x"_s;
#	elif defined(DEATH_TARGET_AMIGAOS4)
		StringView systemName = "AmigaOS 4.1"_s;
#	else
		StringView systemName = "MorphOS"_s;
#	endif
		String deviceDesc = format("{}|{}{}||13|{}", hostName, systemName, systemSuffix, arch);
#elif defined(DEATH_TARGET_UNIX)
#	if defined(DEATH_TARGET_CLANG)
		arch |= 0x100000;
#	endif

		String unixFlavor = Environment::GetUnixFlavor();
		String deviceDesc = format("{}|{}{}||4|{}", hostName, unixFlavor.empty() ? "Unix"_s : StringView(unixFlavor), systemSuffix, arch);
#elif defined(DEATH_TARGET_WINDOWS) || defined(DEATH_TARGET_WINDOWS_RT)
#	if defined(DEATH_TARGET_CLANG)
		arch |= 0x100000;
#	endif

		auto osVersion = Environment::WindowsVersion;
#	if defined(DEATH_TARGET_WINDOWS_RT)
		const char* deviceType;
		switch (Environment::CurrentDeviceType) {
			case DeviceType::Desktop: deviceType = "Desktop"; break;
			case DeviceType::Mobile: deviceType = "Mobile"; break;
			case DeviceType::Iot: deviceType = "Iot"; break;
			case DeviceType::Xbox: deviceType = "Xbox"; break;
			default: deviceType = "Unknown"; break;
		}
		String deviceDesc = format("{}|Windows {}.{}.{} ({}){}||7|{}", hostName,
			std::int32_t((osVersion >> 48) & 0xffffu), std::int32_t((osVersion >> 32) & 0xffffu), std::int32_t(osVersion & 0xffffffffu), deviceType, systemSuffix, arch);
#	else
		bool isWine = Environment::IsWine();
		String deviceDesc = format(isWine ? "{}|Windows {}.{}.{} (Wine){}||3|{}" : "{}|Windows {}.{}.{}{}||3|{}", hostName,
			std::int32_t((osVersion >> 48) & 0xffffu), std::int32_t((osVersion >> 32) & 0xffffu), std::int32_t(osVersion & 0xffffffffu), systemSuffix, arch);
#	endif
#else
		String deviceDesc = format("{}|{}|||", hostName, systemSuffix.trimmed());
#endif
		return toBase64Url(deviceDesc.begin(), deviceDesc.end());
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
				if (playerName.empty()) {
					// Fallback to "Player XXXX" using the last 4 hex digits of the unique player ID
					playerName = format("Player {:.2x}{:.2x}",
						PreferencesCache::UniquePlayerID[14], PreferencesCache::UniquePlayerID[15]);
				}
			}
		}

		return playerName;
	}

	EpisodeContinuationState* PreferencesCache::GetEpisodeEnd(StringView episodeName, bool createIfNotFound)
	{
		auto it = _episodeEnd.find(String::nullTerminatedView(episodeName));
		if (it == _episodeEnd.end()) {
			// Case-insensitive fallback, the name can come from a file name or from inside a ".j2e" file
			for (auto& pair : _episodeEnd) {
				if (StringUtils::equalsIgnoreCase(pair.first, episodeName)) {
					return &pair.second;
				}
			}
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
			// Keyed without case as well, see GetEpisodeEnd() above
			for (auto& pair : _episodeContinue) {
				if (StringUtils::equalsIgnoreCase(pair.first, episodeName)) {
					return &pair.second;
				}
			}
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
		if (episodeName.empty() || StringUtils::equalsIgnoreCase(episodeName, "unknown"_s)) {
			return;
		}

		if (_episodeContinue.erase(String::nullTerminatedView(episodeName)) == 0) {
			// The entry may have been stored under a different case, see GetEpisodeEnd() above
			for (auto it = _episodeContinue.begin(); it != _episodeContinue.end(); ++it) {
				if (StringUtils::equalsIgnoreCase(it->first, episodeName)) {
					_episodeContinue.erase(it);
					break;
				}
			}
		}
	}

	void PreferencesCache::RemoveEpisodeEnd(StringView episodeName)
	{
		if (episodeName.empty()) {
			return;
		}

		if (_episodeEnd.erase(String::nullTerminatedView(episodeName)) == 0) {
			for (auto it = _episodeEnd.begin(); it != _episodeEnd.end(); ++it) {
				if (StringUtils::equalsIgnoreCase(it->first, episodeName)) {
					_episodeEnd.erase(it);
					break;
				}
			}
		}
	}

#if defined(SHAREWARE_DEMO_ONLY)
	bool PreferencesCache::IsEpisodeUnlocked(StringView episodeName)
	{
		static const struct {
			StringView Name;
			UnlockableEpisodes Flag;
		} LockedEpisodes[] = {
			{ "prince"_s, UnlockableEpisodes::FormerlyAPrince },
			{ "rescue"_s, UnlockableEpisodes::JazzInTime },
			{ "flash"_s, UnlockableEpisodes::Flashback },
			{ "monk"_s, UnlockableEpisodes::FunkyMonkeys },
			{ "xmas98"_s, UnlockableEpisodes::ChristmasChronicles },
			{ "xmas99"_s, UnlockableEpisodes::ChristmasChronicles },
			{ "secretf"_s, UnlockableEpisodes::TheSecretFiles }
		};

		// The case doesn't matter, the same episode can be spelled differently, see GetEpisodeEnd() above
		for (const auto& episode : LockedEpisodes) {
			if (StringUtils::equalsIgnoreCase(episodeName, episode.Name)) {
				return ((UnlockedEpisodes & episode.Flag) != UnlockableEpisodes::None);
			}
		}
		return true;
	}
#endif

	void PreferencesCache::TryLoadPreferredLanguage()
	{
		auto& i18n = I18n::Get();
		auto& resolver = ContentResolver::Get();
		StringView userContentPath = resolver.GetUserContentPath();

		Array<String> languages = I18n::GetPreferredLanguages();
		for (String language : languages) {
			if (!language.empty() && language.size() < sizeof(PreferencesCache::Language)) {
				if (language == "en"_s) {
					break;
				}
				if ((!userContentPath.empty() && i18n.LoadFromFile(fs::CombinePath({ userContentPath, "Translations"_s, String(language + ".mo"_s) }))) ||
					i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, String(language + ".mo"_s) })) ||
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
				if ((!userContentPath.empty() && i18n.LoadFromFile(fs::CombinePath({ userContentPath, "Translations"_s, String(baseLanguage + ".mo"_s) }))) ||
					i18n.LoadFromFile(fs::CombinePath({ resolver.GetCachePath(), "Translations"_s, String(baseLanguage + ".mo"_s) })) ||
					i18n.LoadFromFile(fs::CombinePath({ resolver.GetContentPath(), "Translations"_s, String(baseLanguage + ".mo"_s) }))) {
					std::memcpy(PreferencesCache::Language, baseLanguage.data(), baseLanguage.size());
					std::memset(PreferencesCache::Language + baseLanguage.size(), '\0', sizeof(PreferencesCache::Language) - baseLanguage.size());
					break;
				}
			}
		}
	}
}