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
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>
#include <Utf8.h>

#if defined(DEATH_TARGET_ANDROID)
#	include "../nCine/Backends/Android/AndroidApplication.h"
#	include "../nCine/Backends/Android/AndroidJniHelper.h"
#elif defined(DEATH_TARGET_DREAMCAST)
#	include <dc/maple.h>
#	include <dc/maple/vmu.h>

#elif defined(DEATH_TARGET_N64)
#	include <cerrno>
#	include <fcntl.h>
#	include <unistd.h>
#	include <eeprom.h>
#	include <eepromfs.h>
#	include <system.h>	// for attach_filesystem()
#elif defined(DEATH_TARGET_PS2)
#	include <cmath>
extern "C" {
#	include <libmc.h>
}
#elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX)
#	include <unistd.h>
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
	bool PreferencesCache::ShowPerformanceMetrics = false;
	bool PreferencesCache::KeepAspectRatioInCinematics = false;
	bool PreferencesCache::ShowPlayerTrails = true;
	bool PreferencesCache::ShowMinimap = true;
	bool PreferencesCache::LowWaterQuality = false;
	bool PreferencesCache::UnalignedViewport = false;
	bool PreferencesCache::PreferVerticalSplitscreen = false;
	bool PreferencesCache::PreferZoomOut = true;
	bool PreferencesCache::BackgroundDithering = true;
	bool PreferencesCache::BlurEffects = true;
#if defined(DEATH_TARGET_VITA)
	// The lighting buffer is a full-resolution off-screen pass the composite samples per pixel, and halving
	// it costs the SGX a quarter of that work for a difference the light falloff largely hides
	std::uint8_t PreferencesCache::LightingResolutionPercent = 50;
#else
	std::uint8_t PreferencesCache::LightingResolutionPercent = 100;
#endif
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
	// The web build has a backend but needs a local bridge running, so it stays off until asked for
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
	float PreferencesCache::MasterVolume = 0.7f;
	float PreferencesCache::SfxVolume = 0.8f;
	float PreferencesCache::MusicVolume = 0.4f;
	bool PreferencesCache::ToggleRunAction = false;
