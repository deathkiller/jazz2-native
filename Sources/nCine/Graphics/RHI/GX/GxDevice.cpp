#include "GxDevice.h"
#include "GxBuffer.h"
#include "GxShaderProgram.h"
#include "GxRenderTarget.h"
#include "GxTexture.h"
#include "../FixedFunctionPass.h"
#include "../LightingCombine.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <cmath>
#include <cstring>
#include <malloc.h>

#include <ogc/cache.h>

namespace nCine::RHI::GX
{
	namespace
	{
		// The DefaultSprite / DefaultBatchedSprites instance layout is a hard contract of the shader family
		// (std140 offsets within the InstanceBlock / each batched Instance) - identical to the software
		// backend's decode (see SwDevice.cpp)
		constexpr std::uint32_t kModelMatrixOffset = 0;
		constexpr std::uint32_t kColorOffset = 64;
		constexpr std::uint32_t kTexRectOffset = 80;
		constexpr std::uint32_t kSpriteSizeOffset = 96;
		// The palette shaders park a flat palette index in the std140 tail padding after spriteSize
		constexpr std::uint32_t kPaletteOffsetOffset = 104;
		// The no-texture sprite family drops texRect, so spriteSize sits directly after color
		constexpr std::uint32_t kSpriteSizeNoTexOffset = 80;

		constexpr std::uint32_t GxFifoSize = 256 * 1024;

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

		/**
			@brief The projection*view product of the last draw, rebuilt only when either input changed

			The product changes a handful of times a frame (a camera move, a viewport switch) while a frame
			runs a few hundred dispatches, so comparing 128 bytes replaces the 64 multiplies of the full
			product almost every time. Compared by VALUE, not by pointer - the matrices are rewritten in
			place when the camera moves.
		*/
		inline const float* CachedProjView(const float* projMat, const float* viewMat)
		{
			static float cachedPv[16];
			static float cachedProj[16];
			static float cachedView[16];
			static bool cachedValid = false;
			if (!cachedValid || std::memcmp(projMat, cachedProj, sizeof(cachedProj)) != 0 ||
					std::memcmp(viewMat, cachedView, sizeof(cachedView)) != 0) {
				std::memcpy(cachedProj, projMat, sizeof(cachedProj));
				std::memcpy(cachedView, viewMat, sizeof(cachedView));
				Mat4Mul(projMat, viewMat, cachedPv);
				cachedValid = true;
			}
			return cachedPv;
		}

		// Both draw paths only ever transform points of the form (x, y, 0, 1), so just six of the sixteen
		// products of projection*view*model are ever read back. Sprites pay this per instance, which made
		// the full 4x4 multiply the most expensive step of the per-instance loop.
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

		// Maps a pipeline-neutral blend factor onto the GX factor set. GX names the color-source factors
		// from the destination's viewpoint (GX_BL_SRCCLR is only valid as a destination factor and
		// GX_BL_DSTCLR as a source factor), which matches how the game uses them.
		std::uint8_t MapBlendGx(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero:				return GX_BL_ZERO;
				case nCine::BlendingFactor::One:				return GX_BL_ONE;
				case nCine::BlendingFactor::SrcColor:			return GX_BL_SRCCLR;
				case nCine::BlendingFactor::OneMinusSrcColor:	return GX_BL_INVSRCCLR;
				case nCine::BlendingFactor::DstColor:			return GX_BL_DSTCLR;
				case nCine::BlendingFactor::OneMinusDstColor:	return GX_BL_INVDSTCLR;
				case nCine::BlendingFactor::SrcAlpha:			return GX_BL_SRCALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha:	return GX_BL_INVSRCALPHA;
				case nCine::BlendingFactor::DstAlpha:			return GX_BL_DSTALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha:	return GX_BL_INVDSTALPHA;
				default:										return GX_BL_ONE;
			}
		}

