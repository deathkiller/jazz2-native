#include "GsDevice.h"
#include "GsShaderProgram.h"
#include "GsRenderTarget.h"
#include "GsStagingBuffer.h"
#include "GsTexture.h"
#include "GsBuffer.h"
#include "../FixedFunctionPass.h"

#include "../../../../Main.h"

#include <cstring>

extern "C" {
#include <graph.h>
#include <draw.h>
#include <dma.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
}

namespace nCine::RHI::GS
{
	namespace
	{
		/** @brief Words per page and per block - `libgraph`/`libdraw` address local memory in 32-bit words */
		constexpr std::uint32_t WordsPerPage = GsVram::PageBytes / 4;
		constexpr std::uint32_t WordsPerBlock = GsVram::BlockBytes / 4;

		/** @brief Display geometry. NTSC 640x448 interlaced, the mode the probe confirmed the GS accepts */
		constexpr std::int32_t DisplayWidth = 640;
		constexpr std::int32_t DisplayHeight = 448;

		/**
			@brief GIF packet scratch for state and draws

			Sized for a whole frame's state changes plus a generous draw margin; @ref FlushPackets() empties
			it whenever it approaches full, so overflowing only costs an extra DMA rather than dropping work.
		*/
		alignas(64) qword_t _packet[16384];
		qword_t* _packetCursor = _packet;

		/** @brief Staging buffer for CLUT uploads (256 entries of 32 bits, qword-aligned for the transfer) */
		alignas(64) std::uint32_t _clutStaging[256];

		/**
			@brief The CPU lightmap's factor surface in local memory, and the main-memory image it comes from

			Allocated once and rewritten every frame (see @ref GsDevice::ApplyPendingSoftwareLighting()) - a
			per-frame allocation would compete with the texture cache for pages exactly when a level is already
			short of them. A GS transfer reads main memory directly, so the staging image has to be contiguous,
			qword-aligned and in the destination's pitch (see @ref GsStagingBuffer).
		*/
		std::uint32_t _lightmapPage = GsVram::InvalidPage;
		std::uint32_t _lightmapPageCount = 0;
		GsStagingBuffer _lightmapStaging;

		/** @brief Opaque white as a pass colour, for the passes whose colour carries no information */
		constexpr float OpaqueWhite[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		framebuffer_t _frame;
		zbuffer_t _zbuffer;

		inline std::uint8_t QuantizeChannel(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint8_t(v * 255.0f + 0.5f);
		}

		/**
			@brief A 0..1 float on the GS's FRAGMENT-colour scale, where 0x80 - not 0xFF - is 1.0

			Both the texture function and the blender shift their products right by 7, so a vertex colour is
			a 0..2 multiplier with 128 at unity: `(0xFF texel * 0x80) >> 7` is 0xFF, and a blend factor of
			0x80 passes the source through unchanged. Values above 1.0 are clamped rather than allowed to run
			into the 0x81..0xFF overbright range, because that is the range the PVR's `argb` has too - the
			effects that want a multiplier above one (Colorized) split it into additive passes instead, and
			letting one pass carry 2x here would double-count what the next pass adds.
		*/
		inline std::uint8_t QuantizePassChannel(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint8_t(v * 128.0f + 0.5f);
		}

		/** @brief A `FixedFunctionPass` colour packed into `RGBAQ` for a TEXTURED primitive */
		color_t PackPassColor(const float* rgba)
		{
			color_t out;
			out.r = QuantizePassChannel(rgba[0]);
			out.g = QuantizePassChannel(rgba[1]);
			out.b = QuantizePassChannel(rgba[2]);
			out.a = QuantizePassChannel(rgba[3]);
			// Unused under PRIM_MAP_UV, but it shares the register, so it is given the 1.0 every helper uses
			out.q = 1.0f;
			return out;
		}

		/**
			@brief A `FixedFunctionPass` colour packed into `RGBAQ` for an UNTEXTURED primitive

			The `>> 7` that makes 0x80 mean 1.0 belongs to the TEXTURE FUNCTION, and a primitive submitted with
			`TME = 0` never reaches it: its interpolated RGB *is* the fragment colour, on the full 0..255 scale
			@ref QuantizeChannel() packs - which is why @ref GsDevice::Clear(), the one untextured draw that was
			always right, quantizes its rectangle that way. Packing an untextured colour like a modulating pass
			instead halves it, and that is what left the horizon tint of the textured background and the fur
			gradient swatches of the user-profile section visibly dark against every other backend.

			ALPHA is the exception and stays on the 0..0x80 scale, because nothing about it is a colour: it is
			the blender's `C` in `((A - B) * C >> 7) + D` and the alpha test's operand, and 0x80 is 1.0 to both
			(it is also what every textured draw leaves in the frame buffer, so the two agree there too).
		*/
		color_t PackVertexColor(const float* rgba)
		{
			color_t out;
			out.r = QuantizeChannel(rgba[0]);
			out.g = QuantizeChannel(rgba[1]);
			out.b = QuantizeChannel(rgba[2]);
			out.a = QuantizePassChannel(rgba[3]);
			out.q = 1.0f;
			return out;
		}

		/** @brief Sends whatever is in the scratch packet and resets the cursor */
		void FlushPackets()
		{
			if (_packetCursor == _packet) {
				return;
			}
			dma_channel_send_normal(DMA_CHANNEL_GIF, _packet, std::int32_t(_packetCursor - _packet), 0, 0);
			dma_wait_fast();
			_packetCursor = _packet;
		}

		/** @brief Reserves room for @p qwords, flushing first if the scratch cannot hold them */
		qword_t* Reserve(std::size_t qwords)
		{
			const std::size_t capacity = sizeof(_packet) / sizeof(_packet[0]);
			if (std::size_t(_packetCursor - _packet) + qwords > capacity) {
				FlushPackets();
			}
			return _packetCursor;
		}

		/**
			@brief Queues a `TEXFLUSH` into the ordinary register packet

			Deliberately hand-assembled instead of calling `draw_texture_flush()`, which looks like the
			obvious way to do this and is not: that helper emits a DMA **END tag** ahead of its GIF tag,
			because every one of its callers in `libdraw` is building a transfer CHAIN. Appended to a packet
			that goes out with `dma_channel_send_normal()`, the tag reaches the GIF where a GIF tag is
			expected, and the channel wedges with no error of any kind - the same trap
			`draw_texture_transfer()` sets, documented at the top of GsDevice.h. What is actually wanted here
			is two qwords: a packed A+D tag and the register write, exactly as the primitive path emits PRIM.
		*/
		void QueueTextureFlush()
		{
			qword_t* q = Reserve(4);
			q->dw[0] = GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1);
			q->dw[1] = GIF_REG_AD;
			q++;
			q->dw[0] = GS_SET_TEXFLUSH(0);
			q->dw[1] = GS_REG_TEXFLUSH;
			q++;
			_packetCursor = q;
		}

		// The DefaultSprite / DefaultBatchedSprites instance layout is a hard contract of the shader family
		// (std140 offsets within the InstanceBlock / each batched Instance) - identical to the software and
		// PVR backends' decode
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

		/** @brief Column-major 4x4 multiply, out = a * b */
		void Mat4Mul(const float* a, const float* b, float* out)
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

		/**
			@brief The six products of projection*view*model a 2D point of the form (x, y, 0, 1) ever reads

			The same reduction the PVR backend makes: a sprite pays this per instance, so the other ten
			products of a full 4x4 multiply are never computed.
		*/
		struct Transform2D
		{
			float Xx, Xy;	// Column 0, rows 0 and 1
			float Yx, Yy;	// Column 1, rows 0 and 1
			float Tx, Ty;	// Column 3, rows 0 and 1
		};

		void Mat4MulTransform2D(const float* pv, const float* model, Transform2D& out)
		{
			out.Xx = pv[0] * model[0] + pv[4] * model[1] + pv[8] * model[2] + pv[12] * model[3];
			out.Xy = pv[1] * model[0] + pv[5] * model[1] + pv[9] * model[2] + pv[13] * model[3];
			out.Yx = pv[0] * model[4] + pv[4] * model[5] + pv[8] * model[6] + pv[12] * model[7];
			out.Yy = pv[1] * model[4] + pv[5] * model[5] + pv[9] * model[6] + pv[13] * model[7];
			out.Tx = pv[0] * model[12] + pv[4] * model[13] + pv[8] * model[14] + pv[12] * model[15];
			out.Ty = pv[1] * model[12] + pv[5] * model[13] + pv[9] * model[14] + pv[13] * model[15];
		}

		/**
			@brief One blend mode expressed in the terms the GS actually has

			The GS computes exactly ONE equation, `Cv = ((A - B) * C) >> 7 + D`, in which A, B and D each
			select the source colour, the destination colour or zero, and C selects the source alpha, the
			destination alpha or the fixed value carried in the same register. There are no independent
			source/destination factors and no blend equation to choose, so a GL-style factor pair either
			collapses onto this form exactly or it does not exist on this hardware at all.

			Note `>> 7`: **0x80 is 1.0** to that shifter, not 0xFF, which is why every alpha reaching the
			rasterizer is halved on the way in (see @ref GsTexture::RefreshStore and @ref AcquireClut).
		*/
		struct BlendEquation
		{
			std::uint8_t A = BLEND_COLOR_SOURCE;
			std::uint8_t B = BLEND_COLOR_DEST;
			std::uint8_t C = BLEND_ALPHA_SOURCE;
			std::uint8_t D = BLEND_COLOR_DEST;
			std::uint8_t Fixed = 0x80;
			/**
				@brief Whether a source texel of zero alpha leaves the destination exactly as it was

				True for every alpha-weighted mode, and it is what makes the alpha test a free optimisation
				rather than a change of behaviour: a fragment the test throws away would have blended to the
				destination value anyway. False for the modes that write the source unconditionally, where
				discarding those fragments would drop pixels that belong on screen.
			*/
			bool AlphaWeighted = true;

			bool operator!=(const BlendEquation& other) const {
				return (A != other.A || B != other.B || C != other.C || D != other.D ||
					Fixed != other.Fixed || AlphaWeighted != other.AlphaWeighted);
			}
		};

		/** @brief `Cv = Cs` - what "blending disabled" means, expressed as a GS equation */
		constexpr BlendEquation OpaqueEquation()
		{
			// (Cs - 0) * 0x80 >> 7 + 0, which is Cs exactly
			return { BLEND_COLOR_SOURCE, BLEND_COLOR_ZERO, BLEND_ALPHA_FIXED, BLEND_COLOR_ZERO, 0x80, false };
		}

		/** @brief `Cv = Cs*As + Cd*(1-As)` - ordinary source-over, and the fallback for anything unmappable */
		constexpr BlendEquation SourceOverEquation()
		{
			return { BLEND_COLOR_SOURCE, BLEND_COLOR_DEST, BLEND_ALPHA_SOURCE, BLEND_COLOR_DEST, 0x80, true };
		}

		constexpr std::uint32_t BlendKey(nCine::BlendingFactor src, nCine::BlendingFactor dst)
		{
			return (std::uint32_t(src) << 16) | std::uint32_t(dst);
		}

		/**
			@brief Maps a GL-style (source, destination) factor pair onto @ref BlendEquation

			Returns `false` when the pair has no GS form, in which case @p out is left holding source-over -
			the same "fall back to blend_mix" answer `Material::SetShader()` gives for the blend equations
			materials cannot express either.
		*/
		bool ResolveBlendEquation(nCine::BlendingFactor src, nCine::BlendingFactor dst, BlendEquation& out)
		{
			out = SourceOverEquation();
			switch (BlendKey(src, dst)) {
				case BlendKey(nCine::BlendingFactor::One, nCine::BlendingFactor::Zero):
					out = OpaqueEquation();
					return true;
				case BlendKey(nCine::BlendingFactor::SrcAlpha, nCine::BlendingFactor::OneMinusSrcAlpha):
					// Cs*As + Cd*(1-As)
					return true;
				case BlendKey(nCine::BlendingFactor::SrcAlpha, nCine::BlendingFactor::One):
					// Cs*As + Cd - additive, which the weapons, the shields and the lighting pass all ask for
					out = { BLEND_COLOR_SOURCE, BLEND_COLOR_ZERO, BLEND_ALPHA_SOURCE, BLEND_COLOR_DEST, 0x80, true };
					return true;
				case BlendKey(nCine::BlendingFactor::One, nCine::BlendingFactor::One):
					// Cs + Cd
					out = { BLEND_COLOR_SOURCE, BLEND_COLOR_ZERO, BLEND_ALPHA_FIXED, BLEND_COLOR_DEST, 0x80, false };
					return true;
				case BlendKey(nCine::BlendingFactor::Zero, nCine::BlendingFactor::One):
					// Cd - the destination is not touched at all
					out = { BLEND_COLOR_ZERO, BLEND_COLOR_ZERO, BLEND_ALPHA_FIXED, BLEND_COLOR_DEST, 0x00, true };
					return true;
				case BlendKey(nCine::BlendingFactor::Zero, nCine::BlendingFactor::OneMinusSrcAlpha):
					// Cd*(1-As), as (0 - Cd)*As + Cd
					out = { BLEND_COLOR_ZERO, BLEND_COLOR_DEST, BLEND_ALPHA_SOURCE, BLEND_COLOR_DEST, 0x80, true };
					return true;
				case BlendKey(nCine::BlendingFactor::OneMinusSrcAlpha, nCine::BlendingFactor::SrcAlpha):
					// Cs*(1-As) + Cd*As, as (Cd - Cs)*As + Cs
					out = { BLEND_COLOR_DEST, BLEND_COLOR_SOURCE, BLEND_ALPHA_SOURCE, BLEND_COLOR_SOURCE, 0x80, true };
					return true;
				case BlendKey(nCine::BlendingFactor::One, nCine::BlendingFactor::OneMinusSrcAlpha):
					// Premultiplied alpha wants Cs + Cd*(1-As), and C multiplies (A - B) as one term, so the
					// GS cannot separate the two factors. Source-over is the closest it has: it agrees exactly
					// at both ends of the range (an opaque texel gives Cs, a clear one gives Cd) and only
					// under-weights the source in between, where additive - the other candidate - is wrong at
					// the opaque end, which is the end that shows.
					return true;
				default:
					// Everything left needs a factor the GS has no slot for: the colour-product modes
					// (Multiply's DstColor, SrcColor and their complements) and the constant-colour modes
					return false;
			}
		}