#if defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_WII) || \
		defined(DEATH_TARGET_GAMECUBE)
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
		// Default positions derived from HUD constants × (DefaultWidth * 0.5f = 360):
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

		A PlayStation 2 save is a directory, and the browser identifies it by an `icon.sys` inside it - a
		fixed 964-byte record carrying the title, the background and lighting, and the names of the three
		icon models it draws (listed, being copied, being deleted). A directory WITHOUT one is not shown as
		an unnamed save, it is shown as "Corrupted Data" and offered for deletion, which is what the game's
		save looked like even though every byte of it was fine.

		Written once, when the save directory is created; an existing `icon.sys` is left alone so a user who
		replaced the icon keeps theirs. Nothing here can fail in a way worth reporting - the save itself works
		regardless, only its appearance in the browser is at stake - so failures are quietly ignored.
	*/
	void WritePs2BrowserMetadata(StringView saveDir)
	{
		static constexpr StringView IconFileName = "Jazz2.icn"_s;
		// How many slices the body is turned from, and how many rings its profile bends at. The root is
		// one chain of rings from the point at the bottom to the pole at the top, so the shoulder is part
		// of the same surface as the sides and rounds over into it instead of meeting it at a corner.
		static constexpr std::int32_t Segments = 20;
		// The browser lights this per VERTEX and interpolates between them, so how smoothly the shading
		// runs is set by how closely the rings are spaced, not by anything in the lighting. The shoulder
		// gets nearly as many as the whole body below it despite being a fraction of its length: that is
		// where the surface turns through ninety degrees, so it is where sparse rings show as facets.
		static constexpr std::int32_t BodyRings = 8;		// The point up to the widest ring
		static constexpr std::int32_t ShoulderRings = 5;	// That ring over to the top, not counting it
		static constexpr std::int32_t Rings = BodyRings + ShoulderRings;
		// Five leaves, each a blade of this many stations along its length
		static constexpr std::int32_t Leaves = 5;
		static constexpr std::int32_t BladeStations = 4;
		// A fan of triangles at each pole, a band of quads between every other ring pair, and every blade
		// drawn from both sides - a blade being a quad between each station pair but the last, which has
		// tapered to a point and needs one triangle
		static constexpr std::int32_t IconVertexCount = (Segments * (2 * Rings - 4) +
			Leaves * 2 * ((BladeStations - 2) * 2 + 1)) * 3;
		// The texture is a fixed 128x128 at 16 bits per texel, but it is stored compressed, so a nearly
		// flat one costs a few hundred bytes instead of 32 KB. Runs are kept well under the longest a
		// real icon uses rather than emitting long stretches as one.
		static constexpr std::int32_t IconTextureRows = 128;
		static constexpr std::int32_t IconTextureRunLength = 4096;
		// The rows the root is mapped along, then a gap so filtering cannot bleed one mapping into the
		// other, then the rows a leaf blade is mapped across
		static constexpr std::int32_t IconTextureRootRows = 104;
		static constexpr std::int32_t IconTextureLeafRow = 112;
		// icon.sys is a fixed record: a 16-byte header, the four background corners, seven vectors of
		// lighting, the 68-byte title, three 64-byte file names and 512 reserved bytes
		static constexpr std::int64_t IconSysSize = 16 + 4 * 16 + 7 * 16 + 68 + 3 * 64 + 512;
		// A 20-byte header, then a record per vertex - one position per animation shape (8), the normal (8),
		// the texture coordinate (4) and the colour (4) - then the animation block (its own 20-byte header,
		// one 8-byte frame and that frame's single 8-byte key). Everything but the texture, whose length
		// depends on how well it compresses; this only has to be close, as the stream grows if it is short.
		static constexpr std::int64_t IconSizeWithoutTexture = 20 + std::int64_t(IconVertexCount) * (8 + 8 + 4 + 4) +
			20 + 8 + 8;

		// Both files are built in memory so they can be compared against what the card already holds, and
		// written only where the two differ. Comparing their SIZES would be cheaper and is not enough:
		// two revisions of a model with the same vertex count are the same length, and icon.sys is a
		// fixed size whatever is in it, so a card would keep files this build had already replaced. That
		// matters more than a stale picture would suggest - an icon an earlier build wrote froze the
		// browser while it opened the card, and a card that will not open is a card whose save cannot be
		// deleted either, so the game replacing it is the only way back.
		MemoryStream iconData(IconSizeWithoutTexture + 1024);

		// The icon model: a carrot, built from triangles carrying their own colours.
		//
		// The texture that follows the model is a flat white one, and it is NOT optional. The texture type
		// field is a set of flags - 0x04 says a texture is present and 0x08 says it is RLE compressed - so
		// the 0x07 every icon in the wild uses is a promise that an uncompressed 128x128 image follows.
		// Writing 0x07 and then ending the file left the browser sampling whatever came after it, which is
		// what rendered the carrot black; writing 0x0F with a plain image instead pointed the RLE decoder
		// at data that was never compressed, which is what froze the browser as it opened the card. Keeping
		// the flags honest costs 32 KB and makes the vertex colours mean what they say.
		{
			// s16 fixed point throughout, 4096 being 1.0. Y grows DOWNWARD here - built the other way up,
			// the carrot came out standing on its leaves.
			//
			// Sized against the icons shipped by Open PS2 Loader and HDLGameInstaller, which measure 5.0
			// and 2.5 units across their longest side. Those two are widest HORIZONTALLY though, and this
			// one is tallest, which the browser has less room for - 4.9 tall ran off the top of the view,
			// so it stands 3.3 instead.
			// The browser does not look at the origin, it looks well above it, so a model built around
			// zero hangs at the bottom of the view. Both icons measured for the sizes above sit entirely
			// in negative Y - Open PS2 Loader's between -12699 and -7833, HDLGameInstaller's between
			// -10239 and 0 - and this one is hung from the same height rather than from the origin.
			// One dial for how large the carrot is drawn, so its size is a single number rather than a
			// sweep through every measurement below. It scales about the height it hangs at, which is a
			// position rather than a size and so stays put - the icon grows without drifting off centre.
			constexpr float Scale = 1.5f;
			constexpr float Hang = -11000.0f;
			constexpr float TipY = Hang + 6400.0f * Scale;	// The point of the carrot, at the bottom
			constexpr float RimY = Hang - 2200.0f * Scale;	// Where the body meets the leaves
			constexpr float LeafY = Hang - 7000.0f * Scale;	// Leaf tips, at the top
			constexpr float Radius = 2500.0f * Scale;		// At the rim, the widest the body gets
			constexpr float Pi = 3.14159265f;

			// Turned at runtime rather than tabulated: this runs once, beside a 45 KB write to a memory
			// card, and a hand-written table of sixteen angles is a transcription error waiting to happen
			float rim[Segments][2], mid[Segments][2];
			for (std::int32_t i = 0; i < Segments; i++) {
				const float step = 2.0f * Pi / float(Segments);
				rim[i][0] = std::cos(step * float(i));
				rim[i][1] = std::sin(step * float(i));
				// Halfway between two rim directions, where a flat facet's normal would point
				mid[i][0] = std::cos(step * (float(i) + 0.5f));
				mid[i][1] = std::sin(step * (float(i) + 0.5f));
			}

			// The root's profile, as a chain of rings from the point at the bottom to the pole at the top.
			// The sides follow a power curve rather than a straight line - a carrot swells fast out of its
			// tip and then runs nearly parallel, which is what makes it read as a root instead of a cone.
			// The shoulder is a quarter turn from the widest ring over to the pole, flattened well below a
			// hemisphere: the top wants rounding off, not doming.
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

			// The whole chain is smooth shaded, so a ring's normal is the average of the two bands meeting
			// at it and no join shows up as a crease - including the one where the sides become the
			// shoulder, which is the join that used to be a hard edge. A band's own outward normal is its
			// slope turned a quarter: it leans towards the point while the root is still widening, and
			// past the widest ring, where the surface starts closing again, it leans the other way.
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

			// A blade's centreline, as a chain of stations from where it leaves the root to its point, and
			// how wide it is across at each. It climbs before it swings out, so the frond arcs rather than
			// sticking out straight, and it is at its widest a little under halfway up.
			float bladeR[BladeStations], bladeY[BladeStations];
			static const float BladeW[BladeStations] = { 520.0f * Scale, 900.0f * Scale, 620.0f * Scale, 0.0f };
			// Started just inside the shoulder rather than level with its pole, so the blades come out of
			// the root instead of hovering over it
			const float bladeBase = ringY[Rings - 1] + 150.0f * Scale;
			for (std::int32_t k = 0; k < BladeStations; k++) {
				const float s = float(k) / float(BladeStations - 1);
				bladeR[k] = Radius * 1.35f * std::pow(s, 1.75f);
				bladeY[k] = bladeBase + (LeafY - bladeBase) * std::pow(s, 0.75f);
			}

			// Straight 0..0xFF RGBA, the range the format documents, kept low enough that the browser's
			// lighting cannot drive any channel past 255 and clip it
			static const std::uint8_t OrangeDeep[4] = { 0x96, 0x4A, 0x0C, 0xFF };	// At the point
			static const std::uint8_t OrangeLit[4] = { 0xC4, 0x6E, 0x12, 0xFF };	// At the widest ring
			static const std::uint8_t Green[4]     = { 0x2D, 0x87, 0x2D, 0xFF };
			static const std::uint8_t GreenDark[4] = { 0x1E, 0x5A, 0x1E, 0xFF };

			// The root's colour is mixed per ring rather than picked from a few fixed ones. Handing whole
			// stretches of rings the same colour left the entire change to happen across the one band
			// between them, which is the seam that showed up across the lower half of the body; mixing it
			// ring by ring spreads the gradient over the whole length instead. It climbs from the point
			// to the widest ring and eases back a little over the shoulder, where the light is glancing.
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

			// One position per animation shape, then the normal, the texture coordinate and the colour.
			// Coordinates are the same fixed point as everything else, 4096 being the whole image.
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
			// The middle of texture texel @p at along an axis, in those coordinates - the half texel
			// keeps the sample off the boundary between two of them, where filtering would mix them
			const auto texel = [](float at) {
				return std::int16_t((at + 0.5f) * (4096.0f / float(IconTextureRows)));
			};

			// Where ring @p k lands in the texture: the root is mapped with V running along its length
			// and U around it, so the image wraps around the root once. V is spaced by the ring's HEIGHT
			// rather than its index, so the mapping does not stretch where the profile's rings bunch up.
			const auto ringV = [&](std::int32_t k) {
				const float along = (TipY - ringY[k]) / (TipY - ringY[Rings - 1]);
				return texel(along * float(IconTextureRootRows - 1));
			};
			// U for slice @p s, which is deliberately NOT wrapped: the slice after the last one is asked
			// for as Segments rather than 0, so the seam gets the far edge of the image instead of
			// running the whole of it backwards across that one slice
			const auto sliceU = [&](float s) {
				return texel(s / float(Segments) * float(IconTextureRows - 1));
			};
			// A vertex on ring @p k at slice @p s, shaded from the ring's own normal and colour
			const auto ringVertex = [&](std::int32_t k, std::int32_t s) {
				const std::int32_t w = s % Segments;
				vertex(rim[w][0] * ringR[k], ringY[k], rim[w][1] * ringR[k],
					std::int16_t(rim[w][0] * ringNr[k]), ringNy[k], std::int16_t(rim[w][1] * ringNr[k]),
					sliceU(float(s)), ringV(k), ringRgba[k]);
			};
			// A pole is a single vertex with no radial direction of its own, so it borrows the facet's -
			// halfway between the two ring directions the triangle spans
			const auto poleVertex = [&](std::int32_t k, std::int32_t i) {
				vertex(0.0f, ringY[k], 0.0f, std::int16_t(mid[i][0] * ringNr[k]), ringNy[k],
					std::int16_t(mid[i][1] * ringNr[k]),
					sliceU(float(i) + 0.5f), ringV(k), ringRgba[k]);
			};

			// The point, fanned out to the first ring
			for (std::int32_t i = 0; i < Segments; i++) {
				poleVertex(0, i);
				ringVertex(1, i);
				ringVertex(1, i + 1);
			}
			// Everything between the two poles, a band of quads per ring pair
			for (std::int32_t k = 1; k < Rings - 2; k++) {
				for (std::int32_t i = 0; i < Segments; i++) {
					ringVertex(k, i); ringVertex(k + 1, i); ringVertex(k + 1, i + 1);
					ringVertex(k, i); ringVertex(k + 1, i + 1); ringVertex(k, i + 1);
				}
			}
			// The top, fanned in to its pole - wound the other way round, since it faces the opposite way
			for (std::int32_t i = 0; i < Segments; i++) {
				poleVertex(Rings - 1, i);
				ringVertex(Rings - 2, i + 1);
				ringVertex(Rings - 2, i);
			}
			for (std::int32_t i = 0; i < Leaves; i++) {
				const float dx = leaf[i][0], dz = leaf[i][1];

				// The normal at each station, from the across direction crossed with the centreline's own
				// direction there - negated, because Y grows downward here, so the right hand rule applied
				// to an outward-wound triangle points INWARD. The body above is authored the same way
				// round, and a blade whose lit side faced the other way to the root would shade against it.
				std::int16_t bladeNr[BladeStations], bladeNy[BladeStations];
				for (std::int32_t k = 0; k < BladeStations; k++) {
					const std::int32_t from = (k > 0 ? k - 1 : 0);
					const std::int32_t to = (k < BladeStations - 1 ? k + 1 : k);
					const float dR = bladeR[to] - bladeR[from], dY = bladeY[to] - bladeY[from];
					const float scale = 4096.0f / std::sqrt(dR * dR + dY * dY);
					bladeNr[k] = std::int16_t(-dY * scale);
					bladeNy[k] = std::int16_t(dR * scale);
				}

				// Emitted twice with opposite winding and normal - a single-sided blade would vanish for
				// half of the browser's turn. Each copy is pushed a hair along its own normal rather than
				// left where the other one is: two coplanar surfaces at matching depth is a tie for the
				// depth test to settle, and it settles it differently per pixel.
				constexpr float Thickness = 16.0f * Scale / 4096.0f;
				for (std::int32_t side = 0; side < 2; side++) {
					const float f = (side == 0 ? 1.0f : -1.0f);
					// One corner of station @p k, @p edge picking which side of the centreline
					const auto blade = [&](std::int32_t k, float edge) {
						const float across = BladeW[k] * edge * f;
						const float nx = dx * float(bladeNr[k]) * f, ny = float(bladeNy[k]) * f;
						const float nz = dz * float(bladeNr[k]) * f;
						// A blade takes the band of rows below the root's, with V across its width and
						// U along its length, so it gets its own patch of the speckle
						const float row = float(IconTextureLeafRow) +
							(edge + 1.0f) * 0.5f * float(IconTextureRows - 1 - IconTextureLeafRow);
						const float along = float(k) / float(BladeStations - 1) * float(IconTextureRows - 1);
						vertex(dx * bladeR[k] - dz * across + nx * Thickness, bladeY[k] + ny * Thickness,
							dz * bladeR[k] + dx * across + nz * Thickness,
							std::int16_t(nx), std::int16_t(ny), std::int16_t(nz),
							texel(along), texel(row), (k == 0 ? GreenDark : Green));
					};
					// A quad between each pair of stations, keeping one winding all the way up, until the
					// last - where the blade has tapered to a point and one triangle closes it
					for (std::int32_t k = 0; k < BladeStations - 2; k++) {
						blade(k, 1.0f); blade(k, -1.0f); blade(k + 1, -1.0f);
						blade(k, 1.0f); blade(k + 1, -1.0f); blade(k + 1, 1.0f);
					}
					blade(BladeStations - 2, 1.0f);
					blade(BladeStations - 2, -1.0f);
					blade(BladeStations - 1, 0.0f);
				}
			}

			// One frame holding one key, which is the format's way of spelling "does not animate"
			so->WriteValueAsLE<std::uint32_t>(1);			// Tag
			so->WriteValueAsLE<std::uint32_t>(1);			// Frame length
			so->WriteValueAsLE<std::uint32_t>(0x3F800000);	// Animation speed, 1.0f
			so->WriteValueAsLE<std::uint32_t>(0);			// Play offset
			so->WriteValueAsLE<std::uint32_t>(1);			// Frames
			so->WriteValueAsLE<std::uint32_t>(0);			// Shape of frame 0
			so->WriteValueAsLE<std::uint32_t>(1);			// Keys in frame 0
			so->WriteValueAsLE<std::uint32_t>(0);			// Key time, 0.0f
			so->WriteValueAsLE<std::uint32_t>(0);			// Key value, 0.0f

			// The texture: 128x128, five bits per channel in the PlayStation's order (blue high, red
			// low). It is a DETAIL map, not the model's colour - it multiplies the vertex colours, so
			// white leaves them exactly as they are and only what is drawn darker shows up. That keeps
			// the length gradient where it belongs, on the vertices, and spends the texture solely on
			// what the gradient cannot express: the bands ringing a real root, and a stripe down each
			// blade. Both are finer than the mesh, which is the whole reason for having a texture -
			// fourteen rings do not cost fourteen rows of geometry.
			//
			// Every row is a single colour. That is not a limitation here, because each mapping runs V
			// along the direction the detail varies in, and it makes the image compress to almost
			// nothing: one run per stretch of equal rows.
			// Speckle rather than bands. A carrot is mottled, not striped, and dots have a second
			// advantage here: the console maps textures affinely, without correcting for perspective,
			// so anything with a straight edge comes out bent at the diagonal of every slice and those
			// bends line up into lines down the model. Scattered dots have no edge long enough to show
			// that, which bands - however softly they were shaded - always did.
			//
			// The base sits below full brightness so that dots can go both ways from it: this multiplies
			// the vertex colours, and 31 is as bright as a channel gets, so the room for a lighter fleck
			// has to be left rather than added.
			constexpr std::uint16_t SpeckleBase = 29, SpeckleDark = 27, SpeckleLight = 31;
			constexpr std::int32_t SpeckleSize = 4;			// How many texels across one fleck is
			// A hash, not a random number generator: the same texel has to come out the same on every
			// run, or the icon would differ from the card's every launch and be rewritten each time
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

			// Stored RLE compressed, which the texture type's 0x08 bit declares. A code below 0x8000
			// repeats the word after it that many times; at or above, it is a literal block of 0x10000
			// minus the code. The published description of the format gives that second figure as 0xFFFF
			// minus the code, which is off by one and makes a real icon decode into millions of texels -
			// this was settled by decoding Open PS2 Loader's icon, which comes out at exactly 128x128.
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
				// Nothing worth a run here, so copy texels out as they are until one turns up. A run of
				// two is left inside the literal block - breaking out for it would cost the four bytes it
				// saves - and the block itself is capped at what its length field can count.
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
			// Every name field is a fixed 64 bytes, zero padded
			std::uint8_t field[64] = {};
			std::memcpy(field, name.data(), (name.size() < sizeof(field) ? name.size() : sizeof(field) - 1));
			so->Write(field, sizeof(field));
		};

		so->Write("PS2D", 4);
		so->WriteValueAsLE<std::uint16_t>(0);			// Saved data, the type the browser labels as a game save
		// The byte the title wraps at. It counts BYTES, and the title below is two per character, so this
		// is twice the character position - 12 puts "Resurrection" on the second line.
		so->WriteValueAsLE<std::uint16_t>(12);
		so->WriteValueAsLE<std::uint32_t>(0);			// Unused
		so->WriteValueAsLE<std::uint32_t>(0);			// Opaque background
		for (std::int32_t corner = 0; corner < 4; corner++) {
			// One colour per corner of the background quad; the same one throughout is a flat backdrop
			so->WriteValueAsLE<std::uint32_t>(0x10);
			so->WriteValueAsLE<std::uint32_t>(0x20);
			so->WriteValueAsLE<std::uint32_t>(0x50);
			so->WriteValueAsLE<std::uint32_t>(0x80);
		}
		// The three directions are the set every icon seems to use, Open PS2 Loader's included, so they
		// are left where they are. Their STRENGTH was the problem: full white against a 0.5 ambient put
		// the lit side of the root at 1.5x its own colour, so its strongest channel came to 288 and
		// clipped at 255 - which is what made the carrot read as bright rather than orange. These are
		// scaled so that a facet reached by the ambient AND all three lights at once still lands under
		// 255 against the strongest colour in the model, so nothing in it can blow out from any angle.
		// They are tinted rather than white too, a warm key against a cool fill, as OPL's are.
		writeVector(0.5f, 0.5f, 0.5f, 0.0f);			// Three light directions...
		writeVector(0.0f, -0.4f, -0.1f, 0.0f);
		writeVector(-0.5f, -0.5f, 0.5f, 0.0f);
		writeVector(0.45f, 0.41f, 0.31f, 1.0f);			// ...their colours, a warm key...
		writeVector(0.26f, 0.25f, 0.21f, 1.0f);			// ...a softer fill...
		writeVector(0.12f, 0.13f, 0.22f, 1.0f);			// ...and a cool one from behind...
		// What a facet none of the three reaches falls back to, so the icon stays readable from every
		// angle rather than going black wherever the lights do not
		writeVector(0.45f, 0.45f, 0.48f, 1.0f);			// ...and the ambient term

		// The title is Shift-JIS, and it has to be the FULL-WIDTH forms of the Latin letters rather than the
		// half-width ones ASCII maps to. Half-width is legal Shift-JIS and encodes without complaint, and the
		// browser then draws nothing at all for it - which is why the save showed up nameless. Real saves use
		// the full-width block, and it is what the console's font is built around.
		//
		// The block is contiguous per character class, so the mapping is arithmetic rather than a table:
		// U+FF10 '0' at 0x824F, U+FF21 'A' at 0x8260, U+FF41 'a' at 0x8281, ideographic space at 0x8140.
		// Anything outside those becomes a space rather than a malformed code - a title is a name, and a
		// name that loses a stray character still reads.
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
			// Shift-JIS is big-endian: the lead byte comes first
			title[titleLength++] = std::uint8_t(wide >> 8);
			title[titleLength++] = std::uint8_t(wide & 0xFF);
		}
		so->Write(title, sizeof(title));

		writeName(IconFileName);						// Listed
		writeName(IconFileName);						// Being copied
		writeName(IconFileName);						// Being deleted

		std::uint8_t reserved[512] = {};
		so->Write(reserved, sizeof(reserved));

		// Neither file is written unless it differs from the one already on the card. Both are checked,
		// not just the model: the lighting and the title live in icon.sys alone, and a run that changed
		// only those would otherwise find the model unchanged and leave the card as it was.
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
	// The path of the config file inside the EEPROM filesystem, which is also the WHOLE filesystem: eepromfs
	// is a fixed table of files declared up front, and the config is the only thing there is to save
	static constexpr char EepromConfigFile[] = "/JAZZ2CFG";
	// How large that one file is - everything the filesystem leaves free - set once the EEPROM type has been
	// probed in Initialize(), because a flashcart may answer with either the 4-kilobit or the 16-kilobit part
	static std::size_t EepromConfigFileSize = 0;

	/**
		@brief One open handle of the "eeprom:/" bridge below

		eepromfs only reads and writes a file WHOLE (`eepfs_read`/`eepfs_write`), while the config code
		streams through @ref Death::IO::FileStream, so a handle is a RAM copy of the file with a cursor:
		reads serve from the copy, writes fill it, and closing a written handle is what commits it to the
		EEPROM in one piece - the same shape KallistiOS gives a Dreamcast VMU file, just made by hand here.
	*/
	struct EepromOpenFile
	{
		std::unique_ptr<std::uint8_t[]> Buffer;
		/** @brief Cursor, in bytes from the start of the file */
		std::size_t Position;
		/** @brief Extent of valid data: the fixed file size when read back, how far the writes got when writing */
		std::size_t Size;
		bool Writable;
		/** @brief Set once a write did not fit - the close then must NOT commit a truncated config over a good one */
		bool Overflowed;
	};

	void* EepromFsOpen(char* name, int flags)
	{
		// The prefix has already been stripped by the newlib glue, a leading slash may or may not remain
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
			// The compressed config outgrew the EEPROM. Refusing the whole write (rather than taking what
			// fits) is what keeps the flag honest: nothing partial ever lands in the buffer, so the close
			// below has a clean "commit or don't" decision.
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
			// Committing on close is what this bridge exists for, but it also means a writer that opened
			// with O_TRUNC and closed after writing only part of the config commits that part (with a
			// zeroed tail) over the previous good save. Overflow - the only write failure this bridge can
			// produce - is guarded below; PreferencesCache::Save() itself has no early-out between its
			// writes, so any OTHER partial write means a new caller that must arrange its own guard.
			if (f->Overflowed) {
				LOGE("Cannot save settings, the configuration does not fit into the {}-byte EEPROM file", EepromConfigFileSize);
				errno = ENOSPC;
				result = -1;
			} else if (eepfs_write(EepromConfigFile, f->Buffer.get(), EepromConfigFileSize) != EEPFS_ESUCCESS) {
				errno = EIO;
				result = -1;
			}
			// The commit is eventually consistent: eepfs_write() returns at once and the joybus flushes the
			// blocks in the background (about 1.5 s for a full 16-kilobit part), which is fine here - the
			// buffer just handed over is the master copy the flusher reads from
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

		for (int32_t i = 0; i < config.argc(); i++) {
			auto arg = config.argv(i);
			if (arg == "/reset-config"_s) {
				resetConfig = true;
			}
		}
#else
		_configPath = "Jazz2.config"_s;
		bool overrideConfigPath = false;

#	if !defined(DEATH_TARGET_ANDROID) && !defined(DEATH_TARGET_IOS) && !defined(DEATH_TARGET_SWITCH) && \
		!defined(DEATH_TARGET_N64) && !defined(DEATH_TARGET_WII) && !defined(DEATH_TARGET_GAMECUBE) && \
		!defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_PS2) && !defined(DEATH_TARGET_PS3) && \
		!defined(DEATH_TARGET_PSP) && !defined(DEATH_TARGET_VITA)
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

