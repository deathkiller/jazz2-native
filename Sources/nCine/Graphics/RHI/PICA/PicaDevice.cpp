#include "PicaDevice.h"
#include "PicaBuffer.h"
#include "PicaShaderProgram.h"
#include "PicaRenderTarget.h"
#include "PicaTexture.h"
#include "../FixedFunctionPass.h"
#include "../LightingCombine.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <3ds.h>
#include <citro3d.h>

// The passthrough vertex program (Sources/Shaders/Pica/Sprite.v.pica), assembled by picasso and wrapped by
// bin2s at build time - see the 3DS arm of cmake/ncine_extra_sources.cmake
#include "Sprite_shbin.h"

namespace nCine::RHI::PICA
{
	namespace
	{
		// The top screen is 400x240 as seen by the player, but its framebuffer is stored rotated by 90 degrees:
		// 240 texels wide and 400 tall, the first row being the RIGHT edge of the panel. Mtx_OrthoTilt() maps
		// screen coordinates onto it for the vertex program, and the scissor mapping in ApplyScissor() does the
		// same for the rasterizer's clip rectangle.
		constexpr std::int32_t FramebufferWidth = PicaDevice::ScreenHeight;
		constexpr std::int32_t FramebufferHeight = PicaDevice::ScreenWidth;

		// How the finished frame is moved into the LCD's framebuffer by the transfer engine: 16-bit in and out
		// (the target and the display are both RGB565), untiled output, no scaling
		constexpr std::uint32_t DisplayTransferFlags =
			GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
			GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
			GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

		// Vertices are handed to the GPU out of a per-frame bump arena in the LINEAR heap - the part of main
		// memory the GPU addresses physically - rather than out of the command buffer: building a batch is as
		// cheap as any other memory write, and each draw call writes back exactly the range it is about to
		// submit. Two arenas alternate, because the GPU consumes a frame's command list while the next frame
		// is already being built: frame N+1 writes the arena frame N-1 used, and C3D_FrameBegin() has waited
		// for frame N-1's commands to finish before frame N+1's first draw touches it.
		//
		// 384 KB holds ~19600 vertices at 20 bytes each. A busy frame (the weapon wheel open over a level) is
		// ~90 draw calls of a few hundred quads at six vertices each, well inside that; the arena reports when
		// it fills and drops the rest of the frame rather than writing past its end.
		constexpr std::size_t FrameArenaBytes = 384 * 1024;
		// Per-frame data that is not vertices (the lightmap texture the lighting hook generates), tiled and
		// 128-byte aligned the way the texture unit wants it; doubled for the same reason as the arenas
		constexpr std::size_t FrameDataBytes = 128 * 1024;

		struct Vertex
		{
			float X, Y;
			float U, V;
			std::uint32_t Color;
		};

		// The GPU takes texture coordinates normalized to the bound texture, so the texel coordinates the
		// corner synthesis works in (they are what the page selection needs) are scaled by the page's inverse
		// padded size when a vertex is written; those two factors ride along in the draw state
		bool warnedFrameArenaFull = false;

		// Per-frame draw statistics, logged when the switch below is on
		constexpr bool TraceDrawStatistics = false;
		std::uint32_t frameDrawCalls = 0;
		std::uint32_t frameVertices = 0;
		std::uint32_t frameSkippedDraws = 0;

		// The DefaultSprite / DefaultBatchedSprites instance layout is a hard contract of the shader family
		// (std140 offsets within the InstanceBlock / each batched Instance) - identical to the software and
		// PowerVR backends' decode (see SwDevice.cpp / PvrDevice.cpp)
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

		// The clamp is applied to the CONVERTED INTEGER rather than to the float, through a signed int - the
		// form that compiles to a straight-line sequence (see the GU backend for the measurement behind it)
		inline std::uint8_t QuantizeChannel(float v)
		{
			const std::int32_t q = std::int32_t(v * 255.0f + 0.5f);
			return std::uint8_t(q < 0 ? 0 : (q > 255 ? 255 : q));
		}

		// Straight to the 4 bits an RGBA4 channel actually keeps, skipping the round trip through 8 bits
		inline std::uint32_t Quantize4Bit(float v)
		{
			const std::int32_t q = std::int32_t(v * 15.0f + 0.5f);
			return std::uint32_t(q < 0 ? 0 : (q > 15 ? 15 : q));
		}

		// A vertex colour is four bytes in memory order R, G, B, A - which the vertex program reads as
		// v2 = (r, g, b, a) / 255 - and the texture-combiner constant colour has the same layout
		inline std::uint32_t PackRgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return std::uint32_t(r) | (std::uint32_t(g) << 8) | (std::uint32_t(b) << 16) | (std::uint32_t(a) << 24);
		}