		/** @brief The equation the material's own blending state resolves to */
		BlendEquation MaterialEquation(const GsDevice::BlendingState& blending)
		{
			BlendEquation equation;
			if (!blending.Enabled) {
				// Blending disabled means `Cv = Cs` whatever factors were last recorded, exactly as it does
				// in GL - and as the PowerVR backend spells out by mapping to ONE/ZERO
				return OpaqueEquation();
			}
			if (!ResolveBlendEquation(blending.SrcRgb, blending.DstRgb, equation)) {
				static bool warnedUnsupported = false;
				if (!warnedUnsupported) {
					warnedUnsupported = true;
					LOGW("Blending factors 0x{:x}/0x{:x} have no Graphics Synthesizer equation, falling back to source-over",
						std::uint32_t(blending.SrcRgb), std::uint32_t(blending.DstRgb));
				}
			}
			// The alpha blending factors are per-RGB only: the GS writes the fragment's own alpha to the
			// buffer, so `SrcAlpha`/`DstAlpha` of a four-factor call have no register to go to (and the
			// display buffer is PSMCT16, whose single alpha bit FBA pins to 1 regardless)
			return equation;
		}

		/**
			@brief The equation one pass draws under, given the equation its material resolved to

			A `FixedFunctionPass::BlendMode` is an override of the material's blending, so `Material` is the
			identity here and the other three name equations of their own.
		*/
		BlendEquation PassEquation(const BlendEquation& material, FixedFunctionPass::BlendMode mode)
		{
			switch (mode) {
				case FixedFunctionPass::BlendMode::Additive:
					// `Cs*As + Cd`. Deliberately SRCALPHA + ONE rather than ONE + ONE, which is the mapping
					// FixedFunctionPass documents for the split-multiplier passes: their contributions are
					// scaled by the pass alpha, so this stays bit-identical with the PVR's.
					return { BLEND_COLOR_SOURCE, BLEND_COLOR_ZERO, BLEND_ALPHA_SOURCE, BLEND_COLOR_DEST, 0x80, true };
				case FixedFunctionPass::BlendMode::Opaque:
					return OpaqueEquation();
				case FixedFunctionPass::BlendMode::Alpha:
					return SourceOverEquation();
				default:
					return material;
			}
		}

		/** @brief The equation currently programmed into `ALPHA`/`TEST`, so unchanged state costs nothing */
		BlendEquation _appliedBlend;
		bool _appliedBlendValid = false;

		/**
			@brief Programs `ALPHA` and `TEST` for @p equation if they are not already programmed for it

			Both registers are part of the GS's context state and survive across packets, so this only ever
			writes when the mode actually changes - which for a frame of this game is a handful of times.
		*/
		void ApplyBlendEquation(const BlendEquation& equation)
		{
			if (_appliedBlendValid && !(_appliedBlend != equation)) {
				return;
			}
			_appliedBlend = equation;
			_appliedBlendValid = true;

			qword_t* q = Reserve(16);

			blend_t blend;
			blend.color1 = char(equation.A);
			blend.color2 = char(equation.B);
			blend.alpha = char(equation.C);
			blend.color3 = char(equation.D);
			blend.fixed_alpha = equation.Fixed;
			q = draw_alpha_blending(q, 0, &blend);

			// Alpha test as a cutout. It is worth having because the GS is fill-rate bound long before it is
			// anything else and this game's sheets are mostly empty space, but it is only equivalent while an
			// alpha-zero source blends to the destination - so the modes that write the source unconditionally
			// turn it off instead of quietly losing their transparent texels.
			atest_t atest;
			atest.enable = (equation.AlphaWeighted ? DRAW_ENABLE : DRAW_DISABLE);
			atest.method = ATEST_METHOD_NOTEQUAL;
			atest.compval = 0x00;
			// Reads backwards: PS2SDK names the `AFAIL` values after what a FAILING fragment leaves alone, so
			// "keep the framebuffer" is the one that discards the fragment
			atest.keep = ATEST_KEEP_FRAMEBUFFER;
			dtest_t dtest;
			dtest.enable = DRAW_DISABLE;
			dtest.pass = DRAW_DISABLE;
			// There is no Z buffer here (see InitializeGs), but ZTE = 0 is prohibited by the hardware, so the
			// test stays enabled and passes everything - which is also what draw_setup_environment() programs
			ztest_t ztest;
			ztest.enable = DRAW_ENABLE;
			ztest.method = ZTEST_METHOD_ALLPASS;
			q = draw_pixel_test(q, 0, &atest, &dtest, &ztest);

			_packetCursor = q;
		}

		// ------------------------------------------------------------ texture state and primitives

		/**
			@brief Everything one primitive needs from `TEX0`/`TEX1`/`CLAMP`, resolved per instance

			The fixed-function effects submit passes over a quad whose *texture* state is settled before the
			effect runs (page residency, the palette row's CLUT, the sampled extent), and a pass may swap
			part of it - a silhouette pass samples the same store's coverage instead of its colour. Bundling
			it makes that swap a copy-and-edit of a value rather than a second parameter list.
		*/
		struct DrawState
		{
			/** @brief Store page, or @ref GsVram::InvalidPage for an untextured primitive */
			std::uint32_t Page = GsVram::InvalidPage;
			/** @brief `TEX0.CBP`, or @ref GsVram::InvalidBlock when the mode needs no CLUT */
			std::uint32_t ClutBlock = GsVram::InvalidBlock;
			/** @brief Store pitch in TEXELS (libdraw divides by 64 itself) */
			std::int32_t BufferPitch = 0;
			/** @brief Hardware `PSM` the store is sampled with */
			std::int32_t Psm = GS_PSM_32;
			/** @brief `TEX0.TW`/`TH` extents (the padded power of two) */
			std::int32_t SampledWidth = 8, SampledHeight = 8;
			/** @brief `CLAMP.WMS`/`WMT` */
			std::int32_t WrapU = WRAP_CLAMP, WrapV = WRAP_CLAMP;
			/** @brief `TEX1.MMAG` (1 bit) and `TEX1.MMIN` (3 bits - different encodings, hence two mappers) */
			std::int32_t MagFilter = LOD_MAG_NEAREST, MinFilter = LOD_MIN_NEAREST;

			bool operator!=(const DrawState& other) const {
				return (Page != other.Page || ClutBlock != other.ClutBlock || BufferPitch != other.BufferPitch ||
					Psm != other.Psm || SampledWidth != other.SampledWidth || SampledHeight != other.SampledHeight ||
					WrapU != other.WrapU || WrapV != other.WrapV ||
					MagFilter != other.MagFilter || MinFilter != other.MinFilter);
			}
		};

		/**
			@brief A pass colour packed on whichever of the two scales the primitives of @p state read

			The choice is exactly the one @ref SubmitVertexPrimitive() makes for `PRIM.TME`, and it is made
			from the same field, so a pass cannot be packed for a texture function the primitive does not run
			(see @ref PackVertexColor()).
		*/
		color_t PackStateColor(const DrawState& state, const float* rgba)
		{
			return (state.Page == GsVram::InvalidPage ? PackVertexColor(rgba) : PackPassColor(rgba));
		}

		DrawState _appliedTexture;
		bool _appliedTextureValid = false;

		/** @brief `SamplerWrapping` as the `CLAMP` register spells it */
		std::int32_t MapWrapGs(SamplerWrapping wrap)
		{
			return (wrap == SamplerWrapping::Repeat || wrap == SamplerWrapping::MirroredRepeat
				? WRAP_REPEAT : WRAP_CLAMP);
		}

		/**
			@brief Whether @p filter asks for interpolation rather than point sampling

			The GS does have bilinear sampling, and it interpolates the CLUT'ed colours rather than the
			indices, so it is usable on the indexed stores too. The mipmapped variants degrade to their
			non-mipmapped base: they need a `MIPTBP1`/`MIPTBP2` level chain this backend never builds, and
			nothing in a 2D game asks for one.
		*/
		bool IsLinearFilter(nCine::SamplerFilter filter)
		{
			return (filter == nCine::SamplerFilter::Linear ||
				filter == nCine::SamplerFilter::LinearMipmapNearest ||
				filter == nCine::SamplerFilter::LinearMipmapLinear);
		}
		// MMAG and MMIN are separate fields with separate encodings, so they get separate mappers rather than
		// one shared "filter" value that happens to agree for 0 and 1
		std::int32_t MapMagFilterGs(nCine::SamplerFilter filter)
		{
			return (IsLinearFilter(filter) ? LOD_MAG_LINEAR : LOD_MAG_NEAREST);
		}
		std::int32_t MapMinFilterGs(nCine::SamplerFilter filter)
		{
			return (IsLinearFilter(filter) ? LOD_MIN_LINEAR : LOD_MIN_NEAREST);
		}

		/**
			@brief Programs `TEX0` and `CLAMP` for @p state

			Unchanged state is skipped, CLUT or not. Getting this wrong is expensive in both directions and it
			has been wrong both ways:

			- Skipping on the block address ALONE is not safe. The slab reuses slots, so the same `CBP` can
			  hold different palette contents from one draw to the next, and `TEX0` with `CLD = 1` is the only
			  thing that reloads the GS's internal CLUT buffer - so a skipped write leaves it sampling the
			  previous palette, which is what turned the intro and the glow flat.
			- Restating unconditionally, which is what that was fixed with, costs far more than it looks. The
			  write is two qwords, but `CLD = 1` makes the GS re-read a whole kilobyte of local memory into
			  its CLUT buffer, and writing `TEX0` at all invalidates its texture cache. Doing that once per
			  SPRITE - and on this content nearly every sprite is indexed - is a thousand CLUT reloads and a
			  thousand texture-cache refills a frame, which is fill rate this hardware cannot spare.

			The answer is to keep the state cache and invalidate it where the hazard actually is:
			@ref GsDevice::AcquireClut() clears it whenever it overwrites a slot, which is the only way the
			contents behind an unchanged `CBP` can change. Everything else - a different row, a different
			store, an untextured draw in between - already differs in @ref DrawState and restates on its own.
		*/
		void ApplyTexture(const DrawState& state)
		{
			if (state.Page == GsVram::InvalidPage) {
				return;		// Untextured primitive - TEX0 is not read at all
			}
			if (_appliedTextureValid && !(_appliedTexture != state)) {
				return;
			}
			const bool hasClut = (state.ClutBlock != GsVram::InvalidBlock);
			_appliedTexture = state;
			_appliedTextureValid = true;

			texbuffer_t texbuf;
			texbuf.address = state.Page * WordsPerPage;
			texbuf.width = state.BufferPitch;
			texbuf.psm = state.Psm;
			texbuf.info.width = draw_log2(state.SampledWidth);
			texbuf.info.height = draw_log2(state.SampledHeight);
			texbuf.info.components = TEXTURE_COMPONENTS_RGBA;
			texbuf.info.function = TEXTURE_FUNCTION_MODULATE;

			clutbuffer_t clutbuf;
			clutbuf.address = (hasClut ? state.ClutBlock * WordsPerBlock : 0);
			clutbuf.psm = GS_PSM_32;
			clutbuf.storage_mode = CLUT_STORAGE_MODE1;
			clutbuf.start = 0;
			// CLUT_LOAD forces the palette to be re-read rather than trusting the GS's CLUT cache
			clutbuf.load_method = (hasClut ? CLUT_LOAD : CLUT_NO_LOAD);

			texwrap_t wrap;
			wrap.horizontal = state.WrapU;
			wrap.vertical = state.WrapV;
			wrap.minu = wrap.maxu = 0;
			wrap.minv = wrap.maxv = 0;

			// TEX1 selects the sampling filter. `LOD_USE_K` with K = 0 and L = 0 keeps the level fixed at the
			// base texture - there is no mip chain here - so MMIN and MMAG both act as the plain filter choice.
			lod_t lod;
			lod.calculation = LOD_USE_K;
			lod.max_level = 0;
			lod.mag_filter = state.MagFilter;
			lod.min_filter = state.MinFilter;
			lod.mipmap_select = LOD_MIPMAP_REGISTER;
			lod.l = 0;
			lod.k = 0.0f;

			qword_t* q = Reserve(24);
			q = draw_texturebuffer(q, 0, &texbuf, &clutbuf);
			q = draw_texture_wrapping(q, 0, &wrap);
			q = draw_texture_sampling(q, 0, &lod);
			_packetCursor = q;
		}

		/**
			@brief Largest texel coordinate the `UV` register can carry

			`UV` holds U and V as 14-bit 12.4 fixed point, so a texel coordinate above this wraps around
			silently. A repeating texture reaches it easily: the legacy menu background asks for 96 repeats of
			its sheet, which is 1536 texels across - and a wrapped coordinate there reads as a zoomed
			background rather than as a glitch. Those primitives switch to `ST` instead (see
			@ref SubmitVertexPrimitive()), whose coordinates are IEEE floats normalized to the sampled extent
			and therefore effectively unbounded.
		*/
		constexpr float MaxUvTexel = 1023.0f;

		/** @brief Whether every texel coordinate a primitive needs fits the `UV` register */
		bool FitsUvRegister(const float* tu, const float* tv, std::int32_t count,
			float sampledWidth, float sampledHeight)
		{
			for (std::int32_t i = 0; i < count; i++) {
				if (tu[i] < 0.0f || tv[i] < 0.0f ||
					tu[i] * sampledWidth > MaxUvTexel || tv[i] * sampledHeight > MaxUvTexel) {
					return false;
				}
			}
			return true;
		}

		// Forward-declared because a rotated quad falls through to it (see below), and it is the longer of the
		// two so it reads better after the common case
		void SubmitVertexPrimitive(std::int32_t primType, const DrawState& state, const float* sx, const float* sy,
			const float* tu, const float* tv, const std::uint64_t* colors, const color_t& flat,
			std::int32_t count, float offsetX, float offsetY);