		// Packs one RGBA8 palette entry into the RGB5A3 TLUT format (alpha-capable palette entries)
		inline std::uint16_t Rgb5a3FromRgba(std::uint32_t rgba)
		{
			const std::uint32_t r = rgba & 0xFF;
			const std::uint32_t g = (rgba >> 8) & 0xFF;
			const std::uint32_t b = (rgba >> 16) & 0xFF;
			const std::uint32_t a = (rgba >> 24) & 0xFF;
			if (a >= 224) {
				// Opaque: 0b1 RRRRR GGGGG BBBBB
				return std::uint16_t(0x8000u | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
			}
			// Translucent: 0b0 AAA RRRR GGGG BBBB
			return std::uint16_t(((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}

		inline std::uint8_t QuantizeChannel(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint8_t(v * 255.0f + 0.5f);
		}

		// Whether the vertex descriptor currently includes texture coordinates
		bool g_vertexModeTextured = true;

		// GX register state persists across draws (the same reason SetVertexModeTextured/SetTevMode below
		// can early-out), so ApplyProjection() and ApplyRenderState() track what they last issued and skip
		// the reissue when nothing changed - both used to run unconditionally for every draw. Anything
		// that bypasses them to touch the same registers (target switches, EFB copies, the frame present,
		// the immediate clear and the lighting hook) invalidates the caches below.
		bool g_appliedProjectionValid = false;
		const void* g_appliedProjectionTarget = nullptr;
		std::int32_t g_appliedProjectionW = 0;
		std::int32_t g_appliedProjectionH = 0;

		bool g_appliedBlendValid = false;
		bool g_appliedBlendEnabled = false;
		nCine::BlendingFactor g_appliedBlendSrc = nCine::BlendingFactor::One;
		nCine::BlendingFactor g_appliedBlendDst = nCine::BlendingFactor::Zero;

		bool g_appliedScissorValid = false;
		bool g_appliedScissorEnabled = false;
		Recti g_appliedScissorRect(0, 0, 0, 0);
		const void* g_appliedScissorTarget = nullptr;

		void InvalidateAppliedState()
		{
			g_appliedProjectionValid = false;
			g_appliedBlendValid = false;
			g_appliedScissorValid = false;
		}

		enum class TevMode
		{
			Modulate,			// texel * vertex colour (the default sprite combine)
			Silhouette,			// vertex colour, masked by the texel alpha
			ModulateBrighten,	// texel * vertex colour, scaled x2 and clamped
			ModulateScaled4,	// texel * vertex colour, scaled x4 and clamped
			TintMix,			// mix(texel, vertex colour, vertex alpha), opaque (the warp's horizon tint)
			// The two-stage program: a modulate pass with a flat silhouette alpha-blended over
			// it, folded into a single premultiplied draw (the FrozenMask pair - see the equivalence
			// derivation at EffectContext::SubmitMergedSilhouetteOver)
			ModulateSilhouetteOver,
			// The same fold, but the silhouette's tone is picked per texel from a two-endpoint ramp by
			// the texel's amplified luminance instead of being flat (FrozenMask's ice - six stages,
			// see the stage-by-stage derivation in SetTevMode)
			ModulateLumaRampOver
		};

		TevMode g_tevMode = TevMode::Modulate;

		// Whether the luminance program's per-stage channel swizzles are currently installed. They are
		// the only thing in this renderer that touches GX_SetTevSwapMode, and stage swap selections are
		// persistent register state, so leaving the mode has to put stages 1-3 back on the identity
		// table or every later draw would sample a replicated channel instead of the texel.
		bool g_tevSwizzledStages = false;

		// How the luminance ramp weighs the texel channels: Rec.601, the same coefficients the GLSL
		// above every LUMA_RAMP pass uses
		constexpr float LumaWeights[3] = { 0.299f, 0.587f, 0.114f };

		// Puts the channel swizzles the luminance program installs back on the identity table. Stage
		// swap selections persist like every other TEV register, so this runs on every path that leaves
		// that program (SetTevMode's mode change and the vertex-format reset below).
		void ResetTevSwizzles()
		{
			if (!g_tevSwizzledStages) {
				return;
			}
			g_tevSwizzledStages = false;
			GX_SetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
			GX_SetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP0, GX_TEV_SWAP0);
			GX_SetTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP0, GX_TEV_SWAP0);
		}

		void SetVertexModeTextured(bool textured)
		{
			if (g_vertexModeTextured == textured) {
				return;
			}
			g_vertexModeTextured = textured;
			GX_ClearVtxDesc();
			GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
			GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
			if (textured) {
				GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
			}
			GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
			GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
			if (textured) {
				GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
			}
			GX_SetNumTexGens(textured ? 1 : 0);
			if (textured) {
				GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
			}
			GX_SetNumChans(1);
			GX_SetNumTevStages(1);
			GX_SetTevOrder(GX_TEVSTAGE0, textured ? GX_TEXCOORD0 : GX_TEXCOORDNULL,
				textured ? GX_TEXMAP0 : GX_TEXMAP_NULL, GX_COLOR0A0);
			GX_SetTevOp(GX_TEVSTAGE0, textured ? GX_MODULATE : GX_PASSCLR);
			ResetTevSwizzles();
			g_tevMode = TevMode::Modulate;
		}

		// How many TEV stages a mode's program occupies
		std::uint8_t TevStageCount(TevMode mode)
		{
			switch (mode) {
				case TevMode::ModulateSilhouetteOver: return 2;
				case TevMode::ModulateLumaRampOver: return 6;
				default: return 1;
			}
		}

		// How the TEV stage combines the sampled texel with the vertex colour. The actor state effects are
		// per-texel colour transforms in GLSL, which the fixed-function pipe expresses by choosing the
		// combine mode (and, for the outline, by drawing offset silhouettes - see Dispatch).
		void SetTevMode(TevMode mode)
		{
			if (g_tevMode == mode) {
				return;
			}
			// Only the merged modes run more than one stage; the register write is paid solely when the
			// count actually changes (SetVertexModeTextured also resets it to 1 and the mode to Modulate)
			if (TevStageCount(mode) != TevStageCount(g_tevMode)) {
				GX_SetNumTevStages(TevStageCount(mode));
			}
			// Only the luminance program swizzles its texture inputs; anything else needs the identity
			if (mode != TevMode::ModulateLumaRampOver) {
				ResetTevSwizzles();
			}
			g_tevMode = mode;
			switch (mode) {
				case TevMode::Modulate:
					GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
					break;
				case TevMode::Silhouette:
					// rgb = vertex colour, alpha = texel alpha * vertex alpha: the sprite shape filled with
					// a flat colour, which is what a fully saturated mask (luma * 6 in the shader) becomes
					GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
					GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
					GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					break;
				case TevMode::ModulateScaled4:
					// Same combine as ModulateBrighten with a x4 output scale, which lets a vertex colour
					// carry a multiplier of up to 4.0 (encoded as a quarter of it) instead of just 1.0
					GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
					GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_4, GX_TRUE, GX_TEVPREV);
					GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
					GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					break;
				case TevMode::ModulateBrighten:
					// The clamped x2 colour scale stands in for the shader's "luma * 2.5, saturated": the
					// sprite keeps its shading but is pushed toward white
					GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
					GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
					GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
					GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					break;
				case TevMode::TintMix:
					// One stage of `d + mix(a, b, c)`: the texel lerped toward the vertex colour by the
					// vertex ALPHA, which is how a per-row tint becomes part of the textured draw
					// instead of a second gradient pass over it (the TexturedBackground horizon).
					//   rgb = tex * (1 - rasa) + rasc * rasa
					GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_RASC, GX_CC_RASA, GX_CC_ZERO);
					GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					// alpha = 1: the vertex alpha is spent on the lerp, and the effects using this are
					// opaque (their GLSL ends with "COLOR.a = 1.0"). The konst-alpha SELECTION is the
					// 1.0 constant, so no KONST register is consumed.
					GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_1);
					GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
					GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					break;
				case TevMode::ModulateLumaRampOver:
					/*
						The luminance-ramp form of the merged draw below: same premultiplied output and
						same ONE + INVSRCALPHA blend, but the silhouette's tone is picked per TEXEL from a
						two-endpoint ramp instead of being one flat colour, so a dark sprite pixel keeps a
						darker ice than a bright one (the GLSL's "grey" term). Six stages, with

						  t     = texel alpha            iceA  = KONST0.a       s = t * iceA
						  W     = KONST0.rgb             ramp0 = REG1.rgb       ramp1 = KONST1.rgb
						  grey  = min(2 * dot(tex.rgb, W), 1)
						  ice   = mix(ramp0, ramp1, grey)

						  stage 0  C: REG2 = tex.rgb * t          (the premultiplied sprite)
						           A: REG0 = t * iceA = s
						  stage 1  C: PREV = W.r * tex.r          (texture swap table 1 -> rrr)
						  stage 2  C: PREV = PREV + W.g * tex.g   (swap table 2 -> ggg)
						  stage 3  C: PREV = (PREV + W.b * tex.b) * 2, clamped = grey   (swap 3 -> bbb)
						  stage 4  C: PREV = mix(ramp0, ramp1, grey) = ice
						  stage 5  C: PREV = mix(REG2, ice, s)    = the premultiplied colour
						           A: PREV = mix(t, 1, s)         = the coverage

						The x2 output scale of stage 3 is where the caller's LumaGain lives: it hands the
						weights in as W = Rec.601 * gain/2, so any gain up to ~3.4 fits the 0..1 KONST
						range exactly. Clamping a PARTIAL sum at stage 2 or 3 cannot lose anything: a
						partial sum reaching 1 already implies 2 * dot >= 2, i.e. grey saturates anyway.
						The alpha pipes of stages 1-4 have no work, so they park their result in REG1's
						alpha (only REG1's RGB is ever read back).
					*/
					for (std::uint8_t stage = GX_TEVSTAGE1; stage <= GX_TEVSTAGE5; stage++) {
						GX_SetTevOrder(stage, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
					}
					// The identity table plus the three single-channel replications the dot product needs.
					// Set explicitly rather than trusting the GX defaults, and undone by ResetTevSwizzles()
					// as soon as any other program is installed.
					GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
					GX_SetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
					GX_SetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
					GX_SetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);
					GX_SetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP1);
					GX_SetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP0, GX_TEV_SWAP2);
					GX_SetTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP0, GX_TEV_SWAP3);
					g_tevSwizzledStages = true;
					GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
					GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0_R);
					GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K0_G);
					GX_SetTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K0_B);
					GX_SetTevKColorSel(GX_TEVSTAGE4, GX_TEV_KCSEL_K1);
					GX_SetTevKAlphaSel(GX_TEVSTAGE5, GX_TEV_KASEL_1);
					// Stage 0 - premultiply the sprite, and the merged silhouette weight
					GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
					GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG2);
					GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_ZERO);
					GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0);
					// Stages 1-3 - the weighted channel sum, scaled and saturated into `grey`
					GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
					GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
					GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					GX_SetTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
					GX_SetTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
					// Stage 4 - the ramp lookup, stage 5 - the premultiplied mix and the coverage
					GX_SetTevColorIn(GX_TEVSTAGE4, GX_CC_C1, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
					GX_SetTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					GX_SetTevColorIn(GX_TEVSTAGE5, GX_CC_C2, GX_CC_CPREV, GX_CC_A0, GX_CC_ZERO);
					GX_SetTevColorOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					GX_SetTevAlphaIn(GX_TEVSTAGE5, GX_CA_TEXA, GX_CA_KONST, GX_CA_A0, GX_CA_ZERO);
					GX_SetTevAlphaOp(GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					// The idle alpha pipes; REG1's alpha is scratch, only its RGB (the ramp's low end) is read
					for (std::uint8_t stage = GX_TEVSTAGE1; stage <= GX_TEVSTAGE4; stage++) {
						GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
						GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG1);
					}
					break;
				case TevMode::ModulateSilhouetteOver:
					// The merged FrozenMask draw emits the PREMULTIPLIED source of its two original
					// passes and blends with ONE + INVSRCALPHA (set by the caller). With t = texel
					// alpha, iceA = KONST0.a and s = t * iceA, the stages compute
					//   colour = mix(tex.rgb * t, KONST0.rgb, s)
					//   alpha  = mix(t, 1, s) = 1 - (1 - t)*(1 - s)
					// which SubmitMergedSilhouetteOver derives to be exactly the two-pass result. The
					// silhouette colour rides in KONST0 (per-draw), the raster colour is unused.
					GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
					GX_SetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
					GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
					// Stage 1 lerps its alpha toward the CONSTANT 1.0, so its konst-alpha selection is
					// the 1.0 constant, not the K0 register stage 0 reads
					GX_SetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_1);
					// Stage 0: PREV.rgb = tex.rgb * t (the premultiplied sprite), PREV.a = s = t * iceA
					GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
					GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_ZERO);
					GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					// Stage 1: PREV.rgb = mix(PREV.rgb, ice.rgb, s), PREV.a = mix(t, 1, s)
					GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_CPREV, GX_CC_KONST, GX_CC_APREV, GX_CC_ZERO);
					GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_TEXA, GX_CA_KONST, GX_CA_APREV, GX_CA_ZERO);
					GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
					break;
			}
		}

	}

	// EffectContext deliberately has EXTERNAL linkage (namespace scope, though it is still
	// defined only in this translation unit): the generated table struct below is itself at
	// namespace scope - so the backend's ShaderProgram can forward-declare it and hold a typed
	// entry pointer - and names EffectContext in a member type; the console toolchain's GCC
	// ICEs when such an external struct member references an internal-linkage type.
	// ---------------------------------------------------------- fixed-function quad effects
	//
	// The quad-family effects are expressed as FixedFunctionPass descriptors handed to this
	// EffectContext - the structural contract documented in FixedFunctionPass.h. The per-effect
	// functions live in the shaders' fixed_function blocks and are transpiled by the
	// ShaderCompiler into Shaders/Generated/GxGeneratedEffects.h, included below against this
	// concrete context (see Docs/FixedFunctionShaderDesign.md); only the submission machinery
	// stays here.

	struct EffectContext
	{
		// The strip builder stays small - a bigger scratch would only grow the per-instance stack -
		// but the GP is happier with fewer, longer primitives than with many 4-vertex ones, so this
		// holds 16 (twice the contract's floor): enough for the iris fan to submit one angular wedge
		// with all five of its radii as a SINGLE strip. The fixed-function transpiler knows this
		// number and rejects literal strip indices/counts above it.
		static constexpr std::int32_t MaxStripVertices = 16;

		// Decoded instance data of the draw being dispatched (the documented contract)
		const float* InstanceColor;
		float TexelW;
		float TexelH;
		bool Batched;

		// Backend internals the pass state maps onto: the current instance's corner arrays,
		// whether the vertex stream carries texture coordinates, and the material blend
		// arguments (for restoring after a pass-level blend override)
		const float* Px;
		const float* Py;
		const float* Pu;
		const float* Pv;
		const float* TexRect;
		bool HasTexture;
		std::uint8_t MaterialBlendOp;
		std::uint8_t MaterialBlendSrc;
		std::uint8_t MaterialBlendDst;
		bool* BlendOverridden;

		// The pass merger's whole state: whether the previous SubmitQuad was buffered instead of
		// submitted. Only one pass shape is ever buffered - the fully DEFAULT modulate pass (the
		// first half of the FrozenMask pair, see SubmitQuad) - so a flag carries everything and a
		// flush can simply rebuild that default pass.
		bool PendingBasePass = false;

		// Pre-clip quad geometry (the quad_origin/quad_axis_* built-ins - on the GX the corners are
		// never clipped, the hardware scissor does that, but the contract stays shared with the
		// PVR) and the program, for resolved uniforms
		float OriginX, OriginY;
		float AxisXx, AxisXy;
		float AxisYx, AxisYy;
		const GxShaderProgram* Program;

		// The strip builder scratch; colours are quantized at set time (same quantization as the
		// quad path, so identical float inputs produce identical vertex words). GX textures are
		// not padded, so UVs pass through unscaled - the scale fields exist for contract parity.
		float UvScaleU, UvScaleV;
		float StripX[MaxStripVertices];
		float StripY[MaxStripVertices];
		float StripU[MaxStripVertices];
		float StripV[MaxStripVertices];
		std::uint8_t StripR[MaxStripVertices];
		std::uint8_t StripG[MaxStripVertices];
		std::uint8_t StripB[MaxStripVertices];
		std::uint8_t StripA[MaxStripVertices];

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
			// An unresolved name leaves the caller's zeros in place - blocks guard with
			// has_uniform() exactly like handwritten code null-checked the pointers
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
				StripR[i] = QuantizeChannel(r);
				StripG[i] = QuantizeChannel(g);
				StripB[i] = QuantizeChannel(b);
				StripA[i] = QuantizeChannel(a);
			}
		}

		// Whether a UV span can be mapped onto the screen at all (a zero texRect has no scale)
		bool HasTexelStep() const { return TexRect[0] != 0.0f && TexRect[2] != 0.0f; }
		// Maps a span in the sprite's UV space onto the quad's on-screen extent - the texel step
		// the Outline ring taps use (logical pixels, the space the corners live in)
		float TexelToScreenX(float uvSpan) const { return (Px[0] - Px[2]) * (uvSpan / TexRect[0]); }
		float TexelToScreenY(float uvSpan) const { return (Py[1] - Py[0]) * (uvSpan / TexRect[2]); }
		// The documented texel_size() built-in of the fixed_function contract: the Outline shader
		// family carries the sprite's UV-space texel size in its instance color.xy (exactly like
		// the GLSL derives its tap offsets), mapped into the logical pixels the corners live in
		float TexelStepX() const { return TexelToScreenX(InstanceColor[0]); }
		float TexelStepY() const { return TexelToScreenY(InstanceColor[1]); }

		// Emits the quad at an optional screen-space offset, in the given colour
		void Emit(float dx, float dy, float cr, float cg, float cb, float ca) const
		{
			const std::uint8_t r = QuantizeChannel(cr);
			const std::uint8_t g = QuantizeChannel(cg);
			const std::uint8_t b = QuantizeChannel(cb);
			const std::uint8_t a = QuantizeChannel(ca);
			// Strip order (v0, v1, v2, v3) forms the quad perimeter (v0, v1, v3, v2)
			GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			if (HasTexture) {
				GX_Position3f32(Px[0] + dx, Py[0] + dy, 0.0f);	GX_Color4u8(r, g, b, a);	GX_TexCoord2f32(Pu[0], Pv[0]);
				GX_Position3f32(Px[1] + dx, Py[1] + dy, 0.0f);	GX_Color4u8(r, g, b, a);	GX_TexCoord2f32(Pu[1], Pv[1]);
				GX_Position3f32(Px[3] + dx, Py[3] + dy, 0.0f);	GX_Color4u8(r, g, b, a);	GX_TexCoord2f32(Pu[3], Pv[3]);
				GX_Position3f32(Px[2] + dx, Py[2] + dy, 0.0f);	GX_Color4u8(r, g, b, a);	GX_TexCoord2f32(Pu[2], Pv[2]);
			} else {
				GX_Position3f32(Px[0] + dx, Py[0] + dy, 0.0f);	GX_Color4u8(r, g, b, a);
				GX_Position3f32(Px[1] + dx, Py[1] + dy, 0.0f);	GX_Color4u8(r, g, b, a);
				GX_Position3f32(Px[3] + dx, Py[3] + dy, 0.0f);	GX_Color4u8(r, g, b, a);
				GX_Position3f32(Px[2] + dx, Py[2] + dy, 0.0f);	GX_Color4u8(r, g, b, a);
			}
			GX_End();
		}

		// A pass-level blend override bypasses ApplyRenderState(), so its cache no longer
		// matches; the material blend is reissued as soon as a Material pass follows
		void ApplyPassBlend(const FixedFunctionPass& pass)
		{
			if (pass.Blend != FixedFunctionPass::BlendMode::Material) {
				switch (pass.Blend) {
					case FixedFunctionPass::BlendMode::Additive:
						GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
						break;
					case FixedFunctionPass::BlendMode::Alpha:
						GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
						break;
					default:	// Opaque
						GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
						break;
				}
				g_appliedBlendValid = false;
				*BlendOverridden = true;
			} else if (*BlendOverridden) {
				GX_SetBlendMode(MaterialBlendOp, MaterialBlendSrc, MaterialBlendDst, GX_LO_CLEAR);
				*BlendOverridden = false;
			}
		}

		// The TEV combine a pass's intent maps onto (shared by the quad and strip submissions)
		void ApplyPassTev(const FixedFunctionPass& pass) const
		{
			TevMode mode;
			switch (pass.Tev) {
				case FixedFunctionPass::TevPreset::Silhouette: mode = TevMode::Silhouette; break;
				case FixedFunctionPass::TevPreset::ModulateX2: mode = TevMode::ModulateBrighten; break;
				case FixedFunctionPass::TevPreset::ModulateX4: mode = TevMode::ModulateScaled4; break;
				// The lerp needs the texel; without one the stage would blend toward garbage, so an
				// untextured draw falls back to the plain interpolated colour
				case FixedFunctionPass::TevPreset::TintMix: mode = (HasTexture ? TevMode::TintMix : TevMode::Modulate); break;
				// LUMA_RAMP lands in the default only if it never found a base pass to merge with, and
				// it always carries an offset colour, so both submission paths have already peeled it
				// off into their flat-silhouette branch before reaching here
				default: mode = TevMode::Modulate; break;
			}
			SetTevMode(mode);
		}

		// Loads the per-draw registers of the luminance-ramp program: the Rec.601 weights prescaled by
		// the pass's gain (halved, because stage 3 applies the combiner's x2 output scale) plus the
		// merged silhouette weight in KONST0's alpha, the ramp's high end in KONST1 and its low end in
		// TEV register 1. Only three register writes, and the stage program itself stays cached.
		void LoadLumaRampRegisters(const FixedFunctionPass& pass) const
		{
			const float half = 0.5f * (pass.LumaGain > 0.0f ? pass.LumaGain : 0.0f);
			GXColor weights = {
				QuantizeChannel(LumaWeights[0] * half), QuantizeChannel(LumaWeights[1] * half),
				QuantizeChannel(LumaWeights[2] * half), QuantizeChannel(pass.Color[3])
			};
			GX_SetTevKColor(GX_KCOLOR0, weights);
			// The ramp: pass.color.rgb is the tone at grey = 0, pass.offset_color the tone at grey = 1
			GXColor rampHigh = {
				QuantizeChannel(pass.OffsetColor[0]), QuantizeChannel(pass.OffsetColor[1]),
				QuantizeChannel(pass.OffsetColor[2]), 255
			};
			GX_SetTevKColor(GX_KCOLOR1, rampHigh);
			GXColor rampLow = {
				QuantizeChannel(pass.Color[0]), QuantizeChannel(pass.Color[1]),
				QuantizeChannel(pass.Color[2]), 255
			};
			GX_SetTevColor(GX_TEVREG1, rampLow);
		}

		// Whether a pass is exactly the first half the merger buffers: the default "modulate the
		// sprite by white over the material blend" pass. The colour must be white because the fold
		// below has no stage input left to modulate the texel by a vertex colour, and the material
		// blend must be the standard alpha-over - the fold bakes THAT equation into the TEV.
		bool IsMergeableBasePass(const FixedFunctionPass& pass) const
		{
			return HasTexture && !pass.HasOffsetColor &&
				pass.Blend == FixedFunctionPass::BlendMode::Material &&
				pass.Tev == FixedFunctionPass::TevPreset::Modulate &&
				pass.ScreenOffset[0] == 0.0f && pass.ScreenOffset[1] == 0.0f &&
				pass.Color[0] == 1.0f && pass.Color[1] == 1.0f && pass.Color[2] == 1.0f && pass.Color[3] == 1.0f &&
				MaterialBlendOp == GX_BM_BLEND && MaterialBlendSrc == GX_BL_SRCALPHA && MaterialBlendDst == GX_BL_INVSRCALPHA;
		}

		// Whether a pass is the second half: a silhouette alpha-blended over the sprite (the
		// FrozenMask ice pass), either flat or with its tone picked per texel from a luminance ramp.
		// Material blend was already verified to BE alpha-over by the base matcher, so both spellings
		// of the same equation qualify. LUMA_RAMP is the one preset that legitimately carries an offset
		// colour (the ramp's high end), which is why the offset-colour veto is per preset.
		bool IsMergeableSilhouettePass(const FixedFunctionPass& pass) const
		{
			if (pass.Blend != FixedFunctionPass::BlendMode::Material && pass.Blend != FixedFunctionPass::BlendMode::Alpha) {
				return false;
			}
			if (pass.ScreenOffset[0] != 0.0f || pass.ScreenOffset[1] != 0.0f) {
				return false;
			}
			if (pass.Tev == FixedFunctionPass::TevPreset::LumaRamp) {
				return true;
			}
			return (!pass.HasOffsetColor && pass.Tev == FixedFunctionPass::TevPreset::Silhouette);
		}

		/*
			The merged form of the FrozenMask pair: the buffered default modulate pass plus this
			silhouette pass, over the same corners, as ONE draw with a two-stage TEV program.

			Equivalence derivation (t = texel alpha, iceA = the silhouette pass's alpha as its
			vertex colour would have carried it, s = t * iceA; both original passes blended
			SRCALPHA + INVSRCALPHA over the destination dst0):

				pass 0 (modulate, white):   dst1 = tex.rgb*t + dst0*(1 - t)
				pass 1 (silhouette):        dst2 = ice.rgb*s + dst1*(1 - s)
				                                 = [ice.rgb*s + tex.rgb*t*(1 - s)] + dst0*(1 - t)*(1 - s)

			A single SRCALPHA-blended draw cannot express this - its source term would need the
			division C = [...] / A - but a PREMULTIPLIED draw can. With blend ONE + INVSRCALPHA,

				dst2' = C + dst0*(1 - A)

			matches dst2 exactly when

				C = ice.rgb*s + tex.rgb*t*(1 - s) = mix(tex.rgb*t, ice.rgb, s)
				A = 1 - (1 - t)*(1 - s)           = mix(t, 1, s)

			which is precisely the two-stage program of TevMode::ModulateSilhouetteOver: stage 0
			computes tex.rgb*t and s = t*KONST0.a, stage 1 lerps the colour toward KONST0.rgb and
			the alpha toward 1.0, both by s. Exact in real arithmetic for EVERY texel alpha, not
			only the 0/1 of cutout art. (Hardware-wise the TEV rounds each stage to 8 bits where
			the two-pass form rounded in the blender per pass, so the last bit can differ; the
			ice colour/alpha quantization itself is the same 8 bits the vertex colour used to
			carry. EFB destination alpha is not stored on this pipe - RGB8_Z24 - so only the
			colour equation has to match.)

			The derivation only ever assumed that ice.rgb is SOME colour the combiner can produce per
			fragment, so it holds unchanged when the silhouette asks for a LUMA_RAMP tone instead of a
			flat one: the six-stage program of TevMode::ModulateLumaRampOver computes exactly the same
			C and A, with ice.rgb = mix(ramp0, ramp1, grey) evaluated per texel.
		*/
		void SubmitMergedSilhouetteOver(const FixedFunctionPass& silhouette)
		{
			// The fold replaces the material's SRCALPHA + INVSRCALPHA with its premultiplied form
			// for this one draw; the next Material pass restores it through ApplyPassBlend
			GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
			g_appliedBlendValid = false;
			*BlendOverridden = true;
			if (silhouette.Tev == FixedFunctionPass::TevPreset::LumaRamp) {
				// The ramp needs three registers instead of one, on the same per-draw terms: the tone
				// endpoints are constants of the effect, the merged weight animates per sprite
				LoadLumaRampRegisters(silhouette);
				SetTevMode(TevMode::ModulateLumaRampOver);
			} else {
				// KONST0 carries the silhouette colour and alpha; reloaded per merged draw because the
				// ice alpha animates per sprite (the stage program itself is cached by SetTevMode)
				GXColor konst = {
					QuantizeChannel(silhouette.Color[0]), QuantizeChannel(silhouette.Color[1]),
					QuantizeChannel(silhouette.Color[2]), QuantizeChannel(silhouette.Color[3])
				};
				GX_SetTevKColor(GX_KCOLOR0, konst);
				SetTevMode(TevMode::ModulateSilhouetteOver);
			}
			// Every stage ignores the raster colour (the colour terms live in TEXC and the registers)
			Emit(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
		}

		// Immediate single-pass submission (the pre-merger SubmitQuad, byte-identical state math)
		void SubmitQuadDirect(const FixedFunctionPass& pass)
		{
			ApplyPassBlend(pass);

			if (pass.HasOffsetColor) {
				// The GX has no post-texture offset add; a pass carrying an offset colour is the
				// silhouette form of the same intent - the sprite's shape filled flat with that
				// colour at the pass alpha (see the TevMode::Silhouette combine). A LUMA_RAMP pass
				// that reached here never found its base pass to merge with (no texture, or a
				// material blend the fold cannot absorb), and degrades to the same flat silhouette
				// filled with the ramp's high end - which is the tone every saturated texel would
				// have picked anyway.
				SetTevMode(TevMode::Silhouette);
				Emit(pass.ScreenOffset[0], pass.ScreenOffset[1],
					pass.OffsetColor[0], pass.OffsetColor[1], pass.OffsetColor[2], pass.Color[3]);
				return;
			}
			ApplyPassTev(pass);
			Emit(pass.ScreenOffset[0], pass.ScreenOffset[1],
				pass.Color[0], pass.Color[1], pass.Color[2], pass.Color[3]);
		}

		// Submits a buffered first-half pass that never found its silhouette partner (a strip
		// followed, or the effect function returned) - the buffered shape is exactly the default
		// pass, in its original submission slot: nothing else was emitted since it was buffered
		void FlushPendingQuad()
		{
			if (PendingBasePass) {
				PendingBasePass = false;
				FixedFunctionPass base;
				SubmitQuadDirect(base);
			}
		}

		void SubmitQuad(const FixedFunctionPass& pass)
		{
			// Two consecutive passes over the SAME quad that together read "sprite, then flat
			// silhouette alpha-blended over it" (the FrozenMask pair) collapse into one draw with
			// two TEV stages. The first half is buffered instead of submitted eagerly and flushed
			// unmerged as soon as anything else follows; the corners cannot change between the two
			// halves because the context is built per instance, so "same quad" holds by scope, and
			// both matchers require zero screen offsets and a compatible blend up front.
			if (PendingBasePass) {
				PendingBasePass = false;
				if (IsMergeableSilhouettePass(pass)) {
					SubmitMergedSilhouetteOver(pass);
					return;
				}
				FixedFunctionPass base;
				SubmitQuadDirect(base);
			} else if (IsMergeableBasePass(pass)) {
				PendingBasePass = true;
				return;
			}
			SubmitQuadDirect(pass);
		}

		// Textured strip out of the builder scratch: the pass's flat colour over the material
		// state, drawn through the GP's native triangle strip
		void SubmitStrip(const FixedFunctionPass& pass, std::int32_t count)
		{
			// A strip is different geometry - a buffered quad pass can no longer find its partner
			FlushPendingQuad();
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			ApplyPassBlend(pass);
			float cr = pass.Color[0], cg = pass.Color[1], cb = pass.Color[2];
			if (pass.HasOffsetColor) {
				// Same intent mapping as SubmitQuad: no post-texture add on the GX, so an offset
				// colour becomes the silhouette form filled flat with it at the pass alpha
				SetTevMode(TevMode::Silhouette);
				cr = pass.OffsetColor[0]; cg = pass.OffsetColor[1]; cb = pass.OffsetColor[2];
			} else {
				ApplyPassTev(pass);
			}
			const std::uint8_t r = QuantizeChannel(cr);
			const std::uint8_t g = QuantizeChannel(cg);
			const std::uint8_t b = QuantizeChannel(cb);
			const std::uint8_t a = QuantizeChannel(pass.Color[3]);
			const float dx = pass.ScreenOffset[0], dy = pass.ScreenOffset[1];
			GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, std::uint16_t(count));
			for (std::int32_t i = 0; i < count; i++) {
				GX_Position3f32(StripX[i] + dx, StripY[i] + dy, 0.0f);
				GX_Color4u8(r, g, b, a);
				if (HasTexture) {
					GX_TexCoord2f32(StripU[i], StripV[i]);
				}
			}
			GX_End();
		}

		// Shaded (per-vertex-colour) strip out of the builder scratch. A pure gradient has no texture
		// to modulate, so the vertex descriptor drops to the colour-only layout (whose PASSCLR combine
		// is exactly "the interpolated vertex colour") for the strip and is restored right after,
		// keeping later passes of the same instance intact. The exception is a pass whose TEV preset
		// CONSUMES the texel alongside the interpolated colour (TevPreset::TintMix): that strip stays
		// textured and carries its UVs, which is how the warp's horizon tint rides along inside the
		// band's own draw instead of needing a second gradient pass over it.
		void SubmitStripShaded(const FixedFunctionPass& pass, std::int32_t count)
		{
			// A strip is different geometry - a buffered quad pass can no longer find its partner
			FlushPendingQuad();
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			ApplyPassBlend(pass);
			const bool textured = (HasTexture && pass.Tev == FixedFunctionPass::TevPreset::TintMix);
			if (HasTexture) {
				// A gradient must not carry texture coordinates and a tinted strip must; either way
				// the format is only reprogrammed when it actually differs from the current one
				SetVertexModeTextured(textured);
			}
			if (textured) {
				// The combine is programmed AFTER any vertex-format change, which resets it
				ApplyPassTev(pass);
			}
			const float dx = pass.ScreenOffset[0], dy = pass.ScreenOffset[1];
			GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, std::uint16_t(count));
			for (std::int32_t i = 0; i < count; i++) {
				GX_Position3f32(StripX[i] + dx, StripY[i] + dy, 0.0f);
				GX_Color4u8(StripR[i], StripG[i], StripB[i], StripA[i]);
				if (textured) {
					GX_TexCoord2f32(StripU[i], StripV[i]);
				}
			}
			GX_End();
			if (HasTexture && !textured) {
				SetVertexModeTextured(true);
			}
		}
	};
}

