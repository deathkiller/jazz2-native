#include "RdpDevice.h"
#include "RdpBuffer.h"
#include "RdpShaderProgram.h"
#include "RdpRenderTarget.h"
#include "RdpTexture.h"
#include "../FixedFunctionPass.h"
#include "../LightingCombine.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <malloc.h>

#include <display.h>
#include <n64sys.h>
#include <rspq.h>
#include <rdpq.h>
#include <rdpq_attach.h>
#include <rdpq_macros.h>
#include <rdpq_mode.h>
#include <rdpq_rect.h>
#include <rdpq_tex.h>
#include <rdpq_tri.h>
#include <surface.h>

namespace nCine::RHI::RDP
{
	namespace
	{
		// The display surface the window backend brought up (the VI resamples it to the TV)
		constexpr std::int32_t ScreenWidth = 320;
		constexpr std::int32_t ScreenHeight = 240;

		// The DefaultSprite / DefaultBatchedSprites instance layout is a hard contract of the shader family
		// (std140 offsets within the InstanceBlock / each batched Instance) - identical to the software,
		// PowerVR and GE backends' decode (see SwDevice.cpp / PvrDevice.cpp / GuDevice.cpp)
		constexpr std::uint32_t kModelMatrixOffset = 0;
		constexpr std::uint32_t kColorOffset = 64;
		constexpr std::uint32_t kTexRectOffset = 80;
		constexpr std::uint32_t kSpriteSizeOffset = 96;
		constexpr std::uint32_t kPaletteOffsetOffset = 104;
		constexpr std::uint32_t kSpriteSizeNoTexOffset = 80;

		const float IdentityMatrix[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};

		// Column-major 4x4 multiply, out = a * b (matches the software device)
		void Mat4Mul(const float* DEATH_RESTRICT a, const float* DEATH_RESTRICT b, float* DEATH_RESTRICT out)
		{
			for (std::int32_t col = 0; col < 4; col++) {
				for (std::int32_t row = 0; row < 4; row++) {
					out[col * 4 + row] =
						a[0 * 4 + row] * b[col * 4 + 0] +
						a[1 * 4 + row] * b[col * 4 + 1] +
						a[2 * 4 + row] * b[col * 4 + 2] +
						a[3 * 4 + row] * b[col * 4 + 3];
				}
			}
		}

		// Both draw paths only ever transform points of the form (x, y, 0, 1), so just six of the sixteen
		// products of projection*view*model are ever read back
		struct Transform2D
		{
			float Xx, Xy;	// Column 0, rows 0 and 1
			float Yx, Yy;	// Column 1, rows 0 and 1
			float Tx, Ty;	// Column 3, rows 0 and 1
		};

		void Mat4MulTransform2D(const float* DEATH_RESTRICT pv, const float* DEATH_RESTRICT model, Transform2D& out)
		{
			out.Xx = pv[0] * model[0] + pv[4] * model[1] + pv[8] * model[2] + pv[12] * model[3];
			out.Xy = pv[1] * model[0] + pv[5] * model[1] + pv[9] * model[2] + pv[13] * model[3];
			out.Yx = pv[0] * model[4] + pv[4] * model[5] + pv[8] * model[6] + pv[12] * model[7];
			out.Yy = pv[1] * model[4] + pv[5] * model[5] + pv[9] * model[6] + pv[13] * model[7];
			out.Tx = pv[0] * model[12] + pv[4] * model[13] + pv[8] * model[14] + pv[12] * model[15];
			out.Ty = pv[1] * model[12] + pv[5] * model[13] + pv[9] * model[14] + pv[13] * model[15];
		}

		/** @brief Clamps a colour component into the 0..1 a hardware colour register can hold */
		inline float Saturate(float v)
		{
			return (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
		}

		inline std::uint8_t QuantizeChannel(float v)
		{
			return std::uint8_t(Saturate(v) * 255.0f + 0.5f);
		}

		// 0xRRGGBBAA, the packing color_from_packed32() takes for the PRIM/ENV color registers
		inline std::uint32_t PackRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return (std::uint32_t(r) << 24) | (std::uint32_t(g) << 16) | (std::uint32_t(b) << 8) | std::uint32_t(a);
		}

		inline std::uint32_t PackColor(const float* rgba)
		{
			return PackRgba(QuantizeChannel(rgba[0]), QuantizeChannel(rgba[1]), QuantizeChannel(rgba[2]), QuantizeChannel(rgba[3]));
		}

		// ---------------------------------------------------------------- combiner / blender presets
		//
		// The color combiner computes (A-B)*C+D per cycle; the pass colour travels in the PRIM register
		// rather than per-vertex shade because TEXTURE_RECTANGLE - the fast path nearly everything takes -
		// carries no shade attribute at all. Only the gouraud strips (the iris soft edge, the warp's
		// horizon tint) use SHADE, through their own formulas below.

		// texel * colour - the default sprite modulate
		const rdpq_combiner_t CombModulate = RDPQ_COMBINER1((TEX0, 0, PRIM, 0), (TEX0, 0, PRIM, 0));
		// texel * colour + offset: the post-texture additive term of the actor state effects rides the
		// ENV register in the D slot
		const rdpq_combiner_t CombModulateOffset = RDPQ_COMBINER1((TEX0, 0, PRIM, ENV), (TEX0, 0, PRIM, 0));
		// Flat colour where the texture has alpha (masks, shadows, glows)
		const rdpq_combiner_t CombSilhouette = RDPQ_COMBINER1((0, 0, 0, PRIM), (TEX0, 0, PRIM, 0));
		// mix(texel, colour, colour.a) with an opaque result, in one cycle
		const rdpq_combiner_t CombTintMix = RDPQ_COMBINER1((PRIM, TEX0, PRIM_ALPHA, TEX0), (0, 0, 0, 1));
		/*
			The largest value a combiner cycle may compute, in the 0..1 scale of a colour register.

			The combiner's arithmetic is 9-bit and its output clamp covers only HALF of the overflow
			range. Measured on hardware with the doubling combiner below over a flat PRIM (see the
			hardware note in the ApplyPassToState clamp): doubling 176..191 comes back as 255, i.e. a
			result between 1.0 and 1.5 saturates exactly as wanted - but doubling 192 and up comes back
			as ZERO. A result of 1.5 or more wraps to black, the same wrap the blender has (and the same
			one the published RGBA5551 overflow examples show, a blue of 396 reappearing as 140).

			So 1.5 is the hard ceiling of everything the combiner computes, and it is therefore also the
			largest gain a SATURATING multiply can carry on this hardware: within it the clamp does the
			saturating for us, past it the brightest texels of a sprite turn black one channel at a time
			and the sprite changes hue. Every arrangement below stays under it by construction, and the
			one that cannot - the split-multiplier gain, which asks for up to 3 - is clamped to it per
			channel in ApplyPassToState.

			The margin below 1.5 covers the combiner's own rounding: 0.745 packs to PRIM 190, and 190
			doubled is 378, comfortably inside the clamping half.
		*/
		constexpr float MaxCombinerResult = 1.49f;

		// Two-cycle x2: cycle 0 modulates, cycle 1 adds the result to itself (the MUL mux has no ONE, but
		// the SUBA mux does, so 2c = (1-0)*c + c)
		const rdpq_combiner_t CombModulateX2 = RDPQ_COMBINER2((TEX0, 0, PRIM, 0), (TEX0, 0, PRIM, 0),
			(ONE, ZERO, COMBINED, COMBINED), (0, 0, 0, COMBINED));
		// x2 with an offset colour: ENV carries HALF the offset (set by the pass mapping below), so the
		// doubling cycle restores it exactly - 2*(t*p + e/2) = 2*t*p + e
		const rdpq_combiner_t CombModulateX2Offset = RDPQ_COMBINER2((TEX0, 0, PRIM, ENV), (TEX0, 0, PRIM, 0),
			(ONE, ZERO, COMBINED, COMBINED), (0, 0, 0, COMBINED));
		// Untextured flat colour (the no-texture sprite programs, the water tint bands)
		const rdpq_combiner_t CombFlat = RDPQ_COMBINER1((0, 0, 0, PRIM), (0, 0, 0, PRIM));
		// Untextured gouraud (shaded strips - a gradient has no texture to modulate)
		const rdpq_combiner_t CombShade = RDPQ_COMBINER1((0, 0, 0, SHADE), (0, 0, 0, SHADE));
		// TintMix over per-vertex colours (the warp folds its horizon tint into the band's own draw)
		const rdpq_combiner_t CombShadeTintMix = RDPQ_COMBINER1((SHADE, TEX0, SHADE_ALPHA, TEX0), (0, 0, 0, 1));
		// The lightmap combine: RGB 0 at the texture's alpha, under alpha-over blending - out = mem*(1-a),
		// i.e. a per-pixel multiply by the factor the alpha channel encodes (see the combine hook below)
		const rdpq_combiner_t CombLightmap = RDPQ_COMBINER1((0, 0, 0, 0), (0, 0, 0, TEX0));