		/** @brief A triangle strip out of a vertex list (see @ref SubmitVertexPrimitive()) */
		inline void SubmitStripPrimitive(const DrawState& state, const float* sx, const float* sy,
			const float* tu, const float* tv, const std::uint64_t* colors, const color_t& flat,
			std::int32_t count, float offsetX, float offsetY)
		{
			SubmitVertexPrimitive(PRIM_TRIANGLE_STRIP, state, sx, sy, tu, tv, colors, flat, count, offsetX, offsetY);
		}

		/** @brief A connected line strip out of a vertex list (the weapon wheel) */
		inline void SubmitLineStripPrimitive(const DrawState& state, const float* sx, const float* sy,
			const float* tu, const float* tv, const color_t& flat, std::int32_t count)
		{
			SubmitVertexPrimitive(PRIM_LINE_STRIP, state, sx, sy, tu, tv, nullptr, flat, count, 0.0f, 0.0f);
		}

		/** @brief An independent triangle list out of a vertex list (the tile mesh's odd primitives) */
		inline void SubmitTrianglePrimitive(const DrawState& state, const float* sx, const float* sy,
			const float* tu, const float* tv, const color_t& flat, float offsetX, float offsetY)
		{
			SubmitVertexPrimitive(PRIM_TRIANGLE, state, sx, sy, tu, tv, nullptr, flat, 3, offsetX, offsetY);
		}

		/**
			@brief One quad, textured or not, displaced by a pass's screen offset

			@p px / @p py are the four synthesized corners and @p pu / @p pv their NORMALIZED texture
			coordinates - the sampled extent is folded in here, so a caller never has to know it.
		*/
		void SubmitQuadPrimitive(const DrawState& state, const float* px, const float* py,
			const float* pu, const float* pv, const color_t& color, float offsetX, float offsetY,
			bool axisAligned)
		{
			if (!axisAligned) {
				// A rotated quad is not a GS SPRITE - that primitive takes two opposite corners and is
				// axis-aligned by construction - so it goes out as the two triangles of a strip instead. The
				// corner array is indexed by (ax, ay) weights: 0 = (1,0), 1 = (1,1), 2 = (0,0), 3 = (0,1), and
				// a strip wants them in scan order (0,0) (1,0) (0,1) (1,1) = 2, 0, 3, 1.
				static const std::int32_t StripOrder[4] = { 2, 0, 3, 1 };
				float sx[4], sy[4], tu[4], tv[4];
				for (std::int32_t i = 0; i < 4; i++) {
					const std::int32_t c = StripOrder[i];
					sx[i] = px[c];
					sy[i] = py[c];
					tu[i] = pu[c];
					tv[i] = pv[c];
				}
				SubmitStripPrimitive(state, sx, sy, tu, tv, nullptr, color, 4, offsetX, offsetY);
				return;
			}

			if (state.Page == GsVram::InvalidPage) {
				qword_t* q = Reserve(8);
				rect_t rect;
				rect.v0.x = px[2] + offsetX;
				rect.v0.y = py[0] + offsetY;
				rect.v0.z = 0;
				rect.v1.x = px[0] + offsetX;
				rect.v1.y = py[1] + offsetY;
				rect.v1.z = 0;
				rect.color = color;
				q = draw_rect_filled(q, 0, &rect);
				_packetCursor = q;
				return;
			}

			// draw_rect_textured() writes the UV register, so a coordinate that does not fit it has to take the
			// strip path instead - which can fall back to ST. Recursing with axisAligned = false is the whole
			// change of plan: the geometry is identical, only the primitive and the coordinate register differ.
			if (!FitsUvRegister(pu, pv, 4, float(state.SampledWidth), float(state.SampledHeight))) {
				SubmitQuadPrimitive(state, px, py, pu, pv, color, offsetX, offsetY, false);
				return;
			}

			ApplyTexture(state);
			qword_t* q = Reserve(8);
			texrect_t rect;
			rect.v0.x = px[2] + offsetX;
			rect.v0.y = py[0] + offsetY;
			rect.v0.z = 0;
			rect.v1.x = px[0] + offsetX;
			rect.v1.y = py[1] + offsetY;
			rect.v1.z = 0;
			// UVs arrive normalized; the GS addresses texels, so they scale by the sampled extent
			rect.t0.u = pu[2] * float(state.SampledWidth);
			rect.t0.v = pv[0] * float(state.SampledHeight);
			rect.t1.u = pu[0] * float(state.SampledWidth);
			rect.t1.v = pv[1] * float(state.SampledHeight);
			rect.color = color;
			q = draw_rect_textured(q, 0, &rect);
			_packetCursor = q;
		}

		/**
			@brief A vertex list of @p count vertices as one GS primitive of @p primType

			@p colors is per-vertex when non-null (a shaded strip - the only way to draw a gradient without a
			fragment shader) and @p flat is the uniform colour otherwise. Positions are in raster space and
			@p tu / @p tv NORMALIZED to the sampled extent; both are null for an untextured primitive.

			Texture coordinates go out through `UV` when they fit its 14-bit fixed-point field and through
			`ST` - IEEE floats, divided by the `Q` of `RGBAQ`, which every colour this backend packs sets to
			1.0 - when they do not. `UV` is the cheaper of the two to interpolate and is what the validated
			quad path uses, so the choice is made per primitive rather than once.

			Emitted as one packed GIF list rather than through a libdraw helper, because `libdraw` has no
			primitive that takes UVs and per-vertex colours together - `draw_prim_start`/`_end` build the whole
			vertex format from a `prim_t`, which is the same PRMODE juggling the quad paths avoid.
		*/
		void SubmitVertexPrimitive(std::int32_t primType, const DrawState& state, const float* sx, const float* sy,
			const float* tu, const float* tv, const std::uint64_t* colors, const color_t& flat,
			std::int32_t count, float offsetX, float offsetY)
		{
			// A line strip is the one primitive here that means something with two vertices
			const std::int32_t minVertices = (primType == PRIM_LINE_STRIP || primType == PRIM_LINE ? 2 : 3);
			if (count < minVertices) {
				return;
			}
			const bool textured = (state.Page != GsVram::InvalidPage && tu != nullptr && tv != nullptr);
			if (textured) {
				ApplyTexture(state);
			}

			// Three qwords of header - the A+D GIF tag, the PRIM it carries, and the REGLIST tag - and then a
			// strip vertex is RGBAQ + (UV) + XYZ2, one register-list qword per PAIR of registers. Reserving
			// two for the header was one short, so a primitive submitted with the scratch nearly full wrote
			// its last qword past the end of it
			const std::int32_t regsPerVertex = (textured ? 3 : 2);
			qword_t* q = Reserve(std::size_t(3 + (count * regsPerVertex + 1) / 2));

			// PRIM, then a REGLIST of `regsPerVertex` registers repeated once per vertex. ABE comes from the
			// same library-wide blending flag draw_enable_blending() set, so a strip blends like a quad.
			const std::int32_t tme = (textured ? 1 : 0);
			const bool useUv = (!textured ||
				FitsUvRegister(tu, tv, count, float(state.SampledWidth), float(state.SampledHeight)));
			q->dw[0] = GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1);
			q->dw[1] = GIF_REG_AD;
			q++;
			q->dw[0] = GS_SET_PRIM(primType, 1 /*IIP: interpolate the vertex colours*/, tme, 0,
				1 /*ABE*/, 0, (useUv ? PRIM_MAP_UV : PRIM_MAP_ST), 0, 0);
			q->dw[1] = GS_REG_PRIM;
			q++;

			std::uint64_t regList = (std::uint64_t(GIF_REG_RGBAQ) << 0);
			if (textured) {
				regList |= (std::uint64_t(useUv ? GIF_REG_UV : GIF_REG_ST) << 4) | (std::uint64_t(GIF_REG_XYZ2) << 8);
			} else {
				regList |= (std::uint64_t(GIF_REG_XYZ2) << 4);
			}
			q->dw[0] = GIF_SET_TAG(count, 0, 0, 0, GIF_FLG_REGLIST, regsPerVertex);
			q->dw[1] = regList;
			q++;

			std::uint64_t* dw = reinterpret_cast<std::uint64_t*>(q);
			for (std::int32_t i = 0; i < count; i++) {
				*dw++ = (colors != nullptr ? colors[i] : flat.rgbaq);
				if (textured) {
					if (useUv) {
						*dw++ = GIF_SET_UV(ftoi4(tu[i] * float(state.SampledWidth)),
							ftoi4(tv[i] * float(state.SampledHeight)));
					} else {
						// ST takes the float BIT PATTERNS, and it is already normalized to the sampled extent
						std::uint32_t s, tt;
						std::memcpy(&s, &tu[i], sizeof(s));
						std::memcpy(&tt, &tv[i], sizeof(tt));
						*dw++ = GIF_SET_ST(s, tt);
					}
				}
				// The 2048 bias is the primitive-space origin draw_setup_environment() put XYOFFSET at
				*dw++ = GIF_SET_XYZ(ftoi4(sx[i] + offsetX + 2048.0f), ftoi4(sy[i] + offsetY + 2048.0f), 0);
			}
			// A REGLIST payload is qword-granular, so an odd register count needs the last half filled
			if ((count * regsPerVertex) & 1) {
				*dw++ = 0;
			}
			_packetCursor = reinterpret_cast<qword_t*>(dw);
		}

		/** @brief The scissor window currently programmed, in the inclusive form the register takes */
		std::int32_t _appliedScissor[4] = {};
		bool _appliedScissorValid = false;