		// Maps a pipeline-neutral blend factor onto the GPU's, which are exactly OpenGL's set
		GPU_BLENDFACTOR MapBlend(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero:					return GPU_ZERO;
				case nCine::BlendingFactor::One:					return GPU_ONE;
				case nCine::BlendingFactor::SrcColor:				return GPU_SRC_COLOR;
				case nCine::BlendingFactor::OneMinusSrcColor:		return GPU_ONE_MINUS_SRC_COLOR;
				case nCine::BlendingFactor::DstColor:				return GPU_DST_COLOR;
				case nCine::BlendingFactor::OneMinusDstColor:		return GPU_ONE_MINUS_DST_COLOR;
				case nCine::BlendingFactor::SrcAlpha:				return GPU_SRC_ALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha:		return GPU_ONE_MINUS_SRC_ALPHA;
				case nCine::BlendingFactor::DstAlpha:				return GPU_DST_ALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha:		return GPU_ONE_MINUS_DST_ALPHA;
				case nCine::BlendingFactor::SrcAlphaSaturate:		return GPU_SRC_ALPHA_SATURATE;
				case nCine::BlendingFactor::ConstantColor:			return GPU_CONSTANT_COLOR;
				case nCine::BlendingFactor::OneMinusConstantColor:	return GPU_ONE_MINUS_CONSTANT_COLOR;
				case nCine::BlendingFactor::ConstantAlpha:			return GPU_CONSTANT_ALPHA;
				case nCine::BlendingFactor::OneMinusConstantAlpha:	return GPU_ONE_MINUS_CONSTANT_ALPHA;
				default:											return GPU_ONE;
			}
		}

		GPU_TEXTURE_WRAP_PARAM MapWrap(SamplerWrapping wrap)
		{
			// Only a store whose real size is already a power of two tiles correctly: the GPU wraps at the
			// TEXTURE size, which is the padded one. That is exactly the case for the tiling layers of the
			// legacy main menu (16x16 / 32x32 / 128x128), the only content that asks for it.
			switch (wrap) {
				case SamplerWrapping::Repeat: return GPU_REPEAT;
				case SamplerWrapping::MirroredRepeat: return GPU_MIRRORED_REPEAT;
				default: return GPU_CLAMP_TO_EDGE;
			}
		}

		/** @brief The texture-combiner programs a draw can run - the PICA200's equivalent of the GX's TEV presets */
		enum class TexEnvProgram : std::uint8_t
		{
			Untextured,		/**< Vertex colour only */
			Modulate,		/**< texel * vertex colour (the default) */
			ModulateX2,		/**< Modulate with the combiner's x2 output scale on the colour */
			ModulateX4,		/**< Modulate with the combiner's x4 output scale on the colour */
			Silhouette,		/**< Vertex colour where the texture has alpha (flat masks, shadows, glows) */
			TintMix			/**< mix(texel, vertex colour, vertex alpha) with an opaque result */
		};
	}

	/**
		@brief The whole GPU state one primitive is drawn under

		Consecutive primitives whose state matches field for field are accumulated into a single
		`C3D_DrawArrays`, so a tile layer, a text run or a particle batch costs one draw call. The state is
		derived once per draw from the material (texture page, filter, wrap, blend) and then adjusted per
		pass by the effect (blend override, combiner program) - which is what makes the batching automatic
		rather than something the effects have to think about.

		At namespace scope rather than in the anonymous namespace below because EffectContext names it in a
		member type and itself has to be externally visible (see the note there).
	*/
	struct DrawState
	{
		const void* TextureData = nullptr;		// nullptr = untextured
		GPU_TEXCOLOR TextureFormat = GPU_RGBA4;
		std::int32_t TextureWidth = 0, TextureHeight = 0;	// The padded page size the GPU is told about
		float InvTextureWidth = 0.0f, InvTextureHeight = 0.0f;	// Texel coordinates -> normalized ones
		GPU_TEXTURE_FILTER_PARAM Filter = GPU_NEAREST;
		GPU_TEXTURE_WRAP_PARAM WrapU = GPU_CLAMP_TO_EDGE, WrapV = GPU_CLAMP_TO_EDGE;
		TexEnvProgram Env = TexEnvProgram::Modulate;
		std::uint32_t EnvColor = 0;
		GPU_BLENDFACTOR BlendSrc = GPU_SRC_ALPHA, BlendDst = GPU_ONE_MINUS_SRC_ALPHA;
		GPU_BLENDFACTOR BlendSrcAlpha = GPU_SRC_ALPHA, BlendDstAlpha = GPU_ONE_MINUS_SRC_ALPHA;
		bool BlendEnabled = true;
	};

	namespace
	{
		bool SameDrawState(const DrawState& a, const DrawState& b)
		{
			return a.TextureData == b.TextureData && a.TextureFormat == b.TextureFormat &&
				a.TextureWidth == b.TextureWidth && a.TextureHeight == b.TextureHeight &&
				a.Filter == b.Filter && a.WrapU == b.WrapU && a.WrapV == b.WrapV &&
				a.Env == b.Env && a.EnvColor == b.EnvColor &&
				a.BlendSrc == b.BlendSrc && a.BlendDst == b.BlendDst &&
				a.BlendSrcAlpha == b.BlendSrcAlpha && a.BlendDstAlpha == b.BlendDstAlpha &&
				a.BlendEnabled == b.BlendEnabled;
		}

		// The GPU session
		C3D_RenderTarget* screenTarget = nullptr;
		DVLB_s* shaderBinary = nullptr;
		shaderProgram_s shaderProgram;
		std::int8_t projectionUniform = -1;
		C3D_Mtx screenProjection;
		C3D_AttrInfo vertexAttributes;

		// citro3d keeps a POINTER to the bound texture descriptor and reads it when the next draw call is
		// issued, so one descriptor per unit, rewritten before each bind, is all the binding costs
		C3D_Tex boundTexture;

		Vertex* frameArena[2] = { nullptr, nullptr };
		std::uint8_t* frameData[2] = { nullptr, nullptr };
		std::int32_t arenaIndex = 0;
		std::size_t frameArenaUsed = 0;		// In vertices
		std::size_t frameDataUsed = 0;		// In bytes

		DrawState batchState;
		// The primitive is NOT part of DrawState. It is picked at submission time, so keeping it in the
		// material state meant every submitter had to take a private copy of that state just to stamp one
		// field into it. It still takes part in the batch key: a draw call is one primitive type.
		GPU_Primitive_t batchPrim = GPU_TRIANGLES;
		std::size_t batchFirstVertex = 0;
		std::int32_t batchVertexCount = 0;

		DrawState appliedState;
		bool appliedStateValid = false;

		// Blocks whose owner dropped them while a command list could still reference them (see
		// PicaDevice::DeferredLinearFree); freed once two presents have gone by
		struct DeferredBlock
		{
			void* Block;
			std::uint32_t Frame;
			bool InVram;
		};
		std::vector<DeferredBlock> deferredBlocks;

		void ApplyTexEnv(const DrawState& state)
		{
			C3D_TexEnv* env = C3D_GetTexEnv(0);
			C3D_TexEnvInit(env);
			switch (state.Env) {
				case TexEnvProgram::Untextured:
					C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
					C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
					break;
				case TexEnvProgram::Silhouette:
					// Flat colour where the texture has alpha - the mask/outline/shield family. The pass colour
					// arrives as the vertex colour, so the combiner only has to say "take RGB from the vertex,
					// coverage from the texel times the vertex alpha".
					C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
					C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
					C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
					C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
					break;
				case TexEnvProgram::TintMix:
					// mix(texel, colour, alpha) with an opaque result: GPU_INTERPOLATE computes
					// src0 * src2 + src1 * (1 - src2), so the pass colour goes in as src0, the texel as src1
					// and the pass alpha as the weight; the alpha channel is the constant colour's, set opaque
					C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
					C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
					C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
					C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
					C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
					break;
				case TexEnvProgram::ModulateX2:
				case TexEnvProgram::ModulateX4:
					C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
					C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
					// The output scale applies to the colour, exactly like GL_RGB_SCALE on the legacy GL backend
					C3D_TexEnvScale(env, C3D_RGB, state.Env == TexEnvProgram::ModulateX4 ? GPU_TEVSCALE_4 : GPU_TEVSCALE_2);
					break;
				default:
					C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
					C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
					break;
			}
			C3D_TexEnvColor(env, state.EnvColor);
		}

		void ApplyDrawState(const DrawState& state)
		{
			const bool textureChanged = (!appliedStateValid ||
				appliedState.TextureData != state.TextureData || appliedState.TextureFormat != state.TextureFormat ||
				appliedState.TextureWidth != state.TextureWidth || appliedState.TextureHeight != state.TextureHeight ||
				appliedState.Filter != state.Filter || appliedState.WrapU != state.WrapU || appliedState.WrapV != state.WrapV);
			if (textureChanged && state.TextureData != nullptr) {
				// The descriptor names the store by address, the padded size and the format; there is no
				// separate "enable" - an untextured draw simply runs a combiner program that never reads the
				// unit, so whatever was bound last stays bound
				std::memset(&boundTexture, 0, sizeof(boundTexture));
				boundTexture.data = const_cast<void*>(state.TextureData);
				boundTexture.fmt = state.TextureFormat;
				boundTexture.size = std::uint32_t(state.TextureWidth) * std::uint32_t(state.TextureHeight) * 2;
				boundTexture.width = std::uint16_t(state.TextureWidth);
				boundTexture.height = std::uint16_t(state.TextureHeight);
				boundTexture.param = GPU_TEXTURE_MODE(GPU_TEX_2D) |
					GPU_TEXTURE_MAG_FILTER(state.Filter) | GPU_TEXTURE_MIN_FILTER(state.Filter) |
					GPU_TEXTURE_WRAP_S(state.WrapU) | GPU_TEXTURE_WRAP_T(state.WrapV);
				C3D_TexBind(0, &boundTexture);
			}
			if (!appliedStateValid || appliedState.Env != state.Env || appliedState.EnvColor != state.EnvColor) {
				ApplyTexEnv(state);
			}
			if (!appliedStateValid || appliedState.BlendEnabled != state.BlendEnabled ||
					(state.BlendEnabled && (appliedState.BlendSrc != state.BlendSrc || appliedState.BlendDst != state.BlendDst ||
						appliedState.BlendSrcAlpha != state.BlendSrcAlpha || appliedState.BlendDstAlpha != state.BlendDstAlpha))) {
				if (state.BlendEnabled) {
					C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, state.BlendSrc, state.BlendDst, state.BlendSrcAlpha, state.BlendDstAlpha);
				} else {
					// The PICA200 has no "blending off" - a logic operation replaces the blender, and COPY is the
					// one that writes the source unchanged
					C3D_ColorLogicOp(GPU_LOGICOP_COPY);
				}
			}
			appliedState = state;
			appliedStateValid = true;
		}

		void FlushBatch()
		{
			if (batchVertexCount <= 0) {
				return;
			}
			ApplyDrawState(batchState);
			Vertex* const base = frameArena[arenaIndex] + batchFirstVertex;
			const std::size_t bytes = std::size_t(batchVertexCount) * sizeof(Vertex);
			// The GPU reads the linear heap without seeing the data cache; this is the writeback that makes the
			// vertices visible to it (the 3DS equivalent of the GX's DCFlushRange)
			GSPGPU_FlushDataCache(base, std::uint32_t(bytes));
			C3D_DrawArrays(batchPrim, std::int32_t(batchFirstVertex), batchVertexCount);
			frameDrawCalls++;
			frameVertices += std::uint32_t(batchVertexCount);
			batchVertexCount = 0;
		}

		/** @brief Reserves per-frame data (a generated texture) the GPU will read, 128-byte aligned */
		void* AllocFrameData(std::size_t size)
		{
			const std::size_t aligned = (frameDataUsed + 127) & ~std::size_t(127);
			if (aligned + size > FrameDataBytes) {
				return nullptr;
			}
			frameDataUsed = aligned + size;
			return frameData[arenaIndex] + aligned;
		}

		/** @brief Reserves @p count vertices of the open (or a new) batch of @p prim drawn under @p state */
		Vertex* AllocVertices(const DrawState& state, GPU_Primitive_t prim, std::int32_t count)
		{
			if (batchVertexCount > 0 && (batchPrim != prim || !SameDrawState(batchState, state))) {
				FlushBatch();
			}
			if (batchVertexCount == 0) {
				batchFirstVertex = frameArenaUsed;
				batchState = state;
				batchPrim = prim;
			}
			if ((frameArenaUsed + std::size_t(count)) * sizeof(Vertex) > FrameArenaBytes) {
				FlushBatch();
				if (!warnedFrameArenaFull) {
					warnedFrameArenaFull = true;
					LOGW("The {} KB per-frame vertex arena is full; the rest of the frame is dropped", FrameArenaBytes / 1024);
				}
				return nullptr;
			}
			Vertex* const result = frameArena[arenaIndex] + frameArenaUsed;
			frameArenaUsed += std::size_t(count);
			batchVertexCount += count;
			return result;
		}

		/**
			@brief Submits one quad as a triangle pair

			Corners 0/1 share the sprite's local x = 1 edge and 2/3 its x = 0 edge, 0/2 the y = 0 edge and 1/3
			the y = 1 edge (see the corner synthesis in Dispatch). The texture coordinates arrive as texel
			indices of the bound page and are normalized here.
		*/
		void SubmitQuadPrimitive(const DrawState& state, const float* px, const float* py, const float* pu, const float* pv,
			std::uint32_t rgba, float dx = 0.0f, float dy = 0.0f)
		{
			Vertex* const v = AllocVertices(state, GPU_TRIANGLES, 6);
			if (v == nullptr) {
				return;
			}
			// The two triangles of the v0..v3 strip; culling is off, so the winding is free
			static const std::int32_t Order[6] = { 0, 1, 2, 2, 1, 3 };
			for (std::int32_t i = 0; i < 6; i++) {
				const std::int32_t o = Order[i];
				v[i] = { px[o] + dx, py[o] + dy, pu[o] * state.InvTextureWidth, pv[o] * state.InvTextureHeight, rgba };
			}
		}

		/** @brief Submits a triangle strip of its own draw call (arbitrary synthesized geometry) */
		void SubmitStripPrimitive(const DrawState& state, const float* px, const float* py, const float* pu, const float* pv,
			std::int32_t count, const std::uint32_t* rgba, std::uint32_t flatRgba, float dx, float dy)
		{
			// A strip cannot share a draw call with anything else - the GPU would connect it to whatever
			// vertices follow - so it is bracketed by flushes
			FlushBatch();
			Vertex* const v = AllocVertices(state, GPU_TRIANGLE_STRIP, count);
			if (v == nullptr) {
				return;
			}
			for (std::int32_t i = 0; i < count; i++) {
				v[i].X = px[i] + dx;
				v[i].Y = py[i] + dy;
				v[i].U = (pu != nullptr ? pu[i] * state.InvTextureWidth : 0.0f);
				v[i].V = (pv != nullptr ? pv[i] * state.InvTextureHeight : 0.0f);
				v[i].Color = (rgba != nullptr ? rgba[i] : flatRgba);
			}
			FlushBatch();
		}

		/** @brief Folds a pass descriptor's blend mode and combiner preset into a copy of the material state */
		void ApplyPassToState(DrawState& state, const FixedFunctionPass& pass)
		{
			switch (pass.Blend) {
				case FixedFunctionPass::BlendMode::Additive:
					// Deliberately SRCALPHA rather than a literal ONE source factor, matching the PVR: it is
					// the additive mechanism the split-multiplier passes rely on, whose contributions are
					// scaled by the pass alpha
					state.BlendEnabled = true;
					state.BlendSrc = GPU_SRC_ALPHA;
					state.BlendDst = GPU_ONE;
					state.BlendSrcAlpha = GPU_SRC_ALPHA;
					state.BlendDstAlpha = GPU_ONE;
					break;
				case FixedFunctionPass::BlendMode::Opaque:
					state.BlendEnabled = false;
					break;
				case FixedFunctionPass::BlendMode::Alpha:
					state.BlendEnabled = true;
					state.BlendSrc = GPU_SRC_ALPHA;
					state.BlendDst = GPU_ONE_MINUS_SRC_ALPHA;
					state.BlendSrcAlpha = GPU_SRC_ALPHA;
					state.BlendDstAlpha = GPU_ONE_MINUS_SRC_ALPHA;
					break;
				default:
					break;		// The material's own blending
			}

			switch (pass.Tev) {
				case FixedFunctionPass::TevPreset::Silhouette:
					state.Env = TexEnvProgram::Silhouette;
					break;
				case FixedFunctionPass::TevPreset::ModulateX2:
					state.Env = TexEnvProgram::ModulateX2;
					break;
				case FixedFunctionPass::TevPreset::ModulateX4:
					state.Env = TexEnvProgram::ModulateX4;
					break;
				case FixedFunctionPass::TevPreset::TintMix:
					// The lerp needs an opaque result, which the combiner takes from the constant colour's alpha
					state.Env = TexEnvProgram::TintMix;
					state.EnvColor = PackRgba(0, 0, 0, 255);
					break;
				default:
					// Modulate. LumaRamp is the one preset with no form here - it is a six-stage GX combiner
					// program built on swap tables the PICA200 lacks - and the transpiler rejects it outside a gx
					// block, so it cannot arrive here.
					state.Env = TexEnvProgram::Modulate;
					break;
			}
			if (state.TextureData == nullptr) {
				state.Env = TexEnvProgram::Untextured;
			}
		}
	}

	// EffectContext deliberately has EXTERNAL linkage (namespace scope, though it is still defined only in
	// this translation unit), for the same reason as on the PVR: the effect-table struct below is at
	// namespace scope - so the backend's ShaderProgram can forward-declare it and hold a typed entry
	// pointer - and names EffectContext in a member type.
	// ---------------------------------------------------------- fixed-function quad effects
	//
	// The quad-family effects are expressed as FixedFunctionPass descriptors handed to this EffectContext -
	// the structural contract documented in FixedFunctionPass.h, implemented here against the GPU's
	// batching submission helpers above. The per-effect functions themselves are GENERATED from the
	// shaders' void fixed_function([pica]) blocks by the ShaderCompiler, exactly as on the PVR, the GX and
	// the GU (Shaders/Generated/PicaGeneratedEffects.h, included below), so this file contains no
	// effect-specific code at all.

	struct EffectContext
	{
		// Matches the GX's capacity rather than the PVR's, because nothing here needs the geometry split
		// into small pieces: the GPU takes a strip of any length in one draw call
		static constexpr std::int32_t MaxStripVertices = 16;

		// Decoded instance data of the draw being dispatched (the documented contract)
		const float* InstanceColor;
		float TexelW;
		float TexelH;
		bool Batched;

		// The GPU state the material resolved to, and the current instance's corner arrays (already in
		// raster pixels and texel-space texture coordinates)
		const DrawState* Material;
		const float* Px;
		const float* Py;
		const float* Pu;
		const float* Pv;
		const float* TexRect;

		// Pre-clip quad geometry (the quad_origin/quad_axis_* built-ins), the program (for resolved
		// uniforms) and the conversion from the shader's normalized texture space into the texel
		// coordinates the corner synthesis works in, page origin included
		float OriginX, OriginY;
		float AxisXx, AxisXy;
		float AxisYx, AxisYy;
		const PicaShaderProgram* Program;
		float UvScaleU, UvScaleV;
		float UvBiasU, UvBiasV;

		// The strip builder scratch; colours are packed at set time (same quantization as the quad path, so
		// identical float inputs produce identical vertex words)
		float StripX[MaxStripVertices];
		float StripY[MaxStripVertices];
		float StripU[MaxStripVertices];
		float StripV[MaxStripVertices];
		std::uint32_t StripRgba[MaxStripVertices];

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
				StripU[i] = u * UvScaleU - UvBiasU;
				StripV[i] = v * UvScaleV - UvBiasV;
			}
		}
		void SetStripVertexColor(std::int32_t i, float r, float g, float b, float a)
		{
			if (std::uint32_t(i) < std::uint32_t(MaxStripVertices)) {
				StripRgba[i] = PackRgba(QuantizeChannel(r), QuantizeChannel(g), QuantizeChannel(b), QuantizeChannel(a));
			}
		}

		// Whether a UV span can be mapped onto the screen at all (a zero texRect has no scale)
		bool HasTexelStep() const { return TexRect[0] != 0.0f && TexRect[2] != 0.0f; }
		// Maps a span in the sprite's UV space onto the quad's on-screen extent - the texel step the
		// Outline ring taps use. The corners are already in raster space, so the result is a raster-space
		// displacement.
		float TexelToScreenX(float uvSpan) const { return (Px[0] - Px[2]) * (uvSpan / TexRect[0]); }
		float TexelToScreenY(float uvSpan) const { return (Py[1] - Py[0]) * (uvSpan / TexRect[2]); }
		// The documented texel_size() built-in of the fixed_function contract: the Outline shader family
		// carries the sprite's UV-space texel size in its instance color.xy (exactly like the GLSL derives
		// its tap offsets), folded through the raster-space conversion above
		float TexelStepX() const { return TexelToScreenX(InstanceColor[0]); }
		float TexelStepY() const { return TexelToScreenY(InstanceColor[1]); }

		// One quad draw for a pass the combiner can express directly (no offset colour left on it)
		void SubmitQuadPass(const FixedFunctionPass& pass)
		{
			DrawState state = *Material;
			ApplyPassToState(state, pass);
			const std::uint32_t rgba = PackRgba(QuantizeChannel(pass.Color[0]), QuantizeChannel(pass.Color[1]),
				QuantizeChannel(pass.Color[2]), QuantizeChannel(pass.Color[3]));
			SubmitQuadPrimitive(state, Px, Py, Pu, Pv, rgba, pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		/*
			Submits one pass over the current instance's quad. A pass carrying an offset colour is
			EXPANDED here into a modulate draw and an additive silhouette draw, exactly as the GU and legacy
			GL backends do - see GuDevice.cpp for the derivation that shows the pair to be term for term the
			PVR's single draw. (The PICA200's combiner could add a constant to the modulated texel in a second
			stage, but the offset would then be added where the texel is transparent as well, which is not
			what the effects mean; the silhouette form keeps the coverage.) When colour.rgb is zero - the
			"pure offset colour" idiom of the mask/outline/shield family - the pair collapses to one draw.
		*/
		void SubmitQuad(const FixedFunctionPass& pass)
		{
			if (pass.HasOffsetColor) {
				const bool modulateVisible = (pass.Color[0] != 0.0f || pass.Color[1] != 0.0f || pass.Color[2] != 0.0f);
				if (modulateVisible) {
					FixedFunctionPass modulate = pass;
					modulate.HasOffsetColor = false;
					SubmitQuadPass(modulate);
				}
				FixedFunctionPass silhouette = pass;
				silhouette.HasOffsetColor = false;
				silhouette.Tev = FixedFunctionPass::TevPreset::Silhouette;
				silhouette.Color[0] = pass.OffsetColor[0];
				silhouette.Color[1] = pass.OffsetColor[1];
				silhouette.Color[2] = pass.OffsetColor[2];
				// Only the second half of a split pair is additive; a collapsed one keeps the pass's blend
				// (the alpha-over the effects rely on) so it still attenuates the destination itself
				if (modulateVisible) {
					silhouette.Blend = FixedFunctionPass::BlendMode::Additive;
				}
				SubmitQuadPass(silhouette);
				return;
			}
			SubmitQuadPass(pass);
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
			// Same intent mapping as the GX's SubmitStrip: an offset colour becomes the silhouette form
			// filled flat with it at the pass alpha. Deliberately NOT split into the exact two draws
			// SubmitQuad builds - a strip's geometry would have to be resubmitted, and no effect asks for it
			// (both strip users, the iris and the warp bands, carry no offset colour at all).
			FixedFunctionPass effective = pass;
			if (pass.HasOffsetColor) {
				effective.HasOffsetColor = false;
				effective.Tev = FixedFunctionPass::TevPreset::Silhouette;
				effective.Color[0] = pass.OffsetColor[0];
				effective.Color[1] = pass.OffsetColor[1];
				effective.Color[2] = pass.OffsetColor[2];
			}
			DrawState state = *Material;
			ApplyPassToState(state, effective);
			const std::uint32_t rgba = PackRgba(QuantizeChannel(effective.Color[0]), QuantizeChannel(effective.Color[1]),
				QuantizeChannel(effective.Color[2]), QuantizeChannel(effective.Color[3]));
			SubmitStripPrimitive(state, StripX, StripY, StripU, StripV, count, nullptr, rgba,
				pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		// Shaded (per-vertex-colour) strip out of the builder scratch: UNTEXTURED - a gradient has no texture
		// to modulate - unless the pass's preset consumes the texel as well (TintMix), whose blend comes
		// from the pass
		void SubmitStripShaded(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			DrawState state = *Material;
			const bool textured = (state.TextureData != nullptr && pass.Tev == FixedFunctionPass::TevPreset::TintMix);
			if (!textured) {
				state.TextureData = nullptr;
			}
			ApplyPassToState(state, pass);
			SubmitStripPrimitive(state, StripX, StripY, textured ? StripU : nullptr, textured ? StripV : nullptr,
				count, StripRgba, 0, pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}
	};
}

// The per-effect functions themselves are GENERATED: the ShaderCompiler transpiles each shader's
// fixed_function block into C++ over the EffectContext defined above (the type name itself is the
// "using EffectContext = ...;" alias the header expects - the anonymous namespace above is the same
// namespace the header's payload reopens within this translation unit). Included at global scope
// because the header opens nCine::RHI::PICA itself. Programs with no fixed_function block are absent
// from its table and their draws are skipped with a one-time warning, exactly as on the other
// consoles - which is also what keeps the CPU-lightmap tier (Lighting, whose Combine hook IS in the
// table) from painting its light quads over the scene.
#include "../../../../Shaders/Generated/PicaGeneratedEffects.h"

namespace nCine::RHI::PICA
{
	const FixedFunctionGeneratedEffect* PicaDevice::FindGeneratedEffect(const char* program, const char* variant)
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

	PicaDevice::BlendingState PicaDevice::_blending;
	PicaDevice::DepthTestState PicaDevice::_depthTest;
	PicaDevice::CullFaceState PicaDevice::_cullFace;
	PicaDevice::ScissorState PicaDevice::_scissor;
	Recti PicaDevice::_viewport(0, 0, 0, 0);
	Colorf PicaDevice::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	PicaShaderProgram* PicaDevice::_currentProgram = nullptr;
	const PicaTexture* PicaDevice::_boundTextures[PicaDevice::MaxTextureUnits] = {};
	PicaDevice::UniformRange PicaDevice::_boundUniformRanges[PicaDevice::MaxUniformBindings] = {};
	PicaRenderTarget* PicaDevice::_currentRenderTarget = nullptr;

	bool PicaDevice::_picaInitialized = false;
	bool PicaDevice::_frameOpen = false;
	std::int32_t PicaDevice::_logicalWidth = PicaDevice::ScreenWidth;
	std::int32_t PicaDevice::_logicalHeight = PicaDevice::ScreenHeight;
	std::uint32_t PicaDevice::_sceneCounter = 0;

	PicaTexture* PicaDevice::_paletteTexture = nullptr;
	std::uint32_t PicaDevice::_paletteGeneration = 1;

	std::vector<PicaDevice::PendingSoftwareLight> PicaDevice::_pendingSoftwareLights;

	namespace
	{
		// Which surface the GPU is currently rendering into, so a target switch only re-emits the commands
		// when it really changes
		const PicaRenderTarget* appliedTarget = nullptr;
		bool appliedTargetValid = false;
		// The scissor rect last programmed, in raster pixels of the current target
		std::int32_t appliedScissor[4] = { -1, -1, -1, -1 };
		// Whether the frame's command list has been split already (see Clear), in which case a memory fill
		// enqueued now runs after everything recorded so far - and whether anything was drawn at all
		std::uint32_t frameSplits = 0;

		/** @brief The clear value the transfer engine fills a 16-bit RGB565 surface with */
		std::uint32_t PackClear565(const Colorf& color)
		{
			const std::uint32_t r = QuantizeChannel(color.R) >> 3;
			const std::uint32_t g = QuantizeChannel(color.G) >> 2;
			const std::uint32_t b = QuantizeChannel(color.B) >> 3;
			const std::uint32_t value = (r << 11) | (g << 5) | b;
			return value | (value << 16);
		}
	}

	// ------------------------------------------------------------------ session

	bool PicaDevice::InitializePica()
	{
		if (_picaInitialized) {
			return true;
		}

		// The command buffer lives in the linear heap as well; the default 256 KB holds a frame of state
		// changes and draw calls many times over - the vertices are NOT in it
		if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
			LOGE("C3D_Init() failed");
			return false;
		}

		// The screen target is the rotated 240x400 colour buffer the transfer engine copies to the top LCD
		// every frame. RGB565 is chosen over RGBA8 because the game's 2D output needs no destination alpha
		// and the panel cannot show more, so the wider format would only double the fill bandwidth and the
		// transfer; no depth buffer at all, the game draws in painter's order.
		screenTarget = C3D_RenderTargetCreate(FramebufferWidth, FramebufferHeight, GPU_RB_RGB565, -1);
		if (screenTarget == nullptr) {
			LOGE("Cannot create the screen render target");
			C3D_Fini();
			return false;
		}
		C3D_RenderTargetSetOutput(screenTarget, GFX_TOP, GFX_LEFT, DisplayTransferFlags);

		// The passthrough vertex program and its one uniform
		shaderBinary = DVLB_ParseFile(const_cast<std::uint32_t*>(reinterpret_cast<const std::uint32_t*>(Sprite_shbin)), std::uint32_t(Sprite_shbin_size));
		if (shaderBinary == nullptr) {
			LOGE("Cannot parse the vertex program");
			C3D_RenderTargetDelete(screenTarget);
			screenTarget = nullptr;
			C3D_Fini();
			return false;
		}
		shaderProgramInit(&shaderProgram);
		shaderProgramSetVsh(&shaderProgram, &shaderBinary->DVLE[0]);
		C3D_BindProgram(&shaderProgram);
		projectionUniform = shaderInstanceGetUniformLocation(shaderProgram.vertexShader, "projection");

		// The vertex layout (see Vertex): two floats of position, two of texture coordinates, four bytes of colour
		AttrInfo_Init(&vertexAttributes);
		AttrInfo_AddLoader(&vertexAttributes, 0, GPU_FLOAT, 2);
		AttrInfo_AddLoader(&vertexAttributes, 1, GPU_FLOAT, 2);
		AttrInfo_AddLoader(&vertexAttributes, 2, GPU_UNSIGNED_BYTE, 4);
		C3D_SetAttrInfo(&vertexAttributes);

		for (std::int32_t i = 0; i < 2; i++) {
			frameArena[i] = static_cast<Vertex*>(linearAlloc(FrameArenaBytes));
			frameData[i] = static_cast<std::uint8_t*>(linearMemAlign(FrameDataBytes, 128));
			if (frameArena[i] == nullptr || frameData[i] == nullptr) {
				LOGE("Out of linear memory allocating the per-frame arenas ({} B free)", linearSpaceFree());
				for (std::int32_t j = 0; j < 2; j++) {
					linearFree(frameArena[j]);
					linearFree(frameData[j]);
					frameArena[j] = nullptr;
					frameData[j] = nullptr;
				}
				shaderProgramFree(&shaderProgram);
				DVLB_Free(shaderBinary);
				shaderBinary = nullptr;
				C3D_RenderTargetDelete(screenTarget);
				screenTarget = nullptr;
				C3D_Fini();
				return false;
			}
		}

		// Screen space to clip space for the rotated framebuffer, y running down like the engine's raster
		Mtx_OrthoTilt(&screenProjection, 0.0f, float(ScreenWidth), float(ScreenHeight), 0.0f, 1.0f, -1.0f, true);

		// The game draws 2D in painter's order, so there is nothing for the depth test to arbitrate, and
		// nothing is culled; only the first combiner stage is ever programmed, the other five pass through
		C3D_DepthTest(false, GPU_GEQUAL, GPU_WRITE_COLOR);
		C3D_CullFace(GPU_CULL_NONE);
		C3D_AlphaTest(false, GPU_ALWAYS, 0);
		for (std::int32_t i = 1; i < 6; i++) {
			C3D_TexEnvInit(C3D_GetTexEnv(i));
		}

		_picaInitialized = true;
		_frameOpen = false;
		LOGI("PICA200 session initialized: {}x{} RGB565 screen target, {} KB command buffer, 2x{} KB vertex arenas, {} KB VRAM free, {} KB linear heap free",
			ScreenWidth, ScreenHeight, C3D_DEFAULT_CMDBUF_SIZE / 1024, FrameArenaBytes / 1024, vramSpaceFree() / 1024, linearSpaceFree() / 1024);
		return true;
	}

	void PicaDevice::ShutdownPica()
	{
		if (!_picaInitialized) {
			return;
		}
		if (_frameOpen) {
			FlushBatch();
			C3D_FrameEnd(GX_CMDLIST_FLUSH);
			_frameOpen = false;
		}
		// C3D_Fini() waits for the GPU to finish everything queued, so every deferred block is free to go after it
		C3D_RenderTargetDelete(screenTarget);
		screenTarget = nullptr;
		shaderProgramFree(&shaderProgram);
		DVLB_Free(shaderBinary);
		shaderBinary = nullptr;
		C3D_Fini();
		for (const DeferredBlock& block : deferredBlocks) {
			if (block.InVram) {
				vramFree(block.Block);
			} else {
				linearFree(block.Block);
			}
		}
		deferredBlocks.clear();
		for (std::int32_t i = 0; i < 2; i++) {
			linearFree(frameArena[i]);
			linearFree(frameData[i]);
			frameArena[i] = nullptr;
			frameData[i] = nullptr;
		}
		_picaInitialized = false;
	}

	void PicaDevice::EnsureFrame()
	{
		if (!_picaInitialized || _frameOpen) {
			return;
		}
		// Waits for the previous frame's commands to finish and for the vblank, which is what paces the game
		// to the display - and what makes the arena this frame takes over safe to overwrite
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		_frameOpen = true;
		arenaIndex ^= 1;
		frameArenaUsed = 0;
		frameDataUsed = 0;
		frameSplits = 0;

		// A fresh frame re-sends everything: citro3d tracks its own dirty state across frames, but the batch
		// state comparison below is ours and nothing about the previous frame's binding can be relied upon
		C3D_BufInfo* bufInfo = C3D_GetBufInfo();
		BufInfo_Init(bufInfo);
		BufInfo_Add(bufInfo, frameArena[arenaIndex], sizeof(Vertex), 3, 0x210);
		C3D_BindProgram(&shaderProgram);
		appliedStateValid = false;
		appliedTargetValid = false;
		appliedScissor[0] = -1;
	}

	void PicaDevice::ApplyDrawTarget()
	{
		if (appliedTargetValid && appliedTarget == _currentRenderTarget) {
			return;
		}
		FlushBatch();

		if (_currentRenderTarget != nullptr) {
			PicaTexture* texture = _currentRenderTarget->GetColorTexture(0);
			C3D_RenderTarget* target = (texture != nullptr ? texture->GetRenderTarget() : nullptr);
			if (target == nullptr) {
				// No surface to render into; the draws will be skipped by their own checks
				return;
			}
			const PicaTexture::Page* page = texture->AcquirePage(0, 0);
			const float paddedW = float(page != nullptr ? page->PaddedWidth : texture->GetWidth());
			const float paddedH = float(page != nullptr ? page->PaddedHeight : texture->GetHeight());
			C3D_FrameDrawOn(target);
			// A render target is not rotated, so the plain orthographic matrix over the padded store: raster
			// pixel (x, y) lands on texel (x, y), which is what the sampling passes expect
			C3D_Mtx projection;
			Mtx_Ortho(&projection, 0.0f, paddedW, paddedH, 0.0f, 1.0f, -1.0f, true);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projectionUniform, &projection);
		} else {
			C3D_FrameDrawOn(screenTarget);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projectionUniform, &screenProjection);
		}
		appliedTarget = _currentRenderTarget;
		appliedTargetValid = true;
		// The rect is expressed in the target's raster space, so it has to be reprogrammed as well
		appliedScissor[0] = -1;
	}

	void PicaDevice::ApplyScissor()
	{
		std::int32_t x = 0, y = 0, w = ScreenWidth, h = ScreenHeight;
		std::int32_t targetW = ScreenWidth, targetH = ScreenHeight;
		std::int32_t storeH = ScreenHeight;
		if (_currentRenderTarget != nullptr) {
			const PicaTexture* texture = _currentRenderTarget->GetColorTexture(0);
			targetW = (texture != nullptr ? texture->GetWidth() : ScreenWidth);
			targetH = (texture != nullptr ? texture->GetHeight() : ScreenHeight);
			storeH = targetH;
			if (texture != nullptr) {
				const PicaTexture::Page* page = const_cast<PicaTexture*>(texture)->AcquirePage(0, 0);
				if (page != nullptr) {
					storeH = page->PaddedHeight;
				}
			}
			w = targetW;
			h = targetH;
		}
		if (_scissor.Enabled) {
			float scaleX, scaleY;
			GetTargetScale(scaleX, scaleY);
			// The engine hands scissor rectangles in top-down logical coordinates, and both kinds of pass map
			// them straight onto raster rows (see Dispatch for why a render-to-texture pass needs no flip here,
			// unlike on the GE or GX). A target renders 1:1, so its logical height IS its raster height.
			const std::int32_t rasterY = _scissor.Rect.Y;
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
		FlushBatch();
		// The register wants at least one pixel (its extents are stored minus one); an empty rect keeps a
		// one-pixel corner and the dispatch's own culling drops everything anyway
		const std::int32_t fw = (w > 0 ? w : 1), fh = (h > 0 ? h : 1);
		if (_currentRenderTarget == nullptr) {
			// The rotated screen framebuffer: raster y runs along its x axis backwards, raster x along its y
			// axis backwards (the transfer engine and the tilt matrix agree on this orientation)
			C3D_SetScissor(GPU_SCISSOR_NORMAL, std::uint32_t(ScreenHeight - (y + fh)), std::uint32_t(ScreenWidth - (x + fw)),
				std::uint32_t(ScreenHeight - y), std::uint32_t(ScreenWidth - x));
		} else {
			// An unrotated store: raster x is its x, raster y runs against its y (row 0 is clip y = -1)
			C3D_SetScissor(GPU_SCISSOR_NORMAL, std::uint32_t(x), std::uint32_t(storeH - (y + fh)),
				std::uint32_t(x + fw), std::uint32_t(storeH - y));
		}
		appliedScissor[0] = x;
		appliedScissor[1] = y;
		appliedScissor[2] = w;
		appliedScissor[3] = h;
	}

	void PicaDevice::GetTargetScale(float& scaleX, float& scaleY)
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

	void PicaDevice::ProcessDeferredFrees()
	{
		// A block dropped during frame N was referenced by frame N's command list at the latest, and
		// C3D_FrameBegin() of frame N+2 waited for frame N+1's list - so once frame N+2 has been presented
		// the GPU is provably past it
		std::size_t kept = 0;
		for (std::size_t i = 0; i < deferredBlocks.size(); i++) {
			const DeferredBlock& block = deferredBlocks[i];
			if (_sceneCounter >= block.Frame + 3) {
				if (block.InVram) {
					vramFree(block.Block);
				} else {
					linearFree(block.Block);
				}
			} else {
				deferredBlocks[kept++] = block;
			}
		}
		deferredBlocks.resize(kept);
	}

	void PicaDevice::DeferredLinearFree(void* block)
	{
		DeferredFree(block, false);
	}

	void PicaDevice::DeferredFree(void* block, bool inVram)
	{
		if (block == nullptr) {
			return;
		}
		if (!_picaInitialized) {
			// No GPU session, nothing can be reading it
			if (inVram) {
				vramFree(block);
			} else {
				linearFree(block);
			}
			return;
		}
		deferredBlocks.push_back({ block, _sceneCounter, inVram });
	}

	void PicaDevice::PresentFrame()
	{
		if (!_picaInitialized) {
			return;
		}
		if (!_frameOpen) {
			// Nothing was drawn this frame; still run a frame that clears, so the display keeps its pacing
			// and a frame that produced no geometry does not show the previous one's contents
			EnsureFrame();
			C3D_RenderTargetClear(screenTarget, C3D_CLEAR_COLOR, PackClear565(_clearColor), 0);
		}
		FlushBatch();

		if (TraceDrawStatistics) {
			// Once a second at 60 Hz, so the trace stays readable while the game runs
			if ((_sceneCounter % 60) == 0) {
				LOGI("Frame {}: {} draw calls, {} vertices, {} KB of the vertex arena, {} skipped draws, {}% GPU, {}% CPU",
					_sceneCounter, frameDrawCalls, frameVertices, (frameArenaUsed * sizeof(Vertex)) / 1024, frameSkippedDraws,
					std::int32_t(C3D_GetDrawingTime() * 6.0f), std::int32_t(C3D_GetProcessingTime() * 6.0f));
			}
		}

		// Closes the command list and queues the display transfer of the screen target behind it. The flag
		// asks citro3d to flush the command list itself rather than the WHOLE linear heap, which is its
		// default and would cost a cache walk over every texture each frame; everything else the GPU reads
		// was written back where it was written (the arenas per draw call, the stores when built).
		C3D_FrameEnd(GX_CMDLIST_FLUSH);
		_frameOpen = false;
		appliedTargetValid = false;
		_sceneCounter++;

		ProcessDeferredFrees();
		frameDrawCalls = 0;
		frameVertices = 0;
		frameSkippedDraws = 0;
	}

	void PicaDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width > 0 && height > 0) {
			_logicalWidth = width;
			_logicalHeight = height;
		}
	}

	// ------------------------------------------------------------------ state

	void PicaDevice::SetBlendingEnabled(bool enabled) { _blending.Enabled = enabled; }
	void PicaDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}
	PicaDevice::BlendingState PicaDevice::GetBlendingState() { return _blending; }
	void PicaDevice::SetBlendingState(const BlendingState& state) { _blending = state; }

	void PicaDevice::SetDepthTestEnabled(bool enabled) { _depthTest.TestEnabled = enabled; }
	void PicaDevice::SetDepthMaskEnabled(bool enabled) { _depthTest.MaskEnabled = enabled; }
	PicaDevice::DepthTestState PicaDevice::GetDepthTestState() { return _depthTest; }
	void PicaDevice::SetDepthTestState(const DepthTestState& state) { _depthTest = state; }

	void PicaDevice::SetCullFaceEnabled(bool enabled) { _cullFace.Enabled = enabled; }
	PicaDevice::CullFaceState PicaDevice::GetCullFaceState() { return _cullFace; }
	void PicaDevice::SetCullFaceState(const CullFaceState& state) { _cullFace = state; }

	PicaDevice::ScissorState PicaDevice::GetScissorState() { return _scissor; }
	void PicaDevice::SetScissorState(const ScissorState& state) { _scissor = state; }
	void PicaDevice::SetScissor(const Recti& rect)
	{
		// Same contract as the GL device: setting a rect also enables the test (callers like RenderCommand
		// and Viewport rely on it and restore via SetScissorState afterwards)
		_scissor.Enabled = true;
		_scissor.Rect = rect;
	}
	void PicaDevice::SetScissorTestEnabled(bool enabled) { _scissor.Enabled = enabled; }

	Recti PicaDevice::GetViewport() { return _viewport; }
	void PicaDevice::SetViewport(const Recti& rect) { _viewport = rect; }
	void PicaDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
	}

	Colorf PicaDevice::GetClearColor() { return _clearColor; }
	void PicaDevice::SetClearColor(const Colorf& color) { _clearColor = color; }

	void PicaDevice::Clear(ClearFlags flags)
	{
		if (!_picaInitialized || (flags & ClearFlags::Color) != ClearFlags::Color) {
			// Depth and stencil are never cleared: the game draws 2D in painter's order and the targets have
			// no depth buffer at all, so there is nothing to clear
			return;
		}
		EnsureFrame();
		ApplyDrawTarget();
		ApplyScissor();
		// Whatever is still batched was submitted BEFORE this clear and has to reach the list first,
		// otherwise the clear would wipe it (a mid-frame clear is exactly what the render targets do)
		FlushBatch();

		std::int32_t targetW = ScreenWidth, targetH = ScreenHeight;
		C3D_RenderTarget* target = screenTarget;
		if (_currentRenderTarget != nullptr) {
			PicaTexture* texture = _currentRenderTarget->GetColorTexture(0);
			target = (texture != nullptr ? texture->GetRenderTarget() : nullptr);
			if (target == nullptr) {
				return;
			}
			targetW = texture->GetWidth();
			targetH = texture->GetHeight();
		}

		const bool wholeTarget = (appliedScissor[0] <= 0 && appliedScissor[1] <= 0 &&
			appliedScissor[0] + appliedScissor[2] >= targetW && appliedScissor[1] + appliedScissor[3] >= targetH);
		if (wholeTarget) {
			// The transfer engine's memory fill, which ignores the scissor. It is queued behind the commands
			// recorded so far only if those are submitted first, which is what the split does - without it the
			// fill would run before every draw of the frame, including the ones into this very target.
			C3D_FrameSplit(GX_CMDLIST_FLUSH);
			frameSplits++;
			C3D_RenderTargetClear(target, C3D_CLEAR_COLOR, PackClear565(_clearColor), 0);
			appliedStateValid = false;
		} else {
			// A scissored clear is an opaque, untextured quad over the scissor rect, exactly like glClear
			// bounded by the scissor test - the same batching path as everything else
			DrawState state;
			state.TextureData = nullptr;
			state.Env = TexEnvProgram::Untextured;
			state.BlendEnabled = false;
			const float x0 = float(appliedScissor[0]), y0 = float(appliedScissor[1]);
			const float x1 = x0 + float(appliedScissor[2]), y1 = y0 + float(appliedScissor[3]);
			const float px[4] = { x1, x1, x0, x0 };
			const float py[4] = { y0, y1, y0, y1 };
			const float pu[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			SubmitQuadPrimitive(state, px, py, pu, pu, PackRgba(QuantizeChannel(_clearColor.R), QuantizeChannel(_clearColor.G),
				QuantizeChannel(_clearColor.B), QuantizeChannel(_clearColor.A)));
			FlushBatch();
		}
	}

	void PicaDevice::ClearRenderTargetSurface(C3D_RenderTarget* target)
	{
		if (!_picaInitialized || target == nullptr) {
			return;
		}
		if (_frameOpen) {
			// Sequenced behind whatever was recorded into other targets so far (see Clear)
			C3D_FrameSplit(GX_CMDLIST_FLUSH);
			frameSplits++;
		}
		C3D_RenderTargetClear(target, C3D_CLEAR_COLOR, 0, 0);
	}

	// ------------------------------------------------------------------ draw entry points

	void PicaDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		Dispatch(primitive, firstVertex, numVertices);
	}
	void PicaDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, firstVertex, numVertices);
	}
	const std::uint16_t* PicaDevice::ResolveHostIndices(IndexFormat indexFormat, std::uintptr_t indexOffset, std::uint32_t numIndices)
	{
		// Only the mesh dispatches read the vertex stream, and only through the 16-bit indices the pipeline
		// produces (Geometry hands out no other width). Anything else falls back to the non-indexed walk,
		// which is also what a draw with no index buffer bound gets.
		if (indexFormat != IndexFormat::UInt16 || numIndices == 0 || _currentProgram == nullptr) {
			return nullptr;
		}
		const PicaBuffer* ibo = _currentProgram->GetBoundIbo();
		if (ibo == nullptr || indexOffset + numIndices * sizeof(std::uint16_t) > ibo->GetSize()) {
			return nullptr;
		}
		return reinterpret_cast<const std::uint16_t*>(ibo->HostData() + indexOffset);
	}

	void PicaDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		// The index count doubles as the vertex count, unchanged: the procedural quad family never reads the
		// stream and derives its instance count from it, so only the mesh dispatches take the range as well.
		Dispatch(primitive, baseVertex, std::int32_t(numIndices),
			ResolveHostIndices(indexFormat, indexOffset, numIndices), std::int32_t(numIndices));
	}
	void PicaDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices),
			ResolveHostIndices(indexFormat, indexOffset, numIndices), std::int32_t(numIndices));
	}

	FenceHandle PicaDevice::InsertFence()
	{
		return reinterpret_cast<FenceHandle>(std::uintptr_t(1));
	}
	void PicaDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}
	bool PicaDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void PicaDevice::SetupInitialState()
	{
		_blending = BlendingState();
		_depthTest = DepthTestState();
		_cullFace = CullFaceState();
		_scissor = ScissorState();
	}

	// ------------------------------------------------------------------ extensions

	void PicaDevice::BindProgram(PicaShaderProgram* program) { _currentProgram = program; }
	PicaShaderProgram* PicaDevice::CurrentProgram() { return _currentProgram; }

	void PicaDevice::BindTexture(std::uint32_t unit, const PicaTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void PicaDevice::UnbindTexture(const PicaTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
		if (_paletteTexture == texture) {
			_paletteTexture = nullptr;
		}
		// A destroyed texture's store must not stay referenced by an open batch (its memory is only handed
		// back once the GPU is done with the frame, see DeferredLinearFree)
		FlushBatch();
		if (appliedStateValid && appliedState.TextureData != nullptr) {
			appliedStateValid = false;
		}
	}

	const PicaTexture* PicaDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void PicaDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void PicaDevice::SetRenderTarget(PicaRenderTarget* renderTarget)
	{
		// The draw path reacts lazily at the next draw or clear (ApplyDrawTarget), which is also where the
		// open batch is closed - it belongs to the previous surface
		_currentRenderTarget = renderTarget;
	}

	void PicaDevice::UnbindRenderTarget(const PicaRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
		}
		if (appliedTarget == renderTarget) {
			FlushBatch();
			appliedTargetValid = false;
			appliedTarget = nullptr;
		}
	}

	void PicaDevice::UnbindRenderTargetSurface(const C3D_RenderTarget* target)
	{
		static_cast<void>(target);
		// The surface itself is only freed once the GPU is done with the frame, but the citro3d target
		// wrapping it goes right away - so nothing recorded from here on may name it
		FlushBatch();
		appliedTargetValid = false;
		appliedTarget = nullptr;
	}

	// ------------------------------------------------------------------ palette

	void PicaDevice::RegisterPaletteTexture(PicaTexture* texture)
	{
		_paletteTexture = texture;
		NotifyPaletteTextureChanged(texture, 0, texture != nullptr ? texture->GetHeight() : 0);
	}

	void PicaDevice::NotifyPaletteTextureChanged(PicaTexture* texture, std::int32_t firstRow, std::int32_t rowCount)
	{
		static_cast<void>(firstRow);
		static_cast<void>(rowCount);
		if (texture != _paletteTexture) {
			return;
		}
		// Every bake keyed on the old generation rebuilds at its next draw
		_paletteGeneration++;
	}

	namespace
	{
		/** @brief Points the draw state at one page of a texture */
		void ApplyPageToState(DrawState& state, const PicaTexture* texture, const PicaTexture::Page& page)
		{
			state.TextureData = page.Data;
			state.TextureFormat = texture->GetPicaFormat();
			state.TextureWidth = page.PaddedWidth;
			state.TextureHeight = page.PaddedHeight;
			state.InvTextureWidth = 1.0f / float(page.PaddedWidth);
			state.InvTextureHeight = 1.0f / float(page.PaddedHeight);
		}

		/**
			@brief Resolves the texture state of one draw: the page a texture rectangle samples

			@p texRect is the instance's (uSpan, uOffset, vSpan, vOffset). The corner synthesis works in texel
			indices of the page, so the returned scale is simply the source size and the bias is the page
			origin - a single-page texture (everything up to 1024x1024) biases by zero; the vertex writer
			normalizes by the padded page size.
		*/
		bool ResolveTextureState(DrawState& state, PicaTexture* texture, const float* texRect,
			float& uvScaleU, float& uvScaleV, float& uvBiasU, float& uvBiasV)
		{
			const float u0 = texRect[1] * float(texture->GetWidth());
			const float u1 = (texRect[1] + texRect[0]) * float(texture->GetWidth());
			const float v0 = texRect[3] * float(texture->GetHeight());
			const float v1 = (texRect[3] + texRect[2]) * float(texture->GetHeight());
			const PicaTexture::Page* page = texture->AcquirePage(std::int32_t(std::min(u0, u1)),
				std::int32_t(std::min(v0, v1)));
			if (page == nullptr || page->Data == nullptr) {
				return false;
			}
			if (texture->GetPageCountX() > 1 || texture->GetPageCountY() > 1) {
				// A primitive samples one frame rect of an atlas, so it practically never straddles a page
				// boundary; when it does, the containing page is used and the overhang is clamped by the
				// hardware, which is a visibly wrong edge rather than a missing sprite
				const float maxU = std::max(u0, u1), maxV = std::max(v0, v1);
				if (maxU > float(page->OriginX + page->Width) + 0.5f ||
					maxV > float(page->OriginY + page->Height) + 0.5f) {
					static bool warnedStraddle = false;
					if (!warnedStraddle) {
						warnedStraddle = true;
						LOGW("A primitive samples across a GPU texture page boundary; its edge will be clamped");
					}
				}
			}

			ApplyPageToState(state, texture, *page);
			state.Filter = (texture->GetMagFilter() == nCine::SamplerFilter::Linear ? GPU_LINEAR : GPU_NEAREST);
			state.WrapU = MapWrap(texture->GetWrapS());
			state.WrapV = MapWrap(texture->GetWrapT());
			uvScaleU = float(texture->GetWidth());
			uvScaleV = float(texture->GetHeight());
			uvBiasU = float(page->OriginX);
			uvBiasV = float(page->OriginY);
			return true;
		}

		/** @brief The material's own blending, shared by every instance of a draw */
		void ApplyMaterialBlend(DrawState& state, const PicaDevice::BlendingState& blending)
		{
			state.BlendEnabled = blending.Enabled;
			state.BlendSrc = MapBlend(blending.SrcRgb);
			state.BlendDst = MapBlend(blending.DstRgb);
			state.BlendSrcAlpha = MapBlend(blending.SrcAlpha);
			state.BlendDstAlpha = MapBlend(blending.DstAlpha);
		}
	}

	// ------------------------------------------------------------------ lighting hook

	void PicaDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
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

	void PicaDevice::EndFrame()
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

	void PicaDevice::ApplyPendingSoftwareLighting()
	{
		if (_pendingSoftwareLights.empty()) {
			return;
		}
		const PendingSoftwareLight light = _pendingSoftwareLights.front();
		_pendingSoftwareLights.erase(_pendingSoftwareLights.begin());

		// Only the lightmap composite: the water overlay is a fixed_function block of the
		// CombineWithWater shaders, run by Dispatch after this hook (see CombineWithWater.shader)
		if (light.Lightmap == nullptr || light.LmW <= 0 || light.LmH <= 0) {
			return;
		}

		EnsureFrame();
		ApplyDrawTarget();
		ApplyScissor();

		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const float vpX = float(light.VpX) * scaleX, vpY = float(light.VpY) * scaleY;
		const float vpW = float(light.VpW) * scaleX, vpH = float(light.VpH) * scaleY;

		// Multiply factor from the CPU lightmap: out ~= scene * (r*(1+g) + amb*(1-r)) per channel (the
		// multiply-only approximation shared with the GX and PVR backends), as an RGBA4 texture drawn with
		// a dst * src blend over the viewport. The texture is generated into the frame-data arena, which is
		// exactly the lifetime it needs - the GPU consumes it within this frame and it is gone at present.
		std::int32_t texW = PicaTexture::MinPageDimension, texH = PicaTexture::MinPageDimension;
		while (texW < light.LmW && texW < PicaTexture::MaxPageDimension) texW <<= 1;
		while (texH < light.LmH && texH < PicaTexture::MaxPageDimension) texH <<= 1;
		const std::size_t size = std::size_t(texW) * std::size_t(texH) * 2;
		std::uint16_t* const surface = static_cast<std::uint16_t*>(AllocFrameData(size));
		if (surface == nullptr) {
			static bool warnedLightmapArena = false;
			if (!warnedLightmapArena) {
				warnedLightmapArena = true;
				LOGW("The {} KB per-frame data arena cannot hold a {}x{} lightmap texture", FrameDataBytes / 1024, texW, texH);
			}
			return;
		}

		// Eight rows at a time into a linear band, then tiled the way the texture unit reads (the same
		// conversion the texture stores go through, bottom-up like them: lightmap row 0 goes into the last
		// memory row, which is what v = 0 samples - see PicaTexture::BuildPage)
		alignas(16) static std::uint16_t band[8 * PicaTexture::MaxPageDimension];
		// The padding rows (first in memory, past v = LmH / texH) repeat the lightmap's last row, so a bilinear
		// tap at that edge sees it; they are converted before any real row, so the value is computed up front
		std::uint16_t lastRowTexel = 0xF00F;
		if (light.LmH > 0) {
			const float* DEATH_RESTRICT last = light.Lightmap + std::size_t(light.LmH - 1) * light.LmW * 2 + std::size_t(light.LmW - 1) * 2;
			float factorR, factorG, factorB;
			LightingCombineFactors(ClampLightmapChannel(last[0]), ClampLightmapChannel(last[1]), light.AmbR, light.AmbG, light.AmbB, factorR, factorG, factorB);
			lastRowTexel = std::uint16_t((Quantize4Bit(factorR) << 12) | (Quantize4Bit(factorG) << 8) | (Quantize4Bit(factorB) << 4) | 0xF);
		}
		for (std::int32_t bandY = 0; bandY < texH; bandY += 8) {
			for (std::int32_t row = 0; row < 8; row++) {
				const std::int32_t y = texH - 1 - (bandY + row);
				std::uint16_t* DEATH_RESTRICT dst = band + std::size_t(row) * texW;
				if (y >= light.LmH) {
					for (std::int32_t x = 0; x < texW; x++) {
						dst[x] = lastRowTexel;
					}
					continue;
				}
				const float* DEATH_RESTRICT src = light.Lightmap + std::size_t(y) * light.LmW * 2;
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
					float factorR, factorG, factorB;
					LightingCombineFactors(r, g, light.AmbR, light.AmbG, light.AmbB, factorR, factorG, factorB);
					// RGBA4 with red in the high nibble and an opaque alpha
					prevTexel = std::uint16_t((Quantize4Bit(factorR) << 12) | (Quantize4Bit(factorG) << 8) | (Quantize4Bit(factorB) << 4) | 0xF);
					dst[x] = prevTexel;
				}
				// The padding columns are reached by the bilinear tap at the last texel
				for (std::int32_t x = light.LmW; x < texW; x++) {
					dst[x] = prevTexel;
				}
			}
			PicaTexture::TileBand16(surface + std::size_t(bandY) * texW, band, texW);
		}
		GSPGPU_FlushDataCache(surface, std::uint32_t(size));

		DrawState state;
		state.TextureData = surface;
		state.TextureFormat = GPU_RGBA4;
		state.TextureWidth = texW;
		state.TextureHeight = texH;
		state.InvTextureWidth = 1.0f / float(texW);
		state.InvTextureHeight = 1.0f / float(texH);
		state.Filter = GPU_LINEAR;
		state.Env = TexEnvProgram::Modulate;
		// out = dst * src: the source is scaled by the destination colour, the destination by zero
		state.BlendEnabled = true;
		state.BlendSrc = GPU_DST_COLOR;
		state.BlendDst = GPU_ZERO;
		state.BlendSrcAlpha = GPU_ZERO;
		state.BlendDstAlpha = GPU_ONE;

		// The lightmap's row 0 is the TOP of the displayed viewport (CombineRenderer builds it in the scene's
		// own top-down raster space), and the bottom-up store above puts that row where v = 0 samples, so V
		// runs 0 -> used from top to bottom - verified in the emulator with a lightmap darkened everywhere but
		// one quadrant
		const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
		const float py[4] = { vpY, vpY + vpH, vpY, vpY + vpH };
		const float pu[4] = { float(light.LmW), float(light.LmW), 0.0f, 0.0f };
		const float pv[4] = { 0.0f, float(light.LmH), 0.0f, float(light.LmH) };
		SubmitQuadPrimitive(state, px, py, pu, pv, PackRgba(255, 255, 255, 255));
	}

	// ------------------------------------------------------------------ draw dispatch

	void PicaDevice::DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
		const std::uint16_t* indices, std::int32_t indexCount)
	{
		// A tile-layer mesh is a plain triangle list of 8-float vertices (position.xy, texcoords.uv,
		// color.rgba) - the layout TileMap::AppendTileQuad() writes and TileMapVs.inc declares. It is a
		// hard contract of this shader family exactly like the std140 instance block is of the sprite one.
		constexpr std::int32_t FloatsPerVertex = 8;
		if (primitive != PrimitiveType::Triangles || numVertices < 3) {
			return;
		}

		const PicaBuffer* vbo = _currentProgram->GetBoundVbo();
		if (vbo == nullptr) {
			return;
		}
		// The mesh arrives either as a plain triangle list, or - on the pipeline's indexed path - as the four
		// distinct corners of each quad addressed through the index pattern every quad mesh shares (see
		// RenderResources::GetQuadIndices()). `numVertices` counts element slots either way, three to a
		// triangle; all that differs is how a slot resolves to a vertex, which is what vertexAt() below does.
		std::size_t vertexExtent = std::size_t(numVertices);
		if (indices != nullptr) {
			std::uint32_t maxIndex = 0;
			for (std::int32_t i = 0; i < indexCount; i++) {
				if (indices[i] > maxIndex) {
					maxIndex = indices[i];
				}
			}
			vertexExtent = std::size_t(maxIndex) + 1;
		}
		const std::size_t firstFloat = (std::size_t(_currentProgram->GetBoundVboOffset()) / sizeof(float)) +
			std::size_t(firstVertex) * FloatsPerVertex;
		if ((firstFloat + vertexExtent * FloatsPerVertex) * sizeof(float) > vbo->GetSize()) {
			return;
		}
		const float* DEATH_RESTRICT vertices = reinterpret_cast<const float*>(vbo->HostData()) + firstFloat;
		// Where an element slot's vertex is: at the slot's own position in the stream, or where its index says
		const auto vertexAt = [vertices, indices](std::int32_t element) {
			return vertices + std::size_t(indices != nullptr ? indices[element] : element) * FloatsPerVertex;
		};

		const PicaUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		PicaTexture* texture = const_cast<PicaTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		const float* pv = CachedProjView(projMat, viewMat);
		Transform2D mvp;
		Mat4MulTransform2D(pv, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// The layer tint modulates every vertex colour, which already carries the per-tile alpha
		float layerColor[4];
		std::memcpy(layerColor, blockData + kColorOffset, sizeof(layerColor));

		// The palette to remap with is whatever the material bound to the palette sampler; the registered
		// global palette is the fallback (mirrors the sprite path). TileMapMeshPalette binds uTexturePalette
		// in its reflection, which is exactly what UsesPalette() reports.
		const bool isPaletteRemap = _currentProgram->UsesPalette();
		const PicaTexture* paletteTex = nullptr;
		if (isPaletteRemap || texture->NeedsPaletteBake()) {
			paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}

		// Unlike a sprite batch, the whole mesh shares one texture and one palette offset, so the residency,
		// the bake and the GPU state are resolved once for the entire layer
		std::int32_t paletteOffset = 0;
		if (isPaletteRemap) {
			float palOffset = 0.0f;
			std::memcpy(&palOffset, blockData + kPaletteOffsetOffset, sizeof(palOffset));
			paletteOffset = std::int32_t(palOffset + 0.5f);
		}
		const std::uint32_t paletteVersion = (paletteTex == _paletteTexture
			? _paletteGeneration : (paletteTex != nullptr ? paletteTex->GetContentVersion() : 0));

		DrawState state;
		if (texture->NeedsPaletteBake()) {
			// The bake takes the palette and the offset and bounds the row itself
			if (!texture->EnsureBakedStore(paletteTex, paletteOffset, paletteVersion)) {
				return;
			}
		}

		const float texRect[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
		float uvScaleU = 1.0f, uvScaleV = 1.0f, uvBiasU = 0.0f, uvBiasV = 0.0f;
		if (!ResolveTextureState(state, texture, texRect, uvScaleU, uvScaleV, uvBiasU, uvBiasV)) {
			return;
		}
		ApplyMaterialBlend(state, _blending);

		EnsureFrame();
		ApplyDrawTarget();
		ApplyScissor();

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const bool screenPass = (_currentRenderTarget == nullptr);

		// The NDC-to-raster mapping is affine and constant for the whole mesh, so it is folded into the
		// transform once instead of being reapplied per vertex. Both kinds of pass mirror NDC (+1 = raster row
		// 0, the top): the screen because the panel is scanned top-down, and a render target because the GPU
		// writes AND samples a surface bottom-up - the row rendered at the top lands last in memory, which is
		// exactly where v = 0 reads (see PicaTexture::BuildPage) - so the store needs no second flip, unlike
		// the top-down stores of the GE and GX.
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY;
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		// A tileset atlas wider or taller than one page is split into several, and different tiles of the
		// same layer then live in different pages - so the page is chosen per primitive rather than once for
		// the mesh. Single-page atlases (the common case) skip all of this and keep one state for the whole layer.
		const bool pagedTexture = (texture->GetPageCountX() > 1 || texture->GetPageCountY() > 1);
		auto project = [&](const float* v, float& outX, float& outY, float& outU, float& outV) {
			outX = raster.Xx * v[0] + raster.Yx * v[1] + raster.Tx;
			outY = raster.Xy * v[0] + raster.Yy * v[1] + raster.Ty;
			outU = v[2] * uvScaleU - uvBiasU;
			outV = v[3] * uvScaleV - uvBiasV;
		};
		// Repoints the state at the page a primitive's texture coordinates fall into and rebases them onto it
		auto selectPage = [&](DrawState& target, float* pu, float* pvv, std::int32_t count) {
			float minU = pu[0], minV = pvv[0];
			for (std::int32_t i = 1; i < count; i++) {
				minU = std::min(minU, pu[i]);
				minV = std::min(minV, pvv[i]);
			}
			const PicaTexture::Page* page = texture->AcquirePage(std::int32_t(minU), std::int32_t(minV));
			if (page == nullptr || page->Data == nullptr) {
				return false;
			}
			ApplyPageToState(target, texture, *page);
			for (std::int32_t i = 0; i < count; i++) {
				pu[i] -= float(page->OriginX);
				pvv[i] -= float(page->OriginY);
			}
			return true;
		};

		const std::int32_t triangleCount = numVertices / 3;
		std::int32_t triangle = 0;
		// Virtually every tile of a layer carries the same colour (white at the layer's alpha), so the four
		// clamp+float-to-int quantizations run once per change instead of once per tile
		float lastColor[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
		std::uint32_t lastRgba = 0;
		while (triangle < triangleCount) {
			// Tiles reach here as two triangles whose third and fourth slots repeat the first and third.
			// Recognizing that pattern lets a tile go out as one quad
			const std::int32_t element = triangle * 3;
			const float* group = vertexAt(element);
			const bool isQuad = (triangle + 2 <= triangleCount &&
				vertexAt(element + 3)[0] == group[0] && vertexAt(element + 3)[1] == group[1] &&
				vertexAt(element + 4)[0] == vertexAt(element + 2)[0] &&
				vertexAt(element + 4)[1] == vertexAt(element + 2)[1]);

			float px[4], py[4], pu[4], pvv[4];
			if (group[4] != lastColor[0] || group[5] != lastColor[1] || group[6] != lastColor[2] || group[7] != lastColor[3]) {
				lastColor[0] = group[4]; lastColor[1] = group[5]; lastColor[2] = group[6]; lastColor[3] = group[7];
				lastRgba = PackRgba(QuantizeChannel(group[4] * layerColor[0]),
					QuantizeChannel(group[5] * layerColor[1]), QuantizeChannel(group[6] * layerColor[2]),
					QuantizeChannel(group[7] * layerColor[3]));
			}

			if (isQuad) {
				// Corner order of the sprite strip (v0, v1, v2, v3): slots 1, 2, 0 and 5 of the tile's six
				static const std::int32_t QuadOrder[4] = { 1, 2, 0, 5 };
				for (std::int32_t i = 0; i < 4; i++) {
					project(vertexAt(element + QuadOrder[i]), px[i], py[i], pu[i], pvv[i]);
				}
				if DEATH_LIKELY(!pagedTexture) {
					SubmitQuadPrimitive(state, px, py, pu, pvv, lastRgba);
				} else {
					DrawState quadState = state;
					if (selectPage(quadState, pu, pvv, 4)) {
						SubmitQuadPrimitive(quadState, px, py, pu, pvv, lastRgba);
					}
				}
				triangle += 2;
			} else {
				for (std::int32_t i = 0; i < 3; i++) {
					project(vertexAt(element + i), px[i], py[i], pu[i], pvv[i]);
				}
				DrawState triState = state;
				if (!pagedTexture || selectPage(triState, pu, pvv, 3)) {
					Vertex* v = AllocVertices(triState, GPU_TRIANGLES, 3);
					if (v == nullptr) {
						return;
					}
					for (std::int32_t i = 0; i < 3; i++) {
						v[i] = { px[i], py[i], pu[i] * triState.InvTextureWidth, pvv[i] * triState.InvTextureHeight, lastRgba };
					}
				}
				triangle++;
			}
		}
	}

	void PicaDevice::DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices)
	{
		// The weapon wheel arrives as a textured line strip of 4-float vertices (position.xy, texcoords.uv)
		// - the layout the MeshSprite shader's attributes declare. Every segment goes out as a quad half a
		// pixel to each side of the line, which is what the 1-wide GL lines this stands in for rasterize to -
		// the same expansion the PowerVR and the GE make.
		constexpr std::int32_t FloatsPerVertex = 4;
		if (numVertices < 2) {
			return;
		}

		const PicaBuffer* vbo = _currentProgram->GetBoundVbo();
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

		const PicaUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		PicaTexture* texture = const_cast<PicaTexture*>(_boundTextures[0]);
		if (texture == nullptr) {
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);
		const float* pvMat = CachedProjView(projMat, viewMat);
		Transform2D mvp;
		Mat4MulTransform2D(pvMat, reinterpret_cast<const float*>(blockData + kModelMatrixOffset), mvp);

		// Every vertex of the strip carries the instance colour, so it is packed once
		float color[4];
		std::memcpy(color, blockData + kColorOffset, sizeof(color));
		const std::uint32_t rgba = PackRgba(QuantizeChannel(color[0]), QuantizeChannel(color[1]),
			QuantizeChannel(color[2]), QuantizeChannel(color[3]));

		DrawState state;
		if (texture->NeedsPaletteBake()) {
			const PicaTexture* paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
			if (!texture->EnsureBakedStore(paletteTex, 0, _paletteGeneration)) {
				return;
			}
		}

		const float texRect[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
		float uvScaleU = 1.0f, uvScaleV = 1.0f, uvBiasU = 0.0f, uvBiasV = 0.0f;
		if (!ResolveTextureState(state, texture, texRect, uvScaleU, uvScaleV, uvBiasU, uvBiasV)) {
			return;
		}
		ApplyMaterialBlend(state, _blending);

		EnsureFrame();
		ApplyDrawTarget();
		ApplyScissor();

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const bool screenPass = (_currentRenderTarget == nullptr);

		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY;
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		const float pixelScale = std::max(scaleX, scaleY);
		const std::int32_t segments = numVertices - 1;
		Vertex* const v = AllocVertices(state, GPU_TRIANGLES, segments * 6);
		if (v == nullptr) {
			return;
		}

		const auto project = [vertices, &raster, uvScaleU, uvScaleV, uvBiasU, uvBiasV]
			(std::int32_t i, float& x, float& y, float& u, float& w) {
			const float* DEATH_RESTRICT src = vertices + std::size_t(i) * FloatsPerVertex;
			x = raster.Xx * src[0] + raster.Yx * src[1] + raster.Tx;
			y = raster.Xy * src[0] + raster.Yy * src[1] + raster.Ty;
			u = src[2] * uvScaleU - uvBiasU;
			w = src[3] * uvScaleV - uvBiasV;
		};

		float prevX, prevY, prevU, prevV;
		project(0, prevX, prevY, prevU, prevV);
		for (std::int32_t i = 1; i < numVertices; i++) {
			float curX, curY, curU, curV;
			project(i, curX, curY, curU, curV);

			Vertex* const q = v + std::size_t(i - 1) * 6;
			const float dx = curX - prevX, dy = curY - prevY;
			const float len2 = dx * dx + dy * dy;
			if (len2 > 0.000001f) {
				const float len = std::sqrt(len2);
				// A quad exactly one pixel wide covers too few pixel centres on a diagonal and the line
				// comes out dashed and dimmer. Widening by the slope's Manhattan factor (1 for axis
				// aligned, sqrt(2) at 45 degrees) restores the unbroken one-pixel chain GL's line
				// rasterization guarantees whatever the slope.
				const float invLen = (0.5f * pixelScale * (std::fabs(dx) + std::fabs(dy)) / len) / len;
				const float nx = -dy * invLen;
				const float ny = dx * invLen;

				const float px[4] = { prevX + nx, prevX - nx, curX + nx, curX - nx };
				const float py[4] = { prevY + ny, prevY - ny, curY + ny, curY - ny };
				const float pu[4] = { prevU, prevU, curU, curU };
				const float pv[4] = { prevV, prevV, curV, curV };
				static const std::int32_t Order[6] = { 0, 1, 2, 2, 1, 3 };
				for (std::int32_t k = 0; k < 6; k++) {
					const std::int32_t o = Order[k];
					q[k] = { px[o], py[o], pu[o] * state.InvTextureWidth, pv[o] * state.InvTextureHeight, rgba };
				}
			} else {
				// The batch reserves six vertices per segment up front, so a segment of no length has to
				// fill its own slot - six coincident vertices rasterize nothing
				for (std::int32_t k = 0; k < 6; k++) {
					q[k] = { prevX, prevY, prevU * state.InvTextureWidth, prevV * state.InvTextureHeight, rgba };
				}
			}

			prevX = curX; prevY = curY; prevU = curU; prevV = curV;
		}
	}

	void PicaDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
		const std::uint16_t* indices, std::int32_t indexCount)
	{
		if (_currentProgram == nullptr || numVertices <= 0 || !_picaInitialized) {
			return;
		}

		// The program's whole console identity is its effect-table entry, resolved at load from the true
		// (program, variant) the loaders plumbed in - a program without an entry has no fixed_function block
		// in its .shader file (Lighting, Blur, the Resize* family, runtime-compiled shaders, ...) and keeps
		// the logged, skipped draw
		const FixedFunctionGeneratedEffect* generated = _currentProgram->GetGeneratedEffect();
		if (generated == nullptr) {
			frameSkippedDraws++;
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
			DispatchTileMesh(primitive, firstVertex, numVertices, indices, indexCount);
			return;
		}

		// The weapon wheel is the one vertex-fed mesh on this tier, a textured line strip
		if (intrinsic == FixedFunctionIntrinsic::LineStripMesh) {
			if (primitive == PrimitiveType::LineStrip) {
				DispatchLineStrip(firstVertex, numVertices);
			} else if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Only the line-strip form of the mesh pipeline is supported by the PICA dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		// Everything else is the procedural sprite-quad family
		const bool isQuadFamily = (generated->Fn != nullptr);
		if (!isQuadFamily || (primitive != PrimitiveType::TriangleStrip && primitive != PrimitiveType::Triangles)) {
			frameSkippedDraws++;
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Effect not supported by the PICA dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		// Resolved once at introspection (see DispatchFacts)
		const PicaUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		const PicaShaderProgram::DispatchFacts& facts = _currentProgram->GetDispatchFacts();
		std::uint32_t instanceStride = facts.InstanceStride;

		const float* pv = CachedProjView(projMat, viewMat);

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

		// A program samples the sprite texture exactly when its reflection binds uTexture
		const bool hasTexture = facts.HasTexture;
		// The instance layout follows the block's own reflected declaration rather than any effect identity:
		// a block that declares texRect uses the textured member offsets whether or not the program samples
		// a texture (the Transition carries texRect but samples nothing)
		const bool texturedLayout = facts.TexturedLayout;
		// Every effect that samples indexed sprites through the palette texture binds uTexturePalette in its
		// reflection, which is what UsesPalette() reports
		const bool isPaletteRemap = _currentProgram->UsesPalette();

		const FixedFunctionRequirements reqs = generated->Requirements;
		const bool needsTexelStep = ((reqs & FixedFunctionRequirements::NeedsTexelStep) == FixedFunctionRequirements::NeedsTexelStep);
		const bool needsUniforms = ((reqs & FixedFunctionRequirements::NeedsUniforms) == FixedFunctionRequirements::NeedsUniforms);
		const bool needsStripBuilder = ((reqs & FixedFunctionRequirements::NeedsStripBuilder) == FixedFunctionRequirements::NeedsStripBuilder);
		const bool needsQuadAxes = ((reqs & FixedFunctionRequirements::NeedsQuadAxes) == FixedFunctionRequirements::NeedsQuadAxes);
		const bool samplesTexture = ((reqs & FixedFunctionRequirements::SamplesTexture) == FixedFunctionRequirements::SamplesTexture);
		const std::int32_t textureUnit = facts.TextureUnit;
		PicaTexture* texture = const_cast<PicaTexture*>(hasTexture
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
		const PicaTexture* paletteTex = nullptr;
		if (isPaletteRemap || (texture != nullptr && texture->NeedsPaletteBake())) {
			const std::int32_t paletteUnit = facts.PaletteUnit;
			paletteTex = (std::uint32_t(paletteUnit) < MaxTextureUnits ? _boundTextures[paletteUnit] : nullptr);
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}
		const std::uint32_t paletteVersion = (paletteTex == _paletteTexture
			? _paletteGeneration : (paletteTex != nullptr ? paletteTex->GetContentVersion() : 0));

		EnsureFrame();
		ApplyDrawTarget();
		ApplyScissor();

		// Bounds guard, mirroring the software device
		const std::uint32_t rangeSize = _boundUniformRanges[binding].Size;
		if (batched && rangeSize > 0 && std::uint32_t(numInstances) * instanceStride > rangeSize) {
			numInstances = std::int32_t(rangeSize / instanceStride);
		}

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);

		// The material's own blending, shared by every instance of the draw
		DrawState material;
		ApplyMaterialBlend(material, _blending);

		// Texel sizes of the sprite texture in texture space (part of the EffectContext contract), derived
		// only for effects flagged with the texel-size facility
		const float texelWidth = (needsTexelStep && hasTexture && texture->GetWidth() > 0 ? 1.0f / float(texture->GetWidth()) : 0.0f);
		const float texelHeight = (needsTexelStep && hasTexture && texture->GetHeight() > 0 ? 1.0f / float(texture->GetHeight()) : 0.0f);

		// The engine's NDC orientation matches the software backend, whose top-down raster is flipped at
		// present time; here the raster IS top-down (the orthographic matrix maps raster y down), so screen
		// passes mirror NDC (+1 = bottom row). Render-to-texture passes keep the unmirrored top-down store,
		// which is what the sampling passes already expect - which is just the sign of the raster Y scale below.
		const bool screenPass = (_currentRenderTarget == nullptr);

		// Constant NDC-to-raster mapping, folded in once rather than reapplied for every sprite corner
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY;
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY;

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

			// Select this instance's texture page (and bake) - the corner synthesis works in texel indices
			// of the bound page, so the conversion also carries the page origin
			DrawState state = material;
			float uvScaleU = 1.0f, uvScaleV = 1.0f, uvBiasU = 0.0f, uvBiasV = 0.0f;
			if (hasTexture) {
				std::int32_t paletteOffset = 0;
				if (isPaletteRemap) {
					float palOffset = 0.0f;
					std::memcpy(&palOffset, inst + kPaletteOffsetOffset, sizeof(palOffset));
					paletteOffset = std::int32_t(palOffset + 0.5f);
				}
				if (texture->NeedsPaletteBake()) {
					// Indices can only be sampled baked through a palette row on this GPU. An effect that
					// remaps takes the row from the instance; anything else (the fonts) uses the base row.
					// The bake bounds the row against the palette itself.
					if (!texture->EnsureBakedStore(paletteTex, paletteOffset, paletteVersion)) {
						continue;
					}
				}
				if (!ResolveTextureState(state, texture, texRect, uvScaleU, uvScaleV, uvBiasU, uvBiasV)) {
					continue;
				}
			} else {
				state.TextureData = nullptr;
				state.Env = TexEnvProgram::Untextured;
			}

			// Synthesize the four sprite corners exactly like the software FetchVertex, with the constant
			// NDC-to-raster mapping folded into the transform so a corner costs one multiply-add per axis.
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
				pu[i] = (ax * texRect[0] + texRect[1]) * uvScaleU - uvBiasU;
				pvv[i] = (ay * texRect[2] + texRect[3]) * uvScaleV - uvBiasV;
			}

			// Nothing is clipped geometrically - the GPU has a real raster scissor and it is already
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

			// The pass descriptors the per-effect functions declare are mapped onto this instance's corners
			// and the resolved GPU state through the context
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
			// The optional context facilities are only wired up for effects whose static analysis says they
			// can call them; members of an unused facility are simply never read
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
				ctx.UvBiasU = uvBiasU;
				ctx.UvBiasV = uvBiasV;
			}

			generated->Fn(ctx);
		}
	}
}