// The per-effect functions themselves are GENERATED: the ShaderCompiler transpiles each shader's
// fixed_function block into C++ over the EffectContext defined above (the type name itself is the
// "using EffectContext = ...;" alias the header expects - the anonymous namespace above is the same
// namespace the header's payload reopens within this translation unit). Included at global scope
// because the header opens nCine::RHI::GX itself.
#include "../../../../Shaders/Generated/GxGeneratedEffects.h"

namespace nCine::RHI::GX
{
	const FixedFunctionGeneratedEffect* GxDevice::FindGeneratedEffect(const char* program, const char* variant)
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

	GxDevice::BlendingState GxDevice::_blending;
	GxDevice::DepthTestState GxDevice::_depthTest;
	GxDevice::CullFaceState GxDevice::_cullFace;
	GxDevice::ScissorState GxDevice::_scissor;
	Recti GxDevice::_viewport(0, 0, 0, 0);
	Colorf GxDevice::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	GxShaderProgram* GxDevice::_currentProgram = nullptr;
	const GxTexture* GxDevice::_boundTextures[GxDevice::MaxTextureUnits] = {};
	GxDevice::UniformRange GxDevice::_boundUniformRanges[GxDevice::MaxUniformBindings] = {};
	GxRenderTarget* GxDevice::_currentRenderTarget = nullptr;