		/**
			@brief Programs `SCISSOR` for the inclusive window [@p x0, @p x1] x [@p y0, @p y1]

			Every draw and every clear goes through this, including the ones that do not scissor at all -
			those pass their whole target. Leaving the register alone when the test is off was wrong in both
			directions: the rectangle of the last scissored pass stayed active for everything after it,
			the next frame's clear included, and a render-target pass inherited the display's rectangle.
		*/
		void ApplyScissorArea(std::int32_t x0, std::int32_t x1, std::int32_t y0, std::int32_t y1)
		{
			// The register fields are 11-bit unsigned, so an out-of-range edge would wrap rather than clip.
			// A degenerate rectangle collapses to a single row/column rather than to nothing, which no caller
			// in the engine produces.
			x0 = (x0 < 0 ? 0 : (x0 > 2047 ? 2047 : x0));
			y0 = (y0 < 0 ? 0 : (y0 > 2047 ? 2047 : y0));
			x1 = (x1 < x0 ? x0 : (x1 > 2047 ? 2047 : x1));
			y1 = (y1 < y0 ? y0 : (y1 > 2047 ? 2047 : y1));

			if (_appliedScissorValid && _appliedScissor[0] == x0 && _appliedScissor[1] == x1 &&
				_appliedScissor[2] == y0 && _appliedScissor[3] == y1) {
				return;
			}
			_appliedScissor[0] = x0; _appliedScissor[1] = x1;
			_appliedScissor[2] = y0; _appliedScissor[3] = y1;
			_appliedScissorValid = true;

			qword_t* q = Reserve(8);
			q = draw_scissor_area(q, 0, x0, x1, y0, y1);
			_packetCursor = q;
		}
	}

	GsDevice::BlendingState GsDevice::_blending;
	GsDevice::DepthTestState GsDevice::_depthTest;
	GsDevice::CullFaceState GsDevice::_cullFace;
	GsDevice::ScissorState GsDevice::_scissor;
	Recti GsDevice::_viewport(0, 0, 0, 0);
	Colorf GsDevice::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	GsShaderProgram* GsDevice::_currentProgram = nullptr;
	const GsTexture* GsDevice::_boundTextures[GsDevice::MaxTextureUnits] = {};
	GsDevice::UniformRange GsDevice::_boundUniformRanges[GsDevice::MaxUniformBindings] = {};
	GsRenderTarget* GsDevice::_currentRenderTarget = nullptr;
	bool GsDevice::_renderTargetSurfaceMissing = false;

	bool GsDevice::_gsInitialized = false;
	std::int32_t GsDevice::_logicalWidth = DisplayWidth;
	std::int32_t GsDevice::_logicalHeight = DisplayHeight;
	std::int32_t GsDevice::_displayBufferIndex = 0;

	GsTexture* GsDevice::_paletteTexture = nullptr;
	std::uint32_t GsDevice::_paletteGeneration = 1;
	std::uint32_t GsDevice::_paletteRowStamp[GsDevice::MaxPaletteRows] = {};
	GsDevice::ClutSlot GsDevice::_clutSlots[GsDevice::MaxClutSlots] = {};
	std::uint32_t GsDevice::_clutUseCounter = 0;
	std::uint32_t GsDevice::_frameCounter = 0;

	std::vector<GsDevice::PendingSoftwareLight> GsDevice::_pendingSoftwareLights;

	// ------------------------------------------------------------------ session

	void GsDevice::InitializeGs()
	{
		if (_gsInitialized) {
			return;
		}

		GsVramLayout layout;
		layout.DisplayWidth = DisplayWidth;
		layout.DisplayHeight = DisplayHeight;
		// Two 16-bit colour surfaces. Single-buffering was tried, to hand the cache the 70 pages (560 KB) a
		// second one costs - but the rasterizer and the CRT then share a buffer, and the CRT does not wait.
		// A frame is cleared and redrawn starting at the vertical blank while the beam is already scanning
		// the same memory, so whatever is drawn LAST only appears in the frames where the rasterizer beat the
		// beam to those rows. That is the HUD, every time, because it is drawn last - and it flashed on and
		// off frame to frame. (Not visible under an emulator, which presents a finished buffer at the vsync
		// and has no beam, which is why it survived.) The pages come back out of the CPU lightmap instead:
		// see CombineRenderer, where the PS2 now builds a quarter-resolution map like the other consoles.
		//
		// PSMCT32 would cost 1120 KB per buffer and leave the cache barely larger than a level's biggest
		// atlas, so 16-bit it stays; the game's art is indexed and dithers into it without banding.
		layout.DisplayPsm = GsPsm::Ct16;
		layout.DisplayBufferCount = 2;
		layout.ReservedRttPages = 16;
		layout.ClutSlotCount = MaxClutSlots;
		if (!GsVram::Initialize(layout)) {
			LOGE("The GS video-memory layout does not fit in local memory");
			return;
		}

		dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);
		dma_channel_fast_waits(DMA_CHANNEL_GIF);

		// `_displayBufferIndex` is the buffer being RENDERED into, so the one handed to the read circuits
		// below is the other one - blank for the first frame, and swapped for good by PresentFrame()
		_displayBufferIndex = 0;
		_frame.address = GsVram::GetDisplayBufferPage(_displayBufferIndex) * WordsPerPage;
		_frame.width = DisplayWidth;
		_frame.height = DisplayHeight;
		_frame.psm = GS_PSM_16;
		_frame.mask = 0;

		// No Z buffer at all: the engine's queue is already in painter's order, so the test is ALWAYS with
		// writes masked and the pointer is never read or written. It can therefore alias the frame, which is
		// worth 560 KB of local memory - the single largest saving available in this layout.
		_zbuffer.enable = DRAW_DISABLE;
		_zbuffer.method = ZTEST_METHOD_ALLPASS;
		_zbuffer.address = _frame.address;
		_zbuffer.zsm = GS_PSMZ_16;
		_zbuffer.mask = 1;

		// The read circuits get the buffer NOT being rendered into. `graph_initialize` and
		// `graph_set_framebuffer_filtered` both take the address in the same 32-bit words `framebuffer_t`
		// uses (they shift it down by 11 for `DISPFB.FBP`), so the two agree with `draw_framebuffer` and no
		// unit conversion belongs between them.
		graph_initialize(std::int32_t(GsVram::GetDisplayBufferPage(_displayBufferIndex ^ 1) * WordsPerPage),
			DisplayWidth, DisplayHeight, _frame.psm, 0, 0);

		// PS2SDK's primitive helpers bake the ABE bit of the PRIM register they emit from a LIBRARY-WIDE
		// flag that starts out clear, so every `draw_rect_textured()` and `draw_rect_filled()` asks the
		// rasterizer for an unblended primitive until this is called. That is what made the picture read as
		// pure cutout - the ALPHA register below was programmed correctly all along and simply never
		// consulted, while the alpha test draw_setup_environment() leaves enabled discarded the fully
		// transparent texels. The two together are exactly "opaque or invisible, nothing in between".
		//
		// Enabling it here rather than overriding PRMODE per draw is deliberate: PRMODE applies to EVERY
		// primitive including the untextured strips draw_clear() emits, and forcing a textured/blended
		// PRMODE onto those made the clear sample whatever texture happened to be bound (a flat grey
		// screen). With the override left off, each helper's own PRIM governs and this flag is the one
		// knob that reaches it.
		draw_enable_blending();

		qword_t* q = Reserve(96);
		// draw_setup_environment() already programs XYOFFSET for a screen-space origin at the top left.
		// Setting it again here was wrong: the extra call left the origin at the centre of the 4096x4096
		// primitive space instead of the top-left of the display, which drew every sprite exactly half a
		// screen down and to the right while draw_clear() - which does not go through the same path - kept
		// covering the whole frame and hid the cause.
		//
		// It also leaves PRMODECONT selecting the PRIM register (`PRIM_OVERRIDE_DISABLE`), which is the
		// steady state the draw paths rely on. Nothing flips it afterwards: draw_clear() switches to PRMODE
		// for its own strips and switches back before returning.
		q = draw_setup_environment(q, 0, &_frame, &_zbuffer);

		// The ALPHA/TEST and SCISSOR registers are programmed from the requested state instead, per draw, by
		// ApplyBlending() and ApplyScissor() - what the environment left in them is not assumed to still hold
		_appliedBlendValid = false;
		_appliedScissorValid = false;
		_appliedTextureValid = false;

		q = draw_finish(q);
		_packetCursor = q;
		FlushPackets();
		draw_wait_finish();

		_gsInitialized = true;
	}

	void GsDevice::PresentFrame()
	{
		if (!_gsInitialized) {
			return;
		}

		qword_t* q = Reserve(8);
		q = draw_finish(q);
		_packetCursor = q;
		FlushPackets();
		draw_wait_finish();

		graph_wait_vsync();

		// The flip. The buffer just finished goes to the read circuits and the rasterizer moves to the other
		// one, so nothing is ever drawn into memory the CRT is scanning (see InitializeGs for what that cost
		// when there was only one). DISPFB is an EE register write rather than a GIF register, so it takes
		// effect immediately - which is why it is done here, after the GS is known to be finished and right
		// at the vertical blank, rather than queued in a packet.
		graph_set_framebuffer_filtered(std::int32_t(GsVram::GetDisplayBufferPage(_displayBufferIndex) * WordsPerPage),
			DisplayWidth, GS_PSM_16, 0, 0);

		_displayBufferIndex ^= 1;
		_frame.address = GsVram::GetDisplayBufferPage(_displayBufferIndex) * WordsPerPage;
		// FRAME has to follow the flip. Skipped while a render target owns the colour buffer - it restores
		// from `_frame` when it unbinds, which is now the new back buffer - though no frame ends that way.
		if (_currentRenderTarget == nullptr) {
			qword_t* qf = Reserve(32);
			qf = draw_framebuffer(qf, 0, &_frame);
			_packetCursor = qf;
		}

		_frameCounter++;
	}

	void GsDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width > 0 && height > 0) {
			_logicalWidth = width;
			_logicalHeight = height;
		}
	}

	void GsDevice::GetTargetScale(float& scaleX, float& scaleY, float& offsetX, float& offsetY)
	{
		offsetX = 0.0f;
		offsetY = 0.0f;
		if (_currentRenderTarget != nullptr) {
			// Render-to-texture passes render 1:1 into the target surface
			scaleX = 1.0f;
			scaleY = 1.0f;
		} else {
			scaleX = (_logicalWidth > 0 ? float(DisplayWidth) / float(_logicalWidth) : 1.0f);
			scaleY = (_logicalHeight > 0 ? float(DisplayHeight) / float(_logicalHeight) : 1.0f);
		}
	}

	// ------------------------------------------------------------------ state

	void GsDevice::SetBlendingEnabled(bool enabled) { _blending.Enabled = enabled; }
	void GsDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}
	GsDevice::BlendingState GsDevice::GetBlendingState() { return _blending; }
	void GsDevice::SetBlendingState(const BlendingState& state) { _blending = state; }

	void GsDevice::SetDepthTestEnabled(bool enabled) { _depthTest.TestEnabled = enabled; }
	void GsDevice::SetDepthMaskEnabled(bool enabled) { _depthTest.MaskEnabled = enabled; }
	GsDevice::DepthTestState GsDevice::GetDepthTestState() { return _depthTest; }
	void GsDevice::SetDepthTestState(const DepthTestState& state) { _depthTest = state; }

	void GsDevice::SetCullFaceEnabled(bool enabled) { _cullFace.Enabled = enabled; }
	GsDevice::CullFaceState GsDevice::GetCullFaceState() { return _cullFace; }
	void GsDevice::SetCullFaceState(const CullFaceState& state) { _cullFace = state; }

	GsDevice::ScissorState GsDevice::GetScissorState() { return _scissor; }
	void GsDevice::SetScissorState(const ScissorState& state) { _scissor = state; }
	void GsDevice::SetScissor(const Recti& rect)
	{
		// Same contract as the GL device: setting a rect also enables the test (RenderCommand and Viewport
		// rely on it and restore via SetScissorState afterwards)
		_scissor.Enabled = true;
		_scissor.Rect = rect;
	}
	void GsDevice::SetScissorTestEnabled(bool enabled) { _scissor.Enabled = enabled; }

	Recti GsDevice::GetViewport() { return _viewport; }
	void GsDevice::SetViewport(const Recti& rect) { _viewport = rect; }
	void GsDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
	}

	Colorf GsDevice::GetClearColor() { return _clearColor; }
	void GsDevice::SetClearColor(const Colorf& color) { _clearColor = color; }

	void GsDevice::ApplyScissor()
	{
		// Extent of the colour surface being rendered into, which is what "no scissor" has to pass
		std::int32_t extentW = DisplayWidth, extentH = DisplayHeight;
		if (_currentRenderTarget != nullptr) {
			if (const GsTexture* target = _currentRenderTarget->GetColorTexture(0); target != nullptr) {
				extentW = target->GetBufferPitch();
				extentH = target->GetPaddedHeight();
			}
		}

		// A render-target pass renders bottom-up (see the Y mirror in Dispatch), so a scissor rectangle
		// given in top-down logical coordinates does not describe the rows it would clip. Those passes
		// therefore pass the whole target - which is what they effectively did before this existed, minus
		// the stale rectangle left behind by the previous screen pass.
		if (!_scissor.Enabled || _currentRenderTarget != nullptr) {
			ApplyScissorArea(0, extentW - 1, 0, extentH - 1);
			return;
		}

		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const std::int32_t x0 = std::int32_t(float(_scissor.Rect.X) * scaleX + offsetX);
		const std::int32_t y0 = std::int32_t(float(_scissor.Rect.Y) * scaleY + offsetY);
		const std::int32_t x1 = std::int32_t(float(_scissor.Rect.X + _scissor.Rect.W) * scaleX + offsetX) - 1;
		const std::int32_t y1 = std::int32_t(float(_scissor.Rect.Y + _scissor.Rect.H) * scaleY + offsetY) - 1;
		ApplyScissorArea(x0, (x1 < extentW ? x1 : extentW - 1), y0, (y1 < extentH ? y1 : extentH - 1));
	}

	void GsDevice::Clear(ClearFlags flags)
	{
		static_cast<void>(flags);
		if (!_gsInitialized || _renderTargetSurfaceMissing) {
			return;
		}

		// A clear is bounded by the scissor test, exactly as glClear is - and this is also what stops it
		// inheriting the rectangle of whatever drew last
		ApplyScissor();

		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const float w = float(_currentRenderTarget != nullptr ? _viewport.W : _logicalWidth) * scaleX;
		const float h = float(_currentRenderTarget != nullptr ? _viewport.H : _logicalHeight) * scaleY;

		// Unlike the PowerVR - where the frame clear comes free with the scene background plane and an
		// explicit clear would push ~300k blended pixels through the translucent pipe - the GS has no
		// background plane, so a clear really is a filled rectangle. Its fill rate is enormous, which is why
		// that trade goes the other way here.
		// draw_clear() overrides PRMODE itself - untextured, unblended, fixed colour - for the duration of
		// its own strips and puts PRMODECONT back to the PRIM register before it returns, so the clear
		// cannot inherit the sprite path's primitive state and needs no override programmed around it.
		// It writes its rectangle with alpha 0x80, which passes the alpha test the blended modes leave on.
		qword_t* q = Reserve(48);
		q = draw_clear(q, 0, 0.0f, 0.0f, w, h,
			QuantizeChannel(_clearColor.R), QuantizeChannel(_clearColor.G), QuantizeChannel(_clearColor.B));
		_packetCursor = q;
	}

	// ------------------------------------------------------------------ draw entry points

	void GsDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		Dispatch(primitive, firstVertex, numVertices);
	}

	void GsDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, firstVertex, numVertices);
	}

	void GsDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	void GsDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	FenceHandle GsDevice::InsertFence()
	{
		// The device synchronises with the GS through FINISH inside FlushPackets(), so a fence is already
		// satisfied by the time it could be waited on
		return FenceHandle();
	}

	void GsDevice::DeleteFence(FenceHandle& fence)
	{
		fence = FenceHandle();
	}

	bool GsDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void GsDevice::SetupInitialState()
	{
		_blending = BlendingState();
		_depthTest = DepthTestState();
		_cullFace = CullFaceState();
		_scissor = ScissorState();
	}

	// ------------------------------------------------------------------ bindings

	void GsDevice::BindProgram(GsShaderProgram* program)
	{
		_currentProgram = program;
	}
	GsShaderProgram* GsDevice::CurrentProgram() { return _currentProgram; }

	void GsDevice::BindTexture(std::uint32_t unit, const GsTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void GsDevice::UnbindTexture(const GsTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
		// A destroyed texture must not stay referenced by a CLUT slot either, or a later slot-cache hit
		// would compare against freed memory
		for (std::uint32_t i = 0; i < MaxClutSlots; i++) {
			if (_clutSlots[i].Palette == texture) {
				_clutSlots[i].Palette = nullptr;
				_clutSlots[i].PaletteOffset = -1;
			}
		}
		if (_paletteTexture == texture) {
			_paletteTexture = nullptr;
		}
	}

	const GsTexture* GsDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void GsDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void GsDevice::SetRenderTarget(GsRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			return;
		}

		// Everything already submitted must reach the GS before the colour buffer moves under it.
		// draw_wait_finish() waits on the GS FINISH event, which is only ever raised by a draw_finish()
		// in the packet - waiting without having asked for one blocks forever, which is exactly what hung
		// the first time the menu (whose textured-background pass owns a render target) switched targets.
		// TEXFLUSH first: a target that has just been rendered into is sampled as a texture by the pass that
		// follows, and the GS's texture cache may still hold what was at those addresses before. The upload
		// path flushes for the same reason, but a render target never goes through it - nothing transfers
		// into it, the rasterizer writes it directly.
		QueueTextureFlush();
		qword_t* qf = Reserve(8);
		qf = draw_finish(qf);
		_packetCursor = qf;
		FlushPackets();
		draw_wait_finish();
		_currentRenderTarget = renderTarget;
		_renderTargetSurfaceMissing = false;

		framebuffer_t target = _frame;
		if (renderTarget != nullptr) {
			GsTexture* texture = renderTarget->GetColorTexture(0);
			if (texture == nullptr || texture->GetTexturePage() == GsVram::InvalidPage) {
				// Nothing to render into. FRAME is deliberately left where it was and the pass is dropped
				// instead: returning while _currentRenderTarget is set used to leave the colour buffer
				// pointing at the DISPLAY, so a target that failed to allocate did not lose its pass - it
				// drew the pass over the screen, bottom-up and unscissored, because everything downstream
				// reads _currentRenderTarget to decide the Y direction and the scissor extent
				_renderTargetSurfaceMissing = true;
				return;
			}
			target.address = texture->GetTexturePage() * WordsPerPage;
			target.width = texture->GetBufferPitch();
			target.height = texture->GetPaddedHeight();
			target.psm = GS_PSM_16;
			target.mask = 0;
		}

		qword_t* q = Reserve(32);
		q = draw_framebuffer(q, 0, &target);
		_packetCursor = q;
	}

	void GsDevice::UnbindRenderTarget(const GsRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			SetRenderTarget(nullptr);
		}
	}

	// ------------------------------------------------------------------ palettes

	void GsDevice::FlushPendingPackets()
	{
		FlushPackets();
	}

	void GsDevice::WritebackForDma(const void* start, std::size_t bytes)
	{
		if (start == nullptr || bytes == 0) {
			return;
		}

		// At most the whole data cache can be dirty, so a range larger than it is cheaper to clear by
		// walking the cache (128 lines) than by walking the range - which for a full-screen PSMCT32 upload
		// would be sixteen thousand line operations to write back at most eight kilobytes
		constexpr std::size_t DataCacheBytes = 8 * 1024;
		if (bytes <= DataCacheBytes) {
			void* first = const_cast<void*>(start);
			SyncDCache(first, static_cast<std::uint8_t*>(first) + bytes);
		} else {
			FlushCache(WRITEBACK_DCACHE);
		}
	}

	void GsDevice::RegisterPaletteTexture(GsTexture* texture)
	{
		_paletteTexture = texture;
		// A new palette texture invalidates everything keyed on the old one, so every row is restamped
		_paletteGeneration++;
		for (std::int32_t row = 0; row < MaxPaletteRows; row++) {
			_paletteRowStamp[row] = _paletteGeneration;
		}
	}

	void GsDevice::NotifyPaletteTextureChanged(GsTexture* texture, std::int32_t firstRow, std::int32_t rowCount)
	{
		if (texture != _paletteTexture) {
			return;
		}
		// Stamp only the rows that were uploaded. Everything keyed on a palette - the resident CLUTs, their
		// coverage forms and the RG8 bakes - carries the stamp of the rows it read (see
		// PaletteVersionForOffset), so a cache entry survives an upload that did not touch its entries. A
		// single global counter here would invalidate all of them on every animated-palette frame.
		_paletteGeneration++;
		const std::int32_t lastRow = (rowCount > 0 ? firstRow + rowCount : firstRow + 1);
		for (std::int32_t row = (firstRow > 0 ? firstRow : 0); row < lastRow && row < MaxPaletteRows; row++) {
			_paletteRowStamp[row] = _paletteGeneration;
		}
	}

	std::uint32_t GsDevice::PaletteVersionForOffset(const GsTexture* palette, std::int32_t paletteOffset)
	{
		if (palette != _paletteTexture) {
			return (palette != nullptr ? palette->GetContentVersion() : 0);
		}
		// The 256 entries starting at a flat offset span one row when it is row-aligned and two otherwise
		const std::int32_t firstRow = paletteOffset / 256;
		const std::int32_t lastRow = (paletteOffset + 255) / 256;
		std::uint32_t version = 0;
		for (std::int32_t row = firstRow; row <= lastRow; row++) {
			if (row >= 0 && row < MaxPaletteRows && _paletteRowStamp[row] > version) {
				version = _paletteRowStamp[row];
			}
		}
		return version;
	}

	const std::uint32_t* GsDevice::ResolvePaletteEntries(const GsTexture* palette, std::int32_t paletteOffset)
	{
		// The offset is a FLAT index into the palette texture's entries and does NOT have to be row-aligned:
		// the gem gradients pack two palettes into a single 256-entry row, so their offsets are 128, 256, 384
		// and 512. Reading it as a row index (and multiplying by 256 again) walks off the end of the palette
		// and is what made every gem invisible while row-0 sprites carried on working.
		const std::int32_t maxOffset = (palette != nullptr
			? palette->GetWidth() * palette->GetHeight() - 256 : 0);
		if (palette == nullptr || palette->GetPixels() == nullptr || paletteOffset < 0 || paletteOffset > maxOffset) {
			return nullptr;
		}
		return reinterpret_cast<const std::uint32_t*>(palette->GetPixels()) + paletteOffset;
	}

	std::uint32_t GsDevice::AcquireClutForOffset(const GsTexture* palette, std::int32_t paletteOffset)
	{
		const std::uint32_t* entries = ResolvePaletteEntries(palette, paletteOffset);
		if (entries == nullptr) {
			return GsVram::InvalidBlock;
		}
		return AcquireClut(palette, paletteOffset, PaletteVersionForOffset(palette, paletteOffset), entries);
	}

	std::uint32_t GsDevice::AcquireCoverageClutForOffset(const GsTexture* palette, std::int32_t paletteOffset)
	{
		const std::uint32_t* entries = ResolvePaletteEntries(palette, paletteOffset);
		if (entries == nullptr) {
			return GsVram::InvalidBlock;
		}
		return AcquireClut(palette, paletteOffset, PaletteVersionForOffset(palette, paletteOffset), entries, true);
	}

	std::uint32_t GsDevice::AcquireAlphaCoverageClut()
	{
		// Index i is a texel's own alpha byte (see the declaration: the store is sampled as PSMT8H), so
		// entry i is white at alpha i. The alphas the stores carry are ALREADY in the GS's 0..0x80
		// convention, so they pass straight through - which is why this goes in as a coverage payload
		// rather than through the usual 0xFF-to-0x80 rescale.
		static std::uint32_t entries[256];
		static bool built = false;
		if (!built) {
			built = true;
			for (std::int32_t i = 0; i < 256; i++) {
				entries[i] = 0x00FFFFFFu | (std::uint32_t(i) << 24);
			}
		}
		// Keyed on the palette-texture pointer being null, which no real palette ever is, and on a row
		// index no palette has - so it can never collide with a row's own coverage slot. Its version never
		// changes, so once it is in a slot it stays a cache hit for the rest of the run.
		return AcquireClut(nullptr, -2, 1, entries, true);
	}

	std::uint32_t GsDevice::AcquireClut(const GsTexture* palette, std::int32_t paletteOffset,
		std::uint32_t version, const std::uint32_t* entries, bool coverage)
	{
		_clutUseCounter++;

		// A slot already holding this row of this palette at this version is a hit; no transfer at all
		for (std::uint32_t i = 0; i < MaxClutSlots; i++) {
			ClutSlot& slot = _clutSlots[i];
			if (slot.Block != GsVram::InvalidBlock && slot.Palette == palette && slot.Coverage == coverage &&
				slot.PaletteOffset == paletteOffset && slot.PaletteVersion == version) {
				slot.LastUse = _clutUseCounter;
				return slot.Block;
			}
		}

		// Otherwise take a never-used slot, or the least recently used one. Unlike the PowerVR - which has
		// only four hardware banks and had to keep an LRU over them - the GS holds a CLUT per 1 KB of local
		// memory, so the slab is large enough that eviction is rare rather than every-draw.
		ClutSlot* target = nullptr;
		for (std::uint32_t i = 0; i < MaxClutSlots; i++) {
			ClutSlot& slot = _clutSlots[i];
			if (slot.Block == GsVram::InvalidBlock) {
				slot.Block = GsVram::AllocateClut();
				if (slot.Block == GsVram::InvalidBlock) {
					continue;
				}
			}
			if (slot.PaletteOffset == -1) {
				target = &slot;		// Never used, or invalidated by a palette upload
				break;
			}
			if (target == nullptr || slot.LastUse < target->LastUse) {
				target = &slot;
			}
		}
		if (target == nullptr || target->Block == GsVram::InvalidBlock) {
			return GsVram::InvalidBlock;
		}

		// The engine's palette entries are RGBA8 with 0xFF meaning opaque; the GS treats **0x80** as fully
		// opaque, so a straight copy makes every entry read as twice opaque and blend wrong. The colour
		// bytes are already in the GS's order (R in the low byte), so only alpha is rescaled.
		//
		// A COVERAGE payload keeps the alphas verbatim and forces every colour to white instead: it is what
		// turns MODULATE into a silhouette pass (see AcquireCoverageClutForRow), and its alphas either come
		// from a row that was already rescaled on its way in or ARE the indices themselves.
		for (std::int32_t i = 0; i < 256; i++) {
			const std::uint32_t entry = entries[i];
			const std::uint32_t alpha = (coverage ? (entry >> 24) : (((entry >> 24) + 1) >> 1));
			// A 256-entry CLUT in CSM1 is NOT stored linearly: the hardware reads it back through the same
			// 16x16 PSMCT32 block addressing a texture would use, which interleaves the middle two groups of
			// every 32 entries. Uploading in index order therefore gives every texel the wrong colour - the
			// symptom being a flat wash rather than anything recognisable. Entries whose bits 3-4 are 01 swap
			// with those 8 higher, and vice versa; the other two quarters are already in place.
			std::int32_t target = i;
			if ((i & 0x18) == 0x08) {
				target = i + 8;
			} else if ((i & 0x18) == 0x10) {
				target = i - 8;
			}
			// White here is 0xFF, not 0x80: a CLUT entry's colour is a full-range 0..255 texel, and it is
			// the FRAGMENT colour that runs on the GS's 0..0x80 scale. MODULATE then computes
			// (0xFF * Cf) >> 7 = 2*Cf, which maps the pass colour's 0x80 back onto a full-scale 0xFF - so a
			// white silhouette comes out white rather than half grey.
			_clutStaging[target] = (coverage ? 0x00FFFFFFu : (entry & 0x00FFFFFFu)) | (alpha << 24);
		}

		// 256 entries of 32 bits transferred as a 16x16 PSMCT32 image - the form the GS's CSM1 layout
		// expects, and the one the probe verified end to end. `draw_texture_transfer` builds a DMA CHAIN,
		// so this cannot be appended to the ordinary register packet - and the draws already queued there
		// have to reach the GS first, or they would sample the palette this is about to overwrite.
		FlushPackets();
		// ...and the 1 KB just written has to leave the data cache, or the DMA reads the previous palette
		WritebackForDma(_clutStaging, sizeof(_clutStaging));
		qword_t* q = _packet;
		q = draw_texture_transfer(q, _clutStaging, 16, 16, GS_PSM_32,
			std::int32_t(target->Block * WordsPerBlock), 16);
		q = draw_texture_flush(q);
		dma_channel_send_chain(DMA_CHANNEL_GIF, _packet, std::int32_t(q - _packet), 0, 0);
		dma_wait_fast();
		_packetCursor = _packet;
		// The contents behind this slot's CBP have just changed, and TEX0 is what reloads the GS's internal
		// CLUT buffer. A draw that is otherwise identical to the last one - same store, same block - would
		// skip the write and keep sampling the palette that was there, so the state cache is dropped here.
		// This is the ONLY place local memory behind a live CBP is rewritten, which is what lets ApplyTexture
		// treat an unchanged DrawState as safe everywhere else.
		_appliedTextureValid = false;

		target->Palette = palette;
		target->PaletteOffset = paletteOffset;
		target->PaletteVersion = version;
		// The payload KIND is part of the slot's identity, not an argument that only picks what to upload.
		// Leaving it out meant the two forms of a row shared one cache key: a coverage payload - white in
		// every entry - was stamped as that row's colours, so the next material draw of the row scored a hit
		// on it and came out white, and every silhouette draw (the hit flash, the shield, the outline)
		// poisoned one more slot. That is the "everything turns white after a while in a level" failure, and
		// the reason it took a while is that it needed a silhouette pass per palette row to get there.
		target->Coverage = coverage;
		target->LastUse = _clutUseCounter;
		return target->Block;
	}

	// ------------------------------------------------------------------ direct-tier lighting

	void GsDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
		std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
		bool waterActive, float waterLevelPx, float waterTime, float waterCamY)
	{
		PendingSoftwareLight entry;
		entry.Lightmap = lightmap;
		entry.LmW = lmW;
		entry.LmH = lmH;
		entry.Scale = scale;
		entry.VpX = vpX;
		entry.VpY = vpY;
		entry.VpW = vpW;
		entry.VpH = vpH;
		entry.AmbR = ambR;
		entry.AmbG = ambG;
		entry.AmbB = ambB;
		entry.WaterActive = waterActive;
		entry.WaterLevelPx = waterLevelPx;
		entry.WaterTime = waterTime;
		entry.WaterCamY = waterCamY;
		_pendingSoftwareLights.push_back(entry);
	}

	void GsDevice::EndFrame()
	{
		_pendingSoftwareLights.clear();
	}

	void GsDevice::ApplyPendingSoftwareLighting()
	{
		if (_pendingSoftwareLights.empty()) {
			return;
		}
		const PendingSoftwareLight light = _pendingSoftwareLights.front();
		_pendingSoftwareLights.erase(_pendingSoftwareLights.begin());

		const bool hasLighting = (light.Lightmap != nullptr && light.LmW > 0 && light.LmH > 0);
		const bool hasWater = light.WaterActive;
		if ((!hasLighting && !hasWater) || !_gsInitialized) {
			return;
		}

		ApplyScissor();

		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const float vpX = float(light.VpX) * scaleX + offsetX, vpY = float(light.VpY) * scaleY + offsetY;
		const float vpW = float(light.VpW) * scaleX, vpH = float(light.VpH) * scaleY;

		if (hasLighting) {
			/*
				The CPU lightmap composites as `scene * (r*(1 + g) + amb*(1 - r))` per channel - a per-pixel
				MULTIPLY of the destination, which the PVR and the GE both draw as a `dst * src` blend of a
				factor texture. The GS cannot: its one blend equation is `((A - B) * C) >> 7 + D` where C is an
				ALPHA (or a constant), never a colour, so no configuration of it multiplies the destination by
				a per-pixel colour.

				What it CAN do per pixel is `A = 0, B = Cd, C = As, D = Cd`, i.e. `Cd * (1 - As)` - an
				achromatic multiply driven by the source alpha. So the factor is reduced to one channel here
				and uploaded as an 8-bit surface whose texels ARE the alpha, read through the same
				alpha-to-coverage CLUT the silhouette passes use. That keeps the light SHAPE and the darkness,
				which is what the effect mostly is, and loses only the ambient HUE in unlit areas - the three
				channels of the real factor differ solely through the frame-constant ambient colour.

				Exact per-channel output is reachable at three times the cost: three of these passes, each
				carrying one channel's factor, with `FRAME.FBMSK` masking the other two. That is a fill-rate
				trade the GS could afford, but it needs three surfaces and three uploads a frame, and local
				memory is already the tight resource here - so it is the documented upgrade, not the default.

				One benefit falls out of the alpha form: a fully-lit texel has alpha 0, which the alpha test
				discards, so bright areas cost no blending at all.
			*/
			std::int32_t texW = 8, texH = 8;
			while (texW < light.LmW && texW < 1024) texW <<= 1;
			while (texH < light.LmH && texH < 1024) texH <<= 1;

			// PSMT8's page is 128x64 texels, and the pitch is padded to it exactly as a texture store is
			const std::int32_t pitch = GsVram::GetPaddedWidth(GsPsm::T8, texW);
			std::int32_t pageWidth, pageHeight;
			GsVram::GetPageGeometry(GsPsm::T8, pageWidth, pageHeight);
			const std::int32_t storeHeight = ((texH + pageHeight - 1) / pageHeight) * pageHeight;
			const std::uint32_t pageCount = GsVram::GetPageCount(GsPsm::T8, pitch, storeHeight);

			// The surface is allocated once and rewritten every frame, so it is held for the lifetime of the
			// session rather than churned - a per-frame allocation would compete with the texture cache for
			// pages at exactly the moment a level is already short of them
			if (_lightmapPage != GsVram::InvalidPage && _lightmapPageCount != pageCount) {
				GsVram::FreePages(_lightmapPage, _lightmapPageCount);
				_lightmapPage = GsVram::InvalidPage;
				_lightmapPageCount = 0;
			}
			if (_lightmapPage == GsVram::InvalidPage) {
				// Through GsTexture's allocator rather than GsVram's, so a cache that is momentarily full
				// evicts a sheet for it instead of refusing. The surface is one or two pages and the whole
				// scene is unlit without it, which is a far worse trade than one sheet being re-uploaded -
				// and because it is allocated once and held, the eviction is paid once too
				_lightmapPage = GsTexture::AllocatePages(pageCount, nullptr);
				if (_lightmapPage == GsVram::InvalidPage) {
					return;		// No room for the compositor surface; the scene stays unlit this frame
				}
				_lightmapPageCount = pageCount;
			}

			const std::uint32_t clutBlock = AcquireAlphaCoverageClut();
			if (clutBlock == GsVram::InvalidBlock) {
				return;
			}

			// Luminance weights of the ambient colour, so the one channel carried is the perceptual factor
			const float ambGray = 0.299f * light.AmbR + 0.587f * light.AmbG + 0.114f * light.AmbB;

			std::uint8_t* const surface = _lightmapStaging.Reserve(std::size_t(pitch) * std::size_t(storeHeight));
			if (surface == nullptr) {
				return;		// No main memory for the compositor surface; the scene stays unlit this frame
			}
			for (std::int32_t y = 0; y < light.LmH; y++) {
				const float* DEATH_RESTRICT src = light.Lightmap + std::size_t(y) * light.LmW * 2;
				std::uint8_t* DEATH_RESTRICT dst = surface + std::size_t(y) * pitch;
				// Unlit runs repeat the same pair of factors across long spans, so remembering the last
				// converted texel turns most of the surface into a compare and a store
				float prevR = -1.0f, prevG = -1.0f;
				std::uint8_t prevTexel = 0;
				for (std::int32_t x = 0; x < light.LmW; x++) {
					const float rawR = src[x * 2];
					const float rawG = src[x * 2 + 1];
					if (rawR == prevR && rawG == prevG) {
						dst[x] = prevTexel;
						continue;
					}
					prevR = rawR;
					prevG = rawG;
					const float r = (rawR < 0.0f ? 0.0f : (rawR > 1.0f ? 1.0f : rawR));
					const float g = (rawG < 0.0f ? 0.0f : (rawG > 1.0f ? 1.0f : rawG));
					// The stored value is the ATTENUATION (1 - factor), because the equation subtracts it
					prevTexel = QuantizePassChannel(1.0f - (r * (1.0f + g) + ambGray * (1.0f - r)));
					dst[x] = prevTexel;
				}
				// The padding columns are reached by the bilinear tap at the last texel
				for (std::int32_t x = light.LmW; x < pitch; x++) {
					dst[x] = prevTexel;
				}
			}
			for (std::int32_t y = light.LmH; y < storeHeight; y++) {
				std::memcpy(surface + std::size_t(y) * pitch, surface + std::size_t(light.LmH - 1) * pitch,
					std::size_t(pitch));
			}

			// The transfer builds a DMA CHAIN, so it goes out on its own after the pending register packet -
			// and the surface the CPU has just filled has to be written back for the DMA to see it
			FlushPackets();
			WritebackForDma(surface, std::size_t(pitch) * std::size_t(storeHeight));
			qword_t* qu = _packet;
			qu = draw_texture_transfer(qu, surface, pitch, storeHeight, GS_PSM_8,
				std::int32_t(_lightmapPage * WordsPerPage), pitch);
			qu = draw_texture_flush(qu);
			dma_channel_send_chain(DMA_CHANNEL_GIF, _packet, std::int32_t(qu - _packet), 0, 0);
			dma_wait_fast();
			_packetCursor = _packet;

			DrawState state;
			state.Page = _lightmapPage;
			state.ClutBlock = clutBlock;
			state.BufferPitch = pitch;
			state.Psm = GS_PSM_8;
			state.SampledWidth = texW;
			state.SampledHeight = texH;
			state.WrapU = WRAP_CLAMP;
			state.WrapV = WRAP_CLAMP;
			// The lightmap is one texel per `light.Scale` pixels, so it is interpolated rather than blocky
			state.MagFilter = LOD_MAG_LINEAR;
			state.MinFilter = LOD_MIN_LINEAR;

			BlendEquation equation;
			ResolveBlendEquation(nCine::BlendingFactor::Zero, nCine::BlendingFactor::OneMinusSrcAlpha, equation);
			ApplyBlendEquation(equation);

			// The lightmap's row 0 is the BOTTOM of the displayed viewport (the software buffer convention),
			// so V runs used -> 0 from top to bottom. Corner indexing is the sprite path's (ax, ay) weights.
			const float uMax = float(light.LmW) / float(texW);
			const float vMax = float(light.LmH) / float(texH);
			const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
			const float py[4] = { vpY, vpY + vpH, vpY, vpY + vpH };
			const float pu[4] = { uMax, uMax, 0.0f, 0.0f };
			const float pv[4] = { vMax, 0.0f, vMax, 0.0f };
			SubmitQuadPrimitive(state, px, py, pu, pv, PackPassColor(OpaqueWhite), 0.0f, 0.0f, true);
		}

		if (hasWater) {
			// Water v1: constant underwater tint band + above-deep-water darkening (shared with the PVR and
			// the GE), both flat source-over quads
			BlendEquation equation = SourceOverEquation();
			ApplyBlendEquation(equation);

			DrawState untextured;
			const float waterTop = vpY + light.WaterLevelPx * scaleY;
			const float uv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if (waterTop < vpY + vpH) {
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { waterTop, vpY + vpH, waterTop, vpY + vpH };
				const float tint[4] = { 0.4f, 0.6f, 0.8f, 0.4f };
				SubmitQuadPrimitive(untextured, px, py, uv, uv, PackVertexColor(tint), 0.0f, 0.0f, true);
				}
			const float waterLevelNorm = (light.VpH > 0 ? light.WaterLevelPx / float(light.VpH) : 1.0f);
			if (waterLevelNorm < 0.4f && waterTop > vpY) {
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { vpY, waterTop, vpY, waterTop };
				const float above[4] = { light.AmbR, light.AmbG, light.AmbB, 0.4f - waterLevelNorm };
				SubmitQuadPrimitive(untextured, px, py, uv, uv, PackVertexColor(above), 0.0f, 0.0f, true);
				}
		}
	}

	// ------------------------------------------------------------------ fixed-function quad effects
	//
	// The quad-family effects are expressed as FixedFunctionPass descriptors handed to this EffectContext -
	// the structural contract documented in FixedFunctionPass.h, implemented here against the GIF
	// submission helpers above. The per-effect functions themselves are GENERATED from the shaders'
	// void fixed_function([gs]) blocks by the ShaderCompiler, exactly as on the PVR, the GX and the GU
	// (Shaders/Generated/GsGeneratedEffects.h, included below), so this file contains no effect-specific
	// code at all.
	//
	// EffectContext deliberately has EXTERNAL linkage (namespace scope, though it is still defined only in
	// this translation unit), for the same reason as on the other consoles: the effect-table struct is at
	// namespace scope - so GsShaderProgram can forward-declare it and hold a typed entry pointer - and
	// names EffectContext in a member type.

	struct EffectContext
	{
		// Matches the GX's and the GU's capacity rather than the PVR's: one GIF packet carries a triangle
		// strip of any length, so there is nothing to gain from splitting the geometry into small pieces
		static constexpr std::int32_t MaxStripVertices = 16;

		// Decoded instance data of the draw being dispatched (the documented contract)
		const float* InstanceColor;
		float TexelW;
		float TexelH;
		bool Batched;

		// The texture state this instance resolved to, its COVERAGE form for silhouette passes (null when
		// the store has none - see ResolveCoverageState) and the equation the material's blending resolved
		// to, which a pass's BlendMode overrides
		const DrawState* Material;
		const DrawState* Coverage;
		BlendEquation MaterialBlend;

		// The instance's corner arrays: raster positions, and NORMALIZED texture coordinates over the
		// sampled (padded) extent
		const float* Px;
		const float* Py;
		const float* Pu;
		const float* Pv;
		const float* TexRect;
		// Whether the four corners form an axis-aligned rectangle. They usually do under an orthographic
		// projection, which is what lets a pass go out as one GS SPRITE; a rotated one becomes two triangles
		// instead (see SubmitQuadPrimitive).
		bool AxisAligned;

		// Pre-clip quad geometry (the quad_origin/quad_axis_* built-ins), the program (for resolved
		// uniforms) and the conversion from the shader's normalized texture space into texels
		float OriginX, OriginY;
		float AxisXx, AxisXy;
		float AxisYx, AxisYy;
		const GsShaderProgram* Program;
		// The shader's texture space is normalized over the REAL extent; the primitives want it normalized
		// over the SAMPLED (padded power-of-two) one, which is what this scale folds in
		float UvScaleU, UvScaleV;

		// The strip builder scratch. Colours are packed at set time (same quantization as the quad path, so
		// identical float inputs produce identical vertex words) and UVs converted to texels there too.
		float StripX[MaxStripVertices];
		float StripY[MaxStripVertices];
		float StripU[MaxStripVertices];
		float StripV[MaxStripVertices];
		std::uint64_t StripRgbaq[MaxStripVertices];

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
		// Per-vertex colours are only ever read by SubmitStripShaded(), whose strip is untextured by
		// construction, so they are packed on the full-range scale that primitive's RGB reaches the blender on
		void SetStripVertexColor(std::int32_t i, float r, float g, float b, float a)
		{
			if (std::uint32_t(i) < std::uint32_t(MaxStripVertices)) {
				const float rgba[4] = { r, g, b, a };
				StripRgbaq[i] = PackVertexColor(rgba).rgbaq;
			}
		}

		// Whether a UV span can be mapped onto the screen at all (a zero texRect has no scale)
		bool HasTexelStep() const { return TexRect[0] != 0.0f && TexRect[2] != 0.0f; }
		// Maps a span in the sprite's UV space onto the quad's on-screen extent - the texel step the
		// Outline ring taps use. The corners are already in raster space, so the result is a raster
		// displacement.
		float TexelToRasterX(float uvSpan) const { return (Px[0] - Px[2]) * (uvSpan / TexRect[0]); }
		float TexelToRasterY(float uvSpan) const { return (Py[1] - Py[0]) * (uvSpan / TexRect[2]); }
		// The documented texel_size() built-in of the fixed_function contract: the Outline shader family
		// carries the sprite's UV-space texel size in its instance color.xy (exactly like the GLSL derives
		// its tap offsets), folded through the raster conversion above
		float TexelStepX() const { return TexelToRasterX(InstanceColor[0]); }
		float TexelStepY() const { return TexelToRasterY(InstanceColor[1]); }

		/*
			One quad draw. @p silhouette selects the store's COVERAGE form - the pass colour wherever the
			texture has alpha - which the GS reaches by swapping the CLUT rather than the texture function
			(see GsDevice::AcquireCoverageClutForRow). A store with no coverage form falls back to the
			modulated one, which shows the sprite's own colours where a flat tone belongs; that is only
			reachable for a PSMCT16 render target, whose 1-bit alpha carries no coverage to sample, and no
			effect that offsets a colour draws over one.
		*/
		void SubmitQuadPass(const FixedFunctionPass& pass, bool silhouette)
		{
			const DrawState* state = Material;
			if (silhouette) {
				if (Coverage != nullptr) {
					state = Coverage;
				} else {
					static bool warnedNoCoverage = false;
					if (!warnedNoCoverage) {
						warnedNoCoverage = true;
						LOGW("A silhouette pass has no coverage form of its store, drawing it modulated");
					}
				}
			}
			ApplyBlendEquation(PassEquation(MaterialBlend, pass.Blend));
			SubmitQuadPrimitive(*state, Px, Py, Pu, Pv, PackStateColor(*state, pass.Color),
				pass.ScreenOffset[0], pass.ScreenOffset[1], AxisAligned);
		}

		/*
			Submits one pass over the current instance's quad. A pass carrying an offset colour is EXPANDED
			here, because the GS has no post-texture additive term for a COLOUR: of its four texture
			functions, MODULATE has no additive stage at all and both HIGHLIGHT forms can only add the
			fragment ALPHA broadcast to all three channels, so `texel*colour + offset` is not any single GS
			draw. Doing the expansion in the mechanism (rather than spelling both passes in every shader's
			gs block) is what keeps the portable core portable - the same generic block still describes the
			effect on all four consoles, exactly as the GX reinterprets an offset colour as its silhouette
			form and the GU splits it the same way this does.

			The expansion is EXACT, not an approximation. With a = texel.a * colour.a, the PVR's single draw
			over the destination dst is (its blend being SRCALPHA + INVSRCALPHA):

				dst*(1 - a) + a*(texel*colour.rgb + offset)

			and the two draws below produce, in order,

				pass 1 (modulate, the pass's own blend):   dst1 = dst*(1 - a) + a*texel*colour.rgb
				pass 2 (silhouette, additive):             dst2 = dst1 + a*offset

			whose sum is the same expression term for term. Pass 2 samples the coverage form, so its colour
			is the offset colour with no texel dependence and its coverage is still the texel's own alpha;
			its ADDITIVE blend is what keeps it from attenuating the destination a second time.

			When colour.rgb is zero - the "pure offset colour" idiom of the mask/outline/shield family,
			where the offset colour IS the effect - the modulate term vanishes and the pair collapses to ONE
			draw: the silhouette under the pass's own (alpha-over) blend already computes
			dst*(1 - a) + a*offset. That is the common case, so those effects cost the PVR's draw count.
		*/
		void SubmitQuad(const FixedFunctionPass& pass)
		{
			if (pass.HasOffsetColor) {
				const bool modulateVisible = (pass.Color[0] != 0.0f || pass.Color[1] != 0.0f || pass.Color[2] != 0.0f);
				if (modulateVisible) {
					FixedFunctionPass modulate = pass;
					modulate.HasOffsetColor = false;
					SubmitQuadPass(modulate, false);
				}
				FixedFunctionPass silhouette = pass;
				silhouette.HasOffsetColor = false;
				silhouette.Color[0] = pass.OffsetColor[0];
				silhouette.Color[1] = pass.OffsetColor[1];
				silhouette.Color[2] = pass.OffsetColor[2];
				// Only the second half of a split pair is additive; a collapsed one keeps the pass's blend
				// (the alpha-over the effects rely on) so it still attenuates the destination itself
				if (modulateVisible) {
					silhouette.Blend = FixedFunctionPass::BlendMode::Additive;
				}
				SubmitQuadPass(silhouette, true);
				return;
			}
			// Modulate and Silhouette are the two presets this hardware realises; the output scales are
			// rejected for the gs target at generation time and the other two are GX-only
			SubmitQuadPass(pass, pass.Tev == FixedFunctionPass::TevPreset::Silhouette);
		}

		// Textured strip out of the builder scratch: the pass's flat colour over the material state
		void SubmitStrip(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			// Same intent mapping as the GX's and the GU's SubmitStrip: with no post-texture colour add on
			// this hardware, an offset colour becomes the silhouette form filled flat with it at the pass
			// alpha. Deliberately NOT split into the exact two draws SubmitQuad builds - a strip's geometry
			// would have to be resubmitted, and no effect asks for it (both strip users, the iris and the
			// warp bands, carry no offset colour at all).
			FixedFunctionPass effective = pass;
			const bool silhouette = (pass.HasOffsetColor || pass.Tev == FixedFunctionPass::TevPreset::Silhouette);
			if (pass.HasOffsetColor) {
				effective.HasOffsetColor = false;
				effective.Color[0] = pass.OffsetColor[0];
				effective.Color[1] = pass.OffsetColor[1];
				effective.Color[2] = pass.OffsetColor[2];
			}
			const DrawState* state = (silhouette && Coverage != nullptr ? Coverage : Material);
			ApplyBlendEquation(PassEquation(MaterialBlend, effective.Blend));
			SubmitStripPrimitive(*state, StripX, StripY, StripU, StripV, nullptr,
				PackStateColor(*state, effective.Color), count, pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		// Shaded (per-vertex-colour) strip out of the builder scratch: always UNTEXTURED - a gradient has no
		// texture to modulate - whose blend comes from the pass
		void SubmitStripShaded(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			DrawState untextured = *Material;
			untextured.Page = GsVram::InvalidPage;
			ApplyBlendEquation(PassEquation(MaterialBlend, pass.Blend));
			SubmitStripPrimitive(untextured, StripX, StripY, nullptr, nullptr, StripRgbaq,
				color_t{}, count, pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}
	};
}

// The per-effect functions themselves are GENERATED: the ShaderCompiler transpiles each shader's
// fixed_function block into C++ over the EffectContext defined above (the type name itself is the
// "using EffectContext = ...;" alias the header expects). Included at global scope because the header
// opens nCine::RHI::GS itself. Programs with no fixed_function block are absent from its table and
// their draws are skipped with a one-time warning, exactly as on the other consoles - which is also what
// keeps the CPU-lightmap tier (Lighting, whose Combine hook IS in the table) from painting its light
// quads over the scene.
#include "../../../../Shaders/Generated/GsGeneratedEffects.h"

namespace nCine::RHI::GS
{
	const FixedFunctionGeneratedEffect* GsDevice::FindGeneratedEffect(const char* program, const char* variant)
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

	// ------------------------------------------------------------------ dispatch

	void GsDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		static_cast<void>(firstVertex);
		if (_currentProgram == nullptr || numVertices <= 0 || !_gsInitialized) {
			return;
		}
		if (_renderTargetSurfaceMissing) {
			// The bound target has no colour surface, so FRAME is still on the previous one (see
			// SetRenderTarget). Every entry point below funnels through here, so this is the one guard needed
			return;
		}

		// The program's own shader file says what it draws: either a transpiled effect function or the
		// backend pipeline stage a `pipeline <name>;` block bound it to. A program with neither has no
		// fixed_function block at all, which is how the desktop-only post-process programs and the
		// CPU-lightmap tier's light quads stay off the screen rather than being matched by name here.
		const FixedFunctionGeneratedEffect* generated = _currentProgram->GetGeneratedEffect();
		if (generated == nullptr) {
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": it has no fixed_function block for the gs target",
					_currentProgram->GetObjectLabel());
			}
			return;
		}

		const FixedFunctionIntrinsic intrinsic = generated->Intrinsic;
		if (intrinsic != FixedFunctionIntrinsic::None) {
			if (intrinsic == FixedFunctionIntrinsic::LightingCombine) {
				ApplyPendingSoftwareLighting();
			} else if (intrinsic == FixedFunctionIntrinsic::TileMapMesh) {
				DispatchTileMesh(primitive, firstVertex, numVertices);
			} else if (intrinsic == FixedFunctionIntrinsic::LineStripMesh) {
				DispatchLineStrip(firstVertex, numVertices);
			}
			return;
		}

		if (primitive != PrimitiveType::TriangleStrip && primitive != PrimitiveType::Triangles) {
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": the quad effect path takes only triangle primitives",
					_currentProgram->GetObjectLabel());
			}
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		const GsUniformBlock* block = _currentProgram->FindBlock("InstanceBlock");
		if (block == nullptr) {
			block = _currentProgram->FindBlock("InstancesBlock");
		}
		if (block == nullptr) {
			return;
		}
		std::int32_t binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		const std::uint8_t* blockData = _boundUniformRanges[binding].Data;
		if (blockData == nullptr) {
			return;
		}

		// The reflection is the only source of truth for the instance layout: a strided block means a batched
		// program, a declared `uTexture` means the draw samples one, and a declared `texRect` means the
		// textured member offsets apply even when nothing is sampled (the Transition carries one)
		const ShaderCompiler::ProgramVariant* reflection = _currentProgram->GetReflection();
		std::uint32_t instanceStride = 0;
		bool hasTexture = false;
		bool texturedLayout = false;
		if (reflection != nullptr) {
			for (std::size_t i = 0; i < reflection->BlockCount; i++) {
				if (reflection->Blocks[i].InstanceStride > 0) {
					instanceStride = reflection->Blocks[i].InstanceStride;
					break;
				}
			}
			for (std::size_t i = 0; i < reflection->TextureCount; i++) {
				if (std::strcmp(reflection->Textures[i].Name, "uTexture") == 0) {
					hasTexture = true;
					break;
				}
			}
			texturedLayout = hasTexture;
			for (std::size_t i = 0; i < reflection->BlockCount && !texturedLayout; i++) {
				const ShaderCompiler::UniformBlock& b = reflection->Blocks[i];
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					if (std::strcmp(b.Members[j].Name, "texRect") == 0) {
						texturedLayout = true;
						break;
					}
				}
			}
		}

		GsTexture* texture = (hasTexture ? const_cast<GsTexture*>(_boundTextures[0]) : nullptr);
		if (hasTexture && texture == nullptr) {
			return;
		}
		// A program with no `uTexture` draws flat-colour geometry (the menu dimmer, panels, separators). It
		// takes the same instance decode and corner synthesis as a textured draw, with an untextured
		// primitive at the end of it, so there is nothing to branch on here - only the DrawState differs.

		const bool batched = (instanceStride > 0);
		std::int32_t numInstances = 1;
		if (batched) {
			numInstances = numVertices / 6;
			if (numInstances < 1) {
				numInstances = 1;
			}
			const std::uint32_t rangeSize = _boundUniformRanges[binding].Size;
			if (rangeSize > 0 && std::uint32_t(numInstances) * instanceStride > rangeSize) {
				numInstances = std::int32_t(rangeSize / instanceStride);
			}
		}

		float pv[16];
		Mat4Mul(projMat, viewMat, pv);

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);

		// Constant NDC-to-raster mapping, folded in once rather than reapplied per corner. The GS scans out
		// its buffer top-down, so screen passes mirror NDC exactly as the PVR backend does.
		const bool screenPass = (_currentRenderTarget == nullptr);
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX + offsetX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY + offsetY;

		// The GS has a HARDWARE scissor, so unlike the PowerVR there is no geometric quad clipping here.
		// Both of these are register state shared by every pass of every instance in the draw.
		ApplyScissor();
		const BlendEquation materialBlend = MaterialEquation(_blending);

		const std::int32_t sampledWidth = (texture != nullptr ? texture->GetPaddedWidth() : 8);
		const std::int32_t sampledHeight = (texture != nullptr ? texture->GetPaddedHeight() : 8);
		const float uvScaleU = (texture != nullptr ? texture->GetUScale() : 1.0f);
		const float uvScaleV = (texture != nullptr ? texture->GetVScale() : 1.0f);

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

			// The corner weights are 0 or 1, so the sprite's raster extent is its transformed axes scaled by
			// its size and the corners are sums of those (identical to the PVR synthesis)
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

			// An axis-aligned quad - the overwhelming majority under an orthographic projection - goes out as
			// one GS SPRITE through draw_rect_textured(), the encoding tests/GsProbe.cpp validated on
			// hardware. A rotated one cannot: SPRITE takes two opposite corners and is axis-aligned by
			// construction, so SubmitQuadPrimitive() draws those as two triangles instead. They used to be
			// skipped, which is what left the legacy menu background (three counter-rotating tiled layers)
			// completely black.
			const bool axisAligned = (px[0] == px[1] && px[2] == px[3] && py[0] == py[2] && py[1] == py[3]);
			if (!axisAligned) {
			}

			DrawState material;
			DrawState coverage;
			bool hasCoverage = false;
			const GsTexture* paletteTex = nullptr;
			std::int32_t paletteOffset = 0;
			if (texture != nullptr) {
				// RG8 is an index plus a PER-PIXEL alpha, which a PSMT8 texel cannot express - its alpha comes
				// from the CLUT entry, so every texel sharing an index would share an alpha. Those textures are
				// baked to PSMCT32 through one palette row instead, exactly as the PowerVR backend does.
				const bool needsBake = texture->NeedsPaletteBake();
				if (needsBake || texture->IsIndexed()) {
					// Unit 1 is only the palette when the PROGRAM says it samples one. `Material::Bind()`
					// leaves units above the ones a material uses exactly as the previous material left
					// them - which is correct for a shader that never reads them, and would have this
					// backend take a leftover sheet for a palette on every indexed draw whose program has
					// no palette sampler at all (the colorized HUD text is the common one).
					paletteTex = _paletteTexture;
					if (_currentProgram->UsesPalette()) {
						if (_boundTextures[1] != nullptr && _boundTextures[1] != texture) {
							paletteTex = _boundTextures[1];
						}
						float palOffset = 0.0f;
						std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
						paletteOffset = std::int32_t(palOffset + 0.5f);
					}
				}

				std::uint32_t page;
				if (needsBake) {
					const std::uint32_t* entries = ResolvePaletteEntries(paletteTex, paletteOffset);
					if (entries == nullptr) {
						continue;
					}
					page = texture->EnsureBakedColor(entries, std::uint32_t(paletteOffset),
						PaletteVersionForOffset(paletteTex, paletteOffset), paletteTex);
				} else {
					page = texture->AcquireTexturePage();
				}
				if (page == GsVram::InvalidPage) {
							continue;
				}

				material.Page = page;
				// In TEXELS - libdraw divides by 64 itself; a TBW value here would sample a buffer 64 times
				// too narrow (see GsVram::GetPaddedWidth)
				material.BufferPitch = (needsBake ? texture->GetBakedBufferPitch() : texture->GetBufferPitch());
				// GsPsm's enumerators ARE the hardware PSM values, so the store's own mode goes straight into
				// the field. It has to: a render target is a PSMCT16 colour surface, and a "PSMT8 or else
				// PSMCT32" test would sample one as PSMCT32 - two texels read as one.
				material.Psm = (needsBake ? GS_PSM_32 : std::int32_t(texture->GetPsm()));
				material.SampledWidth = sampledWidth;
				material.SampledHeight = sampledHeight;
				material.WrapU = MapWrapGs(texture->GetWrapS());
				material.WrapV = MapWrapGs(texture->GetWrapT());
				material.MagFilter = MapMagFilterGs(texture->GetMagFiltering());
				material.MinFilter = MapMinFilterGs(texture->GetMinFiltering());
				if (!needsBake && texture->IsIndexed()) {
					material.ClutBlock = AcquireClutForOffset(paletteTex, paletteOffset);
					if (material.ClutBlock == GsVram::InvalidBlock) {
						continue;
					}
				}

				// The coverage form a silhouette pass samples, resolved only for the effects that can ask for
				// one. UsesOffsetColor is exactly "some reachable pass writes an offset colour", which is what
				// SubmitQuad expands into a silhouette - so it is also the answer to "can this effect need
				// coverage", and the masks and outlines are the only draws that pay for the extra CLUT.
				if (generated->UsesOffsetColor) {
					coverage = material;
					if (material.Psm == GS_PSM_8 && paletteTex != nullptr) {
						const std::uint32_t clut = AcquireCoverageClutForOffset(paletteTex, paletteOffset);
						if (clut != GsVram::InvalidBlock) {
							coverage.ClutBlock = clut;
							hasCoverage = true;
						}
					} else if (material.Psm == GS_PSM_32) {
						const std::uint32_t clut = AcquireAlphaCoverageClut();
						if (clut != GsVram::InvalidBlock) {
							coverage.Psm = GS_PSM_8H;
							coverage.ClutBlock = clut;
							hasCoverage = true;
						}
					}
				}
			}

			EffectContext ctx;
			ctx.InstanceColor = color;
			ctx.TexelW = (texture != nullptr && texture->GetWidth() > 0 ? 1.0f / float(texture->GetWidth()) : 0.0f);
			ctx.TexelH = (texture != nullptr && texture->GetHeight() > 0 ? 1.0f / float(texture->GetHeight()) : 0.0f);
			ctx.Batched = batched;
			ctx.Material = &material;
			ctx.Coverage = (hasCoverage ? &coverage : nullptr);
			ctx.MaterialBlend = materialBlend;
			ctx.Px = px;
			ctx.Py = py;
			ctx.Pu = pu;
			ctx.Pv = pvv;
			ctx.TexRect = texRect;
			ctx.AxisAligned = axisAligned;
			// PRE-CLIP quad geometry: the raster origin of the sprite's (0,0) corner and the raster
			// displacements of its local axes. Deliberately the synthesis inputs rather than the corner
			// array, so geometry built from them cannot be distorted by a clipped quad.
			ctx.OriginX = originX;
			ctx.OriginY = originY;
			ctx.AxisXx = spanXx;
			ctx.AxisXy = spanXy;
			ctx.AxisYx = spanYx;
			ctx.AxisYy = spanYy;
			ctx.Program = _currentProgram;
			// Strip UVs arrive in the shader's texture space, so only the padded-store scale is folded in
			ctx.UvScaleU = uvScaleU;
			ctx.UvScaleV = uvScaleV;

			generated->Fn(ctx);

		}
	}

	void GsDevice::DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		// A tile-layer mesh is a plain triangle list of 8-float vertices (position.xy, texcoords.uv,
		// color.rgba) - the layout TileMap::AppendTileQuad() writes and TileMapVs.inc declares. It is a hard
		// contract of this shader family exactly like the std140 instance block is of the sprite one.
		constexpr std::int32_t FloatsPerVertex = 8;
		if (primitive != PrimitiveType::Triangles || numVertices < 3 || !_gsInitialized) {
			return;
		}

		const GsBuffer* vbo = _currentProgram->GetBoundVbo();
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

		const GsUniformBlock* block = _currentProgram->FindBlock("InstanceBlock");
		if (block == nullptr) {
			return;
		}
		std::int32_t binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		const std::uint8_t* blockData = _boundUniformRanges[binding].Data;
		if (blockData == nullptr) {
			return;
		}

		GsTexture* texture = const_cast<GsTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		float pvm[16];
		Mat4Mul(projMat, viewMat, pvm);
		Transform2D mvp;
		Mat4MulTransform2D(pvm, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// The layer tint modulates every vertex colour, which already carries the per-tile alpha
		float layerColor[4];
		std::memcpy(layerColor, blockData + kColorOffset, sizeof(layerColor));

		// The palette to remap with is whatever the material bound to the palette sampler; the registered
		// global palette is the fallback (mirrors the sprite path). TileMapMeshPalette binds uTexturePalette
		// in its reflection, which is exactly what UsesPalette() reports.
		const bool isPaletteRemap = _currentProgram->UsesPalette();
		const GsTexture* paletteTex = nullptr;
		if (isPaletteRemap || texture->IsIndexed() || texture->NeedsPaletteBake()) {
			paletteTex = _paletteTexture;
			if (isPaletteRemap && _boundTextures[1] != nullptr && _boundTextures[1] != texture) {
				paletteTex = _boundTextures[1];
			}
		}

		// Unlike a sprite batch, the whole mesh shares one texture and one palette offset, so residency, the
		// CLUT and the GS state are resolved once for the entire layer. The offset is a flat entry index, not
		// a row (see ResolvePaletteEntries).
		std::int32_t paletteOffset = 0;
		if (isPaletteRemap) {
			float palOffset = 0.0f;
			std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
			paletteOffset = std::int32_t(palOffset + 0.5f);
		}

		DrawState state;
		const bool needsBake = texture->NeedsPaletteBake();
		std::uint32_t page;
		if (needsBake) {
			const std::uint32_t* entries = ResolvePaletteEntries(paletteTex, paletteOffset);
			if (entries == nullptr) {
				return;
			}
			page = texture->EnsureBakedColor(entries, std::uint32_t(paletteOffset),
				PaletteVersionForOffset(paletteTex, paletteOffset), paletteTex);
		} else {
			page = texture->AcquireTexturePage();
		}
		if (page == GsVram::InvalidPage) {
			return;
		}
		state.Page = page;
		state.BufferPitch = (needsBake ? texture->GetBakedBufferPitch() : texture->GetBufferPitch());
		state.Psm = (needsBake ? GS_PSM_32 : std::int32_t(texture->GetPsm()));
		state.SampledWidth = texture->GetPaddedWidth();
		state.SampledHeight = texture->GetPaddedHeight();
		state.WrapU = MapWrapGs(texture->GetWrapS());
		state.WrapV = MapWrapGs(texture->GetWrapT());
		state.MagFilter = MapMagFilterGs(texture->GetMagFiltering());
		state.MinFilter = MapMinFilterGs(texture->GetMinFiltering());
		if (!needsBake && texture->IsIndexed()) {
			state.ClutBlock = AcquireClutForOffset(paletteTex, paletteOffset);
			if (state.ClutBlock == GsVram::InvalidBlock) {
				return;
			}
		}

		ApplyScissor();
		ApplyBlendEquation(MaterialEquation(_blending));

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const bool screenPass = (_currentRenderTarget == nullptr);

		// The NDC-to-raster mapping is affine and constant for the whole mesh, so it is folded into the
		// transform once instead of being reapplied per vertex. A screen pass mirrors NDC, which is just the
		// sign of the Y scale.
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX + offsetX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY + offsetY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};
		// Axis-alignment is a property of the layer transform, not of an individual tile, so it is decided
		// once: with no rotation or shear every tile quad is a GS SPRITE, which costs one primitive instead
		// of the two triangles it would otherwise take.
		const bool axisAligned = (raster.Xy == 0.0f && raster.Yx == 0.0f);

		const float uvScaleU = texture->GetUScale();
		const float uvScaleV = texture->GetVScale();
		auto project = [&](const float* v, float& outX, float& outY, float& outU, float& outV) {
			outX = raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx;
			outY = raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty;
			outU = v[2] * uvScaleU;
			outV = v[3] * uvScaleV;
		};

		const std::int32_t triangleCount = numVertices / 3;
		std::int32_t triangle = 0;
		// Virtually every tile of a layer carries the same colour (white at the layer's alpha), so the four
		// clamp-and-quantize steps run once per change instead of once per tile
		float lastColor[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
		color_t packed{};
		while (triangle < triangleCount) {
			// Tiles reach here as the six vertices of two triangles, of which the fourth and fifth repeat the
			// first and third. Recognizing that pattern lets a tile go out as one quad.
			const float* group = vertices + std::size_t(triangle) * 3 * FloatsPerVertex;
			const bool isQuad = (triangle + 2 <= triangleCount &&
				group[3 * FloatsPerVertex + 0] == group[0] && group[3 * FloatsPerVertex + 1] == group[1] &&
				group[4 * FloatsPerVertex + 0] == group[2 * FloatsPerVertex + 0] &&
				group[4 * FloatsPerVertex + 1] == group[2 * FloatsPerVertex + 1]);

			if (group[4] != lastColor[0] || group[5] != lastColor[1] || group[6] != lastColor[2] || group[7] != lastColor[3]) {
				lastColor[0] = group[4]; lastColor[1] = group[5]; lastColor[2] = group[6]; lastColor[3] = group[7];
				const float modulated[4] = { group[4] * layerColor[0], group[5] * layerColor[1],
					group[6] * layerColor[2], group[7] * layerColor[3] };
				packed = PackPassColor(modulated);
			}

			if (isQuad) {
				// SubmitQuadPrimitive indexes its corners by the sprite path's (ax, ay) weights - 0 = (1,0),
				// 1 = (1,1), 2 = (0,0), 3 = (0,1) - so the tile's top-left (vertex 0) fills the ax/ay = 0
				// slots and its bottom-right (vertex 2) the ax/ay = 1 ones. The duplicated corners keep the
				// axis-aligned test inside it agreeing with the decision made above.
				float tlX, tlY, tlU, tlV, brX, brY, brU, brV;
				project(group, tlX, tlY, tlU, tlV);
				project(group + 2 * FloatsPerVertex, brX, brY, brU, brV);
				const float px[4] = { brX, brX, tlX, tlX };
				const float py[4] = { tlY, brY, tlY, brY };
				const float pu[4] = { brU, brU, tlU, tlU };
				const float pvv[4] = { tlV, brV, tlV, brV };
				SubmitQuadPrimitive(state, px, py, pu, pvv, packed, 0.0f, 0.0f, axisAligned);
				triangle += 2;
			} else {
				// A lone triangle (the debris stream mixes rotated particle quads into the same mesh) goes out
				// as a three-vertex triangle list
				float sx[3], sy[3], tu[3], tv[3];
				for (std::int32_t i = 0; i < 3; i++) {
					project(group + std::size_t(i) * FloatsPerVertex, sx[i], sy[i], tu[i], tv[i]);
				}
				SubmitTrianglePrimitive(state, sx, sy, tu, tv, packed, 0.0f, 0.0f);
				triangle++;
			}
		}
	}

	void GsDevice::DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices)
	{
		// The weapon wheel arrives as a textured line strip of 4-float vertices (position.xy, texcoords.uv) -
		// the layout the MeshSprite shader's attributes declare. The GS has a native LINE_STRIP primitive, so
		// unlike the PowerVR (which expands every segment into a thin quad) the strip goes out as it is.
		constexpr std::int32_t FloatsPerVertex = 4;
		if (numVertices < 2 || !_gsInitialized) {
			return;
		}

		const GsBuffer* vbo = _currentProgram->GetBoundVbo();
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

		const GsUniformBlock* block = _currentProgram->FindBlock("InstanceBlock");
		if (block == nullptr) {
			return;
		}
		std::int32_t binding = block->GetBindingIndex();
		if (binding < 0 || std::uint32_t(binding) >= MaxUniformBindings) {
			binding = 0;
		}
		const std::uint8_t* blockData = _boundUniformRanges[binding].Data;
		if (blockData == nullptr) {
			return;
		}

		GsTexture* texture = const_cast<GsTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		float pvMat[16];
		Mat4Mul(projMat, viewMat, pvMat);
		Transform2D mvp;
		Mat4MulTransform2D(pvMat, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// Every vertex of the strip carries the instance colour, so it is packed once
		float color[4];
		std::memcpy(color, blockData + kColorOffset, sizeof(color));
		const color_t packed = PackPassColor(color);

		// The wheel's sheet is drawn at palette row 0 - it carries no per-instance palette offset. Unit 1 is
		// only consulted when the program declares a palette sampler, for the reason spelled out in Dispatch
		const GsTexture* paletteTex = nullptr;
		if (texture->NeedsPaletteBake() || texture->IsIndexed()) {
			paletteTex = _paletteTexture;
			if (_currentProgram->UsesPalette() && _boundTextures[1] != nullptr && _boundTextures[1] != texture) {
				paletteTex = _boundTextures[1];
			}
		}

		DrawState state;
		const bool needsBake = texture->NeedsPaletteBake();
		std::uint32_t page;
		if (needsBake) {
			const std::uint32_t* entries = ResolvePaletteEntries(paletteTex, 0);
			if (entries == nullptr) {
				return;
			}
			page = texture->EnsureBakedColor(entries, 0, PaletteVersionForOffset(paletteTex, 0), paletteTex);
		} else {
			page = texture->AcquireTexturePage();
		}
		if (page == GsVram::InvalidPage) {
			return;
		}
		state.Page = page;
		state.BufferPitch = (needsBake ? texture->GetBakedBufferPitch() : texture->GetBufferPitch());
		state.Psm = (needsBake ? GS_PSM_32 : std::int32_t(texture->GetPsm()));
		state.SampledWidth = texture->GetPaddedWidth();
		state.SampledHeight = texture->GetPaddedHeight();
		state.WrapU = MapWrapGs(texture->GetWrapS());
		state.WrapV = MapWrapGs(texture->GetWrapT());
		state.MagFilter = MapMagFilterGs(texture->GetMagFiltering());
		state.MinFilter = MapMinFilterGs(texture->GetMinFiltering());
		if (!needsBake && texture->IsIndexed()) {
			state.ClutBlock = AcquireClutForOffset(paletteTex, 0);
			if (state.ClutBlock == GsVram::InvalidBlock) {
				return;
			}
		}

		ApplyScissor();
		ApplyBlendEquation(MaterialEquation(_blending));

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY, offsetX, offsetY;
		GetTargetScale(scaleX, scaleY, offsetX, offsetY);
		const bool screenPass = (_currentRenderTarget == nullptr);
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX + offsetX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY + offsetY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};
		const float uvScaleU = texture->GetUScale();
		const float uvScaleV = texture->GetVScale();

		// Emitted in chunks so the scratch stays a fixed size; consecutive chunks repeat the joining vertex,
		// which is what keeps a split strip continuous
		constexpr std::int32_t MaxChunk = 96;
		float sx[MaxChunk], sy[MaxChunk], tu[MaxChunk], tv[MaxChunk];
		std::int32_t first = 0;
		while (first + 1 < numVertices) {
			const std::int32_t count = (numVertices - first < MaxChunk ? numVertices - first : MaxChunk);
			for (std::int32_t i = 0; i < count; i++) {
				const float* src = vertices + std::size_t(first + i) * FloatsPerVertex;
				sx[i] = raster.Xx * src[0] + raster.Yx * src[1] + raster.Tx;
				sy[i] = raster.Xy * src[0] + raster.Yy * src[1] + raster.Ty;
				tu[i] = src[2] * uvScaleU;
				tv[i] = src[3] * uvScaleV;
			}
			SubmitLineStripPrimitive(state, sx, sy, tu, tv, packed, count);
			first += count - 1;
		}
	}
}