		// Standard source-alpha over
		const rdpq_blender_t BlendAlphaOver = RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA));
		/*
			An additive pass is deliberately given the SAME alpha-over formula rather than the additive one
			the other consoles use, because the RDP's blender does not saturate: libdragon documents its own
			RDPQ_BLENDER_ADDITIVE as "mostly broken" since a sum above 1.0 wraps back towards zero instead
			of clamping. That wrap is per channel and depends on what is already in the framebuffer, which
			is exactly what the menu's white selection glow looked like - green over some backgrounds, red
			over others, because whichever channels happened to overflow came back dark.

			Alpha-over is the closest formula the hardware can express that cannot wrap, and over a dark
			scene it is very close to the intent: for a source s at alpha a it gives dst*(1-a) + s*a against
			the additive dst + s*a, so at the low alphas glows actually use (0.2 in the shield and mask
			families) the two agree to within a*dst - a few percent of a dark destination - and where they
			part company, at high alpha over a bright destination, additive is the one that would have
			wrapped. The split-multiplier effects that really do need summing are unaffected: their passes
			are accumulated on the CPU and submitted as ONE draw (see EffectContext::SubmitQuad), so they
			never reach this formula at all.
		*/
		const rdpq_blender_t BlendAdditive = BlendAlphaOver;
	}

	/**
		@brief The whole RDP state one primitive is drawn under

		Derived once per draw from the material (texture, filter, blend) and then adjusted per pass by
		the effect (blend override, combiner preset). Consecutive primitives whose state matches reissue
		no mode commands (see ApplyDrawState), and consecutive primitives sampling the same TMEM window
		reload nothing (see UploadWindow) - so a tile run or a text run costs almost only its rectangles.

		At namespace scope rather than in the anonymous namespace because EffectContext names it in a
		member type and itself has to be externally visible (see the note there).
	*/
	struct DrawState
	{
		const surface_t* Texture = nullptr;		// nullptr = untextured
		std::uint32_t TextureVersion = 0;		// Content stamp of the sampled store (keys the TMEM window)
		const std::uint16_t* Tlut = nullptr;	// 256-entry RGBA5551 TLUT a CI8 store resolves through
		std::uint32_t TlutStamp = 0;			// Write stamp of the TLUT slot (keys the resident TLUT)
		rdpq_combiner_t Combiner = 0;			// One of the presets above; 0 never reaches the hardware
		rdpq_blender_t Blender = 0;
		bool BlendEnabled = false;
		std::int32_t Filter = FILTER_POINT;
		std::uint32_t PrimColor = 0xFFFFFFFFu;	// 0xRRGGBBAA
		std::uint32_t EnvColor = 0;
	};

	namespace
	{
		// ---------------------------------------------------------------- draw statistics
		//
		// Per-frame counters and section timers, logged twice a second when the switch is on - the only
		// practical way to see what actually reaches the RDP, since every submission goes out through the
		// rspq command queue and leaves no other trace. Everything below compiles away with the switch
		// off, which is how it ships (the same arrangement as the GU backend's statistics).
		//
		// The blit path's one-shot geometry line lives in SubmitTexturedRect under the same switch; it is
		// what verified that a 640x480 intro frame reaches the screen whole (src (0,0)-(640,480) ->
		// screen (0,0)-(320,240), scale 0.5) instead of clamped.
		//
		// What they measured on the main menu, for the next round: 117 ms per frame, of which 49 ms is
		// this dispatch (11 ms of that the TMEM upload helper) and 0.04 ms is waiting for a display
		// buffer - so the RDP is never starved for work, and rather more than half the frame is spent
		// outside the renderer entirely. 238 instances become 167 rectangles and ~120 triangles (the
		// rotating snowflakes cannot be rectangles) with 250 TMEM uploads of 187 KB, and no palette
		// conversion, bake or store rebuild happens at all.
		constexpr bool TraceDrawStatistics = false;
		struct FrameStats
		{
			std::uint32_t Dispatches, Instances, Rects, Triangles;
			std::uint32_t WindowUploads, WindowBytes, WindowHits, Blits, Strips;
			std::uint32_t TlutUploads, TlutConversions, TlutEvictWaits;
			std::uint32_t ModeChanges, PrimChanges, StoreRefreshes, BakeRebuilds, StoreWritebackBytes;
		};
		FrameStats stats;
		// Wall-clock cost of the frames since the last report, so a change is measured rather than inferred
		std::uint64_t traceFrameTicks = 0;
		std::uint32_t traceFrameCount = 0;
		std::uint32_t traceLastTicks = 0;
		// Where a frame's time actually goes: the CPU inside the draw dispatch, the CPU inside the TMEM
		// upload helper (a subset of it), and the CPU blocked in display_get() - which is the RDP and the
		// VI, not this code. Whatever the frame total exceeds their sum by belongs to the rest of the engine.
		std::uint32_t traceDispatchTicks = 0;
		std::uint32_t traceUploadTicks = 0;
		std::uint32_t traceDisplayTicks = 0;

		DrawState appliedState;
		bool appliedStateValid = false;
		// Whether the base render mode (set_mode_standard + the 2D defaults) has to be re-issued before
		// the incremental mode setters can be trusted again (after attach, clear, present)
		bool modeBaseDirty = true;

		void InvalidateAppliedState()
		{
			appliedStateValid = false;
			modeBaseDirty = true;
		}

		void ApplyDrawState(const DrawState& state)
		{
			if (modeBaseDirty) {
				// Standard mode with the 2D defaults: no antialiasing (painter's-order blending wants no
				// coverage math), no perspective correction (all coordinates are screen space, W = 1)
				rdpq_set_mode_standard();
				rdpq_mode_persp(false);
				rdpq_mode_antialias(AA_NONE);
				modeBaseDirty = false;
				appliedStateValid = false;
			}
			if (!appliedStateValid || appliedState.Combiner != state.Combiner) {
				rdpq_mode_combiner(state.Combiner);
				if (TraceDrawStatistics) { stats.ModeChanges++; }
			}
			if (!appliedStateValid || appliedState.BlendEnabled != state.BlendEnabled ||
					(state.BlendEnabled && appliedState.Blender != state.Blender)) {
				rdpq_mode_blender(state.BlendEnabled ? state.Blender : 0);
			}
			if (!appliedStateValid || appliedState.Filter != state.Filter) {
				rdpq_mode_filter(rdpq_filter_t(state.Filter));
			}
			if (!appliedStateValid || (appliedState.Tlut != nullptr) != (state.Tlut != nullptr)) {
				rdpq_mode_tlut(state.Tlut != nullptr ? TLUT_RGBA16 : TLUT_NONE);
			}
			if (!appliedStateValid || appliedState.PrimColor != state.PrimColor) {
				rdpq_set_prim_color(color_from_packed32(state.PrimColor));
				if (TraceDrawStatistics) { stats.PrimChanges++; }
			}
			if (!appliedStateValid || appliedState.EnvColor != state.EnvColor) {
				rdpq_set_env_color(color_from_packed32(state.EnvColor));
			}
			appliedState = state;
			appliedStateValid = true;
		}

		// ---------------------------------------------------------------- TMEM window management
		//
		// The RDP samples only out of TMEM: 4 KB, of which CI8 texels may use only the lower 2 KB (the
		// upper half holds the TLUT). Every textured primitive is preceded by an upload of the texel
		// window it samples, deduplicated against what is already resident - a whole tile-layer run of
		// the same tile, or a text run out of one glyph's neighbourhood, reloads nothing.

		struct TmemWindow
		{
			const void* Buffer = nullptr;
			std::uint32_t Version = 0;
			std::int32_t S0 = 0, T0 = 0, S1 = 0, T1 = 0;
			std::int32_t Bytes = 0;			// TMEM footprint (whether the upper half was touched)
			bool Valid = false;
		};
		TmemWindow tmemWindow;
		/*
			The libdragon texture loader that UploadWindow drives, kept ACROSS uploads.

			@ref rdpq_tex_upload_sub() is a wrapper that builds a fresh loader for every call, and a fresh
			loader has forgotten both the rectangle it last loaded and the tile configuration it last
			issued. That is exactly the state libdragon's own multi-pass loader exists to keep: with it
			alive, a load whose width matches the previous one skips recomputing the TMEM pitch and the
			LOAD_BLOCK feasibility, and skips re-issuing SET_TEXTURE_IMAGE and SET_TILE - two of the four
			RDP commands a load costs. A text run out of one font atlas is dozens of such loads in a row.

			The loader caches the tile setup, so it has to be dropped whenever anything else programs
			TILE0: the blit path (which configures TILE0 itself) and InvalidateTmemWindow, which already
			marks the points where TMEM residency stops being knowable.
		*/
		tex_loader_t tmemLoader;
		const surface_t* tmemLoaderSurface = nullptr;
		const void* tmemLoaderBuffer = nullptr;
		// The TLUT resident in the upper half of TMEM, tracked separately from the texel window: a text
		// run reloads a different glyph window per character but the SAME palette row, and re-sending the
		// 512-byte table (plus the SYNC_LOAD rdpq inserts around it) per glyph was pure churn. The stamp
		// catches a slot whose contents were rewritten for another row (same pointer, new table).
		//
		// The two residencies ALIAS in hardware: a 256-entry TLUT fills the entire upper 2 KB (entries
		// are quadruplicated to 64 bits each), and a direct-color window bigger than 2 KB reaches into
		// that same upper half - so either upload evicts the other's tail, and UploadWindow below
		// cross-invalidates on exactly that condition. Missing this was a real bug: the warp bands'
		// ~2-4 KB RGBA16 windows clobbered the TLUT between glyph draws of the CI8 fonts, whose skipped
		// re-upload then resolved indices through texel garbage.
		const std::uint16_t* tmemTlut = nullptr;
		std::uint32_t tmemTlutStamp = 0;

		inline void InvalidateTmemWindow()
		{
			tmemWindow.Valid = false;
			tmemTlut = nullptr;
			tmemLoaderSurface = nullptr;
		}

		inline std::int32_t TmemBudget(tex_format_t fmt)
		{
			// The formats that may use only half of TMEM, matching libdragon's own table: the paletted
			// ones because the palette occupies the upper half, RGBA32 and YUV16 because their texels are
			// split across both halves
			return (fmt == FMT_CI8 || fmt == FMT_CI4 || fmt == FMT_RGBA32 || fmt == FMT_YUV16 ? 2048 : 4096);
		}

		// TMEM rows are whole 64-bit words. This is deliberately the SAME expression libdragon's uploader
		// computes and asserts on, so a window this accepts is one it will load: anything more
		// conservative rejects windows that in fact fit, and rejecting them is expensive. An earlier
		// version padded the width by 8 texels for safety, which inflated a 256-wide RGBA16 row from 512
		// to 528 bytes - and the textured-background warp's widest band is exactly 256x8, exactly 4096
		// bytes, exactly the budget. Those 16 bytes of caution sent every band down the subdivision path
		// below and took the menu frame from 120 triangles and 187 KB of uploads to 880 and 1.86 MB.
		inline std::int32_t TmemPitch(tex_format_t fmt, std::int32_t widthTexels)
		{
			return (std::int32_t(TEX_FORMAT_PIX2BYTES(fmt, widthTexels)) + 7) & ~7;
		}

		inline bool WindowFits(const surface_t* surf, std::int32_t s0, std::int32_t t0, std::int32_t s1, std::int32_t t1)
		{
			const tex_format_t fmt = surface_get_format(surf);
			return TmemPitch(fmt, s1 - s0) * (t1 - t0) <= TmemBudget(fmt);
		}

		/**
			@brief Clamps a texel window into the surface, guarding it by a texel only where that is needed

			A bilinear tap at the edge of the sampled span reads the neighbouring texel, so a filtered
			window is widened by one on each side. A point-sampled one is NOT: the guard would be two
			extra rows and columns that nothing reads, which is both wasted upload and - because TMEM
			budgets are counted in whole rows - occasionally the difference between fitting and not. It
			was exactly that: the textured-background warp's widest band samples 256x6 of a 256x256
			surface where TMEM holds seven rows, and the guard pushed it to eight and clamped it.
		*/
		void ClampWindow(const surface_t* surf, std::int32_t& s0, std::int32_t& t0, std::int32_t& s1, std::int32_t& t1,
			bool bilinear)
		{
			const std::int32_t guard = (bilinear ? 1 : 0);
			s0 = std::max<std::int32_t>(s0 - guard, 0);
			t0 = std::max<std::int32_t>(t0 - guard, 0);
			s1 = std::min<std::int32_t>(s1 + guard, surf->width);
			t1 = std::min<std::int32_t>(t1 + guard, surf->height);
			if (s1 <= s0) {
				s1 = std::min<std::int32_t>(s0 + 1, surf->width);
				s0 = s1 - 1;
			}
			if (t1 <= t0) {
				t1 = std::min<std::int32_t>(t0 + 1, surf->height);
				t0 = t1 - 1;
			}
		}

		/*
			Two things about TMEM uploads were MEASURED rather than assumed, because both plausible
			policies are wrong in opposite directions.

			Windows are uploaded EXACTLY as sampled. Widening them - to the surface's full width, which is
			the only shape libdragon can load with a single LOAD_BLOCK command instead of a per-row
			LOAD_TILE walk, or to a 16-texel grid so neighbours could share one resident window - was
			implemented and measured twice, and both lost: in the menu frame the grid turned 301
			primitives into 244 uploads of 297 KB (1.2 KB each, five times a glyph's own footprint) at an
			unchanged one-third hit rate, and full-width bands were worse still, 473 KB across 295 uploads
			with 84 rectangles newly split, costing 12 ms a frame. The menu's atlases are wide, so a band
			buys a single command and pays for it in rows: a 256-wide CI8 atlas holds only 8 rows of a
			2 KB budget, less than one glyph is tall. What DOES hit is the identical-window case - a
			sprite drawn many times over, which is most of what a frame repeats - and that needs no
			widening at all.

			A window too big for TMEM is cut into HORIZONTAL bands, though, rather than halved on its
			longer axis. Both terminate, but square pieces are the worst possible shape: none of them
			spans the surface's stride, so every one takes the slow per-row path, and the cinematics'
			full-screen video quad came out as 256 pieces and 256 uploads. Cutting only along T keeps each
			piece as wide as it already was - for a quad sampling a whole texture that is the full stride,
			so the fast single-command path applies - and needs only as many pieces as TMEM has rows for,
			which takes the same quad to ~40.
		*/

		// Loads the window (and the pass's TLUT) into TMEM unless each is already resident - the two
		// halves of TMEM are tracked independently, so a glyph run reloads windows but not the palette.
		// Texture coordinates keep addressing the ORIGINAL surface space afterwards - rdpq folds the
		// window origin into the tile descriptor.
		/** @brief Loads the pass's palette into the upper half of TMEM unless it is already resident */
		void EnsureTlutResident(const DrawState& state)
		{
			if (state.Tlut != nullptr && (tmemTlut != state.Tlut || tmemTlutStamp != state.TlutStamp)) {
				rdpq_tex_upload_tlut(const_cast<std::uint16_t*>(state.Tlut), 0, 256);
				if (TraceDrawStatistics) { stats.TlutUploads++; }
				tmemTlut = state.Tlut;
				tmemTlutStamp = state.TlutStamp;
				// The TLUT fills the whole upper 2 KB, evicting the tail of any resident window that
				// reached into it
				if (tmemWindow.Valid && tmemWindow.Bytes > 2048) {
					tmemWindow.Valid = false;
				}
			}
		}

		void UploadWindow(const DrawState& state, std::int32_t s0, std::int32_t t0, std::int32_t s1, std::int32_t t1)
		{
			const std::uint32_t uploadStart = (TraceDrawStatistics ? std::uint32_t(get_ticks()) : 0);
			EnsureTlutResident(state);
			if (tmemWindow.Valid && tmemWindow.Buffer == state.Texture->buffer && tmemWindow.Version == state.TextureVersion &&
				tmemWindow.S0 == s0 && tmemWindow.T0 == t0 && tmemWindow.S1 == s1 && tmemWindow.T1 == t1) {
				if (TraceDrawStatistics) { stats.WindowHits++; traceUploadTicks += std::uint32_t(get_ticks()) - uploadStart; }
				return;
			}
			if (tmemLoaderSurface != state.Texture || tmemLoaderBuffer != state.Texture->buffer) {
				tmemLoader = tex_loader_init(TILE0, state.Texture);
				tex_loader_set_tmem_addr(&tmemLoader, 0);
				tmemLoaderSurface = state.Texture;
				tmemLoaderBuffer = state.Texture->buffer;
			}
			tex_loader_load(&tmemLoader, s0, t0, s1, t1);
			if (TraceDrawStatistics) {
				stats.WindowUploads++;
				stats.WindowBytes += std::uint32_t(TmemPitch(surface_get_format(state.Texture), s1 - s0) * (t1 - t0));
			}
			tmemWindow.Buffer = state.Texture->buffer;
			tmemWindow.Version = state.TextureVersion;
			tmemWindow.S0 = s0;
			tmemWindow.T0 = t0;
			tmemWindow.S1 = s1;
			tmemWindow.T1 = t1;
			tmemWindow.Bytes = TmemPitch(surface_get_format(state.Texture), s1 - s0) * (t1 - t0);
			tmemWindow.Valid = true;
			// A direct-color window bigger than 2 KB reached into the upper half of TMEM and overwrote
			// the resident TLUT there (CI8 windows never can - their budget is the lower half). This is
			// what garbled the CI8 font glyphs between the warp bands' big windows when it went untracked.
			if (tmemWindow.Bytes > 2048) {
				tmemTlut = nullptr;
			}
			if (TraceDrawStatistics) { traceUploadTicks += std::uint32_t(get_ticks()) - uploadStart; }
		}

		// ---------------------------------------------------------------- primitive submission

		bool warnedOversizedTriangle = false;

		/**
			@brief One axis-aligned textured rectangle

			A window that fits TMEM is uploaded and drawn directly, which is the overwhelmingly common
			case and the one the residency cache serves. A window too big for TMEM - a full-screen
			cinematic frame is 640x480 CI8, whose 648-byte rows leave room for THREE of them in the 2 KB
			a paletted texture may use - is handed to libdragon's blitter instead of being cut up here.
			@ref rdpq_tex_blit tiles a surface of any size into TMEM-sized chunks itself, choosing the
			single-command LOAD_BLOCK path wherever the stride allows it, and it gets the sub-texel edges
			between chunks right.

			The recursive splitter this replaces could not express the case at all: it cut the rectangle in
			screen space and re-derived each piece's window from the interpolated coordinates, but @ref
			ClampWindow widens every window by a texel on each side for the bilinear taps, so a piece's
			window never shrank below THREE rows however deep the recursion went. With exactly three rows
			of headroom the comparison sat on the boundary, the pieces that rounded to four rows kept
			splitting, and the ones that never got under the limit reached the depth guard and drew
			clamped - the whole lower part of every intro frame sampling the last rows it had loaded.
		*/
		void SubmitTexturedRect(const DrawState& state, float x0, float y0, float x1, float y1,
			float s0, float t0, float s1, float t1)
		{
			if (x1 - x0 < 0.01f || y1 - y0 < 0.01f) {
				return;
			}
			std::int32_t winS0 = std::int32_t(std::floor(std::min(s0, s1)));
			std::int32_t winT0 = std::int32_t(std::floor(std::min(t0, t1)));
			std::int32_t winS1 = std::int32_t(std::ceil(std::max(s0, s1)));
			std::int32_t winT1 = std::int32_t(std::ceil(std::max(t0, t1)));
			ClampWindow(state.Texture, winS0, winT0, winS1, winT1, state.Filter == FILTER_BILINEAR);

			if (!WindowFits(state.Texture, winS0, winT0, winS1, winT1)) {
				ApplyDrawState(state);
				// The blitter loads texels itself, but never a palette, so the TLUT still has to be
				// resident before it draws
				EnsureTlutResident(state);

				rdpq_blitparms_t parms = {};
				parms.tile = TILE0;
				// Source sub-rect in texel space, with a mirrored sprite expressed as a flip rather than
				// as reversed coordinates (the blitter takes extents, not a pair of corners)
				parms.s0 = winS0;
				parms.t0 = winT0;
				parms.width = winS1 - winS0;
				parms.height = winT1 - winT0;
				parms.flip_x = (s1 < s0);
				parms.flip_y = (t1 < t0);
				parms.scale_x = (parms.width > 0 ? (x1 - x0) / float(parms.width) : 1.0f);
				parms.scale_y = (parms.height > 0 ? (y1 - y0) / float(parms.height) : 1.0f);
				// Tells the blitter to overlap its chunks by a texel, which is what keeps a bilinear tap
				// at a chunk seam from sampling outside the chunk
				parms.filtering = (state.Filter == FILTER_BILINEAR);
				rdpq_tex_blit(state.Texture, x0, y0, &parms);

				// The blitter left its own last chunk in the texel half of TMEM
				tmemWindow.Valid = false;
				tmemLoaderSurface = nullptr;
				// A direct-color blit chunks through all 4 KB of TMEM and overwrote any resident TLUT in
				// the upper half - the same cross-invalidation UploadWindow does for its >2 KB windows.
				// (A CI blit's chunks stay in the lower half, its budget, so the TLUT survives those.)
				if (state.Tlut == nullptr) {
					tmemTlut = nullptr;
				}
				if (TraceDrawStatistics) {
					stats.Blits++;
					static bool tracedBlit = false;
					if (!tracedBlit) {
						tracedBlit = true;
						LOGI("Blit path: surface {}x{} stride {}, src ({},{})-({},{}), screen ({},{})-({},{}), scale {}/{}",
							state.Texture->width, state.Texture->height, state.Texture->stride,
							winS0, winT0, winS1, winT1, std::int32_t(x0), std::int32_t(y0),
							std::int32_t(x1), std::int32_t(y1), parms.scale_x, parms.scale_y);
					}
				}
				return;
			}

			ApplyDrawState(state);
			UploadWindow(state, winS0, winT0, winS1, winT1);
			rdpq_texture_rectangle_scaled(TILE0, x0, y0, x1, y1, s0, t0, s1, t1);
			if (TraceDrawStatistics) { stats.Rects++; }
		}

		/** @brief One textured triangle; a window bigger than TMEM is clamped (nothing on this tier draws one) */
		void SubmitTexturedTriangle(const DrawState& state, const float* v0, const float* v1, const float* v2)
		{
			std::int32_t winS0 = std::int32_t(std::floor(std::min(std::min(v0[2], v1[2]), v2[2])));
			std::int32_t winT0 = std::int32_t(std::floor(std::min(std::min(v0[3], v1[3]), v2[3])));
			std::int32_t winS1 = std::int32_t(std::ceil(std::max(std::max(v0[2], v1[2]), v2[2])));
			std::int32_t winT1 = std::int32_t(std::ceil(std::max(std::max(v0[3], v1[3]), v2[3])));
			ClampWindow(state.Texture, winS0, winT0, winS1, winT1, state.Filter == FILTER_BILINEAR);
			if (!WindowFits(state.Texture, winS0, winT0, winS1, winT1)) {
				// TODO: split rotated primitives like the rectangle path splits axis-aligned ones if a
				// rotated sprite ever grows past TMEM; today the biggest rotated frames stay well below it
				const tex_format_t fmt = surface_get_format(state.Texture);
				while (TmemPitch(fmt, winS1 - winS0) > TmemBudget(fmt) && winS1 - winS0 > 8) {
					winS1--;
				}
				const std::int32_t maxRows = TmemBudget(fmt) / TmemPitch(fmt, winS1 - winS0);
				if (winT1 - winT0 > maxRows) {
					winT1 = winT0 + maxRows;
				}
				if (!warnedOversizedTriangle) {
					warnedOversizedTriangle = true;
					LOGW("A rotated primitive samples a window bigger than TMEM; its texture is clamped");
				}
			}
			ApplyDrawState(state);
			UploadWindow(state, winS0, winT0, winS1, winT1);
			rdpq_triangle(&TRIFMT_TEX, v0, v1, v2);
			if (TraceDrawStatistics) { stats.Triangles++; }
		}

		/**
			@brief Submits one quad (textured or untextured)

			Corners 0/1 share the sprite's local x = 1 edge and 2/3 its x = 0 edge, 0/2 the y = 0 edge and
			1/3 the y = 1 edge (see the corner synthesis in Dispatch), so an axis-aligned quad is exactly
			the case where those pairs agree on the other axis - and it goes out as a TEXTURE_RECTANGLE,
			the RDP's cheapest primitive. Anything rotated goes out as a triangle pair.
		*/
		void SubmitQuadPrimitive(const DrawState& state, const float* px, const float* py, const float* pu, const float* pv,
			float dx = 0.0f, float dy = 0.0f)
		{
			if (state.Texture == nullptr) {
				ApplyDrawState(state);
				const float v0[2] = { px[0] + dx, py[0] + dy };
				const float v1[2] = { px[1] + dx, py[1] + dy };
				const float v2[2] = { px[2] + dx, py[2] + dy };
				const float v3[2] = { px[3] + dx, py[3] + dy };
				rdpq_triangle(&TRIFMT_FILL, v0, v1, v2);
				rdpq_triangle(&TRIFMT_FILL, v2, v1, v3);
				return;
			}

			const bool axisAligned = (px[0] == px[1] && px[2] == px[3] && py[0] == py[2] && py[1] == py[3]);
			if (axisAligned) {
				// The rectangle takes two opposite corners in increasing order; a mirrored sprite (a
				// negative scale in the model matrix) trades them over, and its texture coordinates travel
				// with them, which is what keeps the flip (the RDP steps S/T with negative increments)
				float x0 = px[2] + dx, u0 = pu[2], x1 = px[0] + dx, u1 = pu[0];
				if (x0 > x1) {
					std::swap(x0, x1);
					std::swap(u0, u1);
				}
				float y0 = py[0] + dy, v0 = pv[0], y1 = py[1] + dy, v1 = pv[1];
				if (y0 > y1) {
					std::swap(y0, y1);
					std::swap(v0, v1);
				}
				SubmitTexturedRect(state, x0, y0, x1, y1, u0, v0, u1, v1);
			} else {
				const float v0[5] = { px[0] + dx, py[0] + dy, pu[0], pv[0], 1.0f };
				const float v1[5] = { px[1] + dx, py[1] + dy, pu[1], pv[1], 1.0f };
				const float v2[5] = { px[2] + dx, py[2] + dy, pu[2], pv[2], 1.0f };
				const float v3[5] = { px[3] + dx, py[3] + dy, pu[3], pv[3], 1.0f };
				SubmitTexturedTriangle(state, v0, v1, v2);
				SubmitTexturedTriangle(state, v2, v1, v3);
			}
		}

		/**
			@brief Submits a triangle strip (arbitrary synthesized geometry), flat or gouraud

			@p colors, when given, are per-vertex floats (r,g,b,a in 0..1, the range rdpq_triangle's shade
			coefficients take); the strip is then drawn gouraud - textured only when @p textured also holds
			(the TintMix strips), untextured otherwise (a gradient has no texture to modulate).
		*/
		void SubmitStripPrimitive(const DrawState& state, const float* px, const float* py, const float* pu, const float* pv,
			std::int32_t count, const float* colors, float dx, float dy)
		{
			const bool textured = (state.Texture != nullptr);
			// One window for the whole strip is the fast path (the band pieces the effects build usually
			// sample small spans, and then the strip costs a single upload). A strip whose combined span
			// does not fit TMEM - the textured-background warp reaches across its source - is uploaded
			// PER TRIANGLE instead: each triangle covers a fraction of the span, so the pieces fit where
			// the whole never could, and the residency cache collapses the ones that repeat a window.
			// Only a single triangle that alone exceeds TMEM is still clamped, which nothing on this tier
			// draws (a rotated primitive that big would have to be a full-screen quad, and those are
			// axis-aligned and take the blit path).
			ApplyDrawState(state);
			if (TraceDrawStatistics) { stats.Strips++; }
			// The strip's window is uploaded exactly as sampled, and consecutive strips then share it
			// through the residency cache without any help: the textured-background warp tiles each band
			// into up to eight horizontal pieces and normalizes every piece's texture coordinates back
			// into ONE wrap period, so the pieces of a band ask for the identical window. Measured, that
			// is ~100 strips a frame costing ~30 uploads. Snapping the window to a full-width row band of
			// the rows that fill the budget was implemented and measured against this on the same scene,
			// on the theory that adjacent bands would share too: 196 uploads / 167 KB / 57.3 ms with it
			// against 197 / 172 KB / 57.7 ms without, i.e. nothing outside the noise, because the sharing
			// that matters was already happening. It was removed again rather than left in to load rows
			// nobody reads.
			if (textured) {
				std::int32_t winS0 = INT32_MAX, winT0 = INT32_MAX, winS1 = INT32_MIN, winT1 = INT32_MIN;
				for (std::int32_t i = 0; i < count; i++) {
					winS0 = std::min(winS0, std::int32_t(std::floor(pu[i])));
					winT0 = std::min(winT0, std::int32_t(std::floor(pv[i])));
					winS1 = std::max(winS1, std::int32_t(std::ceil(pu[i])));
					winT1 = std::max(winT1, std::int32_t(std::ceil(pv[i])));
				}
				ClampWindow(state.Texture, winS0, winT0, winS1, winT1, state.Filter == FILTER_BILINEAR);
				if (!WindowFits(state.Texture, winS0, winT0, winS1, winT1)) {
					// One window for the whole strip, clamped to what TMEM holds when the strip samples
					// more. Drawing it exactly was implemented and MEASURED: per-triangle windows, with a
					// four-way subdivision where a single triangle still overran, took the menu frame from
					// 120 triangles and 187 KB of uploads to 856 and 1.87 MB, and its dispatch from 49 ms
					// to 87 ms - to recover the last rows of the textured-background warp's widest bands,
					// which is a seam a texel or two deep. Not worth it at this frame rate. What did remove
					// most of the clamping was computing the TMEM pitch exactly as libdragon does (see
					// TmemPitch) instead of padding it: the band that used to warn here now fits.
					const tex_format_t fmt = surface_get_format(state.Texture);
					const std::int32_t rows = std::max<std::int32_t>(TmemBudget(fmt) / TmemPitch(fmt, winS1 - winS0), 1);
					if (winT1 - winT0 > rows) {
						if (!warnedOversizedTriangle) {
							warnedOversizedTriangle = true;
							LOGW("A strip samples {}x{} of a {}x{} surface, more than TMEM holds; clamped to {} rows",
								winS1 - winS0, winT1 - winT0, state.Texture->width, state.Texture->height, rows);
						}
						winT1 = winT0 + rows;
					}
				}
				UploadWindow(state, winS0, winT0, winS1, winT1);
			}

			// Vertex layouts of the TRIFMT_* formats: FILL {x,y}, SHADE {x,y,r,g,b,a}, TEX {x,y,s,t,w},
			// SHADE_TEX {x,y,r,g,b,a,s,t,w}
			float verts[3][9];
			for (std::int32_t i = 2; i < count; i++) {
				const std::int32_t idx[3] = { i - 2, i - 1, i };
				for (std::int32_t k = 0; k < 3; k++) {
					const std::int32_t v = idx[k];
					float* out = verts[k];
					std::int32_t n = 0;
					out[n++] = px[v] + dx;
					out[n++] = py[v] + dy;
					if (colors != nullptr) {
						out[n++] = colors[v * 4];
						out[n++] = colors[v * 4 + 1];
						out[n++] = colors[v * 4 + 2];
						out[n++] = colors[v * 4 + 3];
					}
					if (textured) {
						out[n++] = pu[v];
						out[n++] = pv[v];
						out[n++] = 1.0f;
					}
				}
				const rdpq_trifmt_t* fmt = (colors != nullptr
					? (textured ? &TRIFMT_SHADE_TEX : &TRIFMT_SHADE)
					: (textured ? &TRIFMT_TEX : &TRIFMT_FILL));
				rdpq_triangle(fmt, verts[0], verts[1], verts[2]);
				if (TraceDrawStatistics) { stats.Triangles++; }
			}
		}

		// Maps the material's pipeline-neutral blend factors onto a blender formula. The RDP blender
		// computes P*A + M*B with a tiny mux (A in {IN_ALPHA, FOG_ALPHA, SHADE_ALPHA, 0}; B in
		// {1-A, MEM_CVG, 1, 0}), so only the alpha-over and additive families are expressible; anything
		// else falls back to alpha-over. (Notably dst*src cannot be built here at all - the lighting
		// combine encodes its multiply in the texture alpha instead, see ApplyPendingSoftwareLighting.)
		void MapBlendRdp(bool enabled, nCine::BlendingFactor src, nCine::BlendingFactor dst, DrawState& state)
		{
			if (!enabled || (src == nCine::BlendingFactor::One && dst == nCine::BlendingFactor::Zero)) {
				state.BlendEnabled = false;
				state.Blender = 0;
				return;
			}
			state.BlendEnabled = true;
			if (dst == nCine::BlendingFactor::One) {
				// (SrcAlpha, One) and (One, One) both take the alpha-scaled additive form - the blender's
				// A mux has no literal ONE, and the (One, One) materials carry alpha 1 anyway
				state.Blender = BlendAdditive;
			} else {
				state.Blender = BlendAlphaOver;
			}
		}

		// ---------------------------------------------------------------- per-frame TLUT cache
		//
		// The RDP resolves CI8 texels through a 256-entry RGBA5551 TLUT in the upper half of TMEM; the
		// engine's palettes are RGBA8 rows of the shared palette texture, so each row a frame samples is
		// converted once into one of these slots and referenced by the queued upload commands until the
		// frame is presented (PresentFrame waits for the RDP before slots can be rewritten). Keyed exactly
		// like the PowerVR's palette banks, so repeated draws of a row convert nothing.
		constexpr std::int32_t MaxTlutSlots = 8;
		struct TlutSlot
		{
			alignas(64) std::uint16_t Entries[256];
			const void* Palette = nullptr;
			std::int32_t Offset = -1;
			std::uint32_t Version = 0;
			std::uint32_t LastUsedFrame = 0;
			std::uint32_t WriteStamp = 0;
			std::uint64_t PaletteHash = 0;
			bool Valid = false;
		};
		TlutSlot tlutSlots[MaxTlutSlots];
		std::uint32_t tlutWriteCounter = 0;

		const TlutSlot* AcquireTlutForRow(const RdpTexture* palette, std::int32_t paletteOffset, std::uint32_t version)
		{
			// The offset is a flat index into the palette texture and does not need to be row-aligned
			// (the gem gradients pack two palettes into a single 256-entry row)
			const std::int32_t maxOffset = (palette != nullptr
				? palette->GetWidth() * palette->GetHeight() - 256 : 0);
			if (palette == nullptr || palette->GetPixels() == nullptr ||
				paletteOffset < 0 || paletteOffset > maxOffset) {
				return nullptr;
			}

			const std::uint32_t frame = RdpDevice::GetSceneCounter();
			const std::uint32_t* source = reinterpret_cast<const std::uint32_t*>(palette->GetPixels()) + paletteOffset;
			std::int32_t victim = 0;
			std::uint32_t oldestUse = UINT32_MAX;
			for (std::int32_t i = 0; i < MaxTlutSlots; i++) {
				if (tlutSlots[i].Valid && tlutSlots[i].Palette == palette && tlutSlots[i].Offset == paletteOffset) {
					if (tlutSlots[i].Version != version) {
						// A write elsewhere in the palette texture bumped the global generation; only a
						// change to THIS row matters, and re-hashing it keeps both the converted table and
						// its TMEM copy (the WriteStamp) valid - a text run then reloads nothing at all
						if (tlutSlots[i].PaletteHash != HashPaletteRow(source)) {
							continue;
						}
						tlutSlots[i].Version = version;
					}
					tlutSlots[i].LastUsedFrame = frame;
					return &tlutSlots[i];
				}
				const std::uint32_t use = (tlutSlots[i].Valid ? tlutSlots[i].LastUsedFrame : 0);
				if (use < oldestUse) {
					oldestUse = use;
					victim = i;
				}
			}

			TlutSlot& slot = tlutSlots[victim];
			if (TraceDrawStatistics) { stats.TlutConversions++; }
			if (slot.Valid && !RdpDevice::IsFrameRetired(slot.LastUsedFrame)) {
				if (TraceDrawStatistics) { stats.TlutEvictWaits++; }
				// Queued upload commands of a frame still in flight may DMA the victim; wait for exactly
				// that frame. Only more than MaxTlutSlots distinct rows across the in-flight window ever
				// pays this, so the stall is preferable to a per-frame arena.
				RdpDevice::WaitForFrame(slot.LastUsedFrame);
			}
			// A palette entry is a uint32 whose VALUE has red in the low byte (`color & 0xFF` is how the
			// whole engine reads them, ContentResolver included) - and the palette texture's store holds
			// those words verbatim. Extracting by value keeps this correct on the big-endian VR4300,
			// where the same words are the byte sequence a,b,g,r - exactly like the GX backend (the
			// other big-endian console) converts its TLUTs.
			const std::uint32_t* entries = source;
			for (std::int32_t i = 0; i < 256; i++) {
				const std::uint32_t rgba = entries[i];
				slot.Entries[i] = Pack5551(std::uint8_t(rgba & 0xFF), std::uint8_t((rgba >> 8) & 0xFF),
					std::uint8_t((rgba >> 16) & 0xFF), std::uint8_t((rgba >> 24) & 0xFF));
			}
			data_cache_hit_writeback(slot.Entries, sizeof(slot.Entries));
			slot.Palette = palette;
			slot.Offset = paletteOffset;
			slot.Version = version;
			slot.PaletteHash = HashPaletteRow(source);
			slot.LastUsedFrame = frame;
			slot.WriteStamp = ++tlutWriteCounter;
			slot.Valid = true;
			return &slot;
		}
	}

	// EffectContext deliberately has EXTERNAL linkage (namespace scope, though it is still defined only in
	// this translation unit), for the same reason as on the PVR and the GE: the effect-table struct below
	// is at namespace scope - so the backend's ShaderProgram can forward-declare it and hold a typed entry
	// pointer - and names EffectContext in a member type.
	// ---------------------------------------------------------- fixed-function quad effects
	//
	// The quad-family effects are expressed as FixedFunctionPass descriptors handed to this EffectContext -
	// the structural contract documented in FixedFunctionPass.h, implemented here against the rdpq
	// submission helpers above. The per-effect functions themselves are GENERATED from the shaders'
	// fixed_function blocks by the ShaderCompiler (Shaders/Generated/RdpGeneratedEffects.h, included
	// below), so this file contains no effect-specific code at all. The transpiler rejects ModulateX4 and
	// LumaRamp for this target (the combiner has no x4 output scale and no per-texel channel arithmetic).

	struct EffectContext
	{
		// Matches the GX/GU capacity: the RDP takes a strip of any length as triangles, so nothing here
		// needs the geometry split into small pieces
		static constexpr std::int32_t MaxStripVertices = 16;

		// Decoded instance data of the draw being dispatched (the documented contract)
		const float* InstanceColor;
		float TexelW;
		float TexelH;
		bool Batched;

		// The RDP state the material resolved to, and the current instance's corner arrays (already in
		// screen pixels and texel-space texture coordinates)
		const DrawState* Material;
		const float* Px;
		const float* Py;
		const float* Pu;
		const float* Pv;
		const float* TexRect;

		// Pre-clip quad geometry (the quad_origin/quad_axis_* built-ins), the program (for resolved
		// uniforms) and the conversion from the shader's normalized texture space into texel coordinates
		float OriginX, OriginY;
		float AxisXx, AxisXy;
		float AxisYx, AxisYy;
		const RdpShaderProgram* Program;
		float UvScaleU, UvScaleV;

		// The strip builder scratch; colours stay clamped floats because rdpq_triangle takes its shade
		// coefficients in 0..1 (identical float inputs produce identical coefficients)
		float StripX[MaxStripVertices];
		float StripY[MaxStripVertices];
		float StripU[MaxStripVertices];
		float StripV[MaxStripVertices];
		float StripColor[MaxStripVertices][4];

		// The pending merged quad (see SubmitQuad): the first pass's descriptor and the running sum of
		// every pass's colour accumulated into it
		FixedFunctionPass PendingPass;
		float PendingRgb[3] = { 0.0f, 0.0f, 0.0f };
		bool HasPending = false;

		const float* Color() const { return InstanceColor; }
		float TexelWidth() const { return TexelW; }
		float TexelHeight() const { return TexelH; }
		bool IsBatched() const { return Batched; }

		float QuadOriginX() const { return OriginX; }
		float QuadOriginY() const { return OriginY; }
		float QuadAxisXx() const { return AxisXx; }
		float QuadAxisXy() const { return AxisXy; }
		float QuadAxisYx() const { return AxisYx; }
		float QuadAxisYy() const { return AxisYy; }

		bool HasUniform(const char* name) const
		{
			return (Program->ResolveUniform(name) != nullptr);
		}
		void LoadUniform(const char* name, float* out, std::int32_t floatCount) const
		{
			// An unresolved name leaves the caller's zeros in place - blocks guard with has_uniform()
			const std::uint8_t* bytes = Program->ResolveUniform(name);
			if (bytes != nullptr) {
				std::memcpy(out, bytes, std::size_t(floatCount) * sizeof(float));
			}
		}

		void SetStripVertexPosition(std::int32_t i, float x, float y)
		{
			if (std::uint32_t(i) < std::uint32_t(MaxStripVertices)) {
				StripX[i] = x;
				StripY[i] = y;
			}
		}
		void SetStripVertexUv(std::int32_t i, float u, float v)
		{
			if (std::uint32_t(i) < std::uint32_t(MaxStripVertices)) {
				StripU[i] = u * UvScaleU;
				StripV[i] = v * UvScaleV;
			}
		}
		void SetStripVertexColor(std::int32_t i, float r, float g, float b, float a)
		{
			if (std::uint32_t(i) < std::uint32_t(MaxStripVertices)) {
				StripColor[i][0] = Saturate(r);
				StripColor[i][1] = Saturate(g);
				StripColor[i][2] = Saturate(b);
				StripColor[i][3] = Saturate(a);
			}
		}

		// Whether a UV span can be mapped onto the screen at all (a zero texRect has no scale)
		bool HasTexelStep() const { return TexRect[0] != 0.0f && TexRect[2] != 0.0f; }
		// Maps a span in the sprite's UV space onto the quad's on-screen extent - the texel step the
		// Outline ring taps use. The corners are already in screen space, so the result is a screen-space
		// displacement.
		float TexelToScreenX(float uvSpan) const { return (Px[0] - Px[2]) * (uvSpan / TexRect[0]); }
		float TexelToScreenY(float uvSpan) const { return (Py[1] - Py[0]) * (uvSpan / TexRect[2]); }
		// The documented texel_size() built-in of the fixed_function contract: the Outline shader family
		// carries the sprite's UV-space texel size in its instance color.xy (exactly like the GLSL derives
		// its tap offsets), folded through the screen-space conversion above
		float TexelStepX() const { return TexelToScreenX(InstanceColor[0]); }
		float TexelStepY() const { return TexelToScreenY(InstanceColor[1]); }

		/*
			Folds a pass descriptor into a copy of the material state. Unlike the GE - which has no
			post-texture additive term and expands an offset colour into a second silhouette draw - the
			combiner's D slot takes the ENV register, so every pass stays ONE primitive:

			  Modulate            (TEX0-0)*PRIM + 0        [+ ENV when the pass carries an offset colour]
			  Silhouette          RGB = PRIM, A = TEX0*PRIM  (an offset is a constant added to a constant,
			                      so it folds into PRIM on the CPU)
			  TintMix             (PRIM-TEX0)*PRIM_A + TEX0, opaque result, one cycle
			  ModulateX2          two-cycle; cycle 1 doubles COMBINED; with an offset colour ENV carries
			                      half of it, restored exactly by the doubling
		*/
		void ApplyPassToState(DrawState& state, const FixedFunctionPass& pass, const float* rgb) const
		{
			switch (pass.Blend) {
				case FixedFunctionPass::BlendMode::Additive:
					state.BlendEnabled = true;
					state.Blender = BlendAdditive;
					break;
				case FixedFunctionPass::BlendMode::Opaque:
					state.BlendEnabled = false;
					state.Blender = 0;
					break;
				case FixedFunctionPass::BlendMode::Alpha:
					state.BlendEnabled = true;
					state.Blender = BlendAlphaOver;
					break;
				default:
					break;		// The material's own blending
			}

			// The accumulated colour of the merged passes (see SubmitQuad) rather than this pass's own,
			// and the pass alpha alongside it
			float color[4] = { rgb[0], rgb[1], rgb[2], pass.Color[3] };
			/*
				A gain above 1.0 cannot ride PRIM alone (a colour register holds 0..1), so it is halved
				into PRIM and restored by the two-cycle doubling combiner, whose output clamp is what
				saturates the bright end - the saturation the other consoles get from their framebuffer
				and the RDP's blender cannot give at all.

				That clamp only reaches to #MaxCombinerResult though, so the gain is capped there FIRST,
				per channel. Per channel rather than by scaling the whole colour down: a channel already
				within the ceiling then keeps its exact gain and only the ones that ask for more are
				limited, so the brightest texels of the sprite still saturate to the same white the
				reference does, and only the mid-tones of an over-range channel come out less extreme.
				Scaling uniformly would have kept the mid-tone hue exactly but darkened every channel to
				do it, including the ones the hardware could have rendered correctly.

				Without the cap this was the bug that made bright colorized text change colour: a gain of
				2.1 packs to PRIM 255, and the doubling combiner turns a bright texel's 255 into 510,
				which wraps to 0 - so a red-leaning near-white glyph (2.1, 1.02, 1.02) came out CYAN,
				its red channel black and its other two clamped to full.
			*/
			const float offsetHeadroom[3] = {
				pass.HasOffsetColor ? Saturate(pass.OffsetColor[0]) : 0.0f,
				pass.HasOffsetColor ? Saturate(pass.OffsetColor[1]) : 0.0f,
				pass.HasOffsetColor ? Saturate(pass.OffsetColor[2]) : 0.0f
			};
			for (std::int32_t i = 0; i < 3; i++) {
				// An offset colour is added by the same cycle, so it spends the same budget
				const float ceiling = MaxCombinerResult - offsetHeadroom[i];
				if (color[i] > ceiling) {
					color[i] = (ceiling > 0.0f ? ceiling : 0.0f);
				}
			}
			const float peak = (color[0] > color[1] ? (color[0] > color[2] ? color[0] : color[2])
				: (color[1] > color[2] ? color[1] : color[2]));
			const bool doubled = (peak > 1.0f);
			if (doubled) {
				color[0] *= 0.5f;
				color[1] *= 0.5f;
				color[2] *= 0.5f;
			}
			state.EnvColor = 0;
			if (state.Texture == nullptr) {
				state.Combiner = CombFlat;
			} else {
				switch (pass.Tev) {
					case FixedFunctionPass::TevPreset::Silhouette:
						state.Combiner = CombSilhouette;
						if (pass.HasOffsetColor) {
							// PRIM + ENV would need two adds; both are flat constants, so they fold here
							color[0] += pass.OffsetColor[0];
							color[1] += pass.OffsetColor[1];
							color[2] += pass.OffsetColor[2];
						}
						break;
					case FixedFunctionPass::TevPreset::TintMix:
						state.Combiner = CombTintMix;
						break;
					case FixedFunctionPass::TevPreset::ModulateX2:
						if (pass.HasOffsetColor) {
							state.Combiner = CombModulateX2Offset;
							state.EnvColor = PackRgba(QuantizeChannel(pass.OffsetColor[0] * 0.5f),
								QuantizeChannel(pass.OffsetColor[1] * 0.5f), QuantizeChannel(pass.OffsetColor[2] * 0.5f), 0);
						} else {
							state.Combiner = CombModulateX2;
						}
						break;
					default:
						if (pass.HasOffsetColor) {
							// The offset rides ENV in the combiner's D slot; under the doubling combiner
							// it carries half of itself, restored exactly by cycle 1
							state.Combiner = (doubled ? CombModulateX2Offset : CombModulateOffset);
							const float offsetScale = (doubled ? 0.5f : 1.0f);
							state.EnvColor = PackRgba(QuantizeChannel(pass.OffsetColor[0] * offsetScale),
								QuantizeChannel(pass.OffsetColor[1] * offsetScale),
								QuantizeChannel(pass.OffsetColor[2] * offsetScale), 0);
						} else {
							state.Combiner = (doubled ? CombModulateX2 : CombModulate);
						}
						break;
				}
			}
			state.PrimColor = PackColor(color);
		}

		/*
			Submits one pass over the current instance's quad.

			Passes over the same geometry are ACCUMULATED here instead of being submitted one by one,
			because the RDP's blender wraps on overflow instead of clamping - libdragon documents its own
			RDPQ_BLENDER_ADDITIVE as "mostly broken" for exactly that reason. The split-multiplier idiom
			(Colorized: several additive passes whose sum the framebuffer is expected to saturate) is
			therefore unsubmittable pass by pass on this hardware: a sum above 1.0 wraps towards black and
			the glyph comes out a wrong hue, which is what per-glyph rainbow text looked like.

			Each pass's colour is SATURATED into [0,1] before it is added, because that is what submitting
			it separately would have done: a pass colour reaches the hardware through a register that
			holds 0..1, so the split-multiplier idiom above relies on each pass contributing
			clamp(gain - i, 0, 1) and the sum being min(gain, passes) - which is gain, by construction of
			the pass count. Adding the RAW values instead computes passes*gain - (0+1+..) and is not the
			same number at all: a glyph asking for gain (2.1, 1.02, 1.02) - a near-white with a red lean -
			summed raw to (3.3, 0.06, 0.06) and came out pure red, which is what made colorized menu text
			a saturated rainbow instead of the muted tones it asks for.

			Merging is otherwise algebraically EXACT, not an approximation. With a = texel.a * pass.a, pass 0 (over
			the material's alpha blend) contributes a*P0 + dst*(1 - a) and every additive pass after it
			a*Pi with no further attenuation of dst, so the total is

				a*(P0 + P1 + ... ) + dst*(1 - a)

			which is one alpha-blended draw whose colour is the SUM. The sum then reaches the hardware
			through PRIM (up to 1.0) or halved through the two-cycle doubling combiner, where the
			combiner's clamp provides the saturation the blender cannot - but only up to
			#MaxCombinerResult, past which the combiner wraps to black just like the blender does, so
			ApplyPassToState caps the sum there per channel before it is packed. Only geometrically
			identical passes merge - the Outline ring taps displace each pass by ScreenOffset and keep
			their own draws - and only ones sharing the whole non-colour pass state.

			A pass whose own blend is Additive with nothing before it to merge into still goes out on the
			additive blender and can still wrap; TODO if a glow over a bright background ever shows it
			(the shield and mask families add small amounts over mostly dark scenes).
		*/
		void SubmitQuad(const FixedFunctionPass& pass)
		{
			if (HasPending && pass.Blend == FixedFunctionPass::BlendMode::Additive && CanMergeInto(pass)) {
				PendingRgb[0] += Saturate(pass.Color[0]);
				PendingRgb[1] += Saturate(pass.Color[1]);
				PendingRgb[2] += Saturate(pass.Color[2]);
				return;
			}
			FlushPendingQuad();
			PendingPass = pass;
			PendingRgb[0] = Saturate(pass.Color[0]);
			PendingRgb[1] = Saturate(pass.Color[1]);
			PendingRgb[2] = Saturate(pass.Color[2]);
			HasPending = true;
		}

		/** @brief Whether @p pass differs from the pending one only in the colour that is being accumulated */
		bool CanMergeInto(const FixedFunctionPass& pass) const
		{
			return (pass.Tev == PendingPass.Tev && pass.HasOffsetColor == PendingPass.HasOffsetColor &&
				pass.Color[3] == PendingPass.Color[3] && pass.LumaGain == PendingPass.LumaGain &&
				pass.ScreenOffset[0] == PendingPass.ScreenOffset[0] &&
				pass.ScreenOffset[1] == PendingPass.ScreenOffset[1] &&
				pass.OffsetColor[0] == PendingPass.OffsetColor[0] &&
				pass.OffsetColor[1] == PendingPass.OffsetColor[1] &&
				pass.OffsetColor[2] == PendingPass.OffsetColor[2]);
		}

		/** @brief Submits the accumulated quad, if any (called before any other submission and at the end of the instance) */
		void FlushPendingQuad()
		{
			if (!HasPending) {
				return;
			}
			HasPending = false;
			DrawState state = *Material;
			ApplyPassToState(state, PendingPass, PendingRgb);
			SubmitQuadPrimitive(state, Px, Py, Pu, Pv, PendingPass.ScreenOffset[0], PendingPass.ScreenOffset[1]);
		}

		// Textured strip out of the builder scratch: the pass's flat colour over the material state
		void SubmitStrip(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			FlushPendingQuad();
			DrawState state = *Material;
			ApplyPassToState(state, pass, pass.Color);
			SubmitStripPrimitive(state, StripX, StripY, StripU, StripV, count, nullptr,
				pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		// Shaded (per-vertex-colour) strip out of the builder scratch: UNTEXTURED - a gradient has no
		// texture to modulate - unless the pass's TEV preset consumes the texel as well (TintMix), in
		// which case the strip keeps its texture and its UVs, with the tint per vertex in SHADE
		void SubmitStripShaded(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			FlushPendingQuad();
			DrawState state = *Material;
			ApplyPassToState(state, pass, pass.Color);
			if (pass.Tev == FixedFunctionPass::TevPreset::TintMix && state.Texture != nullptr) {
				state.Combiner = CombShadeTintMix;
				SubmitStripPrimitive(state, StripX, StripY, StripU, StripV, count, &StripColor[0][0],
					pass.ScreenOffset[0], pass.ScreenOffset[1]);
			} else {
				state.Texture = nullptr;
				state.Tlut = nullptr;
				state.Combiner = CombShade;
				SubmitStripPrimitive(state, StripX, StripY, nullptr, nullptr, count, &StripColor[0][0],
					pass.ScreenOffset[0], pass.ScreenOffset[1]);
			}
		}
	};
}

// The per-effect functions themselves are GENERATED: the ShaderCompiler transpiles each shader's
// fixed_function block into C++ over the EffectContext defined above (the type name itself is the
// "using EffectContext = ...;" alias the header expects - the anonymous namespace above is the same
// namespace the header's payload reopens within this translation unit). Included at global scope
// because the header opens nCine::RHI::RDP itself. Programs with no fixed_function block are absent
// from its table and their draws are skipped with a one-time warning, exactly as on the other
// consoles.
#include "../../../../Shaders/Generated/RdpGeneratedEffects.h"

namespace nCine::RHI::RDP
{
	const FixedFunctionGeneratedEffect* RdpDevice::FindGeneratedEffect(const char* program, const char* variant)
	{
		// A linear scan is fine - the lookup runs once per program load, not per draw
		for (std::size_t i = 0; i < FixedFunctionGeneratedEffectCount; i++) {
			const FixedFunctionGeneratedEffect& e = FixedFunctionGeneratedEffects[i];
			if (std::strcmp(e.Program, program) == 0 && std::strcmp(e.Variant, variant) == 0) {
				return &e;
			}
		}
		return nullptr;
	}

	RdpDevice::BlendingState RdpDevice::_blending;
	RdpDevice::DepthTestState RdpDevice::_depthTest;
	RdpDevice::CullFaceState RdpDevice::_cullFace;
	RdpDevice::ScissorState RdpDevice::_scissor;
	Recti RdpDevice::_viewport(0, 0, 0, 0);
	Colorf RdpDevice::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	RdpShaderProgram* RdpDevice::_currentProgram = nullptr;
	const RdpTexture* RdpDevice::_boundTextures[RdpDevice::MaxTextureUnits] = {};
	RdpDevice::UniformRange RdpDevice::_boundUniformRanges[RdpDevice::MaxUniformBindings] = {};
	RdpRenderTarget* RdpDevice::_currentRenderTarget = nullptr;

	bool RdpDevice::_rdpInitialized = false;
	std::int32_t RdpDevice::_logicalWidth = ScreenWidth;
	std::int32_t RdpDevice::_logicalHeight = ScreenHeight;
	std::uint32_t RdpDevice::_sceneCounter = 0;

	RdpTexture* RdpDevice::_paletteTexture = nullptr;
	std::uint32_t RdpDevice::_paletteGeneration = 1;

	std::vector<RdpDevice::PendingSoftwareLight> RdpDevice::_pendingSoftwareLights;

	namespace
	{
		// One rspq syncpoint per presented frame, so IsFrameRetired() answers "has the RDP executed
		// everything frame N enqueued" exactly instead of assuming an in-flight depth from the display
		// buffer count. The ring is small: the display pacing never keeps more than a frame or two in
		// flight, and an overflow (which would need the RDP to fall that many frames behind) retires the
		// oldest entry by waiting on it.
		constexpr std::int32_t MaxPendingFrameSyncpoints = 4;
		struct FrameSyncpoint
		{
			std::uint32_t Frame;
			rspq_syncpoint_t Syncpoint;
		};
		FrameSyncpoint pendingFrameSyncpoints[MaxPendingFrameSyncpoints];
		std::int32_t pendingFrameSyncpointCount = 0;
		// Highest frame number known retired; every frame at or below it is safe to rewrite against
		std::int64_t lastRetiredFrame = -1;

		void RetireFrameSyncpoints(std::int32_t upTo)
		{
			std::int32_t retired = 0;
			while (retired < upTo) {
				lastRetiredFrame = pendingFrameSyncpoints[retired].Frame;
				retired++;
			}
			if (retired > 0) {
				for (std::int32_t i = retired; i < pendingFrameSyncpointCount; i++) {
					pendingFrameSyncpoints[i - retired] = pendingFrameSyncpoints[i];
				}
				pendingFrameSyncpointCount -= retired;
			}
		}

		// The display buffer the frame renders into, acquired at the frame's first draw and handed back
		// by rdpq_detach_show() at present
		surface_t* screenSurface = nullptr;
		// Which surface the RDP is attached to, so a target switch only detaches/attaches when it really
		// changes (nullptr target = the screen)
		const RdpRenderTarget* attachedTarget = nullptr;
		bool rdpAttached = false;
		// The scissor rect last programmed, in raster pixels of the current target
		std::int32_t appliedScissor[4] = { -1, -1, -1, -1 };
	}

	void RdpDevice::TraceStoreRebuild(std::uint32_t bytes, bool isBake)
	{
		if (TraceDrawStatistics) {
			if (isBake) { stats.BakeRebuilds++; } else { stats.StoreRefreshes++; }
			stats.StoreWritebackBytes += bytes;
		}
	}

	bool RdpDevice::IsFrameRetired(std::uint32_t frame)
	{
		if (std::int64_t(frame) <= lastRetiredFrame) {
			return true;
		}
		// Advance retirement lazily off the pending syncpoints (a cheap check, no stall)
		std::int32_t passed = 0;
		while (passed < pendingFrameSyncpointCount && rspq_syncpoint_check(pendingFrameSyncpoints[passed].Syncpoint)) {
			passed++;
		}
		RetireFrameSyncpoints(passed);
		return (std::int64_t(frame) <= lastRetiredFrame);
	}

	void RdpDevice::WaitForFrame(std::uint32_t frame)
	{
		if (IsFrameRetired(frame)) {
			return;
		}
		for (std::int32_t i = 0; i < pendingFrameSyncpointCount; i++) {
			if (pendingFrameSyncpoints[i].Frame >= frame) {
				// Waiting on this frame's own syncpoint retires it and everything before it
				rspq_syncpoint_wait(pendingFrameSyncpoints[i].Syncpoint);
				RetireFrameSyncpoints(i + 1);
				return;
			}
		}
		// The frame being built has no syncpoint yet - drain everything enqueued so far. Its stamp stays
		// "not retired" (more of its commands may follow), but the conflicting reads are gone.
		rspq_wait();
		RetireFrameSyncpoints(pendingFrameSyncpointCount);
	}

	// ------------------------------------------------------------------ session

	void RdpDevice::InitializeRdp()
	{
		if (_rdpInitialized) {
			return;
		}

		// The window backend owns the video mode (display_init has already run); this brings up the rdpq
		// command layer on top of it. No Z buffer is ever attached - the game draws 2D in painter's order,
		// so there is nothing for a depth test to arbitrate, and its 150 KB of RDRAM stay available.
		rdpq_init();

		_rdpInitialized = true;
		LOGI("RDP session initialized: {}x{} RGBA16, {} TLUT slots, {} B TMEM windows",
			ScreenWidth, ScreenHeight, MaxTlutSlots, 4096);
	}

	void RdpDevice::ApplyDrawTarget()
	{
		if (rdpAttached && attachedTarget == _currentRenderTarget) {
			return;
		}
		if (rdpAttached) {
			// rdpq_attach nests as a stack; strict detach/attach keeps it one level deep
			rdpq_detach();
			rdpAttached = false;
		}

		if (_currentRenderTarget != nullptr) {
			RdpTexture* texture = _currentRenderTarget->GetColorTexture(0);
			const surface_t* surface = (texture != nullptr ? texture->GetRenderTargetSurface() : nullptr);
			if (surface == nullptr) {
				return;		// No surface to render into; the draws will be skipped by their own checks
			}
			rdpq_attach(surface, nullptr);
		} else {
			if (screenSurface == nullptr) {
				// Blocks until the VI releases a buffer, which is also what paces the frame
				const std::uint32_t waitStart = (TraceDrawStatistics ? std::uint32_t(get_ticks()) : 0);
				screenSurface = display_get();
				if (TraceDrawStatistics) { traceDisplayTicks += std::uint32_t(get_ticks()) - waitStart; }
			}
			rdpq_attach(screenSurface, nullptr);
		}
		attachedTarget = _currentRenderTarget;
		rdpAttached = true;
		// Attaching re-runs the auto-scissor and nothing else about the pipeline state is worth trusting
		InvalidateAppliedState();
		InvalidateTmemWindow();
		appliedScissor[0] = -1;
	}

	void RdpDevice::ApplyScissor()
	{
		std::int32_t x = 0, y = 0, w = ScreenWidth, h = ScreenHeight;
		std::int32_t targetW = ScreenWidth, targetH = ScreenHeight;
		if (_currentRenderTarget != nullptr) {
			const RdpTexture* texture = _currentRenderTarget->GetColorTexture(0);
			targetW = (texture != nullptr ? texture->GetWidth() : ScreenWidth);
			targetH = (texture != nullptr ? texture->GetHeight() : ScreenHeight);
			w = targetW;
			h = targetH;
		}
		if (_scissor.Enabled) {
			float scaleX, scaleY;
			GetTargetScale(scaleX, scaleY);
			// The engine hands scissor rectangles in top-down logical coordinates. A screen pass maps them
			// straight onto raster rows (it mirrors NDC, see Dispatch); a render-to-texture pass keeps the
			// unmirrored top-down store, so its rect is flipped - the same split the GX/GU devices make.
			const std::int32_t rasterY = (_currentRenderTarget == nullptr
				? _scissor.Rect.Y : targetH - _scissor.Rect.Y - _scissor.Rect.H);
			x = std::int32_t(float(_scissor.Rect.X) * scaleX);
			y = std::int32_t(float(rasterY) * scaleY);
			w = std::int32_t(float(_scissor.Rect.W) * scaleX);
			h = std::int32_t(float(_scissor.Rect.H) * scaleY);
			if (x < 0) { w += x; x = 0; }
			if (y < 0) { h += y; y = 0; }
			if (x + w > targetW) { w = targetW - x; }
			if (y + h > targetH) { h = targetH - y; }
			if (w < 0) { w = 0; }
			if (h < 0) { h = 0; }
		}
		if (appliedScissor[0] == x && appliedScissor[1] == y && appliedScissor[2] == w && appliedScissor[3] == h) {
			return;
		}
		rdpq_set_scissor(x, y, x + w, y + h);
		appliedScissor[0] = x;
		appliedScissor[1] = y;
		appliedScissor[2] = w;
		appliedScissor[3] = h;
	}

	void RdpDevice::GetTargetScale(float& scaleX, float& scaleY)
	{
		if (_currentRenderTarget != nullptr) {
			// Render-to-texture passes render 1:1 into the target surface
			scaleX = 1.0f;
			scaleY = 1.0f;
		} else {
			scaleX = (_logicalWidth > 0 ? float(ScreenWidth) / float(_logicalWidth) : 1.0f);
			scaleY = (_logicalHeight > 0 ? float(ScreenHeight) / float(_logicalHeight) : 1.0f);
		}
	}

	void RdpDevice::PresentFrame()
	{
		if (!_rdpInitialized) {
			return;
		}
		if (rdpAttached && attachedTarget != nullptr) {
			// A frame must not end attached to a render target; put the screen back
			rdpq_detach();
			rdpAttached = false;
		}
		if (!rdpAttached) {
			if (screenSurface == nullptr) {
				// Nothing was drawn this frame; still flip a cleared buffer so the display keeps its pacing
				// and a frame that produced no geometry does not show the previous one's contents
				screenSurface = display_get();
				rdpq_attach(screenSurface, nullptr);
				rdpq_clear(color_from_packed32(PackRgba(QuantizeChannel(_clearColor.R),
					QuantizeChannel(_clearColor.G), QuantizeChannel(_clearColor.B), 255)));
			} else {
				rdpq_attach(screenSurface, nullptr);
			}
			rdpAttached = true;
		}

		// Schedules the display flip behind the RDP's completion and returns immediately, so the CPU
		// builds the next frame while the RDP finishes this one (display_get() at the next frame's first
		// draw is what paces the pipeline). Every RDRAM store the queued commands DMA out of (texel
		// stores, TLUT slots, lightmap surfaces) carries a used-in-frame stamp, and its writer checks the
		// frame's syncpoint below - waiting only when the stamped frame has genuinely not retired yet.
		rdpq_detach_show();
		if (pendingFrameSyncpointCount == MaxPendingFrameSyncpoints) {
			// The RDP has fallen several frames behind (never under the display pacing); retire the oldest
			rspq_syncpoint_wait(pendingFrameSyncpoints[0].Syncpoint);
			RetireFrameSyncpoints(1);
		}
		pendingFrameSyncpoints[pendingFrameSyncpointCount++] = { _sceneCounter, rspq_syncpoint_new() };

		if (TraceDrawStatistics) {
			const std::uint32_t now = std::uint32_t(get_ticks());
			if (traceLastTicks != 0) {
				traceFrameTicks += std::uint32_t(now - traceLastTicks);
				traceFrameCount++;
			}
			traceLastTicks = now;
			if ((_sceneCounter % 30) == 0) {
				const std::uint32_t avgUs = (traceFrameCount > 0
					? std::uint32_t(TICKS_TO_US(traceFrameTicks / traceFrameCount)) : 0);
				LOGI("Frame {} ({} us/frame; {} us dispatch, {} us tmem, {} us displaywait): {} draws/{} inst, {} rects ({} blits) + {} tris in {} strips, TMEM {} up/{} hit/{} KB, TLUT {} up/{} conv/{} wait, {} comb/{} prim, {} refresh/{} bake/{} KB wb",
					_sceneCounter, avgUs,
					std::uint32_t(TICKS_TO_US(traceDispatchTicks) / (traceFrameCount > 0 ? traceFrameCount : 1)),
					std::uint32_t(TICKS_TO_US(traceUploadTicks) / (traceFrameCount > 0 ? traceFrameCount : 1)),
					std::uint32_t(TICKS_TO_US(traceDisplayTicks) / (traceFrameCount > 0 ? traceFrameCount : 1)),
					stats.Dispatches, stats.Instances, stats.Rects, stats.Blits, stats.Triangles, stats.Strips,
					stats.WindowUploads, stats.WindowHits, stats.WindowBytes / 1024, stats.TlutUploads,
					stats.TlutConversions, stats.TlutEvictWaits, stats.ModeChanges, stats.PrimChanges,
					stats.StoreRefreshes, stats.BakeRebuilds, stats.StoreWritebackBytes / 1024);
				traceFrameTicks = 0;
				traceFrameCount = 0;
				traceDispatchTicks = 0;
				traceUploadTicks = 0;
				traceDisplayTicks = 0;
			}
			stats = FrameStats();
		}

		rdpAttached = false;
		attachedTarget = nullptr;
		screenSurface = nullptr;
		InvalidateAppliedState();
		InvalidateTmemWindow();
		appliedScissor[0] = -1;
		_sceneCounter++;
	}

	void RdpDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width > 0 && height > 0) {
			_logicalWidth = width;
			_logicalHeight = height;
		}
	}

	// ------------------------------------------------------------------ state

	void RdpDevice::SetBlendingEnabled(bool enabled) { _blending.Enabled = enabled; }
	void RdpDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}
	RdpDevice::BlendingState RdpDevice::GetBlendingState() { return _blending; }
	void RdpDevice::SetBlendingState(const BlendingState& state) { _blending = state; }

	void RdpDevice::SetDepthTestEnabled(bool enabled) { _depthTest.TestEnabled = enabled; }
	void RdpDevice::SetDepthMaskEnabled(bool enabled) { _depthTest.MaskEnabled = enabled; }
	RdpDevice::DepthTestState RdpDevice::GetDepthTestState() { return _depthTest; }
	void RdpDevice::SetDepthTestState(const DepthTestState& state) { _depthTest = state; }

	void RdpDevice::SetCullFaceEnabled(bool enabled) { _cullFace.Enabled = enabled; }
	RdpDevice::CullFaceState RdpDevice::GetCullFaceState() { return _cullFace; }
	void RdpDevice::SetCullFaceState(const CullFaceState& state) { _cullFace = state; }

	RdpDevice::ScissorState RdpDevice::GetScissorState() { return _scissor; }
	void RdpDevice::SetScissorState(const ScissorState& state) { _scissor = state; }
	void RdpDevice::SetScissor(const Recti& rect)
	{
		// Same contract as the GL device: setting a rect also enables the test (callers like RenderCommand
		// and Viewport rely on it and restore via SetScissorState afterwards)
		_scissor.Enabled = true;
		_scissor.Rect = rect;
	}
	void RdpDevice::SetScissorTestEnabled(bool enabled) { _scissor.Enabled = enabled; }

	Recti RdpDevice::GetViewport() { return _viewport; }
	void RdpDevice::SetViewport(const Recti& rect) { _viewport = rect; }
	void RdpDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
	}

	Colorf RdpDevice::GetClearColor() { return _clearColor; }
	void RdpDevice::SetClearColor(const Colorf& color) { _clearColor = color; }

	void RdpDevice::Clear(ClearFlags flags)
	{
		if (!_rdpInitialized) {
			return;
		}
		ApplyDrawTarget();
		if (!rdpAttached) {
			return;
		}
		// The clear is bounded by the scissor, exactly like glClear is by the scissor test (the RDP's
		// fill rectangle is scissored like every other primitive)
		ApplyScissor();
		if ((flags & ClearFlags::Color) == ClearFlags::Color) {
			rdpq_clear(color_from_packed32(PackRgba(QuantizeChannel(_clearColor.R),
				QuantizeChannel(_clearColor.G), QuantizeChannel(_clearColor.B), QuantizeChannel(_clearColor.A))));
			// rdpq_clear pushes/pops the mode stack around its fill, but the defensive invalidation is
			// cheap and keeps this path independent of that implementation detail
			InvalidateAppliedState();
		}
		// No depth or stencil buffer is ever attached on this backend
	}

	// ------------------------------------------------------------------ draw entry points

	void RdpDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		Dispatch(primitive, firstVertex, numVertices);
	}
	void RdpDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, firstVertex, numVertices);
	}
	void RdpDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}
	void RdpDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	FenceHandle RdpDevice::InsertFence()
	{
		return reinterpret_cast<FenceHandle>(std::uintptr_t(1));
	}
	void RdpDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}
	bool RdpDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void RdpDevice::SetupInitialState()
	{
		_blending = BlendingState();
		_depthTest = DepthTestState();
		_cullFace = CullFaceState();
		_scissor = ScissorState();
	}

	// ------------------------------------------------------------------ extensions

	void RdpDevice::BindProgram(RdpShaderProgram* program) { _currentProgram = program; }
	RdpShaderProgram* RdpDevice::CurrentProgram() { return _currentProgram; }

	void RdpDevice::BindTexture(std::uint32_t unit, const RdpTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void RdpDevice::UnbindTexture(const RdpTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
		if (_paletteTexture == texture) {
			_paletteTexture = nullptr;
		}
		// A destroyed texture's store must not stay referenced by a resident TMEM window or a TLUT slot.
		// Anything already submitted is safe: resources are only ever destroyed between frames (level
		// loads, menu transitions), and PresentFrame() has drained the queue by then.
		InvalidateTmemWindow();
		for (std::int32_t i = 0; i < MaxTlutSlots; i++) {
			if (tlutSlots[i].Palette == texture) {
				tlutSlots[i].Valid = false;
				tlutSlots[i].Palette = nullptr;
			}
		}
	}

	const RdpTexture* RdpDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void RdpDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void RdpDevice::SetRenderTarget(RdpRenderTarget* renderTarget)
	{
		// The draw path reacts lazily at the next draw or clear (ApplyDrawTarget)
		_currentRenderTarget = renderTarget;
	}

	void RdpDevice::UnbindRenderTarget(const RdpRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
		}
		if (attachedTarget == renderTarget && rdpAttached) {
			rdpq_detach();
			rdpAttached = false;
			attachedTarget = nullptr;
		}
	}

	// ------------------------------------------------------------------ palette TLUTs

	void RdpDevice::RegisterPaletteTexture(RdpTexture* texture)
	{
		_paletteTexture = texture;
		NotifyPaletteTextureChanged(texture, 0, texture != nullptr ? texture->GetHeight() : 0);
	}

	void RdpDevice::NotifyPaletteTextureChanged(RdpTexture* texture, std::int32_t firstRow, std::int32_t rowCount)
	{
		if (texture != _paletteTexture) {
			return;
		}
		_paletteGeneration++;
		for (std::int32_t i = 0; i < MaxTlutSlots; i++) {
			if (tlutSlots[i].Palette == texture && tlutSlots[i].Offset >= (firstRow - 1) * 256 &&
				tlutSlots[i].Offset < (firstRow + rowCount) * 256) {
				tlutSlots[i].Valid = false;
				tlutSlots[i].Palette = nullptr;
			}
		}
	}

	// ------------------------------------------------------------------ lighting hook

	void RdpDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
		std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
		bool waterActive, float waterLevelPx, float waterTime, float waterCamY)
	{
		// The lightmap scale and the water parameters are part of the cross-backend interface but have no
		// consumer here - the lightmap pass stretches the whole map over the viewport whatever its scale,
		// and the water is a fixed_function block of the CombineWithWater programs now, which reads the
		// waterline and the ambient colour off the draw's own uniforms (see CombineWithWater.shader). Only
		// the software backend, whose richer per-row water is its own, still reads them.
		static_cast<void>(scale);
		static_cast<void>(waterActive);
		static_cast<void>(waterLevelPx);
		static_cast<void>(waterTime);
		static_cast<void>(waterCamY);

		PendingSoftwareLight light;
		light.Lightmap = lightmap;
		light.LmW = lmW;
		light.LmH = lmH;
		light.VpX = vpX;
		light.VpY = vpY;
		light.VpW = vpW;
		light.VpH = vpH;
		light.AmbR = ambR;
		light.AmbG = ambG;
		light.AmbB = ambB;
		_pendingSoftwareLights.push_back(light);
	}

	void RdpDevice::EndFrame()
	{
		if (!_pendingSoftwareLights.empty()) {
			static bool warnedLeftoverLights = false;
			if (!warnedLeftoverLights) {
				warnedLeftoverLights = true;
				LOGW("Dropping {} unconsumed software-lighting entries", _pendingSoftwareLights.size());
			}
			_pendingSoftwareLights.clear();
		}
	}

	namespace
	{
		// The lightmap combine texture, IA16 with the multiply factor encoded in ALPHA (see below). Four
		// buffers cycled per combine: splitscreen runs the hook twice per frame and presentation keeps a
		// frame of commands in flight, so a buffer has to survive two frames' worth of combines before it
		// is rewritten. The stamp guard below catches anything beyond that (three-way splitscreen).
		struct LightmapSurface
		{
			std::uint8_t* Data = nullptr;
			std::size_t Size = 0;
			std::uint32_t LastUsedFrame = 0;
			bool Used = false;
		};
		LightmapSurface lightmapSurfaces[4];
		std::int32_t nextLightmapSurface = 0;
		// Every rebuilt lightmap is new content; a fresh stamp defeats the TMEM window dedup
		std::uint32_t lightmapVersion = 0;
	}

	void RdpDevice::ApplyPendingSoftwareLighting()
	{
		if (_pendingSoftwareLights.empty()) {
			return;
		}
		const PendingSoftwareLight light = _pendingSoftwareLights.front();
		_pendingSoftwareLights.erase(_pendingSoftwareLights.begin());

		// Only the lightmap composite: the water overlay is a fixed_function block of the
		// CombineWithWater shaders, run by Dispatch after this hook (see CombineWithWater.shader)
		const bool hasLighting = (light.Lightmap != nullptr && light.LmW > 0 && light.LmH > 0);
		if (!hasLighting) {
			return;
		}

		ApplyDrawTarget();
		if (!rdpAttached) {
			return;
		}
		ApplyScissor();

		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const float vpX = float(light.VpX) * scaleX, vpY = float(light.VpY) * scaleY;
		const float vpW = float(light.VpW) * scaleX, vpH = float(light.VpH) * scaleY;

		if (hasLighting) {
			// The other consoles draw the CPU lightmap with a dst * src multiply blend; the RDP's blender
			// cannot multiply memory by a texel at all (its B mux only takes 1-A / coverage / one / zero),
			// so the multiply is ENCODED IN ALPHA instead: an IA16 texture whose alpha is 1-f, drawn with
			// combiner RGB 0 under alpha-over blending, computes mem*(1-a) = mem*f per pixel. The factor
			// f = r*(1+g) + amb*(1-r) is the multiply-only approximation shared with the GX/PVR/GU
			// backends, collapsed to the AMBIENT LUMINANCE - a per-channel ambient tint cannot ride a
			// single alpha (TODO: an additive second pass could restore the hue if it ever shows).
			const float ambLuma = AmbientLuminance(light.AmbR, light.AmbG, light.AmbB);
			const std::int32_t stride = ((light.LmW * 2) + 7) & ~7;
			const std::size_t size = std::size_t(stride) * std::size_t(light.LmH);
			LightmapSurface& lm = lightmapSurfaces[nextLightmapSurface];
			nextLightmapSurface = (nextLightmapSurface + 1) % 4;
			if (lm.Used && !IsFrameRetired(lm.LastUsedFrame)) {
				// The buffer's previous contents may still be DMAed by the in-flight frame; wait for it.
				// Unreachable with up to two combines per frame (the buffer count covers those).
				WaitForFrame(lm.LastUsedFrame);
			}
			if (lm.Data == nullptr || lm.Size < size) {
				if (lm.Data != nullptr) {
					free(lm.Data);
				}
				lm.Data = static_cast<std::uint8_t*>(memalign(64, size));
				lm.Size = (lm.Data != nullptr ? size : 0);
			}
			lm.LastUsedFrame = _sceneCounter;
			lm.Used = true;
			if (lm.Data != nullptr) {
				// Rows are flipped at build time - the lightmap's row 0 corresponds to the bottom of the
				// displayed viewport (the software buffer convention) - so the rectangle samples T ascending
				for (std::int32_t y = 0; y < light.LmH; y++) {
					const float* DEATH_RESTRICT src = light.Lightmap + std::size_t(light.LmH - 1 - y) * light.LmW * 2;
					std::uint16_t* DEATH_RESTRICT dst = reinterpret_cast<std::uint16_t*>(lm.Data + std::size_t(y) * stride);
					// Unlit runs repeat the same pair of factors across long spans, so remembering the last
					// converted texel turns most of the surface into a compare and a store
					float prevR = -1.0f, prevG = -1.0f;
					std::uint16_t prevTexel = 0;
					for (std::int32_t x = 0; x < light.LmW; x++) {
						const float rawR = src[x * 2];
						const float rawG = src[x * 2 + 1];
						if (rawR == prevR && rawG == prevG) {
							dst[x] = prevTexel;
							continue;
						}
						prevR = rawR;
						prevG = rawG;
						const float r = ClampLightmapChannel(rawR);
						const float g = ClampLightmapChannel(rawG);
						const float f = Saturate(LightingCombineFactor(r, g, ambLuma));
						// IA16: intensity byte unused (RGB comes out of the combiner as 0), alpha = 1-f
						prevTexel = std::uint16_t(255 - std::uint8_t(f * 255.0f + 0.5f));
						dst[x] = prevTexel;
					}
				}
				data_cache_hit_writeback(lm.Data, size);

				const surface_t lmSurface = surface_make(lm.Data, FMT_IA16,
					std::uint16_t(light.LmW), std::uint16_t(light.LmH), std::uint16_t(stride));
				DrawState state;
				state.Texture = &lmSurface;
				state.TextureVersion = ++lightmapVersion;
				state.Combiner = CombLightmap;
				state.BlendEnabled = true;
				state.Blender = BlendAlphaOver;
				state.Filter = FILTER_BILINEAR;
				SubmitTexturedRect(state, vpX, vpY, vpX + vpW, vpY + vpH,
					0.0f, 0.0f, float(light.LmW), float(light.LmH));
				InvalidateTmemWindow();		// The window key points at a stack surface
			}
		}
	}

	// ------------------------------------------------------------------ draw dispatch

	namespace
	{
		/**
			@brief Resolves the texture half of one draw's state: the surface, its TLUT or bake, the filter

			@p paletteOffset is the flat entry index an effect that remaps selected (0 otherwise). Returns
			`false` when there is nothing to sample. Texture coordinates on this backend are texel indices
			of the surface, so the returned scale is simply the source size.
		*/
		bool ResolveTextureState(DrawState& state, RdpTexture* texture, const RdpTexture* paletteTex,
			std::int32_t paletteOffset, std::uint32_t paletteVersion, float& uvScaleU, float& uvScaleV)
		{
			if (texture->NeedsPaletteBake()) {
				// Index + per-pixel alpha has no paletted form: a CPU bake through the palette row is
				// what the other consoles do too. The offset is validated the same way AcquireTlutForRow
				// validates its own - the bake reads 256 entries starting there, and the offset comes in
				// from instance data, so an out-of-range one must not walk past the palette's pixels.
				if (paletteTex == nullptr || paletteTex->GetPixels() == nullptr ||
					paletteOffset < 0 || paletteOffset > paletteTex->GetWidth() * paletteTex->GetHeight() - 256) {
					return false;
				}
				const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(paletteTex->GetPixels()) + paletteOffset;
				if (!texture->EnsureBakedStore(entries, std::uint32_t(paletteOffset), paletteVersion, paletteTex)) {
					return false;
				}
			} else if (texture->IsIndexed()) {
				// An 8bpp store can only be read through a TLUT, whatever it is being drawn with - the
				// lookup belongs to the texture read rather than to the effect. An effect that remaps
				// takes the row from the instance; anything else (the fonts) uses the base row.
				const TlutSlot* slot = AcquireTlutForRow(paletteTex, paletteOffset, paletteVersion);
				if (slot == nullptr) {
					return false;
				}
				state.Tlut = slot->Entries;
				state.TlutStamp = slot->WriteStamp;
			}
			const surface_t* surface = texture->AcquireSurface();
			if (surface == nullptr) {
				return false;
			}
			state.Texture = surface;
			state.TextureVersion = texture->GetSurfaceStamp();
			state.Filter = (texture->GetMagFilter() == nCine::SamplerFilter::Linear ? FILTER_BILINEAR : FILTER_POINT);
			uvScaleU = float(texture->GetWidth());
			uvScaleV = float(texture->GetHeight());
			return true;
		}

		// The projection*view product of the last dispatch, with the inputs it was computed from (see
		// RdpDevice::ResolveProjView())
		float cachedPv[16];
		float cachedPvProj[16];
		float cachedPvView[16];
		bool cachedPvValid = false;
	}

	const std::uint8_t* RdpDevice::ResolveInstanceBlockData(std::int32_t& binding)
	{
		// The block itself was resolved once at introspection (see DispatchFacts); only its binding is
		// read per draw, because the material assigns bindings after linking
		const RdpUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
		if (block == nullptr) {
			return nullptr;
		}
		binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		return _boundUniformRanges[binding].Data;
	}

	const float* RdpDevice::ResolveProjView()
	{
		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		// The product changes a handful of times a frame (a camera move, a viewport switch) while a frame
		// runs a few hundred dispatches, so it is rebuilt only when either input changed. Compared by
		// VALUE, not by pointer - the matrices are rewritten in place when the camera moves.
		if (!cachedPvValid || std::memcmp(projMat, cachedPvProj, sizeof(cachedPvProj)) != 0 ||
				std::memcmp(viewMat, cachedPvView, sizeof(cachedPvView)) != 0) {
			std::memcpy(cachedPvProj, projMat, sizeof(cachedPvProj));
			std::memcpy(cachedPvView, viewMat, sizeof(cachedPvView));
			Mat4Mul(projMat, viewMat, cachedPv);
			cachedPvValid = true;
		}
		return cachedPv;
	}

	void RdpDevice::ComputeRasterFold(Recti& viewport, float& rasterScaleX, float& rasterBiasX,
		float& rasterScaleY, float& rasterBiasY, float* maxScale)
	{
		viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);

		// The engine's NDC orientation matches the software backend, whose top-down raster is flipped at
		// present time; the RDP scans out its buffer top-down directly, so screen passes mirror NDC here
		// instead (+1 = bottom row). Render-to-texture passes keep the unmirrored top-down store, which
		// is what the sampling passes already expect - which is just the sign of the raster Y scale.
		const bool screenPass = (_currentRenderTarget == nullptr);

		rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY;
		if (maxScale != nullptr) {
			*maxScale = std::max(scaleX, scaleY);
		}
	}

	void RdpDevice::DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		// A tile-layer mesh is a plain triangle list of 8-float vertices (position.xy, texcoords.uv,
		// color.rgba) - the layout TileMap::AppendTileQuad() writes and TileMapVs.inc declares. It is a
		// hard contract of this shader family exactly like the std140 instance block is of the sprite one.
		constexpr std::int32_t FloatsPerVertex = 8;
		if (primitive != PrimitiveType::Triangles || numVertices < 3) {
			return;
		}

		const RdpBuffer* vbo = _currentProgram->GetBoundVbo();
		if (vbo == nullptr) {
			return;
		}
		const std::size_t firstFloat = (std::size_t(_currentProgram->GetBoundVboOffset()) / sizeof(float)) +
			std::size_t(firstVertex) * FloatsPerVertex;
		const std::size_t floatCount = std::size_t(numVertices) * FloatsPerVertex;
		if ((firstFloat + floatCount) * sizeof(float) > vbo->GetSize()) {
			return;
		}
		const float* DEATH_RESTRICT vertices = reinterpret_cast<const float*>(vbo->HostData()) + firstFloat;

		std::int32_t binding = 0;
		const std::uint8_t* blockData = ResolveInstanceBlockData(binding);
		if (blockData == nullptr) {
			return;
		}

		RdpTexture* texture = const_cast<RdpTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const float* pv = ResolveProjView();
		Transform2D mvp;
		Mat4MulTransform2D(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// The layer tint modulates every vertex colour, which already carries the per-tile alpha
		float layerColor[4];
		std::memcpy(layerColor, blockData + kColorOffset, sizeof(layerColor));

		// The palette to remap with is whatever the material bound to the palette sampler; the registered
		// global palette is the fallback (mirrors the sprite path). TileMapMeshPalette binds
		// uTexturePalette in its reflection, which is exactly what UsesPalette() reports.
		const bool isPaletteRemap = _currentProgram->UsesPalette();
		const RdpTexture* paletteTex = nullptr;
		if (isPaletteRemap || texture->IsIndexed() || texture->NeedsPaletteBake()) {
			paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}

		// Unlike a sprite batch, the whole mesh shares one texture and one palette offset, so the
		// residency, the TLUT and the RDP state are resolved once for the entire layer
		std::int32_t paletteOffset = 0;
		if (isPaletteRemap) {
			float palOffset = 0.0f;
			std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
			paletteOffset = std::int32_t(palOffset + 0.5f);
		}
		const std::uint32_t paletteVersion = (paletteTex == _paletteTexture
			? _paletteGeneration : (paletteTex != nullptr ? paletteTex->GetContentVersion() : 0));

		DrawState state;
		float uvScaleU = 1.0f, uvScaleV = 1.0f;
		if (!ResolveTextureState(state, texture, paletteTex, paletteOffset, paletteVersion, uvScaleU, uvScaleV)) {
			return;
		}
		MapBlendRdp(_blending.Enabled, _blending.SrcRgb, _blending.DstRgb, state);
		state.Combiner = CombModulate;

		ApplyDrawTarget();
		if (!rdpAttached) {
			return;
		}
		ApplyScissor();

		// The NDC-to-raster mapping is affine and constant for the whole mesh, so it is folded into the
		// transform once instead of being reapplied per vertex (see ComputeRasterFold)
		Recti viewport;
		float rasterScaleX, rasterBiasX, rasterScaleY, rasterBiasY;
		ComputeRasterFold(viewport, rasterScaleX, rasterBiasX, rasterScaleY, rasterBiasY);
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		auto project = [&](const float* v, float& outX, float& outY, float& outU, float& outV) {
			outX = raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx;
			outY = raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty;
			outU = v[2] * uvScaleU;
			outV = v[3] * uvScaleV;
		};

		const std::int32_t triangleCount = numVertices / 3;
		std::int32_t triangle = 0;
		// Virtually every tile of a layer carries the same colour (white at the layer's alpha), so the
		// four clamp+float-to-int quantizations run once per change instead of once per tile. The colour
		// rides the PRIM register - a tile rectangle carries no shade - and changing it is one command.
		float lastColor[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
		while (triangle < triangleCount) {
			// Tiles reach here as the six vertices of two triangles, of which the third and fourth repeat
			// the first and third. Recognizing that pattern lets a tile go out as one TEXTURE_RECTANGLE;
			// runs of the same tile then reuse the resident TMEM window (see UploadWindow), so a
			// homogeneous layer costs almost only its rectangles. Anything that doesn't match is emitted
			// as plain triangles.
			const float* group = vertices + std::size_t(triangle) * 3 * FloatsPerVertex;
			const bool isQuad = (triangle + 2 <= triangleCount &&
				group[3 * FloatsPerVertex + 0] == group[0] && group[3 * FloatsPerVertex + 1] == group[1] &&
				group[4 * FloatsPerVertex + 0] == group[2 * FloatsPerVertex + 0] &&
				group[4 * FloatsPerVertex + 1] == group[2 * FloatsPerVertex + 1]);

			if (group[4] != lastColor[0] || group[5] != lastColor[1] || group[6] != lastColor[2] || group[7] != lastColor[3]) {
				lastColor[0] = group[4]; lastColor[1] = group[5]; lastColor[2] = group[6]; lastColor[3] = group[7];
				state.PrimColor = PackRgba(QuantizeChannel(group[4] * layerColor[0]),
					QuantizeChannel(group[5] * layerColor[1]), QuantizeChannel(group[6] * layerColor[2]),
					QuantizeChannel(group[7] * layerColor[3]));
			}

			float px[4], py[4], pu[4], pvv[4];
			if (isQuad) {
				// Corner order of the sprite strip (v0, v1, v2, v3): vertices 1, 2, 0 and 5 of the tile's six
				static const std::int32_t QuadOrder[4] = { 1, 2, 0, 5 };
				for (std::int32_t i = 0; i < 4; i++) {
					project(group + std::size_t(QuadOrder[i]) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				SubmitQuadPrimitive(state, px, py, pu, pvv);
				triangle += 2;
			} else {
				for (std::int32_t i = 0; i < 3; i++) {
					project(group + std::size_t(i) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				const float v0[5] = { px[0], py[0], pu[0], pvv[0], 1.0f };
				const float v1[5] = { px[1], py[1], pu[1], pvv[1], 1.0f };
				const float v2[5] = { px[2], py[2], pu[2], pvv[2], 1.0f };
				SubmitTexturedTriangle(state, v0, v1, v2);
				triangle++;
			}
		}
	}

	void RdpDevice::DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices)
	{
		// The weapon wheel arrives as a textured line strip of 4-float vertices (position.xy,
		// texcoords.uv) - the layout the MeshSprite shader's attributes declare. The RDP has no line
		// primitive, so every segment goes out as a quad half a pixel to each side of the line, which is
		// what the 1-wide GL lines this stands in for rasterize to (the same expansion the PVR makes).
		constexpr std::int32_t FloatsPerVertex = 4;
		if (numVertices < 2) {
			return;
		}

		const RdpBuffer* vbo = _currentProgram->GetBoundVbo();
		if (vbo == nullptr) {
			return;
		}
		const std::size_t firstFloat = (std::size_t(_currentProgram->GetBoundVboOffset()) / sizeof(float)) +
			std::size_t(firstVertex) * FloatsPerVertex;
		const std::size_t floatCount = std::size_t(numVertices) * FloatsPerVertex;
		if ((firstFloat + floatCount) * sizeof(float) > vbo->GetSize()) {
			return;
		}
		const float* DEATH_RESTRICT vertices = reinterpret_cast<const float*>(vbo->HostData()) + firstFloat;

		std::int32_t binding = 0;
		const std::uint8_t* blockData = ResolveInstanceBlockData(binding);
		if (blockData == nullptr) {
			return;
		}

		RdpTexture* texture = const_cast<RdpTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const float* pvMat = ResolveProjView();
		Transform2D mvp;
		Mat4MulTransform2D(pvMat, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// Every vertex of the strip carries the instance colour, so it is packed once
		float color[4];
		std::memcpy(color, blockData + kColorOffset, sizeof(color));

		// The strip shares one texture, so its residency (with the base palette row for indexed assets,
		// which use it like the fonts do) is resolved once
		const RdpTexture* paletteTex = nullptr;
		if (texture->IsIndexed() || texture->NeedsPaletteBake()) {
			paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}
		DrawState state;
		float uvScaleU = 1.0f, uvScaleV = 1.0f;
		if (!ResolveTextureState(state, texture, paletteTex, 0, _paletteGeneration, uvScaleU, uvScaleV)) {
			return;
		}
		MapBlendRdp(_blending.Enabled, _blending.SrcRgb, _blending.DstRgb, state);
		state.Combiner = CombModulate;
		state.PrimColor = PackColor(color);

		ApplyDrawTarget();
		if (!rdpAttached) {
			return;
		}
		ApplyScissor();

		// The NDC-to-raster mapping is folded into the transform once, like the other mesh paths
		Recti viewport;
		float rasterScaleX, rasterBiasX, rasterScaleY, rasterBiasY, pixelScale;
		ComputeRasterFold(viewport, rasterScaleX, rasterBiasX, rasterScaleY, rasterBiasY, &pixelScale);
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		float prevX = raster.Xx * vertices[0] + raster.Yx * vertices[1] + raster.Tx;
		float prevY = raster.Xy * vertices[0] + raster.Yy * vertices[1] + raster.Ty;
		float prevU = vertices[2] * uvScaleU;
		float prevV = vertices[3] * uvScaleV;
		for (std::int32_t i = 1; i < numVertices; i++) {
			const float* v = vertices + std::size_t(i) * FloatsPerVertex;
			const float curX = raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx;
			const float curY = raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty;
			const float curU = v[2] * uvScaleU;
			const float curV = v[3] * uvScaleV;

			const float dx = curX - prevX, dy = curY - prevY;
			const float len2 = dx * dx + dy * dy;
			if (len2 > 0.000001f) {
				const float len = std::sqrt(len2);
				// GL's line rasterization guarantees an unbroken one-pixel chain whatever the slope; a
				// quad exactly one pixel wide covers too few pixel centres on diagonals and the line
				// comes out dashed and dimmer. Widening by the slope's Manhattan factor (1 for axis
				// aligned, sqrt(2) at 45 degrees) restores the same continuous coverage.
				const float halfWidth = 0.5f * pixelScale * (std::abs(dx) + std::abs(dy)) / len;
				const float invLen = halfWidth / len;
				const float nx = -dy * invLen;
				const float ny = dx * invLen;

				const float px[4] = { prevX + nx, prevX - nx, curX + nx, curX - nx };
				const float py[4] = { prevY + ny, prevY - ny, curY + ny, curY - ny };
				const float pu[4] = { prevU, prevU, curU, curU };
				const float pvv[4] = { prevV, prevV, curV, curV };
				SubmitQuadPrimitive(state, px, py, pu, pvv);
			}

			prevX = curX; prevY = curY; prevU = curU; prevV = curV;
		}
	}

	void RdpDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		if (_currentProgram == nullptr || numVertices <= 0 || !_rdpInitialized) {
			return;
		}

		// The program's whole console identity is its effect-table entry, resolved at load from the true
		// (program, variant) the loaders plumbed in - a program without an entry has no fixed_function
		// block in its .shader file (Lighting, Blur, the Resize* family, runtime-compiled shaders, ...)
		// and keeps the logged, skipped draw
		if (TraceDrawStatistics) { stats.Dispatches++; }
		const std::uint32_t dispatchStart = (TraceDrawStatistics ? std::uint32_t(get_ticks()) : 0);
		struct DispatchTimer {
			std::uint32_t Start;
			~DispatchTimer() { if (TraceDrawStatistics) { traceDispatchTicks += std::uint32_t(get_ticks()) - Start; } }
		} dispatchTimer{dispatchStart};
		const FixedFunctionGeneratedEffect* generated = _currentProgram->GetGeneratedEffect();
		if (generated == nullptr) {
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": No fixed_function effect declared by the shader", _currentProgram->GetObjectLabel());
			}
			return;
		}
		const FixedFunctionIntrinsic intrinsic = generated->Intrinsic;

		// The Combine draw is the direct-tier lighting hook (see the software backend)
		if (intrinsic == FixedFunctionIntrinsic::LightingCombine) {
			ApplyPendingSoftwareLighting();
			// A block that binds a stage may ALSO carry passes - the water overlay of the CombineWithWater
			// variants, whose colours and thresholds belong next to the GLSL they approximate rather than
			// duplicated in every backend. They composite over what the stage produced, so fall through to
			// the quad-family path below instead of returning.
			if (generated->Fn == nullptr) {
				return;
			}
		}

		// A whole tile layer arrives as one mesh instead of one command per tile
		if (intrinsic == FixedFunctionIntrinsic::TileMapMesh) {
			DispatchTileMesh(primitive, firstVertex, numVertices);
			return;
		}

		// The weapon wheel is the one vertex-fed mesh on this tier, a textured line strip
		if (intrinsic == FixedFunctionIntrinsic::LineStripMesh) {
			if (primitive == PrimitiveType::LineStrip) {
				DispatchLineStrip(firstVertex, numVertices);
			} else if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Only the line-strip form of the mesh pipeline is supported by the RDP dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		// Everything else is the procedural sprite-quad family
		const bool isQuadFamily = (generated->Fn != nullptr);
		if (!isQuadFamily || (primitive != PrimitiveType::TriangleStrip && primitive != PrimitiveType::Triangles)) {
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Effect not supported by the RDP dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		std::int32_t binding = 0;
		const std::uint8_t* blockData = ResolveInstanceBlockData(binding);
		if (blockData == nullptr) {
			return;
		}

		// Everything the dispatch asks of the program's reflection is a constant of the linked program,
		// resolved ONCE at introspection (see RdpShaderProgram::DispatchFacts) - this used to re-scan the
		// reflection's name strings on every RenderCommand
		const RdpShaderProgram::DispatchFacts& facts = _currentProgram->GetDispatchFacts();
		const std::uint32_t instanceStride = facts.InstanceStride;

		const float* pv = ResolveProjView();

		// Batched programs are exactly the ones whose reflection declares a BATCH_SIZE-strided
		// InstancesBlock (non-batched programs use a flat InstanceBlock with no stride), so the reflected
		// stride IS the batching signal - no per-program identity needed
		const bool batched = (instanceStride > 0);
		std::int32_t numInstances = 1;
		if (batched) {
			numInstances = numVertices / 6;
			if (numInstances < 1) {
				numInstances = 1;
			}
		}

		// A program samples the sprite texture exactly when its reflection binds uTexture; the instance
		// layout follows the block's own reflected declaration rather than any effect identity (the
		// Transition carries texRect but samples nothing)
		const bool hasTexture = facts.HasTexture;
		const bool texturedLayout = facts.TexturedLayout;
		// Every effect that samples indexed sprites through the palette texture binds uTexturePalette in
		// its reflection, which is what UsesPalette() reports
		const bool isPaletteRemap = _currentProgram->UsesPalette();

		const FixedFunctionRequirements reqs = generated->Requirements;
		const bool needsTexelStep = ((reqs & FixedFunctionRequirements::NeedsTexelStep) == FixedFunctionRequirements::NeedsTexelStep);
		const bool needsUniforms = ((reqs & FixedFunctionRequirements::NeedsUniforms) == FixedFunctionRequirements::NeedsUniforms);
		const bool needsStripBuilder = ((reqs & FixedFunctionRequirements::NeedsStripBuilder) == FixedFunctionRequirements::NeedsStripBuilder);
		const bool needsQuadAxes = ((reqs & FixedFunctionRequirements::NeedsQuadAxes) == FixedFunctionRequirements::NeedsQuadAxes);
		const bool samplesTexture = ((reqs & FixedFunctionRequirements::SamplesTexture) == FixedFunctionRequirements::SamplesTexture);
		const std::int32_t textureUnit = facts.TextureUnit;
		RdpTexture* texture = const_cast<RdpTexture*>(hasTexture
			? _boundTextures[std::uint32_t(textureUnit) < MaxTextureUnits ? textureUnit : 0] : nullptr);
		// A program whose reflection binds a sampler with nothing bound to it can only be drawn by an
		// effect that never samples: a textured primitive would rasterize garbage, an untextured shaded
		// strip is unaffected. That is exactly what SamplesTexture records (the lighting compositor's
		// water overlay declares uTexture for a fragment stage this tier never runs).
		if (hasTexture && texture == nullptr && samplesTexture) {
			return;
		}

		// The palette to resolve indices with is whatever the material bound to the palette sampler (e.g.
		// the recolored preview palettes of the profile menu); the registered global palette is the fallback
		const RdpTexture* paletteTex = nullptr;
		if (isPaletteRemap || (texture != nullptr && (texture->IsIndexed() || texture->NeedsPaletteBake()))) {
			const std::int32_t paletteUnit = facts.PaletteUnit;
			paletteTex = (std::uint32_t(paletteUnit) < MaxTextureUnits ? _boundTextures[paletteUnit] : nullptr);
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}
		const std::uint32_t paletteVersion = (paletteTex == _paletteTexture
			? _paletteGeneration : (paletteTex != nullptr ? paletteTex->GetContentVersion() : 0));

		ApplyDrawTarget();
		if (!rdpAttached) {
			return;
		}
		ApplyScissor();

		// Bounds guard, mirroring the software device
		const std::uint32_t rangeSize = _boundUniformRanges[binding].Size;
		if (batched && rangeSize > 0 && std::uint32_t(numInstances) * instanceStride > rangeSize) {
			numInstances = std::int32_t(rangeSize / instanceStride);
		}

		// The material's own blending, shared by every instance of the draw
		DrawState material;
		MapBlendRdp(_blending.Enabled, _blending.SrcRgb, _blending.DstRgb, material);

		// Texel sizes of the sprite texture in texture space (part of the EffectContext contract),
		// derived only for effects flagged with the texel-size facility
		const float texelWidth = (needsTexelStep && hasTexture && texture->GetWidth() > 0 ? 1.0f / float(texture->GetWidth()) : 0.0f);
		const float texelHeight = (needsTexelStep && hasTexture && texture->GetHeight() > 0 ? 1.0f / float(texture->GetHeight()) : 0.0f);

		// Constant NDC-to-raster mapping, folded in once rather than reapplied for every sprite corner
		// (see ComputeRasterFold)
		Recti viewport;
		float rasterScaleX, rasterBiasX, rasterScaleY, rasterBiasY;
		ComputeRasterFold(viewport, rasterScaleX, rasterBiasX, rasterScaleY, rasterBiasY);

		// Everything of the texture state that does not depend on the instance is resolved once for the
		// whole batch; only a remap effect picks a palette row per instance and keeps the per-instance
		// path. A menu frame draws a couple hundred batched instances, and each resolution re-runs the
		// bake check, the TLUT-slot scan and the surface acquisition.
		DrawState sharedTexState = material;
		float sharedUvScaleU = 1.0f, sharedUvScaleV = 1.0f;
		if (hasTexture && !isPaletteRemap) {
			if (!ResolveTextureState(sharedTexState, texture, paletteTex, 0, paletteVersion, sharedUvScaleU, sharedUvScaleV)) {
				return;
			}
		}

		for (std::int32_t k = 0; k < numInstances; k++) {
			const std::uint8_t* inst = blockData + std::size_t(k) * (batched ? instanceStride : 0);

			Transform2D mvp;
			Mat4MulTransform2D(pv, reinterpret_cast<const float*>(inst + kModelMatrixOffset), mvp);
			float color[4];
			std::memcpy(color, inst + kColorOffset, sizeof(color));
			float texRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
			float spriteSize[2];
			if (texturedLayout) {
				std::memcpy(texRect, inst + kTexRectOffset, sizeof(texRect));
				std::memcpy(spriteSize, inst + kSpriteSizeOffset, sizeof(spriteSize));
			} else {
				std::memcpy(spriteSize, inst + kSpriteSizeNoTexOffset, sizeof(spriteSize));
			}

			// Select this instance's texture (and TLUT or bake) - texture coordinates on this backend
			// are texel indices of the surface, so the conversion is simply the source size. Everything
			// not depending on the instance was resolved once before the loop; only a remap effect picks
			// its palette row from the instance here.
			DrawState state = sharedTexState;
			float uvScaleU = sharedUvScaleU, uvScaleV = sharedUvScaleV;
			if (hasTexture && isPaletteRemap) {
				state = material;
				float palOffset = 0.0f;
				std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
				const std::int32_t paletteOffset = std::int32_t(palOffset + 0.5f);
				if (!ResolveTextureState(state, texture, paletteTex, paletteOffset, paletteVersion, uvScaleU, uvScaleV)) {
					continue;
				}
			}

			// Synthesize the four sprite corners exactly like the software FetchVertex, with the constant
			// NDC-to-raster mapping folded into the transform so a corner costs one multiply-add per axis.
			// The corner weights are 0 or 1, so the sprite's extent in raster space is just the
			// transformed axes scaled by its size, and the corners are sums of those.
			const float originX = mvp.Tx * rasterScaleX + rasterBiasX;
			const float originY = mvp.Ty * rasterScaleY + rasterBiasY;
			const float spanXx = mvp.Xx * rasterScaleX * spriteSize[0];
			const float spanXy = mvp.Xy * rasterScaleY * spriteSize[0];
			const float spanYx = mvp.Yx * rasterScaleX * spriteSize[1];
			const float spanYy = mvp.Yy * rasterScaleY * spriteSize[1];
			float px[4], py[4], pu[4], pvv[4];
			for (std::int32_t i = 0; i < 4; i++) {
				const float ax = ((i & ~1) == 0) ? 1.0f : 0.0f;
				const float ay = (i & 1) ? 1.0f : 0.0f;
				px[i] = originX + ax * spanXx + ay * spanYx;
				py[i] = originY + ax * spanXy + ay * spanYy;
				pu[i] = (ax * texRect[0] + texRect[1]) * uvScaleU;
				pvv[i] = (ay * texRect[2] + texRect[3]) * uvScaleV;
			}

			// Nothing is clipped geometrically - the RDP has a real raster scissor and it is already
			// programmed - but a quad that cannot touch the surface at all is worth not submitting
			{
				const float minX = std::min(std::min(px[0], px[1]), std::min(px[2], px[3]));
				const float maxX = std::max(std::max(px[0], px[1]), std::max(px[2], px[3]));
				const float minY = std::min(std::min(py[0], py[1]), std::min(py[2], py[3]));
				const float maxY = std::max(std::max(py[0], py[1]), std::max(py[2], py[3]));
				if (maxX <= float(appliedScissor[0]) || minX >= float(appliedScissor[0] + appliedScissor[2]) ||
					maxY <= float(appliedScissor[1]) || minY >= float(appliedScissor[1] + appliedScissor[3])) {
					continue;
				}
			}

			// The pass descriptors the per-effect functions declare are mapped onto this instance's
			// corners and the resolved RDP state through the context
			EffectContext ctx;
			ctx.InstanceColor = color;
			ctx.TexelW = texelWidth;
			ctx.TexelH = texelHeight;
			ctx.Batched = batched;
			ctx.Material = &state;
			ctx.Px = px;
			ctx.Py = py;
			ctx.Pu = pu;
			ctx.Pv = pvv;
			ctx.TexRect = texRect;
			// The optional context facilities are only wired up for effects whose static analysis says
			// they can call them; members of an unused facility are simply never read
			if (needsQuadAxes) {
				ctx.OriginX = originX;
				ctx.OriginY = originY;
				ctx.AxisXx = spanXx;
				ctx.AxisXy = spanXy;
				ctx.AxisYx = spanYx;
				ctx.AxisYy = spanYy;
			}
			ctx.Program = (needsUniforms ? _currentProgram : nullptr);
			if (needsStripBuilder) {
				ctx.UvScaleU = uvScaleU;
				ctx.UvScaleV = uvScaleV;
			}

			if (TraceDrawStatistics) { stats.Instances++; }
			generated->Fn(ctx);
			// Whatever the effect accumulated into the merge slot goes out now (see SubmitQuad); an
			// instance never carries a pending pass into the next one
			ctx.FlushPendingQuad();
		}
	}
}