	GXRModeObj* GxDevice::_rmode = nullptr;
	void* GxDevice::_gxFifo = nullptr;
	bool GxDevice::_gxInitialized = false;
	std::int32_t GxDevice::_logicalWidth = 0;
	std::int32_t GxDevice::_logicalHeight = 0;

	GxTexture* GxDevice::_paletteTexture = nullptr;
	std::uint32_t GxDevice::_paletteGeneration = 1;
	GxDevice::TlutSlot GxDevice::_tlutSlots[GxDevice::MaxTlutSlots] = {};
	std::uint32_t GxDevice::_tlutUseCounter = 0;
	std::uint32_t GxDevice::_frameCounter = 0;

	std::vector<GxDevice::PendingSoftwareLight> GxDevice::_pendingSoftwareLights;

	std::uint8_t* GxDevice::_lightmapStore = nullptr;
	std::size_t GxDevice::_lightmapStoreSize = 0;
	GXTexObj GxDevice::_lightmapTexObj;
	std::uint8_t* GxDevice::_lightmapLinear = nullptr;
	std::size_t GxDevice::_lightmapLinearSize = 0;

	// ------------------------------------------------------------------ session

	void GxDevice::InitializeGx(GXRModeObj* rmode)
	{
		if (_gxInitialized) {
			_rmode = rmode;
			return;
		}
		_rmode = rmode;

		// The FIFO must be accessed through the uncached alias, so the GP sees the commands immediately
		_gxFifo = MEM_K0_TO_K1(memalign(32, GxFifoSize));
		std::memset(_gxFifo, 0, GxFifoSize);
		GX_Init(_gxFifo, GxFifoSize);

		GXColor background = { 0, 0, 0, 255 };
		GX_SetCopyClear(background, GX_MAX_Z24);

		GX_SetViewport(0.0f, 0.0f, float(rmode->fbWidth), float(rmode->efbHeight), 0.0f, 1.0f);
		GX_SetDispCopyYScale(float(rmode->xfbHeight) / float(rmode->efbHeight));
		GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
		GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
		GX_SetDispCopyDst(rmode->fbWidth, rmode->xfbHeight);
		GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
		GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight ? GX_ENABLE : GX_DISABLE));
		GX_SetDispCopyGamma(GX_GM_1_0);

		// 2D painter's-order pipeline: no depth, no culling, premultiplied ortho quads
		GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
		GX_SetCullMode(GX_CULL_NONE);
		GX_SetColorUpdate(GX_TRUE);

		// Force the initial descriptor setup through the mode toggle, and start with no per-draw state
		// assumed to be applied (a fresh GX session resets every register the caches stand in for)
		g_vertexModeTextured = true;
		SetVertexModeTextured(false);
		InvalidateAppliedState();

		_logicalWidth = rmode->fbWidth;
		_logicalHeight = rmode->efbHeight;

		_gxInitialized = true;
	}

	void GxDevice::ShutdownGx()
	{
		if (!_gxInitialized) {
			return;
		}

		// Drop anything still queued and wait until the GP is idle. Leaving GP work in flight while the
		// title exits keeps the graphics pipe busy after the CPU is gone, which stalls the shutdown.
		GX_AbortFrame();
		GX_Flush();
		GX_DrawDone();

		for (std::uint32_t i = 0; i < MaxTlutSlots; i++) {
			if (_tlutSlots[i].Data != nullptr) {
				free(_tlutSlots[i].Data);
				_tlutSlots[i].Data = nullptr;
			}
			_tlutSlots[i].PaletteOffset = -1;
			_tlutSlots[i].Palette = nullptr;
		}

		if (_gxFifo != nullptr) {
			free(MEM_K1_TO_K0(_gxFifo));
			_gxFifo = nullptr;
		}

		_rmode = nullptr;
		_gxInitialized = false;
	}

	void GxDevice::PresentToXfb(void* xfb)
	{
		if (!_gxInitialized) {
			return;
		}
		FlushCurrentRenderTarget();

		// The display copy runs asynchronously on the GP; GX_DrawDone() after it drains the FIFO and waits
		// until the copy has finished, so the following flip never displays a not-yet-copied buffer
		GX_SetColorUpdate(GX_TRUE);
		GX_CopyDisp(xfb, GX_TRUE);	// The copy also clears the EFB for the next frame (GX_SetCopyClear)
		GX_DrawDone();
		_frameCounter++;
		// Nothing is assumed applied across the frame boundary - the first draw of the next frame
		// reissues projection and render state from scratch
		InvalidateAppliedState();
	}

	void GxDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width > 0 && height > 0) {
			_logicalWidth = width;
			_logicalHeight = height;
			// The logical size feeds both the ortho projection and the scissor scaling
			InvalidateAppliedState();
		}
	}

	// ------------------------------------------------------------------ state

	void GxDevice::SetBlendingEnabled(bool enabled) { _blending.Enabled = enabled; }
	void GxDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}
	GxDevice::BlendingState GxDevice::GetBlendingState() { return _blending; }
	void GxDevice::SetBlendingState(const BlendingState& state) { _blending = state; }

	void GxDevice::SetDepthTestEnabled(bool enabled) { _depthTest.TestEnabled = enabled; }
	void GxDevice::SetDepthMaskEnabled(bool enabled) { _depthTest.MaskEnabled = enabled; }
	GxDevice::DepthTestState GxDevice::GetDepthTestState() { return _depthTest; }
	void GxDevice::SetDepthTestState(const DepthTestState& state) { _depthTest = state; }

	void GxDevice::SetCullFaceEnabled(bool enabled) { _cullFace.Enabled = enabled; }
	GxDevice::CullFaceState GxDevice::GetCullFaceState() { return _cullFace; }
	void GxDevice::SetCullFaceState(const CullFaceState& state) { _cullFace = state; }

	GxDevice::ScissorState GxDevice::GetScissorState() { return _scissor; }
	void GxDevice::SetScissorState(const ScissorState& state) { _scissor = state; }
	void GxDevice::SetScissor(const Recti& rect)
	{
		// Same contract as the GL device: setting a rect also enables the test (callers like
		// RenderCommand and Viewport rely on it and restore via SetScissorState afterwards)
		_scissor.Enabled = true;
		_scissor.Rect = rect;
	}
	void GxDevice::SetScissorTestEnabled(bool enabled) { _scissor.Enabled = enabled; }

	Recti GxDevice::GetViewport() { return _viewport; }
	void GxDevice::SetViewport(const Recti& rect) { _viewport = rect; }
	void GxDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
	}

	Colorf GxDevice::GetClearColor() { return _clearColor; }
	void GxDevice::SetClearColor(const Colorf& color)
	{
		_clearColor = color;
		if (_gxInitialized) {
			GXColor c = { QuantizeChannel(color.R), QuantizeChannel(color.G), QuantizeChannel(color.B), QuantizeChannel(color.A) };
			GX_SetCopyClear(c, GX_MAX_Z24);
		}
	}

	void GxDevice::Clear(ClearFlags flags)
	{
		static_cast<void>(flags);
		if (!_gxInitialized) {
			return;
		}
		// Immediate clear: a full-target flat quad (the EFB copy-clear only applies at copy time)
		ApplyProjection();
		// The direct blend-mode write below bypasses ApplyRenderState(), so its cache no longer matches
		g_appliedBlendValid = false;
		GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
		const std::int32_t w = (_currentRenderTarget != nullptr ? _viewport.W : _logicalWidth);
		const std::int32_t h = (_currentRenderTarget != nullptr ? _viewport.H : _logicalHeight);
		SetVertexModeTextured(false);
		const std::uint8_t r = QuantizeChannel(_clearColor.R), g = QuantizeChannel(_clearColor.G);
		const std::uint8_t b = QuantizeChannel(_clearColor.B), a = QuantizeChannel(_clearColor.A);
		GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
			GX_Position3f32(0.0f, 0.0f, 0.0f);				GX_Color4u8(r, g, b, a);
			GX_Position3f32(float(w), 0.0f, 0.0f);			GX_Color4u8(r, g, b, a);
			GX_Position3f32(float(w), float(h), 0.0f);		GX_Color4u8(r, g, b, a);
			GX_Position3f32(0.0f, float(h), 0.0f);			GX_Color4u8(r, g, b, a);
		GX_End();
	}

	// ------------------------------------------------------------------ per-draw state application

	void GxDevice::ApplyProjection()
	{
		// Ortho projection over the current target's logical space; the GX viewport spans the whole EFB for
		// the screen (that maps the logical resolution onto the display copy = free upscale) and the target
		// rect for a render-target pass (rendered 1:1 into the EFB corner, then copied out)
		std::int32_t w, h;
		if (_currentRenderTarget != nullptr) {
			GxTexture* texture = _currentRenderTarget->GetColorTexture(0);
			w = (texture != nullptr ? texture->GetWidth() : _viewport.W);
			h = (texture != nullptr ? texture->GetHeight() : _viewport.H);
		} else {
			w = (_logicalWidth > 0 ? _logicalWidth : (_rmode != nullptr ? _rmode->fbWidth : 640));
			h = (_logicalHeight > 0 ? _logicalHeight : (_rmode != nullptr ? _rmode->efbHeight : 480));
		}

		// The projection, viewport and position matrix only depend on the target and its logical size,
		// which are identical for whole runs of consecutive draws - skip the reissue when nothing changed
		if (g_appliedProjectionValid && g_appliedProjectionTarget == _currentRenderTarget &&
			g_appliedProjectionW == w && g_appliedProjectionH == h) {
			return;
		}

		if (_currentRenderTarget != nullptr) {
			GX_SetViewport(0.0f, 0.0f, float(w), float(h), 0.0f, 1.0f);
		} else if (_rmode != nullptr) {
			GX_SetViewport(0.0f, 0.0f, float(_rmode->fbWidth), float(_rmode->efbHeight), 0.0f, 1.0f);
		}
		Mtx44 proj;
		guOrtho(proj, 0.0f, float(h), 0.0f, float(w), 0.0f, 1.0f);
		GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

		Mtx identity;
		guMtxIdentity(identity);
		GX_LoadPosMtxImm(identity, GX_PNMTX0);
		GX_SetCurrentMtx(GX_PNMTX0);

		g_appliedProjectionValid = true;
		g_appliedProjectionTarget = _currentRenderTarget;
		g_appliedProjectionW = w;
		g_appliedProjectionH = h;
	}

	void GxDevice::ApplyRenderState()
	{
		// The blend factors only matter while blending is enabled, so a disabled state always matches a
		// cached disabled one whatever factors it carries
		if (!g_appliedBlendValid || g_appliedBlendEnabled != _blending.Enabled ||
			(_blending.Enabled && (g_appliedBlendSrc != _blending.SrcRgb || g_appliedBlendDst != _blending.DstRgb))) {
			if (_blending.Enabled) {
				GX_SetBlendMode(GX_BM_BLEND, MapBlendGx(_blending.SrcRgb), MapBlendGx(_blending.DstRgb), GX_LO_CLEAR);
			} else {
				GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
			}
			g_appliedBlendValid = true;
			g_appliedBlendEnabled = _blending.Enabled;
			g_appliedBlendSrc = _blending.SrcRgb;
			g_appliedBlendDst = _blending.DstRgb;
		}

		// The scissor mapping depends on the target (the flip and the EFB scale), so the target pointer is
		// part of the cache key; a disabled state always maps to the same full-EFB rect
		if (g_appliedScissorValid && g_appliedScissorEnabled == _scissor.Enabled &&
			g_appliedScissorTarget == _currentRenderTarget &&
			(!_scissor.Enabled || g_appliedScissorRect == _scissor.Rect)) {
			return;
		}

		// The engine hands scissor rectangles in bottom-up (OpenGL) window coordinates of the logical
		// space; GX scissors in top-down EFB pixels, so flip and scale
		std::int32_t targetW = _logicalWidth, targetH = _logicalHeight;
		float scaleX = 1.0f, scaleY = 1.0f;
		if (_currentRenderTarget == nullptr && _rmode != nullptr && targetW > 0 && targetH > 0) {
			scaleX = float(_rmode->fbWidth) / float(targetW);
			scaleY = float(_rmode->efbHeight) / float(targetH);
		}
		if (_scissor.Enabled && targetH > 0) {
			// Screen passes mirror NDC (see Dispatch), so the engine's bottom-up scissor maps to raster
			// rows directly; render-to-texture passes keep the unmirrored top-down store and flip it
			const std::int32_t rasterY = (_currentRenderTarget == nullptr
				? _scissor.Rect.Y : targetH - _scissor.Rect.Y - _scissor.Rect.H);
			GX_SetScissor(std::uint32_t(float(_scissor.Rect.X) * scaleX), std::uint32_t(float(rasterY) * scaleY),
				std::uint32_t(float(_scissor.Rect.W) * scaleX), std::uint32_t(float(_scissor.Rect.H) * scaleY));
		} else if (_rmode != nullptr) {
			GX_SetScissor(0, 0, _rmode->fbWidth, _rmode->efbHeight);
		}

		g_appliedScissorValid = true;
		g_appliedScissorEnabled = _scissor.Enabled;
		g_appliedScissorRect = _scissor.Rect;
		g_appliedScissorTarget = _currentRenderTarget;
	}

	void GxDevice::FlushCurrentRenderTarget()
	{
		if (_currentRenderTarget == nullptr) {
			return;
		}
		GxTexture* texture = _currentRenderTarget->GetColorTexture(0);
		if (texture == nullptr || texture->GetRenderTargetStore() == nullptr) {
			return;
		}
		// Copy the rendered EFB region out into the texture's tiled store (and leave the EFB cleared for
		// whatever pass follows)
		const std::uint16_t w = std::uint16_t(texture->GetWidth());
		const std::uint16_t h = std::uint16_t(texture->GetHeight());
		GX_DrawDone();
		GX_SetTexCopySrc(0, 0, w, h);
		GX_SetTexCopyDst(w, h, GX_TF_RGBA8, GX_FALSE);
		GX_CopyTex(texture->GetRenderTargetStore(), GX_TRUE);
		GX_PixModeSync();
		GX_InvalidateTexAll();
		// Whatever renders next runs against a different target context; reissue everything once
		InvalidateAppliedState();
	}

	// ------------------------------------------------------------------ draw entry points

	void GxDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		Dispatch(primitive, firstVertex, numVertices);
	}
	void GxDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, firstVertex, numVertices);
	}
	void GxDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}
	void GxDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	FenceHandle GxDevice::InsertFence()
	{
		return reinterpret_cast<FenceHandle>(std::uintptr_t(1));
	}
	void GxDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}
	bool GxDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;	// The FIFO is drained at present; fences signal immediately
	}

	void GxDevice::SetupInitialState()
	{
		_blending = BlendingState();
		_depthTest = DepthTestState();
		_cullFace = CullFaceState();
		_scissor = ScissorState();
	}

	// ------------------------------------------------------------------ extensions

	void GxDevice::BindProgram(GxShaderProgram* program) { _currentProgram = program; }
	GxShaderProgram* GxDevice::CurrentProgram() { return _currentProgram; }

	void GxDevice::BindTexture(std::uint32_t unit, const GxTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void GxDevice::UnbindTexture(const GxTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
		if (_paletteTexture == texture) {
			_paletteTexture = nullptr;
		}
		// Drop TLUTs built from the destroyed palette so a stale pointer can never match
		for (std::uint32_t i = 0; i < MaxTlutSlots; i++) {
			if (_tlutSlots[i].Palette == texture) {
				_tlutSlots[i].PaletteOffset = -1;
				_tlutSlots[i].Palette = nullptr;
			}
		}
	}

	const GxTexture* GxDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void GxDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void GxDevice::SetRenderTarget(GxRenderTarget* renderTarget)
	{
		if (renderTarget == _currentRenderTarget) {
			return;
		}
		// Leaving a target resolves it into its texture before anything else renders over the EFB
		FlushCurrentRenderTarget();
		_currentRenderTarget = renderTarget;
		// The projection space and the scissor mapping are keyed on the target
		InvalidateAppliedState();
	}

	void GxDevice::UnbindRenderTarget(const GxRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
		}
	}

	// ------------------------------------------------------------------ palette TLUTs

	void GxDevice::RegisterPaletteTexture(GxTexture* texture)
	{
		_paletteTexture = texture;
		NotifyPaletteTextureChanged(texture, 0, texture != nullptr ? texture->GetHeight() : 0);
	}

	void GxDevice::NotifyPaletteTextureChanged(GxTexture* texture, std::int32_t firstRow, std::int32_t rowCount)
	{
		if (texture != _paletteTexture) {
			return;
		}
		_paletteGeneration++;
		for (std::uint32_t i = 0; i < MaxTlutSlots; i++) {
			if (_tlutSlots[i].PaletteOffset >= (firstRow - 1) * 256 && _tlutSlots[i].PaletteOffset < (firstRow + rowCount) * 256) {
				_tlutSlots[i].PaletteOffset = -1;
			}
		}
	}

	std::int32_t GxDevice::AcquireTlutForRow(const GxTexture* palette, std::int32_t paletteOffset)
	{
		// The offset is a flat index into the palette texture and does not need to be row-aligned
		// (e.g. the gem gradients pack two palettes into a single 256-entry row). The palette is usually
		// the registered global one, but effects like the profile character previews bind their own
		// recolored palette texture instead.
		const std::int32_t maxOffset = palette != nullptr
			? palette->GetWidth() * palette->GetHeight() - 256 : 0;
		if (palette == nullptr || palette->GetPixels() == nullptr ||
			paletteOffset < 0 || paletteOffset > maxOffset) {
			return -1;
		}

		_tlutUseCounter++;

		// Reuse a slot already holding this row of this palette (and its current content), or evict the
		// least recently used one
		std::int32_t slot = -1;
		std::uint32_t oldestUse = UINT32_MAX;
		std::int32_t oldestSlot = 0;
		for (std::uint32_t i = 0; i < MaxTlutSlots; i++) {
			if (_tlutSlots[i].PaletteOffset == paletteOffset && _tlutSlots[i].Palette == palette &&
				_tlutSlots[i].PaletteVersion == palette->GetContentVersion()) {
				slot = std::int32_t(i);
				break;
			}
			if (_tlutSlots[i].LastUse < oldestUse) {
				oldestUse = _tlutSlots[i].LastUse;
				oldestSlot = std::int32_t(i);
			}
		}

		if (slot < 0) {
			slot = oldestSlot;
			TlutSlot& s = _tlutSlots[slot];
			if (s.Data == nullptr) {
				s.Data = static_cast<std::uint16_t*>(memalign(32, 256 * sizeof(std::uint16_t)));
				if (s.Data == nullptr) {
					return -1;
				}
			}
			const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(
				palette->GetPixels()) + paletteOffset;
			for (std::int32_t i = 0; i < 256; i++) {
				s.Data[i] = Rgb5a3FromRgba(entries[i]);
			}
			DCFlushRange(s.Data, 256 * sizeof(std::uint16_t));

			GXTlutObj tlut;
			GX_InitTlutObj(&tlut, s.Data, GX_TL_RGB5A3, 256);
			GX_LoadTlut(&tlut, GX_TLUT0 + std::uint32_t(slot));
			s.PaletteOffset = paletteOffset;
			s.Palette = palette;
			s.PaletteVersion = palette->GetContentVersion();
		}

		_tlutSlots[slot].LastUse = _tlutUseCounter;
		return slot;
	}

	// ------------------------------------------------------------------ lighting hook

	void GxDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
		std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
		bool waterActive, float waterLevelPx, float waterTime, float waterCamY)
	{
		// The water parameters stay in the cross-backend interface but have no consumer here: the water is a
		// fixed_function block of the CombineWithWater programs now, and it reads the waterline and the ambient
		// colour off the draw's own uniforms (see CombineWithWater.shader). Only the software backend, whose
		// richer per-row water is its own, still reads them.
		static_cast<void>(waterActive);
		static_cast<void>(waterLevelPx);
		static_cast<void>(waterTime);
		static_cast<void>(waterCamY);

		PendingSoftwareLight light;
		light.Lightmap = lightmap;
		light.LmW = lmW;
		light.LmH = lmH;
		light.Scale = (scale > 0 ? scale : 1);
		light.VpX = vpX;
		light.VpY = vpY;
		light.VpW = vpW;
		light.VpH = vpH;
		light.AmbR = ambR;
		light.AmbG = ambG;
		light.AmbB = ambB;
		_pendingSoftwareLights.push_back(light);
	}

	void GxDevice::EndFrame()
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

	void GxDevice::ApplyPendingSoftwareLighting()
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

		ApplyProjection();
		// Both the lightmap multiply and the water bands set their blend modes directly, bypassing
		// ApplyRenderState() - its cache no longer matches once this hook ran
		g_appliedBlendValid = false;
		const float vpX = float(light.VpX), vpY = float(light.VpY);
		const float vpW = float(light.VpW), vpH = float(light.VpH);

		if (hasLighting) {
			// Build the per-texel multiply factor from the CPU lightmap: out ≈ scene * (r*(1+g) + amb*(1-r))
			// per channel - the multiply-only approximation of the software combine (the small additive glow
			// term of very bright lights is dropped). Tiled RGBA8, drawn as one linear-filtered quad with a
			// dst*src blend over the viewport.
			const std::int32_t w = light.LmW, h = light.LmH;
			const std::size_t linearSize = std::size_t(w) * std::size_t(h) * 4;
			const std::size_t tiledSize = std::size_t((w + 3) & ~3) * std::size_t((h + 3) & ~3) * 4;
			if (_lightmapStore == nullptr || _lightmapStoreSize < tiledSize) {
				if (_lightmapStore != nullptr) {
					free(_lightmapStore);
				}
				_lightmapStore = static_cast<std::uint8_t*>(memalign(32, tiledSize));
				_lightmapStoreSize = tiledSize;
			}
			// The linear staging buffer persists across frames like the tiled store above - allocating
			// (and zero-initializing) it per lit frame is wasted work, every byte is written below
			if (_lightmapLinear == nullptr || _lightmapLinearSize < linearSize) {
				if (_lightmapLinear != nullptr) {
					free(_lightmapLinear);
				}
				_lightmapLinear = static_cast<std::uint8_t*>(malloc(linearSize));
				_lightmapLinearSize = (_lightmapLinear != nullptr ? linearSize : 0);
			}
			if (_lightmapStore != nullptr && _lightmapLinear != nullptr) {
				std::uint8_t* const linear = _lightmapLinear;
				for (std::int32_t y = 0; y < h; y++) {
					const float* src = light.Lightmap + std::size_t(y) * w * 2;
					std::uint8_t* dst = linear + std::size_t(y) * w * 4;
					for (std::int32_t x = 0; x < w; x++) {
						const float r = ClampLightmapChannel(src[x * 2]);
						const float g = ClampLightmapChannel(src[x * 2 + 1]);
						dst[x * 4 + 0] = QuantizeChannel(LightingCombineFactor(r, g, light.AmbR));
						dst[x * 4 + 1] = QuantizeChannel(LightingCombineFactor(r, g, light.AmbG));
						dst[x * 4 + 2] = QuantizeChannel(LightingCombineFactor(r, g, light.AmbB));
						dst[x * 4 + 3] = 255;
					}
				}

				// Tile 4x4 RGBA8 (same layout GxTexture uses)
				const std::int32_t tilesX = (w + 3) / 4;
				const std::int32_t tilesY = (h + 3) / 4;
				for (std::int32_t ty = 0; ty < tilesY; ty++) {
					for (std::int32_t tx = 0; tx < tilesX; tx++) {
						std::uint8_t* tile = _lightmapStore + std::size_t(ty * tilesX + tx) * 64;
						for (std::int32_t row = 0; row < 4; row++) {
							for (std::int32_t col = 0; col < 4; col++) {
								const std::int32_t x = tx * 4 + col;
								const std::int32_t y = ty * 4 + row;
								std::uint8_t r = 255, g = 255, b = 255, a = 255;
								if (x < w && y < h) {
									const std::uint8_t* px = linear + (std::size_t(y) * w + x) * 4;
									r = px[0]; g = px[1]; b = px[2]; a = px[3];
								}
								const std::int32_t i = row * 4 + col;
								tile[i * 2 + 0] = a;
								tile[i * 2 + 1] = r;
								tile[32 + i * 2 + 0] = g;
								tile[32 + i * 2 + 1] = b;
							}
						}
					}
				}
				DCFlushRange(_lightmapStore, std::uint32_t(tiledSize));
				GX_InvalidateTexAll();

				GX_InitTexObj(&_lightmapTexObj, _lightmapStore, std::uint16_t(w), std::uint16_t(h), GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
				GX_InitTexObjFilterMode(&_lightmapTexObj, GX_LINEAR, GX_LINEAR);
				GX_LoadTexObj(&_lightmapTexObj, GX_TEXMAP0);

				SetVertexModeTextured(true);
				GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_ZERO, GX_LO_CLEAR);	// out = dst * src
				// The lightmap's row 0 corresponds to the bottom of the displayed viewport (the software
				// buffer is bottom-up and flipped at present), so V runs 1 -> 0 top -> bottom
				GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
					GX_Position3f32(vpX, vpY, 0.0f);				GX_Color4u8(255, 255, 255, 255);	GX_TexCoord2f32(0.0f, 1.0f);
					GX_Position3f32(vpX + vpW, vpY, 0.0f);			GX_Color4u8(255, 255, 255, 255);	GX_TexCoord2f32(1.0f, 1.0f);
					GX_Position3f32(vpX + vpW, vpY + vpH, 0.0f);	GX_Color4u8(255, 255, 255, 255);	GX_TexCoord2f32(1.0f, 0.0f);
					GX_Position3f32(vpX, vpY + vpH, 0.0f);			GX_Color4u8(255, 255, 255, 255);	GX_TexCoord2f32(0.0f, 0.0f);
				GX_End();
			}
		}
	}

	// ------------------------------------------------------------------ draw dispatch

	void GxDevice::DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		// A tile-layer mesh is a plain triangle list of 8-float vertices (position.xy, texcoords.uv,
		// color.rgba) - the layout TileMap::AppendTileQuad() writes and TileMapVs.inc declares. It is a
		// hard contract of this shader family exactly like the std140 instance block is of the sprite one.
		constexpr std::int32_t FloatsPerVertex = 8;
		if (primitive != PrimitiveType::Triangles || numVertices < 3) {
			return;
		}

		const GxBuffer* vbo = _currentProgram->GetBoundVbo();
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

		const GxUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		GxTexture* texture = const_cast<GxTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjectionMatrix();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedViewMatrix();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		const float* pv = CachedProjView(projMat, viewMat);
		Transform2D mvp;
		Mat4MulTransform2D(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// The layer tint modulates every vertex colour, which already carries the per-tile alpha
		float layerColor[4];
		std::memcpy(layerColor, blockData + kColorOffset, sizeof(layerColor));

		// The palette to remap with is whatever the material bound to the palette sampler; the registered
		// global palette is the fallback (mirrors the sprite path). TileMapMeshPalette binds
		// uTexturePalette in its reflection, which is exactly what UsesPalette() reports - the remap
		// intent needs no effect identity of its own.
		const bool isPaletteRemap = _currentProgram->UsesPalette();
		const GxTexture* paletteTex = nullptr;
		if (isPaletteRemap || texture->IsIndexed()) {
			paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}

		// Unlike a sprite batch, the whole mesh shares one texture and one palette offset, so the texture
		// variant (TLUT row for CI8 indices, palette-baked copy for RG8) is resolved and loaded once for
		// the entire layer
		GXTexObj* texObj = nullptr;
		if (texture->IsIndexed()) {
			// An 8bpp store can only be read through a palette, whatever it is being drawn with - the lookup
			// belongs to the texture read rather than to the effect. An effect that remaps takes the row from
			// the instance; anything else uses the base row.
			std::int32_t paletteOffset = 0;
			if (isPaletteRemap) {
				float palOffset = 0.0f;
				std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
				paletteOffset = std::int32_t(palOffset + 0.5f);
			}
			const std::int32_t slot = AcquireTlutForRow(paletteTex, paletteOffset);
			texObj = texture->GetTexObj();
			if (texObj != nullptr && slot >= 0) {
				GX_InitTexObjTlut(texObj, GX_TLUT0 + std::uint32_t(slot));
			}
		} else if (isPaletteRemap && texture->NeedsPaletteBake() && paletteTex != nullptr && paletteTex->GetPixels() != nullptr) {
			float palOffset = 0.0f;
			std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
			const std::uint32_t paletteOffset = std::uint32_t(std::int32_t(palOffset + 0.5f));
			const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(
				paletteTex->GetPixels()) + paletteOffset;
			texObj = texture->EnsureBakedRgba(entries, paletteOffset,
				(paletteTex == _paletteTexture ? _paletteGeneration : paletteTex->GetContentVersion()), paletteTex);
		} else {
			texObj = texture->GetTexObj();
		}
		if (texObj == nullptr) {
			return;
		}

		ApplyProjection();
		ApplyRenderState();
		SetVertexModeTextured(true);
		SetTevMode(TevMode::Modulate);
		GX_LoadTexObj(texObj, GX_TEXMAP0);

		const bool screenPass = (_currentRenderTarget == nullptr);
		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);

		// The NDC-to-raster mapping is affine and constant for the whole mesh, so it is folded into the
		// transform once instead of being reapplied per vertex - every vertex then costs one multiply-add
		// per axis (matching the sprite path's corner synthesis). A screen pass mirrors NDC, which is
		// just the sign of the Y scale.
		const float rasterScaleX = 0.5f * float(viewport.W);
		const float rasterBiasX = rasterScaleX + float(viewport.X);
		const float rasterScaleY = 0.5f * float(viewport.H) * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) + float(viewport.Y);
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		const std::int32_t triangleCount = numVertices / 3;

		// Tiles reach here as the six vertices of two triangles, of which the fourth repeats the first
		// and the fifth repeats the third. Recognizing that pattern lets a tile go out as one four-vertex
		// GX quad instead of two triangles, and consecutive tiles share a single GX_Begin run, so the
		// begin/end register writes are paid once per run instead of once per tile. GX has a hardware
		// scissor (applied by ApplyRenderState above), so unlike the PVR path everything is submitted
		// as-is and the GP clips it - no geometric clipping or bounding-box rejection is needed.
		auto isTileQuad = [vertices, triangleCount](std::int32_t triangle) {
			if (triangle + 2 > triangleCount) {
				return false;
			}
			const float* group = vertices + std::size_t(triangle) * 3 * FloatsPerVertex;
			return (group[3 * FloatsPerVertex + 0] == group[0] && group[3 * FloatsPerVertex + 1] == group[1] &&
				group[4 * FloatsPerVertex + 0] == group[2 * FloatsPerVertex + 0] &&
				group[4 * FloatsPerVertex + 1] == group[2 * FloatsPerVertex + 1]);
		};

		// GX_Begin carries the vertex count in a 16-bit register, so runs are chopped below that limit
		constexpr std::int32_t MaxQuadsPerRun = 0x3FFF;
		constexpr std::int32_t MaxTrianglesPerRun = 0x5554;

		std::int32_t triangle = 0;
		while (triangle < triangleCount) {
			if (isTileQuad(triangle)) {
				const std::int32_t runStart = triangle;
				std::int32_t quadRun = 0;
				do {
					quadRun++;
					triangle += 2;
				} while (quadRun < MaxQuadsPerRun && isTileQuad(triangle));

				GX_Begin(GX_QUADS, GX_VTXFMT0, std::uint16_t(quadRun * 4));
				for (std::int32_t q = 0; q < quadRun; q++) {
					const float* group = vertices + std::size_t(runStart + q * 2) * 3 * FloatsPerVertex;
					// Every vertex of a tile carries the same colour, so it only has to be folded with the
					// layer tint and quantized once
					const std::uint8_t r = QuantizeChannel(group[4] * layerColor[0]);
					const std::uint8_t g = QuantizeChannel(group[5] * layerColor[1]);
					const std::uint8_t b = QuantizeChannel(group[6] * layerColor[2]);
					const std::uint8_t a = QuantizeChannel(group[7] * layerColor[3]);
					// Perimeter order of the tile's six strip-ordered vertices (see AppendTileQuad)
					static const std::int32_t QuadOrder[4] = { 0, 1, 2, 5 };
					for (std::int32_t i = 0; i < 4; i++) {
						const float* v = group + std::size_t(QuadOrder[i]) * FloatsPerVertex;
						GX_Position3f32(raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx,
							raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty, 0.0f);
						GX_Color4u8(r, g, b, a);
						GX_TexCoord2f32(v[2], v[3]);
					}
				}
				GX_End();
			} else {
				const std::int32_t runStart = triangle;
				std::int32_t triRun = 0;
				do {
					triRun++;
					triangle++;
				} while (triangle < triangleCount && triRun < MaxTrianglesPerRun && !isTileQuad(triangle));

				GX_Begin(GX_TRIANGLES, GX_VTXFMT0, std::uint16_t(triRun * 3));
				for (std::int32_t i = 0; i < triRun * 3; i++) {
					const float* v = vertices + std::size_t(runStart) * 3 * FloatsPerVertex + std::size_t(i) * FloatsPerVertex;
					GX_Position3f32(raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx,
						raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty, 0.0f);
					GX_Color4u8(QuantizeChannel(v[4] * layerColor[0]), QuantizeChannel(v[5] * layerColor[1]),
						QuantizeChannel(v[6] * layerColor[2]), QuantizeChannel(v[7] * layerColor[3]));
					GX_TexCoord2f32(v[2], v[3]);
				}
				GX_End();
			}
		}
	}

	void GxDevice::DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices)
	{
		// The weapon wheel arrives as a textured line strip of 4-float vertices (position.xy,
		// texcoords.uv) - the layout the MeshSprite shader's attributes declare. Unlike the PVR,
		// the GP draws lines natively, so the strip is passed through as GX_LINESTRIP.
		constexpr std::int32_t FloatsPerVertex = 4;
		if (numVertices < 2 || numVertices > 0xFFFF) {
			return;
		}

		const GxBuffer* vbo = _currentProgram->GetBoundVbo();
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

		const GxUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		GxTexture* texture = const_cast<GxTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjectionMatrix();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedViewMatrix();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		const float* pv = CachedProjView(projMat, viewMat);
		Transform2D mvp;
		Mat4MulTransform2D(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// Every vertex of the strip carries the instance colour, so it is quantized once
		float color[4];
		std::memcpy(color, blockData + kColorOffset, sizeof(color));
		const std::uint8_t r = QuantizeChannel(color[0]);
		const std::uint8_t g = QuantizeChannel(color[1]);
		const std::uint8_t b = QuantizeChannel(color[2]);
		const std::uint8_t a = QuantizeChannel(color[3]);

		// The strip shares one texture; indexed assets read through the base TLUT row like the fonts do
		GXTexObj* texObj = nullptr;
		if (texture->IsIndexed()) {
			const GxTexture* paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
			const std::int32_t slot = AcquireTlutForRow(paletteTex, 0);
			texObj = texture->GetTexObj();
			if (texObj != nullptr && slot >= 0) {
				GX_InitTexObjTlut(texObj, GX_TLUT0 + std::uint32_t(slot));
			}
		} else {
			texObj = texture->GetTexObj();
		}
		if (texObj == nullptr) {
			return;
		}

		ApplyProjection();
		ApplyRenderState();
		SetVertexModeTextured(true);
		SetTevMode(TevMode::Modulate);
		GX_LoadTexObj(texObj, GX_TEXMAP0);
		// Width is in sixths of a pixel, so 6 matches the 1-wide GL lines this stands in for
		GX_SetLineWidth(6, GX_TO_ZERO);

		const bool screenPass = (_currentRenderTarget == nullptr);
		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);

		// The NDC-to-raster mapping is folded into the transform once, like the other mesh paths
		const float rasterScaleX = 0.5f * float(viewport.W);
		const float rasterBiasX = rasterScaleX + float(viewport.X);
		const float rasterScaleY = 0.5f * float(viewport.H) * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) + float(viewport.Y);
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		GX_Begin(GX_LINESTRIP, GX_VTXFMT0, std::uint16_t(numVertices));
		for (std::int32_t i = 0; i < numVertices; i++) {
			const float* v = vertices + std::size_t(i) * FloatsPerVertex;
			GX_Position3f32(raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx,
				raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty, 0.0f);
			GX_Color4u8(r, g, b, a);
			GX_TexCoord2f32(v[2], v[3]);
		}
		GX_End();
	}

	void GxDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		if (_currentProgram == nullptr || numVertices <= 0 || !_gxInitialized) {
			return;
		}

		// The program's whole console identity is its generated-table entry, resolved at load from the
		// true (program, variant) the loaders plumbed in - a program without an entry has no
		// fixed_function block in its .shader file (Lighting, Blur, the Resize* family,
		// runtime-compiled shaders, ...) and keeps the logged, skipped draw.
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
				LOGW("Skipping draws of program \"{}\": Only the line-strip form of the mesh pipeline is supported by the GX dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		// Everything else is the procedural sprite-quad family: a transpiled effect function
		// (geometry synthesis included - the iris fan and the warped background are ordinary blocks
		// since phase 4)
		const bool isQuadFamily = (generated->Fn != nullptr);
		if (!isQuadFamily || (primitive != PrimitiveType::TriangleStrip && primitive != PrimitiveType::Triangles)) {
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Effect not supported by the GX dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjectionMatrix();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedViewMatrix();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		// Resolved once at introspection (see DispatchFacts) - this used to re-scan the
		// reflection's name strings on every RenderCommand
		const GxUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		const GxShaderProgram::DispatchFacts& facts = _currentProgram->GetDispatchFacts();
		std::uint32_t instanceStride = facts.InstanceStride;

		const float* pv = CachedProjView(projMat, viewMat);

		// Batched programs are exactly the ones whose reflection declares a BATCH_SIZE-strided
		// InstancesBlock (non-batched programs use a flat InstanceBlock with no stride), so the
		// reflected stride IS the batching signal - no per-program identity needed
		const bool batched = (instanceStride > 0);
		std::int32_t numInstances = 1;
		if (batched) {
			numInstances = numVertices / 6;
			if (numInstances < 1) {
				numInstances = 1;
			}
			if (instanceStride == 0) {
				instanceStride = 112;
			}
		}

		// A program samples the sprite texture exactly when its reflection binds uTexture - the
		// no-texture sprite programs and the Transition (which carries texRect in its block but
		// samples nothing, hence the separate layout flag) simply do not declare it
		const bool hasTexture = facts.HasTexture;
		// The instance layout follows the block's own reflected declaration rather than any effect
		// identity: a block that declares texRect uses the textured member offsets whether or not
		// the program samples a texture (the Transition carries texRect but samples nothing)
		const bool texturedLayout = facts.TexturedLayout;
		// Every effect that samples indexed sprites through the palette texture binds uTexturePalette
		// in its reflection, which is what UsesPalette() reports (PaletteRemap and the "...Palette"
		// variants of the actor state effects alike)
		const bool isPaletteRemap = _currentProgram->UsesPalette();
		const std::int32_t textureUnit = facts.TextureUnit;
		const GxTexture* texture = (hasTexture ? _boundTextures[std::uint32_t(textureUnit) < MaxTextureUnits ? textureUnit : 0] : nullptr);
		// A program whose reflection binds a sampler with nothing bound to it can only be drawn by an
		// effect that never samples: a textured primitive would rasterize garbage, an untextured shaded
		// strip is unaffected. That is exactly what SamplesTexture records (the lighting compositor's
		// water overlay declares uTexture for a fragment stage this tier never runs).
		if (hasTexture && texture == nullptr &&
			(generated->Requirements & FixedFunctionRequirements::SamplesTexture) == FixedFunctionRequirements::SamplesTexture) {
			return;
		}

		// The palette to remap with is whatever the material bound to the palette sampler (e.g. the
		// recolored preview palettes of the profile menu); the registered global palette is the fallback
		const GxTexture* paletteTex = nullptr;
		if (isPaletteRemap) {
			const std::int32_t paletteUnit = facts.PaletteUnit;
			paletteTex = (std::uint32_t(paletteUnit) < MaxTextureUnits ? _boundTextures[paletteUnit] : nullptr);
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}

		ApplyProjection();
		ApplyRenderState();
		SetVertexModeTextured(hasTexture);

		// Bounds guard, mirroring the software device
		const std::uint32_t rangeSize = _boundUniformRanges[binding].Size;
		if (batched && rangeSize > 0 && std::uint32_t(numInstances) * instanceStride > rangeSize) {
			numInstances = std::int32_t(rangeSize / instanceStride);
		}

		// The viewport rect maps NDC to logical pixels exactly like the software FetchVertex
		// The engine's NDC orientation matches the software backend, whose top-down raster is flipped at
		// present time; the EFB is scanned out top-down directly, so screen passes mirror NDC here instead
		// (+1 = bottom row). Render-to-texture passes keep the unmirrored top-down store, which is what the
		// sampling passes already expect.
		const bool screenPass = (_currentRenderTarget == nullptr);

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);

		// Constant NDC-to-raster mapping, folded in once rather than reapplied for every sprite corner;
		// the screen-pass NDC mirror is just the sign of the Y scale
		const float rasterScaleX = 0.5f * float(viewport.W);
		const float rasterBiasX = rasterScaleX + float(viewport.X);
		const float rasterScaleY = 0.5f * float(viewport.H) * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) + float(viewport.Y);

		std::int32_t lastTlutSlot = -2;
		GXTexObj* loadedTexObj = nullptr;

		// Statically computed per generated function: which optional context facilities the effect's
		// code can ever call. The setup gated on these flags only feeds those facilities, so skipping
		// it is invisible to the effect - it cannot read what it never calls - and every submitted
		// primitive stays bit-identical.
		const FixedFunctionRequirements reqs = generated->Requirements;
		const bool needsTexelStep = ((reqs & FixedFunctionRequirements::NeedsTexelStep) == FixedFunctionRequirements::NeedsTexelStep);
		const bool needsUniforms = ((reqs & FixedFunctionRequirements::NeedsUniforms) == FixedFunctionRequirements::NeedsUniforms);
		const bool needsStripBuilder = ((reqs & FixedFunctionRequirements::NeedsStripBuilder) == FixedFunctionRequirements::NeedsStripBuilder);
		const bool needsQuadAxes = ((reqs & FixedFunctionRequirements::NeedsQuadAxes) == FixedFunctionRequirements::NeedsQuadAxes);

		// Texel sizes of the sprite texture in texture space (part of the EffectContext contract;
		// the effects that need an on-screen texel step derive it from the instance colour instead,
		// exactly like their GLSL does). Derived only for effects flagged with the texel-size
		// facility - everything else gets deterministic zeros without the divides.
		const float texelWidth = (needsTexelStep && hasTexture && texture->GetWidth() > 0 ? 1.0f / float(texture->GetWidth()) : 0.0f);
		const float texelHeight = (needsTexelStep && hasTexture && texture->GetHeight() > 0 ? 1.0f / float(texture->GetHeight()) : 0.0f);
		// The material blend arguments, mirroring what ApplyRenderState() issued above, so
		// EffectContext::SubmitQuad can restore them after a pass-level blend override. The override
		// flag outlives one instance because the material state does too.
		const std::uint8_t materialBlendOp = std::uint8_t(_blending.Enabled ? GX_BM_BLEND : GX_BM_NONE);
		const std::uint8_t materialBlendSrc = (_blending.Enabled ? MapBlendGx(_blending.SrcRgb) : std::uint8_t(GX_BL_ONE));
		const std::uint8_t materialBlendDst = (_blending.Enabled ? MapBlendGx(_blending.DstRgb) : std::uint8_t(GX_BL_ZERO));
		bool blendOverridden = false;

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

			// Bind the instance's texture variant (TLUT row for CI8 indices, palette-baked copy for RG8)
			if (hasTexture) {
				GXTexObj* texObj = nullptr;
				GxTexture* mutableTexture = const_cast<GxTexture*>(texture);
				if (isPaletteRemap && texture->IsIndexed()) {
					float palOffset = 0.0f;
					std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
					const std::int32_t paletteOffset = std::int32_t(palOffset + 0.5f);
					const std::int32_t slot = AcquireTlutForRow(paletteTex, paletteOffset);
					texObj = mutableTexture->GetTexObj();
					if (texObj != nullptr && slot >= 0 && slot != lastTlutSlot) {
						GX_InitTexObjTlut(texObj, GX_TLUT0 + std::uint32_t(slot));
						lastTlutSlot = slot;
						loadedTexObj = nullptr;		// Force a reload with the new TLUT binding
					}
				} else if (isPaletteRemap && texture->NeedsPaletteBake() && paletteTex != nullptr && paletteTex->GetPixels() != nullptr) {
					float palOffset = 0.0f;
					std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
					const std::uint32_t paletteOffset = std::uint32_t(std::int32_t(palOffset + 0.5f));
					const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(
						paletteTex->GetPixels()) + paletteOffset;
					texObj = mutableTexture->EnsureBakedRgba(entries, paletteOffset,
						(paletteTex == _paletteTexture ? _paletteGeneration : paletteTex->GetContentVersion()), paletteTex);
				} else {
					texObj = mutableTexture->GetTexObj();
				}
				if (texObj == nullptr) {
					continue;
				}
				if (texObj != loadedTexObj) {
					GX_LoadTexObj(texObj, GX_TEXMAP0);
					loadedTexObj = texObj;
				}
			}

			// Synthesize the four sprite corners exactly like the software FetchVertex, with the constant
			// NDC-to-raster mapping folded into the transform (see the PVR device) so a corner costs one
			// multiply-add per axis. The corner weights are 0 or 1, so the sprite's extent in raster space
			// is just the transformed axes scaled by its size, and the corners are sums of those.
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
				pu[i] = ax * texRect[0] + texRect[1];
				pvv[i] = ay * texRect[2] + texRect[3];
			}

			// The pass descriptors the per-effect functions declare are mapped onto this instance's
			// corners and the surrounding material state through the context
			EffectContext ctx;
			ctx.InstanceColor = color;
			ctx.TexelW = texelWidth;
			ctx.TexelH = texelHeight;
			ctx.Batched = batched;
			ctx.Px = px;
			ctx.Py = py;
			ctx.Pu = pu;
			ctx.Pv = pvv;
			ctx.TexRect = texRect;
			ctx.HasTexture = hasTexture;
			ctx.MaterialBlendOp = materialBlendOp;
			ctx.MaterialBlendSrc = materialBlendSrc;
			ctx.MaterialBlendDst = materialBlendDst;
			ctx.BlendOverridden = &blendOverridden;
			// The optional context facilities are only wired up for effects whose static analysis
			// says they can call them (see reqs above); the loop-invariant conditions predict
			// perfectly, and members of an unused facility are simply never read
			if (needsQuadAxes) {
				ctx.OriginX = originX;
				ctx.OriginY = originY;
				ctx.AxisXx = spanXx;
				ctx.AxisXy = spanXy;
				ctx.AxisYx = spanYx;
				ctx.AxisYy = spanYy;
			}
			// Resolved uniforms are the only thing the context needs the program for, so effects
			// without the facility get no program plumbed at all (no resolution can ever run)
			ctx.Program = (needsUniforms ? _currentProgram : nullptr);
			if (needsStripBuilder) {
				// GX textures are not padded, so strip UVs pass through 1:1 (contract parity with the PVR)
				ctx.UvScaleU = 1.0f;
				ctx.UvScaleV = 1.0f;
			}

			// Every quad-family effect is the transpiled form of its shader's fixed_function block
			// (masks, outline, shields, colorized, palette remap, the default sprites - batched
			// twins and palette variants included - and the geometry-synthesized iris fan and
			// warped background)
			generated->Fn(ctx);
			// The dispatch end of this instance: a pass the merger buffered but never paired
			// (a single default pass, or FrozenMask with a zero ice alpha) goes out unmerged
			ctx.FlushPendingQuad();
		}

		// Leave the pipe in the default combine for the next draw
		SetTevMode(TevMode::Modulate);
	}
}