#	if defined(WITH_LIBRETRO)
		// The frontend owns the save directory, so use it instead of the common path for current user,
		// the portable config is skipped as well, the working directory belongs to the frontend
		if (!overrideConfigPath) {
			StringView saveDir = Backends::theLibretroApplication().GetHostPaths().Save;
			if (!saveDir.empty()) {
				// Own subdirectory, the save directory is shared by all the cores of the frontend
				_configPath = fs::CombinePath({saveDir, NCINE_APP, "Jazz2.config"_s});
				fs::CreateDirectories(fs::GetDirectoryName(_configPath));
				overrideConfigPath = true;
			}
		}
#	endif

		// A portable config next to the executable wins over the per-user path chosen below. The Dreamcast,
		// the PlayStation 2 and the Nintendo 64 don't look for one: their whole tree is on the disc or ROM
		// they booted from, so it could never be written back - and a miss there costs the drive a seek and
		// a retry on every boot.
#	if defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_N64) || defined(DEATH_TARGET_PS2)
		constexpr bool hasPortableConfig = false;
#	else
		const bool hasPortableConfig = fs::IsReadableFile(_configPath);
#	endif

		// If config path is not overriden and portable config doesn't exist, use common path for current user
		if (!overrideConfigPath && !hasPortableConfig) {
#	if defined(DEATH_TARGET_DREAMCAST)
			// The game runs from a disc, so the only writable storage is a memory card. KallistiOS mounts
			// each one as "/vmu/<port><unit>", buffers the whole file in RAM and commits it to the card when
			// the handle closes, so it can be streamed like any other file. VMU names are limited to twelve
			// characters. The first attached card wins, with none inserted there is nowhere to save and the
			// path is left on the disc, where opening it for writing simply fails as before.
			if (maple_device_t* memoryCard = maple_enum_type(0, MAPLE_FUNC_MEMCARD)) {
				char vmuPath[] = "/vmu/a1/JAZZ2CFG";
				vmuPath[5] = char('a' + memoryCard->port);
				vmuPath[6] = char('0' + memoryCard->unit);
				_configPath = String(vmuPath, sizeof(vmuPath) - 1);
			} else {
				LOGW("No memory card found, settings and progress cannot be saved");
				auto& resolver = ContentResolver::Get();
				_configPath = fs::CombinePath(fs::GetDirectoryName(resolver.GetSourcePath()), "Jazz2.config"_s);
			}
#	elif defined(DEATH_TARGET_PS2)
			// Same situation as the Dreamcast: the game runs from a disc, so the only writable storage is a
			// memory card. Once a slot has been probed, MCMAN serves it through the original `ioman` - the
			// I/O manager the newlib port's POSIX calls reach - so a card is an ordinary "mc0:" / "mc1:"
			// path and the config file needs no special-case I/O at all. The probe is not optional: before
			// it, MCMAN answers for the slot as if it were empty and an mkdir on a good card returns ENOENT.
			//
			// The save lives in a directory of its own, as every PlayStation 2 save does. The first usable
			// card wins, exactly as the Dreamcast takes the first attached VMU; with neither slot usable the
			// path is left on the disc, where opening it for writing simply fails as before.
			{
				bool memoryCardFound = false;
				// Plain `int`: PS2SDK takes `int*` here and `std::int32_t` is `long` on the Emotion Engine,
				// so the two are distinct types even though they are the same width
				for (int port = 0; port < 2 && !memoryCardFound; port++) {
					int type = 0, freeClusters = 0, formatted = 0;
					mcGetInfo(port, 0, &type, &freeClusters, &formatted);
					int probe = -1;
					mcSync(MC_WAIT, nullptr, &probe);

					if (type != MC_TYPE_PS2) {
						continue;	// Empty slot, or a PlayStation 1 card this game cannot use
					}

					// Whether the card is usable is settled by TRYING, never by reading the probe. mcGetInfo
					// reports what changed since the last call, and on the first call of a session that is
					// always "a card has been inserted" - -1 for a formatted one, -2 for an unformatted one -
					// with `formatted` left unfilled. Believing that flag therefore rejected every good card
					// on the first look, which is the only look there is.
					char saveDir[] = "mc0:/Jazz2";
					saveDir[2] = char('0' + port);
					if (fs::CreateDirectories(saveDir)) {
						_configPath = fs::CombinePath(StringView(saveDir, sizeof(saveDir) - 1), "Jazz2.config"_s);
						memoryCardFound = true;
						// Without this the console's browser calls the save "Corrupted Data" and offers to
						// delete it, however healthy the save actually is
						WritePs2BrowserMetadata(StringView(saveDir, sizeof(saveDir) - 1));
					} else if (probe == -2 || formatted != MC_FORMATTED) {
						// Only reached once the card has actually refused, so the flag is explaining a
						// failure rather than causing one. Worth separating from "no card": the fix is to
						// format it in the console's own browser. Doing it here is not an option - mcFormat
						// erases the WHOLE card, every game's saves, for the sake of one config file.
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
			// The game runs from a read-only ROM, so the only writable storage is the cartridge's own save
			// EEPROM, which the ROM header requests as the 16-kilobit part (2048 bytes, a cheap flashcart
			// that ignores the header answers with the 4-kilobit one, so the size is taken from the probe
			// rather than assumed). eepromfs manages it as a fixed table of files - a single one here,
			// spanning every block the filesystem leaves free - but only through its own whole-file calls,
			// so the bridge above is attached as "eeprom:/" to let the config be streamed like any other
			// file. With no EEPROM at all there is nowhere to save and the path is left on the ROM, where
			// opening it for writing simply fails as before.
			bool eepromFound = false;
			if (eeprom_present() != EEPROM_NONE) {
				// The first 8-byte block holds the filesystem's signature, the file gets all the rest
				EepromConfigFileSize = (eeprom_total_blocks() - 1) * EEPROM_BLOCK_SIZE;
				const eepfs_entry_t eepfsEntries[] = {
					{ EepromConfigFile, EepromConfigFileSize, false, false }
				};
				if (eepfs_init(eepfsEntries, arraySize(eepfsEntries)) == EEPFS_ESUCCESS) {
					if (!eepfs_verify_signature()) {
						// The EEPROM was last written by another game, or never written at all - either way
						// its contents are garbage to this filesystem, so erase everything and start over,
						// as eepromfs asks. This blocks for a few seconds, but only on the very first boot
						// (or after the cartridge hosted a different save), before anything is playing yet.
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
#	elif defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || \
				defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA) || defined(DEATH_TARGET_AMIGAOS)
			// Save config file next to `Source` directory (on the storage device the content is read from;
			// on the Amiga that is the game's own directory, the conventional home of a program's settings)
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

		// DEATH_TRACE_LOG_PATH overrides default log path, and on some platforms
		// (Apple, Unix, Windows) it also forces tracing to the file even without
		// using any command-line argument
#	if defined(DEATH_TRACE)
#		if defined(DEATH_TARGET_ANDROID) || defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_WII) || \
			defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA)
		fs::CreateDirectories(configDir);
#			if defined(DEATH_TRACE_LOG_PATH)
		theApplication().AttachTraceTarget(fs::CombinePath(configDir, DEATH_TRACE_LOG_PATH));
#			else
		theApplication().AttachTraceTarget(fs::CombinePath(configDir, "Jazz2.log"_s));
#			endif
#		elif defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_UNIX) || (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT))
		DEATH_UNUSED bool logFileSpecified = false;
		for (std::int32_t i = 0; i < config.argc(); i++) {
			auto arg = config.argv(i);
			if (arg == "/log:file"_s || arg.hasPrefix("/log:file:"_s)) {
				fs::CreateDirectories(configDir);
				if (arg.size() > "/log:file:"_s.size()) {
					theApplication().AttachTraceTarget(fs::CombinePath(configDir, arg.exceptPrefix("/log:file:"_s)));
					logFileSpecified = true;
				}
#			if !defined(DEATH_TRACE_LOG_PATH)
				else {
					theApplication().AttachTraceTarget(fs::CombinePath(configDir, "Jazz2.log"_s));
					logFileSpecified = true;
				}
#			endif
			}
#			if defined(DEATH_TARGET_WINDOWS)
			else if (arg == "/log"_s) {
				theApplication().AttachTraceTarget(Application::ConsoleTarget);
			}
#			endif
		}
#			if defined(DEATH_TRACE_LOG_PATH)
		if (!logFileSpecified) {
			fs::CreateDirectories(configDir);
			theApplication().AttachTraceTarget(fs::CombinePath(configDir, DEATH_TRACE_LOG_PATH));
		}
#			endif
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

				if (signature == 0x2095A59FF0BFBBEF && fileType == ContentFileType::Config && version <= FileVersion) {
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

					if (version >= 15) {
						ShowMinimap = ((boolOptions & BoolOptions::ShowMinimap) == BoolOptions::ShowMinimap);
						PlayerFurColor = uc.ReadValueAsLE<std::uint32_t>();
						PlayerColors = (PlayerColorMode)uc.ReadValue<std::uint8_t>();
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
			ResetTouchButtons();
			Random().Uuid(UniquePlayerID);
			Random().Uuid(UniqueServerID);
			PlayerName = GetEffectivePlayerName();
			TryLoadPreferredLanguage();

			fs::CreateDirectories(configDir);

#if defined(NCINE_HAS_WRITABLE_CACHE)
			// Create "Source" directory on the first launch, so there is somewhere to put the original files.
			// A platform that cannot convert them has no use for it, prepared content is all it ever loads.
			auto& resolver = ContentResolver::Get();
			fs::CreateDirectories(resolver.GetSourcePath());
#endif

#if defined(DEATH_TARGET_ANDROID)
			// Use native Back button as default on smart watches
			UseNativeBackButton = static_cast<AndroidApplication&>(theApplication()).IsScreenRound();
#elif defined(DEATH_TARGET_SWITCH) || defined(DEATH_TARGET_N64)
			// Use Switch button labels (on the N64 they are the closest fit, see the static initializer)
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
		!defined(DEATH_TARGET_N64) && !defined(DEATH_TARGET_WII) && !defined(DEATH_TARGET_GAMECUBE) && \
		!defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_PS2) && !defined(DEATH_TARGET_PS3) && \
		!defined(DEATH_TARGET_PSP) && !defined(DEATH_TARGET_VITA)
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

#if !defined(DEATH_TARGET_DREAMCAST) && !defined(DEATH_TARGET_N64)
		// A memory card's (or the EEPROM's) mount point always exists and has no subdirectories to create
		fs::CreateDirectories(fs::GetDirectoryName(_configPath));
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
		if (EnableContinuousJump) boolOptions |= BoolOptions::EnableContinuousJump;
		if (BlurEffects) boolOptions |= BoolOptions::BlurEffects;
		if (EnableTouchJoystick) boolOptions |= BoolOptions::EnableTouchJoystick;
		if (EnableTouchVibration) boolOptions |= BoolOptions::EnableTouchVibration;
		if (ShowMinimap) boolOptions |= BoolOptions::ShowMinimap;
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
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|WASM||8|{}", arch);
#elif defined(DEATH_TARGET_SWITCH)
		std::uint32_t switchVersion = Environment::GetSwitchVersion();
		bool isAtmosphere = Environment::HasSwitchAtmosphere();

		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|Nintendo Switch {}.{}.{}{}||9|{}",
			((switchVersion >> 16) & 0xFF), ((switchVersion >> 8) & 0xFF), (switchVersion & 0xFF), isAtmosphere ? " (Atmosphère)"_s : ""_s, arch);
#elif defined(DEATH_TARGET_WII)
		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|Nintendo Wii||14|{}", arch);
#elif defined(DEATH_TARGET_GAMECUBE)
		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|Nintendo GameCube||15|{}", arch);
#elif defined(DEATH_TARGET_PS2)
		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|PlayStation 2||11|{}", arch);
#elif defined(DEATH_TARGET_PS3)
		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|PlayStation 3||12|{}", arch);
#elif defined(DEATH_TARGET_VITA)
		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|PlayStation Vita||10|{}", arch);
#elif defined(DEATH_TARGET_PSP)
		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|PlayStation Portable||16|{}", arch);
#elif defined(DEATH_TARGET_AMIGAOS) || defined(DEATH_TARGET_AMIGAOS4) || defined(DEATH_TARGET_MORPHOS)
#	if defined(DEATH_TARGET_AMIGAOS)
		StringView systemName = "AmigaOS 3.x"_s;
#	elif defined(DEATH_TARGET_AMIGAOS4)
		StringView systemName = "AmigaOS 4.1"_s;
#	else
		StringView systemName = "MorphOS"_s;
#	endif
		char DeviceDesc[128];
		std::int32_t DeviceDescLength = formatInto(DeviceDesc, "|{}||13|{}", systemName, arch);
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