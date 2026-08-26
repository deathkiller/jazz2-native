#include "LegacyGlDevice.h"
#include <cstdio>
#include <cstdlib>
#include "LegacyGlBuffer.h"
#include "LegacyGlShaderProgram.h"
#include "LegacyGlRenderTarget.h"
#include "LegacyGlTexture.h"
#include "../FixedFunctionPass.h"
#include "../LightingCombine.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include "LegacyGlApi.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nCine::RHI::LegacyGL
{
	namespace
	{
		// Vertices are handed to the driver out of this per-frame bump arena. Nothing here is DMA'd behind
		// the CPU's back the way the console backends' arenas are - glDrawArrays copies what it needs
		// before returning - but keeping the same shape means the batching logic below is the consoles',
		// unchanged, and one frame's geometry stays in one contiguous allocation.
		constexpr std::size_t FrameArenaBytes = 1024 * 1024;
		std::uint8_t frameArena[FrameArenaBytes];
		std::size_t frameArenaUsed = 0;
		bool warnedFrameArenaFull = false;


		// glInterleavedArrays(GL_T2F_C4UB_V3F) describes exactly this record - two texture floats, four
		// colour bytes, three position floats - so the vertex the console backends build is handed to GL
		// without a repack. Positions are screen pixels (the projection below is an ortho matrix over the
		// target in pixels) and texture coordinates are TEXEL indices, which the texture matrix scales to
		// the normalized range when a texture is bound; both match what the shared submission code emits.
		struct Vertex2D
		{
			float U, V;
			std::uint32_t Color;
			float X, Y, Z;
		};

		// Packs a colour so that its four bytes lie in memory as R, G, B, A - what GL_C4UB expects, on a
		// big-endian machine (MorphOS) as much as on a little-endian one
		inline std::uint32_t PackRgbaBytes(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a)
		{
#if defined(DEATH_TARGET_BIG_ENDIAN)
			return (r << 24) | (g << 16) | (b << 8) | a;
#else
			return (a << 24) | (b << 16) | (g << 8) | r;
#endif
		}

		// GL has no two-vertex rectangle primitive, so every quad goes out as a triangle pair. The
		// console backends keep the switch because their hardware does have one.
		constexpr bool PreferSpritePrimitive = false;

		// Per-frame draw statistics, logged when the switch below is on - how much geometry the frame really
		// costs, which is what a batching bug shows up in first
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

		inline std::uint8_t QuantizeChannel(float v)
		{
			v = (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
			return std::uint8_t(v * 255.0f + 0.5f);
		}

		// Vertex colours are handed to GL as four bytes in R, G, B, A memory order (see PackRgbaBytes);
		// the name is kept from the console backends so the shared submission code below is unchanged
		inline std::uint32_t PackAbgr(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
		{
			return PackRgbaBytes(r, g, b, a);
		}

		// Environment colours, on the other hand, are handed to glTexEnvfv as four floats
		inline void UnpackToFloats(std::uint32_t packed, float* out)
		{
#if defined(DEATH_TARGET_BIG_ENDIAN)
			out[0] = float((packed >> 24) & 0xFF) / 255.0f;
			out[1] = float((packed >> 16) & 0xFF) / 255.0f;
			out[2] = float((packed >> 8) & 0xFF) / 255.0f;
			out[3] = float(packed & 0xFF) / 255.0f;
#else
			out[0] = float(packed & 0xFF) / 255.0f;
			out[1] = float((packed >> 8) & 0xFF) / 255.0f;
			out[2] = float((packed >> 16) & 0xFF) / 255.0f;
			out[3] = float((packed >> 24) & 0xFF) / 255.0f;
#endif
		}

		// Maps a pipeline-neutral blend factor onto GL's. Unlike the console backends there is nothing to
		// approximate here - GL has the whole factor set the engine's enum names.
		GLenum MapBlendFactor(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero:				return GL_ZERO;
				case nCine::BlendingFactor::One:				return GL_ONE;
				case nCine::BlendingFactor::SrcColor:			return GL_SRC_COLOR;
				case nCine::BlendingFactor::OneMinusSrcColor:	return GL_ONE_MINUS_SRC_COLOR;
				case nCine::BlendingFactor::DstColor:			return GL_DST_COLOR;
				case nCine::BlendingFactor::OneMinusDstColor:	return GL_ONE_MINUS_DST_COLOR;
				case nCine::BlendingFactor::SrcAlpha:			return GL_SRC_ALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha:	return GL_ONE_MINUS_SRC_ALPHA;
				case nCine::BlendingFactor::DstAlpha:			return GL_DST_ALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha:	return GL_ONE_MINUS_DST_ALPHA;
				default:										return GL_ONE;
			}
		}

		GLenum MapWrap(SamplerWrapping wrap)
		{
			switch (wrap) {
				case SamplerWrapping::Repeat:			return GL_REPEAT;
				case SamplerWrapping::MirroredRepeat:	return RHI_LEGACYGL_MIRRORED_REPEAT;
				default:								return GL_CLAMP_TO_EDGE;
			}
		}

		/**
			@brief What the texture environment computes for a pass

			The fixed_function contract's TEV presets, named here as the small set of glTexEnv programs
			they map onto. Modulate is the plain 1.1 environment; everything else needs GL_COMBINE, which
			is 1.3 vocabulary and present on both hosts this backend targets (TinyGL on MorphOS reports
			1.5 and exports the combiner entry points; a desktop GL has had it since 2001).
		*/
		enum class TexEnvProgram : std::uint8_t
		{
			Modulate,		/**< texel * vertex colour */
			ModulateX2,		/**< the same, output scaled by two */
			ModulateX4,		/**< the same, output scaled by four */
			Silhouette,		/**< vertex colour where the texel has alpha */
			TintMix			/**< mix(texel, pass colour, pass alpha), opaque */
		};
	}

	/**
		@brief The whole GL state one primitive is drawn under

		Consecutive primitives whose state matches field for field are accumulated into a single
		`glDrawArrays`, so a tile layer, a text run or a particle batch costs one draw call. The state is
		derived once per draw from the material (texture page, filter, wrap, blend) and then adjusted per
		pass by the effect (blend override, texture function) - which is what makes the batching automatic
		rather than something the effects have to think about.

		At namespace scope rather than in the anonymous namespace below because EffectContext names it in a
		member type and itself has to be externally visible (see the note there).
	*/
	struct DrawState
	{
		std::uint32_t Texture = 0;				// GL texture name; 0 = untextured
		std::int32_t TextureWidth = 0, TextureHeight = 0;	// what the texture matrix scales texel coordinates by
		GLenum Filter = GL_NEAREST;
		GLenum WrapU = GL_CLAMP_TO_EDGE, WrapV = GL_CLAMP_TO_EDGE;
		TexEnvProgram Env = TexEnvProgram::Modulate;
		std::uint32_t EnvColor = 0;
		GLenum BlendSrc = GL_SRC_ALPHA, BlendDst = GL_ONE_MINUS_SRC_ALPHA;
		GLenum Prim = GL_TRIANGLES;
		bool BlendEnabled = true;
	};

	namespace
	{
		bool SameDrawState(const DrawState& a, const DrawState& b)
		{
			return a.Texture == b.Texture && a.TextureWidth == b.TextureWidth &&
				a.TextureHeight == b.TextureHeight && a.Filter == b.Filter &&
				a.WrapU == b.WrapU && a.WrapV == b.WrapV && a.Env == b.Env && a.EnvColor == b.EnvColor &&
				a.BlendSrc == b.BlendSrc && a.BlendDst == b.BlendDst && a.Prim == b.Prim &&
				a.BlendEnabled == b.BlendEnabled;
		}

		DrawState batchState;
		std::size_t batchFirstByte = 0;
		std::int32_t batchVertexCount = 0;

		DrawState appliedState;
		bool appliedStateValid = false;

		// One texture object the per-frame generated content (today only the CPU lightmap of the lighting
		// compositor) is uploaded into. It is reallocated when the size changes and otherwise re-specified
		// in place, so a frame costs one upload rather than a texture creation.
		std::uint32_t scratchTexture = 0;
		std::int32_t scratchTextureWidth = 0, scratchTextureHeight = 0;

		std::uint32_t UploadScratchTexture(const void* rgba, std::int32_t width, std::int32_t height)
		{
			if (scratchTexture == 0) {
				GLuint name = 0;
				glGenTextures(1, &name);
				scratchTexture = name;
				if (scratchTexture == 0) {
					return 0;
				}
			}
			glBindTexture(GL_TEXTURE_2D, scratchTexture);
			if (width != scratchTextureWidth || height != scratchTextureHeight) {
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
				scratchTextureWidth = width;
				scratchTextureHeight = height;
			} else {
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
			}
			// The state cache has no idea this binding happened
			appliedStateValid = false;
			return scratchTexture;
		}

		// Programs the texture environment for one of the contract's presets. Only Modulate is 1.1; the
		// rest are GL_COMBINE programs, which is why they are set up in full rather than incrementally -
		// a combiner left half-configured from a previous preset is the classic source of a wrong pass.
		void ApplyTexEnv(TexEnvProgram program, std::uint32_t envColor)
		{
			float color[4];
			UnpackToFloats(envColor, color);
			glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);
			switch (program) {
				case TexEnvProgram::ModulateX2:
				case TexEnvProgram::ModulateX4:
					// The GX's output scales, which the engine's Colorized/PartialWhiteMask family uses to
					// carry a multiplier a vertex colour cannot hold, are GL_RGB_SCALE here
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
					glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
					glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, (program == TexEnvProgram::ModulateX4 ? 4.0f : 2.0f));
					glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
					break;
				case TexEnvProgram::Silhouette:
					// Flat colour where the texture has alpha: the RGB comes from the vertex colour alone
					// and the coverage is the texel's alpha times the vertex alpha
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
					glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
					glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
					glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
					break;
				case TexEnvProgram::TintMix:
					// mix(texel, colour, alpha) with an opaque result. GL_INTERPOLATE is Arg0*Arg2 +
					// Arg1*(1-Arg2), so the vertex alpha enters through Arg2 as ONE_MINUS to put the tint
					// on the side the contract names; the alpha channel is replaced by the environment
					// colour's, which the caller sets to one.
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
					glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_PRIMARY_COLOR);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_ONE_MINUS_SRC_ALPHA);
					glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
					glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_CONSTANT);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
					glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
					glTexEnvf(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1.0f);
					break;
				default:
					glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
					break;
			}
		}

		void ApplyDrawState(const DrawState& state)
		{
			const bool textureChanged = (!appliedStateValid || appliedState.Texture != state.Texture ||
				appliedState.TextureWidth != state.TextureWidth || appliedState.TextureHeight != state.TextureHeight);
			if (textureChanged) {
				if (state.Texture == 0) {
					glDisable(GL_TEXTURE_2D);
				} else {
					glEnable(GL_TEXTURE_2D);
					glBindTexture(GL_TEXTURE_2D, state.Texture);
					// The submission code emits texel coordinates (the console backends' "through mode"
					// convention); one texture matrix per binding is what turns them into GL's normalized
					// range, so no per-vertex division is needed anywhere
					glMatrixMode(GL_TEXTURE);
					glLoadIdentity();
					if (state.TextureWidth > 0 && state.TextureHeight > 0) {
						glScalef(1.0f / float(state.TextureWidth), 1.0f / float(state.TextureHeight), 1.0f);
					}
					glMatrixMode(GL_MODELVIEW);
				}
			}
			if (state.Texture != 0 && (textureChanged || appliedState.Env != state.Env ||
					appliedState.EnvColor != state.EnvColor)) {
				ApplyTexEnv(state.Env, state.EnvColor);
			}
			if (state.Texture != 0 && (textureChanged || appliedState.Filter != state.Filter)) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(state.Filter));
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(state.Filter));
			}
			if (state.Texture != 0 && (textureChanged ||
					appliedState.WrapU != state.WrapU || appliedState.WrapV != state.WrapV)) {
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(state.WrapU));
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(state.WrapV));
			}
			if (!appliedStateValid || appliedState.BlendEnabled != state.BlendEnabled ||
					(state.BlendEnabled && (appliedState.BlendSrc != state.BlendSrc ||
						appliedState.BlendDst != state.BlendDst))) {
				if (state.BlendEnabled) {
					glEnable(GL_BLEND);
					glBlendFunc(state.BlendSrc, state.BlendDst);
				} else {
					glDisable(GL_BLEND);
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
			const std::uint8_t* const base = frameArena + batchFirstByte;
			// GL_T2F_C4UB_V3F is Vertex2D's layout exactly, so the interleaved array IS the batch
			glInterleavedArrays(GL_T2F_C4UB_V3F, 0, base);
			glDrawArrays(batchState.Prim, 0, batchVertexCount);
			frameDrawCalls++;
			frameVertices += std::uint32_t(batchVertexCount);
			batchVertexCount = 0;
		}

		/** @brief Reserves per-frame data (the generated lightmap texels) out of the frame arena */
		void* AllocFrameData(std::size_t size, std::size_t alignment)
		{
			// The open batch's vertices have to stay contiguous in the arena, so anything else that takes
			// space from it closes the batch first
			FlushBatch();
			const std::size_t aligned = (frameArenaUsed + alignment - 1) & ~(alignment - 1);
			if (aligned + size > FrameArenaBytes) {
				return nullptr;
			}
			frameArenaUsed = aligned + size;
			return frameArena + aligned;
		}

		/** @brief Reserves @p count vertices of the open (or a new) batch drawn under @p state */
		Vertex2D* AllocVertices(const DrawState& state, std::int32_t count)
		{
			if (batchVertexCount > 0 && !SameDrawState(batchState, state)) {
				FlushBatch();
			}
			const std::size_t bytes = std::size_t(count) * sizeof(Vertex2D);
			if (batchVertexCount == 0) {
				frameArenaUsed = (frameArenaUsed + 15) & ~std::size_t(15);
				batchFirstByte = frameArenaUsed;
				batchState = state;
			}
			if (frameArenaUsed + bytes > FrameArenaBytes) {
				FlushBatch();
				if (!warnedFrameArenaFull) {
					warnedFrameArenaFull = true;
					LOGW("The {} KB per-frame vertex arena is full; the rest of the frame is dropped", FrameArenaBytes / 1024);
				}
				return nullptr;
			}
			Vertex2D* const result = reinterpret_cast<Vertex2D*>(frameArena + frameArenaUsed);
			frameArenaUsed += bytes;
			batchVertexCount += count;
			return result;
		}

		/**
			@brief Submits one quad as a triangle pair (see @ref PreferSpritePrimitive)

			Corners 0/1 share the sprite's local x = 1 edge and 2/3 its x = 0 edge, 0/2 the y = 0 edge and 1/3
			the y = 1 edge (see the corner synthesis in Dispatch), so an axis-aligned quad is exactly the case
			where those pairs agree on the other axis.
		*/
		void SubmitQuadPrimitive(DrawState state, const float* px, const float* py, const float* pu, const float* pv,
			std::uint32_t abgr, float dx = 0.0f, float dy = 0.0f)
		{
			const bool axisAligned = (px[0] == px[1] && px[2] == px[3] && py[0] == py[2] && py[1] == py[3]);
			if (axisAligned && PreferSpritePrimitive) {
				state.Prim = GL_TRIANGLES;
				Vertex2D* const v = AllocVertices(state, 2);
				if (v == nullptr) {
					return;
				}
				// The rectangle primitive takes two opposite corners in increasing order, so a mirrored
				// sprite (a negative scale in the model matrix) trades them over - and its texture
				// coordinates travel with them, which is what keeps the flip
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
				v[0] = { u0, v0, abgr, x0, y0, 0.0f };
				v[1] = { u1, v1, abgr, x1, y1, 0.0f };
			} else {
				state.Prim = GL_TRIANGLES;
				Vertex2D* const v = AllocVertices(state, 6);
				if (v == nullptr) {
					return;
				}
				// The two triangles of the v0..v3 strip; culling is off, so the winding is free
				static const std::int32_t Order[6] = { 0, 1, 2, 2, 1, 3 };
				for (std::int32_t i = 0; i < 6; i++) {
					const std::int32_t o = Order[i];
					v[i] = { pu[o], pv[o], abgr, px[o] + dx, py[o] + dy, 0.0f };
				}
			}
		}

		/** @brief Submits a triangle strip of its own draw call (arbitrary synthesized geometry) */
		void SubmitStripPrimitive(DrawState state, const float* px, const float* py, const float* pu, const float* pv,
			std::int32_t count, const std::uint32_t* abgr, std::uint32_t flatAbgr, float dx, float dy)
		{
			// A strip cannot share a draw call with anything else - GL would connect it to whatever vertices
			// follow - so it is bracketed by flushes
			state.Prim = GL_TRIANGLE_STRIP;
			FlushBatch();
			Vertex2D* const v = AllocVertices(state, count);
			if (v == nullptr) {
				return;
			}
			for (std::int32_t i = 0; i < count; i++) {
				v[i].U = (pu != nullptr ? pu[i] : 0.0f);
				v[i].V = (pv != nullptr ? pv[i] : 0.0f);
				v[i].Color = (abgr != nullptr ? abgr[i] : flatAbgr);
				v[i].X = px[i] + dx;
				v[i].Y = py[i] + dy;
				v[i].Z = 0.0f;
			}
			FlushBatch();
		}

		/** @brief Folds a pass descriptor's blend mode and texture function into a copy of the material state */
		void ApplyPassToState(DrawState& state, const FixedFunctionPass& pass)
		{
			switch (pass.Blend) {
				case FixedFunctionPass::BlendMode::Additive:
					// Deliberately SRCALPHA rather than a literal ONE source factor, matching the PVR: it is
					// the additive mechanism the split-multiplier passes rely on, whose contributions are
					// scaled by the pass alpha
					state.BlendEnabled = true;
					state.BlendSrc = GL_SRC_ALPHA;
					state.BlendDst = GL_ONE;
					break;
				case FixedFunctionPass::BlendMode::Opaque:
					state.BlendEnabled = false;
					break;
				case FixedFunctionPass::BlendMode::Alpha:
					state.BlendEnabled = true;
					state.BlendSrc = GL_SRC_ALPHA;
					state.BlendDst = GL_ONE_MINUS_SRC_ALPHA;
					break;
				default:
					break;		// The material's own blending
			}

			switch (pass.Tev) {
				case FixedFunctionPass::TevPreset::Silhouette:
					// Flat colour where the texture has alpha - the mask/outline/shield family. The pass
					// colour arrives as the vertex colour, so the environment only has to say "take RGB
					// from the vertex, coverage from the texel".
					state.Env = TexEnvProgram::Silhouette;
					break;
				case FixedFunctionPass::TevPreset::ModulateX2:
					state.Env = TexEnvProgram::ModulateX2;
					break;
				case FixedFunctionPass::TevPreset::ModulateX4:
					state.Env = TexEnvProgram::ModulateX4;
					break;
				case FixedFunctionPass::TevPreset::TintMix:
					// The lerp needs an opaque result, which the combiner takes from the environment
					// colour's alpha (see ApplyTexEnv)
					state.Env = TexEnvProgram::TintMix;
					state.EnvColor = PackAbgr(0, 0, 0, 255);
					break;
				default:
					// Modulate. LumaRamp is the one preset with no GL form - it is a six-stage GX combiner
					// program - and the transpiler rejects it outside a gx block, so it cannot arrive here.
					state.Env = TexEnvProgram::Modulate;
					break;
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
	// the structural contract documented in FixedFunctionPass.h, implemented here against GL's
	// batching submission helpers above. The per-effect functions themselves are GENERATED from the
	// shaders' void fixed_function([legacygl]) blocks by the ShaderCompiler, exactly as on the PVR and the GX
	// (Shaders/Generated/LegacyGlGeneratedEffects.h, included below), so this file contains no effect-specific
	// code at all.

	struct EffectContext
	{
		// Matches the GX's capacity rather than the PVR's, because nothing here needs the geometry split
		// into small pieces: GL takes a strip of any length in one draw call
		static constexpr std::int32_t MaxStripVertices = 16;

		// Decoded instance data of the draw being dispatched (the documented contract)
		const float* InstanceColor;
		float TexelW;
		float TexelH;
		bool Batched;

		// The GL state the material resolved to, and the current instance's corner arrays (already in
		// screen pixels and texel-space texture coordinates)
		const DrawState* Material;
		const float* Px;
		const float* Py;
		const float* Pu;
		const float* Pv;
		const float* TexRect;

		// Pre-clip quad geometry (the quad_origin/quad_axis_* built-ins), the program (for resolved
		// uniforms) and the conversion from the shader's normalized texture space into the texel
		// coordinates through mode wants, page origin included
		float OriginX, OriginY;
		float AxisXx, AxisXy;
		float AxisYx, AxisYy;
		const LegacyGlShaderProgram* Program;
		float UvScaleU, UvScaleV;
		float UvBiasU, UvBiasV;

		// The strip builder scratch; colours are packed at set time (same quantization as the quad path, so
		// identical float inputs produce identical vertex words)
		float StripX[MaxStripVertices];
		float StripY[MaxStripVertices];
		float StripU[MaxStripVertices];
		float StripV[MaxStripVertices];
		std::uint32_t StripAbgr[MaxStripVertices];

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
				StripAbgr[i] = PackAbgr(QuantizeChannel(r), QuantizeChannel(g), QuantizeChannel(b), QuantizeChannel(a));
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

		// One quad draw for a pass the texture environment can express directly (no offset colour left on it)
		void SubmitQuadPass(const FixedFunctionPass& pass)
		{
			DrawState state = *Material;
			ApplyPassToState(state, pass);
			const std::uint32_t abgr = PackAbgr(QuantizeChannel(pass.Color[0]), QuantizeChannel(pass.Color[1]),
				QuantizeChannel(pass.Color[2]), QuantizeChannel(pass.Color[3]));
			SubmitQuadPrimitive(state, Px, Py, Pu, Pv, abgr, pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		/*
			Submits one pass over the current instance's quad. A pass carrying an offset colour is
			EXPANDED here, because a single texture environment has no post-texture additive term: GL_ADD
			adds the texel to the fragment colour rather than a third value, so `texel*colour + offset` is
			not any single draw. Doing the expansion in the mechanism (rather than spelling both passes in
			every shader's block) is what keeps the portable core portable - the same generic block still
			describes the effect on every fixed-function backend, exactly as the GX reinterprets an offset
			colour as its silhouette form.

			The expansion is EXACT, not an approximation. With a = texel.a * colour.a, the PVR's single
			draw over the destination dst is (its blend being SRCALPHA + INVSRCALPHA):

				dst*(1 - a) + a*(texel*colour.rgb + offset)

			and the two draws below produce, in order,

				pass 1 (modulate, the pass's own blend):   dst1 = dst*(1 - a) + a*texel*colour.rgb
				pass 2 (silhouette, additive):             dst2 = dst1 + a*offset

			whose sum is the same expression term for term. Pass 2 is a silhouette (flat colour where
			the texture has alpha) whose vertex AND environment colour is the offset colour, so its own
			contribution is a*offset with no texel dependence, and its ADDITIVE blend is what keeps it
			from attenuating the destination a second time.

			When colour.rgb is zero - the "pure offset colour" idiom of the mask/outline/shield family,
			where the offset colour IS the effect - the modulate term vanishes and the pair collapses to
			ONE draw: the silhouette under the pass's own (alpha-over) blend already computes
			dst*(1 - a) + a*offset. That is the common case, so those effects cost the PVR's draw count.
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
			// Same intent mapping as the GX's SubmitStrip: with no post-texture add on this hardware, an
			// offset colour becomes the silhouette form filled flat with it at the pass alpha. Deliberately
			// NOT split into the exact two draws SubmitQuad builds - a strip's geometry would have to be
			// resubmitted, and no effect asks for it (both strip users, the iris and the warp bands, carry
			// no offset colour at all).
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
			const std::uint32_t abgr = PackAbgr(QuantizeChannel(effective.Color[0]), QuantizeChannel(effective.Color[1]),
				QuantizeChannel(effective.Color[2]), QuantizeChannel(effective.Color[3]));
			SubmitStripPrimitive(state, StripX, StripY, StripU, StripV, count, nullptr, abgr,
				pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}

		// Shaded (per-vertex-colour) strip out of the builder scratch, whose blend comes from the pass.
		// Two shapes use one: a gradient, which has no texture to modulate, and the warp bands of the
		// textured background, whose TintMix is defined over a texel - so the texture (and the strip's
		// texture coordinates with it) is kept for exactly that preset, as on the GX.
		void SubmitStripShaded(const FixedFunctionPass& pass, std::int32_t count)
		{
			if (count > MaxStripVertices) {
				count = MaxStripVertices;
			}
			if (count < 3) {
				return;
			}
			DrawState state = *Material;
			ApplyPassToState(state, pass);
			const bool textured = (state.Texture != 0 && pass.Tev == FixedFunctionPass::TevPreset::TintMix);
			if (!textured) {
				state.Texture = 0;
			}
			SubmitStripPrimitive(state, StripX, StripY, (textured ? StripU : nullptr), (textured ? StripV : nullptr),
				count, StripAbgr, 0, pass.ScreenOffset[0], pass.ScreenOffset[1]);
		}
	};
}

// The per-effect functions themselves are GENERATED: the ShaderCompiler transpiles each shader's
// fixed_function block into C++ over the EffectContext defined above (the type name itself is the
// "using EffectContext = ...;" alias the header expects - the anonymous namespace above is the same
// namespace the header's payload reopens within this translation unit). Included at global scope
// because the header opens nCine::RHI::LegacyGL itself. Programs with no fixed_function block are absent
// from its table and their draws are skipped with a one-time warning, exactly as on the other
// consoles - which is also what keeps the CPU-lightmap tier (Lighting, whose Combine hook IS in the
// table) from painting its light quads over the scene.
#include "../../../../Shaders/Generated/LegacyGlGeneratedEffects.h"

namespace nCine::RHI::LegacyGL
{
	const FixedFunctionGeneratedEffect* LegacyGlDevice::FindGeneratedEffect(const char* program, const char* variant)
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

	LegacyGlDevice::BlendingState LegacyGlDevice::_blending;
	LegacyGlDevice::DepthTestState LegacyGlDevice::_depthTest;
	LegacyGlDevice::CullFaceState LegacyGlDevice::_cullFace;
	LegacyGlDevice::ScissorState LegacyGlDevice::_scissor;
	Recti LegacyGlDevice::_viewport(0, 0, 0, 0);
	Colorf LegacyGlDevice::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	LegacyGlShaderProgram* LegacyGlDevice::_currentProgram = nullptr;
	const LegacyGlTexture* LegacyGlDevice::_boundTextures[LegacyGlDevice::MaxTextureUnits] = {};
	LegacyGlDevice::UniformRange LegacyGlDevice::_boundUniformRanges[LegacyGlDevice::MaxUniformBindings] = {};
	LegacyGlRenderTarget* LegacyGlDevice::_currentRenderTarget = nullptr;

	bool LegacyGlDevice::_glInitialized = false;
	bool LegacyGlDevice::_supportsFbo = false;
	bool LegacyGlDevice::_supportsNpot = false;
	std::int32_t LegacyGlDevice::_maxTextureSize = 0;
	std::int32_t LegacyGlDevice::_rasterWidth = 640;
	std::int32_t LegacyGlDevice::_rasterHeight = 480;
	// Until the render pipeline sets the real one (ResizeScreenFramebuffer), the logical resolution is
	// the drawable's - the screen pass then renders 1:1
	std::int32_t LegacyGlDevice::_logicalWidth = 640;
	std::int32_t LegacyGlDevice::_logicalHeight = 480;
	std::uint32_t LegacyGlDevice::_sceneCounter = 0;

	LegacyGlTexture* LegacyGlDevice::_paletteTexture = nullptr;
	std::uint32_t LegacyGlDevice::_paletteGeneration = 1;

	std::vector<LegacyGlDevice::PendingSoftwareLight> LegacyGlDevice::_pendingSoftwareLights;

	namespace
	{
		// Which surface GL is currently rendering into, so a target switch only re-emits the projection and
		// the framebuffer binding when it really changes
		const LegacyGlRenderTarget* appliedTarget = nullptr;
		// Whether appliedTarget names the target the last draws went into (which is what decides whether a
		// copy-back is still owed), and separately whether GL is still pointed at it
		bool appliedTargetValid = false;
		bool appliedTargetBound = false;
		// The scissor rect last programmed, in raster pixels of the current target
		std::int32_t appliedScissor[4] = { -1, -1, -1, -1 };
	}

	namespace
	{
		/** @brief Points GL back at the drawable; a no-op where framebuffer objects do not exist */
		inline void BindDefaultFramebuffer()
		{
#if defined(RHI_LEGACYGL_HAS_FBO)
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
		}
	}

	// ------------------------------------------------------------------ session

	void LegacyGlDevice::InitializeGl()
	{
		if (_glInitialized) {
			return;
		}
		// The window backend owns the context; everything here is the once-per-context pipeline state this
		// backend relies on. Nothing is ever turned back on, so a state left here holds for every draw.
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);
		glDisable(GL_LIGHTING);
		glDisable(GL_FOG);
		glDisable(GL_ALPHA_TEST);
		glShadeModel(GL_SMOOTH);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_SCISSOR_TEST);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		// How large one texture may be. Legacy GL only guarantees 64 and a Voodoo-class card really does
		// stop at 256, while a desktop driver answers with 16384 - which is not a size anything here
		// should build, so the answer is capped as well (see MaxTextureDimension).
		GLint maxTextureSize = 0;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
		_maxTextureSize = (maxTextureSize >= 64 ? std::int32_t(maxTextureSize) : 256);
		if (_maxTextureSize > MaxTextureDimension) {
			_maxTextureSize = MaxTextureDimension;
		}

		// Whether a texture may keep its own dimensions. Legacy GL is not required to sample a
		// non-power-of-two one at all, and where it cannot, every page is padded up - which costs real
		// memory on an atlas whose width is just over a power of two, so it is worth knowing. Both the
		// advertisement and an actual upload have to agree: a driver that only claims it would fall back
		// to software, and one that only accepts the upload is not promising anything about sampling.
		_supportsNpot = false;
		{
			const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
			const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
			std::int32_t major = 0, minor = 0;
			if (version != nullptr) {
				std::sscanf(version, "%2d.%2d", &major, &minor);
			}
			// NCINE_NO_NPOT forces the padded path, for the same reason NCINE_NO_FBO exists below: it is
			// the shape a TinyGL-era driver has, and no machine that has one is reachable from here
			const bool advertised = ((major >= 2) || (extensions != nullptr &&
				std::strstr(extensions, "GL_ARB_texture_non_power_of_two") != nullptr)) &&
				std::getenv("NCINE_NO_NPOT") == nullptr;
			if (advertised) {
				GLuint probeTexture = 0;
				glGenTextures(1, &probeTexture);
				if (probeTexture != 0) {
					glBindTexture(GL_TEXTURE_2D, probeTexture);
					while (glGetError() != GL_NO_ERROR) { }
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 3, 5, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
					_supportsNpot = (glGetError() == GL_NO_ERROR);
					glDeleteTextures(1, &probeTexture);
					glBindTexture(GL_TEXTURE_2D, 0);
				}
			}
		}

		// Whether framebuffer objects actually work, which decides how a render target gets its pixels
		// (see LegacyGlRenderTarget). Probed rather than believed: TinyGL declares the entry points
		// unconditionally and the running tinygl.library may still answer nothing, and a plain GL 1.x
		// without the extension would not have got this far at all. Where they are not declared at all
		// (MiniGL on AmigaOS 4), the whole path is compiled out and the copy-back one is the only one.
		//
		// NCINE_NO_FBO in the environment forces the copy-back path. It exists because that path is the
		// one this backend cannot exercise where it matters - the machines that need it are exactly the
		// ones without a developer sitting at them - so it stays reachable on a desktop, where it can be
		// looked at next to the framebuffer-object path.
		_supportsFbo = false;
#if defined(RHI_LEGACYGL_HAS_FBO)
		if (std::getenv("NCINE_NO_FBO") == nullptr) {
			GLuint probeFbo = 0, probeTexture = 0;
			glGenFramebuffers(1, &probeFbo);
			if (probeFbo != 0) {
				glGenTextures(1, &probeTexture);
				glBindTexture(GL_TEXTURE_2D, probeTexture);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glBindFramebuffer(GL_FRAMEBUFFER, probeFbo);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probeTexture, 0);
				_supportsFbo = (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glDeleteFramebuffers(1, &probeFbo);
				glDeleteTextures(1, &probeTexture);
				glBindTexture(GL_TEXTURE_2D, 0);
			}
		}
#endif
		LOGI("Legacy GL: \"{}\" by \"{}\" ({}), textures up to {}{}, render targets by {}",
			reinterpret_cast<const char*>(glGetString(GL_VERSION) != nullptr ? glGetString(GL_VERSION) : reinterpret_cast<const GLubyte*>("?")),
			reinterpret_cast<const char*>(glGetString(GL_VENDOR) != nullptr ? glGetString(GL_VENDOR) : reinterpret_cast<const GLubyte*>("?")),
			reinterpret_cast<const char*>(glGetString(GL_RENDERER) != nullptr ? glGetString(GL_RENDERER) : reinterpret_cast<const GLubyte*>("?")),
			_maxTextureSize, (_supportsNpot ? "" : " (powers of two only)"),
			(_supportsFbo ? "framebuffer object" : "copying out of the back buffer"));

		_glInitialized = true;
		appliedStateValid = false;
		appliedTargetValid = false;
		appliedTargetBound = false;
		appliedScissor[0] = -1;
	}

	void LegacyGlDevice::ShutdownGl()
	{
		if (!_glInitialized) {
			return;
		}
		FlushBatch();
		_glInitialized = false;
	}

	void LegacyGlDevice::EnsureList()
	{
		// The console backends open a display list here. GL has no such thing - a frame is just the draws
		// between two presents - so this only brings the context up the first time and marks the cached
		// state unknown, which is what the callers of this expect.
		if (!_glInitialized) {
			InitializeGl();
		}
	}

	void LegacyGlDevice::ApplyDrawTarget()
	{
		if (appliedTargetValid && appliedTargetBound && appliedTarget == _currentRenderTarget) {
			return;
		}
		FlushBatch();
		if (appliedTargetValid && appliedTarget != nullptr && appliedTarget != _currentRenderTarget &&
				appliedTarget->IsCopyBack()) {
			// Everything drawn for that target is in the back buffer and has to be lifted into its
			// texture before the next pass overwrites it
			appliedTarget->ResolveCopyBack();
		}

		std::int32_t targetWidth = _rasterWidth, targetHeight = _rasterHeight;
		bool topDown = true;
		if (_currentRenderTarget != nullptr) {
			LegacyGlTexture* texture = _currentRenderTarget->GetColorTexture(0);
			if (texture == nullptr || texture->GetRenderTargetTexture() == 0) {
				// No surface to render into; the draws will be skipped by their own checks
				return;
			}
			targetWidth = texture->GetWidth();
			targetHeight = texture->GetHeight();
			// A render target is sampled afterwards with the same top-left texel coordinates it was
			// drawn with, and a GL texture's row 0 is the BOTTOM of what was rendered - so a target is
			// rendered bottom-up, which puts what the pass calls the top in row 0. The screen keeps the
			// top-down projection, since there the drawn image is what is displayed.
			topDown = false;
#if defined(RHI_LEGACYGL_HAS_FBO)
			if (_currentRenderTarget->GetFramebuffer() != 0) {
				glBindFramebuffer(GL_FRAMEBUFFER, _currentRenderTarget->GetFramebuffer());
			} else
#endif
			{
				// Copy-back: the pass is rendered into the bottom-left corner of the drawable, which is
				// what ResolveCopyBack() reads back. Anything the drawable cannot hold is lost, so the
				// viewport is clamped rather than left to scale the image.
				BindDefaultFramebuffer();
				ClampToDrawable(targetWidth, targetHeight);
			}
		} else {
			BindDefaultFramebuffer();
		}
		// Positions are raster pixels with the origin at the top left - the convention the shared
		// submission code emits and the one the engine's logical coordinates already use. An ortho
		// projection over the target in pixels is what turns those into clip space, so no vertex ever
		// needs a transform.
		glViewport(0, 0, targetWidth, targetHeight);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		if (topDown) {
			glOrtho(0.0, double(targetWidth), double(targetHeight), 0.0, -1.0, 1.0);
		} else {
			glOrtho(0.0, double(targetWidth), 0.0, double(targetHeight), -1.0, 1.0);
		}
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		appliedTarget = _currentRenderTarget;
		appliedTargetValid = true;
		appliedTargetBound = true;
		// The rect is expressed in the target's raster space, so it has to be reprogrammed as well
		appliedScissor[0] = -1;
	}

	void LegacyGlDevice::ClampToDrawable(std::int32_t& width, std::int32_t& height)
	{
		if (width > _rasterWidth) {
			width = _rasterWidth;
		}
		if (height > _rasterHeight) {
			height = _rasterHeight;
		}
	}

	void LegacyGlDevice::ApplyScissor()
	{
		std::int32_t targetW = _rasterWidth, targetH = _rasterHeight;
		if (_currentRenderTarget != nullptr) {
			const LegacyGlTexture* texture = _currentRenderTarget->GetColorTexture(0);
			targetW = (texture != nullptr ? texture->GetWidth() : _rasterWidth);
			targetH = (texture != nullptr ? texture->GetHeight() : _rasterHeight);
		}
		std::int32_t x = 0, y = 0, w = targetW, h = targetH;
		if (_scissor.Enabled) {
			float scaleX, scaleY;
			GetTargetScale(scaleX, scaleY);
			x = std::int32_t(float(_scissor.Rect.X) * scaleX);
			y = std::int32_t(float(_scissor.Rect.Y) * scaleY);
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
		// The rect arrives top-down (the engine's logical space) and GL measures the scissor box from the
		// bottom left, so the screen's flips - a render target is already drawn bottom-up (see
		// ApplyDrawTarget), and flipping its rect as well would scissor away the wrong half
		glScissor(x, (_currentRenderTarget != nullptr ? y : targetH - y - h), w, h);
		appliedScissor[0] = x;
		appliedScissor[1] = y;
		appliedScissor[2] = w;
		appliedScissor[3] = h;
	}

	void LegacyGlDevice::GetTargetScale(float& scaleX, float& scaleY)
	{
		if (_currentRenderTarget != nullptr) {
			// Render-to-texture passes render 1:1 into the target surface
			scaleX = 1.0f;
			scaleY = 1.0f;
		} else {
			scaleX = (_logicalWidth > 0 ? float(_rasterWidth) / float(_logicalWidth) : 1.0f);
			scaleY = (_logicalHeight > 0 ? float(_rasterHeight) / float(_logicalHeight) : 1.0f);
		}
	}

	void LegacyGlDevice::PresentFrame()
	{
		if (!_glInitialized) {
			return;
		}
		FlushBatch();
		if (appliedTargetValid && appliedTarget != nullptr && appliedTarget->IsCopyBack()) {
			appliedTarget->ResolveCopyBack();
			appliedTarget = nullptr;
			appliedTargetValid = false;
			appliedTargetBound = false;
		}
		// The buffer swap belongs to the window backend (it owns the context and the vsync policy), so a
		// frame ends here with the pipeline flushed and the per-frame arena released

		if (TraceDrawStatistics) {
			if ((_sceneCounter % 60) == 0) {
				LOGI("Frame {}: {} draw calls, {} vertices, {} KB of the vertex arena, {} skipped draws",
					_sceneCounter, frameDrawCalls, frameVertices, frameArenaUsed / 1024, frameSkippedDraws);
			}
		}

		// A frame always ends on the screen, and the next one re-emits its projection anyway
		appliedTarget = nullptr;
		appliedTargetValid = false;
		appliedTargetBound = false;
		_sceneCounter++;

		frameArenaUsed = 0;
		frameDrawCalls = 0;
		frameVertices = 0;
		frameSkippedDraws = 0;
	}

	void LegacyGlDevice::ResizeScreenFramebuffer(std::int32_t width, std::int32_t height)
	{
		if (width > 0 && height > 0) {
			_logicalWidth = width;
			_logicalHeight = height;
		}
	}

	void LegacyGlDevice::ResizeSwapchain(std::int32_t width, std::int32_t height)
	{
		// What the window backend calls when the drawable changes size: the raster the screen pass targets
		if (width > 0 && height > 0) {
			_rasterWidth = width;
			_rasterHeight = height;
			appliedTargetBound = false;
		}
	}

	void LegacyGlDevice::SetBlendingEnabled(bool enabled) { _blending.Enabled = enabled; }
	void LegacyGlDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}
	LegacyGlDevice::BlendingState LegacyGlDevice::GetBlendingState() { return _blending; }
	void LegacyGlDevice::SetBlendingState(const BlendingState& state) { _blending = state; }

	void LegacyGlDevice::SetDepthTestEnabled(bool enabled) { _depthTest.TestEnabled = enabled; }
	void LegacyGlDevice::SetDepthMaskEnabled(bool enabled) { _depthTest.MaskEnabled = enabled; }
	LegacyGlDevice::DepthTestState LegacyGlDevice::GetDepthTestState() { return _depthTest; }
	void LegacyGlDevice::SetDepthTestState(const DepthTestState& state) { _depthTest = state; }

	void LegacyGlDevice::SetCullFaceEnabled(bool enabled) { _cullFace.Enabled = enabled; }
	LegacyGlDevice::CullFaceState LegacyGlDevice::GetCullFaceState() { return _cullFace; }
	void LegacyGlDevice::SetCullFaceState(const CullFaceState& state) { _cullFace = state; }

	LegacyGlDevice::ScissorState LegacyGlDevice::GetScissorState() { return _scissor; }
	void LegacyGlDevice::SetScissorState(const ScissorState& state) { _scissor = state; }
	void LegacyGlDevice::SetScissor(const Recti& rect)
	{
		// Same contract as the GL device: setting a rect also enables the test (callers like RenderCommand
		// and Viewport rely on it and restore via SetScissorState afterwards)
		_scissor.Enabled = true;
		_scissor.Rect = rect;
	}
	void LegacyGlDevice::SetScissorTestEnabled(bool enabled) { _scissor.Enabled = enabled; }

	Recti LegacyGlDevice::GetViewport() { return _viewport; }
	void LegacyGlDevice::SetViewport(const Recti& rect) { _viewport = rect; }
	void LegacyGlDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
	}

	Colorf LegacyGlDevice::GetClearColor() { return _clearColor; }
	void LegacyGlDevice::SetClearColor(const Colorf& color) { _clearColor = color; }

	void LegacyGlDevice::Clear(ClearFlags flags)
	{
		if (!_glInitialized) {
			return;
		}
		EnsureList();
		ApplyDrawTarget();
		// The clear is bounded by the scissor, exactly like glClear is by the scissor test
		ApplyScissor();
		// Whatever is still batched was submitted BEFORE this clear and has to reach the list first,
		// otherwise the clear would wipe it (a mid-frame clear is exactly what the render targets do)
		FlushBatch();

		GLbitfield mask = 0;
		if ((flags & ClearFlags::Color) == ClearFlags::Color) {
			mask |= GL_COLOR_BUFFER_BIT;
			glClearColor(_clearColor.R, _clearColor.G, _clearColor.B, _clearColor.A);
		}
		// A colour-only target has no depth or stencil attachment, so asking for those would be an error
		if ((flags & ClearFlags::Depth) == ClearFlags::Depth && _currentRenderTarget == nullptr) {
			mask |= GL_DEPTH_BUFFER_BIT;
		}
		if ((flags & ClearFlags::Stencil) == ClearFlags::Stencil && _currentRenderTarget == nullptr) {
			mask |= GL_STENCIL_BUFFER_BIT;
		}
		if (mask != 0) {
			// The colour mask is never changed by this backend, but the depth mask is off for drawing and
			// glClear honours it, so it is opened for the clear itself
			if ((mask & GL_DEPTH_BUFFER_BIT) != 0) {
				glDepthMask(GL_TRUE);
				glClear(mask);
				glDepthMask(GL_FALSE);
			} else {
				glClear(mask);
			}
		}
	}

	// ------------------------------------------------------------------ draw entry points

	void LegacyGlDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		Dispatch(primitive, firstVertex, numVertices);
	}
	void LegacyGlDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		static_cast<void>(numInstances);
		Dispatch(primitive, firstVertex, numVertices);
	}
	void LegacyGlDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}
	void LegacyGlDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		static_cast<void>(indexFormat);
		static_cast<void>(indexOffset);
		static_cast<void>(numInstances);
		Dispatch(primitive, baseVertex, std::int32_t(numIndices));
	}

	FenceHandle LegacyGlDevice::InsertFence()
	{
		return reinterpret_cast<FenceHandle>(std::uintptr_t(1));
	}
	void LegacyGlDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}
	bool LegacyGlDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	void LegacyGlDevice::SetupInitialState()
	{
		_blending = BlendingState();
		_depthTest = DepthTestState();
		_cullFace = CullFaceState();
		_scissor = ScissorState();
	}

	// ------------------------------------------------------------------ extensions

	void LegacyGlDevice::BindProgram(LegacyGlShaderProgram* program) { _currentProgram = program; }
	LegacyGlShaderProgram* LegacyGlDevice::CurrentProgram() { return _currentProgram; }

	void LegacyGlDevice::BindTexture(std::uint32_t unit, const LegacyGlTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void LegacyGlDevice::UnbindTexture(const LegacyGlTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
		if (_paletteTexture == texture) {
			_paletteTexture = nullptr;
		}
		// A destroyed texture's store must not stay referenced by an open batch. Anything already submitted
		// is safe: resources are only ever destroyed between frames (level loads, menu transitions), and
		// PresentFrame() has synced the previous frame's list by then.
		FlushBatch();
		if (appliedStateValid && appliedState.Texture != 0) {
			appliedStateValid = false;
		}
	}

	const LegacyGlTexture* LegacyGlDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void LegacyGlDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void LegacyGlDevice::SetRenderTarget(LegacyGlRenderTarget* renderTarget)
	{
		// The draw path reacts lazily at the next draw or clear (ApplyDrawTarget), which is also where the
		// open batch is closed - it belongs to the previous surface
		_currentRenderTarget = renderTarget;
	}

	void LegacyGlDevice::UnbindRenderTarget(const LegacyGlRenderTarget* renderTarget)
	{
		if (appliedTargetValid && appliedTarget == renderTarget) {
			// A destroyed target must not be left as the one a copy-back would resolve, and its pixels
			// are still worth keeping - the texture usually outlives the target object
			FlushBatch();
			if (renderTarget != nullptr && renderTarget->IsCopyBack()) {
				renderTarget->ResolveCopyBack();
			}
			appliedTarget = nullptr;
			appliedTargetValid = false;
			appliedTargetBound = false;
		}
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
		}
		if (appliedTarget == renderTarget) {
			FlushBatch();
			appliedTargetValid = false;
			appliedTarget = nullptr;
		}
	}

	// ------------------------------------------------------------------ palettes

	std::int32_t LegacyGlDevice::GetMaxTextureDimension()
	{
		// Answered before the first draw (the capabilities object is built while the window backend is
		// still setting up), so the context has to be brought up here rather than waited for
		EnsureList();
		return _maxTextureSize;
	}

	void LegacyGlDevice::DescribeContext(std::int32_t& majorVersion, std::int32_t& minorVersion, const char*& vendor,
		const char*& renderer, const char*& apiVersion)
	{
		EnsureList();
		vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
		renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
		apiVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
		majorVersion = 0;
		minorVersion = 0;
		if (apiVersion != nullptr) {
			std::sscanf(apiVersion, "%2d.%2d", &majorVersion, &minorVersion);
		}
	}

	void LegacyGlDevice::InvalidateStateCache()
	{
		// Something outside the draw path bound a texture or a framebuffer (a texture upload, a render
		// target being pointed at its attachment), so what the batch believes GL is set to is now a
		// guess. Both caches are dropped rather than repaired: they are re-emitted by the next draw.
		appliedStateValid = false;
		// The target itself is still the one the draws are going into - only GL's binding is in doubt, and
		// forgetting which target is current would lose a copy-back that is still owed
		appliedTargetBound = false;
	}

	void LegacyGlDevice::RegisterPaletteTexture(LegacyGlTexture* texture)
	{
		_paletteTexture = texture;
		NotifyPaletteTextureChanged(texture, 0, texture != nullptr ? texture->GetHeight() : 0);
	}

	void LegacyGlDevice::NotifyPaletteTextureChanged(LegacyGlTexture* texture, std::int32_t firstRow, std::int32_t rowCount)
	{
		if (texture != _paletteTexture) {
			return;
		}
		// Every texture that was baked through one of these rows has to be rebuilt, which the bump of the
		// generation is what tells them (see LegacyGlTexture::EnsureBakedStore); the rows themselves are
		// only read when a bake runs, so there is nothing else cached here to drop
		static_cast<void>(firstRow);
		static_cast<void>(rowCount);
		_paletteGeneration++;
	}

	namespace
	{
		/** @brief Points the draw state at one page of a texture */
		void ApplyPageToState(DrawState& state, const LegacyGlTexture* texture, const LegacyGlTexture::Page& page)
		{
			state.Texture = page.GlTexture;
			// Texel coordinates are scaled by the PADDED size, which is what the store (and so the GL
			// texture) actually is; the shared submission code biases them by the page origin
			state.TextureWidth = page.PaddedWidth;
			state.TextureHeight = page.PaddedHeight;
			static_cast<void>(texture);
		}

		/**
			@brief Resolves the texture state of one draw: the page a texture rectangle samples

			@p texRect is the instance's (uSpan, uOffset, vSpan, vOffset). The submission code emits texture
			coordinates as texel indices, so the returned scale is simply the source size and the bias is the
			page origin - a single-page texture (which is practically everything) biases by zero.
		*/
		bool ResolveTextureState(DrawState& state, LegacyGlTexture* texture, const float* texRect,
			float& uvScaleU, float& uvScaleV, float& uvBiasU, float& uvBiasV)
		{
			const float u0 = texRect[1] * float(texture->GetWidth());
			const float u1 = (texRect[1] + texRect[0]) * float(texture->GetWidth());
			const float v0 = texRect[3] * float(texture->GetHeight());
			const float v1 = (texRect[3] + texRect[2]) * float(texture->GetHeight());
			const LegacyGlTexture::Page* page = texture->AcquirePage(std::int32_t(std::min(u0, u1)),
				std::int32_t(std::min(v0, v1)));
			if (page == nullptr || page->GlTexture == 0) {
				// What matters is the texture object, not the host store behind it - a render target has
				// no host store at all (see LegacyGlTexture::SetRenderTarget)
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
						LOGW("A primitive samples across a texture page boundary; its edge will be clamped");
					}
				}
			}

			ApplyPageToState(state, texture, *page);
			state.Filter = (texture->GetMagFilter() == nCine::SamplerFilter::Linear ? GL_LINEAR : GL_NEAREST);
			state.WrapU = MapWrap(texture->GetWrapS());
			state.WrapV = MapWrap(texture->GetWrapT());
			uvScaleU = float(texture->GetWidth());
			uvScaleV = float(texture->GetHeight());
			uvBiasU = float(page->OriginX);
			uvBiasV = float(page->OriginY);
			return true;
		}
	}

	// ------------------------------------------------------------------ lighting hook

	void LegacyGlDevice::SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
		std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
		bool waterActive, float waterLevelPx, float waterTime, float waterCamY)
	{
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
		light.WaterActive = waterActive;
		light.WaterLevelPx = waterLevelPx;
		light.WaterTime = waterTime;
		light.WaterCamY = waterCamY;
		_pendingSoftwareLights.push_back(light);
	}

	void LegacyGlDevice::EndFrame()
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

	void LegacyGlDevice::ApplyPendingSoftwareLighting()
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

		EnsureList();
		ApplyDrawTarget();
		ApplyScissor();

		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const float vpX = float(light.VpX) * scaleX, vpY = float(light.VpY) * scaleY;
		const float vpW = float(light.VpW) * scaleX, vpH = float(light.VpH) * scaleY;

		if (hasLighting) {
			// Multiply factor from the CPU lightmap: out ~= scene * (r*(1+g) + amb*(1-r)) per channel (the
			// multiply-only approximation shared with the GX and PVR backends), as an RGBA8 texture drawn
			// over the viewport with a dst * src blend. The texels go into the frame arena, which is
			// exactly the lifetime they need - the upload copies them and they are gone at present.
			std::int32_t texW = 8, texH = 8;
			while (texW < light.LmW && texW < LegacyGlTexture::MaxPageDimension) texW <<= 1;
			while (texH < light.LmH && texH < LegacyGlTexture::MaxPageDimension) texH <<= 1;
			// One texture page is the whole store here, so a lightmap larger than that (a viewport wider
			// than twice MaxPageDimension) has more texels than were allocated. What fits is copied and
			// then stretched over the whole viewport by the quad below - the alternative is writing past
			// the surface, and the map is a smooth falloff that survives being scaled a little further.
			const std::int32_t copyW = (light.LmW < texW ? light.LmW : texW);
			const std::int32_t copyH = (light.LmH < texH ? light.LmH : texH);
			const std::size_t size = std::size_t(texW) * std::size_t(texH) * 4;
			std::uint32_t* const surface = static_cast<std::uint32_t*>(AllocFrameData(size, 4));
			if (surface != nullptr) {
				for (std::int32_t y = 0; y < copyH; y++) {
					const float* DEATH_RESTRICT src = light.Lightmap + std::size_t(y) * light.LmW * 2;
					std::uint32_t* DEATH_RESTRICT dst = surface + std::size_t(y) * texW;
					// Unlit runs repeat the same pair of factors across long spans, so remembering the last
					// converted texel turns most of the surface into a compare and a store
					float prevR = -1.0f, prevG = -1.0f;
					std::uint32_t prevTexel = 0;
					for (std::int32_t x = 0; x < copyW; x++) {
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
						prevTexel = PackRgbaBytes(QuantizeChannel(LightingCombineFactor(r, g, light.AmbR)),
							QuantizeChannel(LightingCombineFactor(r, g, light.AmbG)),
							QuantizeChannel(LightingCombineFactor(r, g, light.AmbB)), 255);
						dst[x] = prevTexel;
					}
					// The padding columns are reached by the bilinear tap at the last texel
					for (std::int32_t x = copyW; x < texW; x++) {
						dst[x] = prevTexel;
					}
				}
				for (std::int32_t y = copyH; y < texH; y++) {
					std::memcpy(surface + std::size_t(y) * texW, surface + std::size_t(copyH - 1) * texW,
						std::size_t(texW) * 4);
				}
				DrawState state;
				state.Texture = UploadScratchTexture(surface, texW, texH);
				if (state.Texture == 0) {
					return;
				}
				state.TextureWidth = texW;
				state.TextureHeight = texH;
				state.Filter = GL_LINEAR;
				state.Env = TexEnvProgram::Modulate;
				// out = dst * src, the multiply the compositor applies the lightmap with
				state.BlendEnabled = true;
				state.BlendSrc = GL_DST_COLOR;
				state.BlendDst = GL_ZERO;

				// The lightmap's row 0 corresponds to the bottom of the displayed viewport (the software
				// buffer convention), so V runs used -> 0 top -> bottom
				const float px[4] = { vpX + vpW, vpX + vpW, vpX, vpX };
				const float py[4] = { vpY, vpY + vpH, vpY, vpY + vpH };
				const float pu[4] = { float(copyW), float(copyW), 0.0f, 0.0f };
				const float pv[4] = { float(copyH), 0.0f, float(copyH), 0.0f };
				SubmitQuadPrimitive(state, px, py, pu, pv, PackAbgr(255, 255, 255, 255));
			}
		}
	}

	// ------------------------------------------------------------------ draw dispatch

	void LegacyGlDevice::DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		// A tile-layer mesh is a plain triangle list of 8-float vertices (position.xy, texcoords.uv,
		// color.rgba) - the layout TileMap::AppendTileQuad() writes and TileMapVs.inc declares. It is a
		// hard contract of this shader family exactly like the std140 instance block is of the sprite one.
		constexpr std::int32_t FloatsPerVertex = 8;
		if (primitive != PrimitiveType::Triangles || numVertices < 3) {
			return;
		}

		const LegacyGlBuffer* vbo = _currentProgram->GetBoundVbo();
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

		const LegacyGlUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		LegacyGlTexture* texture = const_cast<LegacyGlTexture*>(_boundTextures[0]);
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
		const LegacyGlTexture* paletteTex = nullptr;
		if (isPaletteRemap || texture->IsIndexed() || texture->NeedsPaletteBake()) {
			paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}

		// Unlike a sprite batch, the whole mesh shares one texture and one palette offset, so the residency,
		// the GL state is resolved once for the entire layer
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
			if (paletteTex == nullptr || paletteTex->GetPixels() == nullptr) {
				return;
			}
			const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(paletteTex->GetPixels())
				+ paletteOffset;
			if (!texture->EnsureBakedStore(entries, std::uint32_t(paletteOffset), paletteVersion, paletteTex)) {
				return;
			}
		}

		const float texRect[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
		float uvScaleU = 1.0f, uvScaleV = 1.0f, uvBiasU = 0.0f, uvBiasV = 0.0f;
		if (!ResolveTextureState(state, texture, texRect, uvScaleU, uvScaleV, uvBiasU, uvBiasV)) {
			return;
		}

		state.BlendEnabled = _blending.Enabled;
		state.BlendSrc = MapBlendFactor(_blending.SrcRgb);
		state.BlendDst = MapBlendFactor(_blending.DstRgb);

		EnsureList();
		ApplyDrawTarget();
		ApplyScissor();

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const bool screenPass = (_currentRenderTarget == nullptr);

		// The NDC-to-raster mapping is affine and constant for the whole mesh, so it is folded into the
		// transform once instead of being reapplied per vertex. A screen pass mirrors NDC, which is just the
		// sign of the Y scale.
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		// A tileset atlas wider or taller than one page is split into several, and different tiles of the
		// same layer then live in different pages - so the page is chosen per primitive rather than once for
		// the mesh. Single-page atlases (the common case: a 10-tile-wide sheet is a 340 px atlas) skip all of
		// this and keep one state for the whole layer.
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
			const LegacyGlTexture::Page* page = texture->AcquirePage(std::int32_t(minU), std::int32_t(minV));
			if (page == nullptr || page->GlTexture == 0) {
				// What matters is the texture object, not the host store behind it - a render target has
				// no host store at all (see LegacyGlTexture::SetRenderTarget)
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
		std::uint32_t lastAbgr = 0;
		while (triangle < triangleCount) {
			// Tiles reach here as the six vertices of two triangles, of which the third and fourth repeat the
			// first and third. Recognizing that pattern lets a tile go out as one quad - and, since tile
			// quads are axis-aligned, they would be a two-vertex rectangle on hardware that has one.
			const float* group = vertices + std::size_t(triangle) * 3 * FloatsPerVertex;
			const bool isQuad = (triangle + 2 <= triangleCount &&
				group[3 * FloatsPerVertex + 0] == group[0] && group[3 * FloatsPerVertex + 1] == group[1] &&
				group[4 * FloatsPerVertex + 0] == group[2 * FloatsPerVertex + 0] &&
				group[4 * FloatsPerVertex + 1] == group[2 * FloatsPerVertex + 1]);

			float px[4], py[4], pu[4], pvv[4];
			if (group[4] != lastColor[0] || group[5] != lastColor[1] || group[6] != lastColor[2] || group[7] != lastColor[3]) {
				lastColor[0] = group[4]; lastColor[1] = group[5]; lastColor[2] = group[6]; lastColor[3] = group[7];
				lastAbgr = PackAbgr(QuantizeChannel(group[4] * layerColor[0]),
					QuantizeChannel(group[5] * layerColor[1]), QuantizeChannel(group[6] * layerColor[2]),
					QuantizeChannel(group[7] * layerColor[3]));
			}

			if (isQuad) {
				// Corner order of the sprite strip (v0, v1, v2, v3): vertices 1, 2, 0 and 5 of the tile's six
				static const std::int32_t QuadOrder[4] = { 1, 2, 0, 5 };
				for (std::int32_t i = 0; i < 4; i++) {
					project(group + std::size_t(QuadOrder[i]) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				DrawState quadState = state;
				if (!pagedTexture || selectPage(quadState, pu, pvv, 4)) {
					SubmitQuadPrimitive(quadState, px, py, pu, pvv, lastAbgr);
				}
				triangle += 2;
			} else {
				for (std::int32_t i = 0; i < 3; i++) {
					project(group + std::size_t(i) * FloatsPerVertex, px[i], py[i], pu[i], pvv[i]);
				}
				DrawState triState = state;
				triState.Prim = GL_TRIANGLES;
				if (!pagedTexture || selectPage(triState, pu, pvv, 3)) {
					Vertex2D* v = AllocVertices(triState, 3);
					if (v == nullptr) {
						return;
					}
					for (std::int32_t i = 0; i < 3; i++) {
						v[i] = { pu[i], pvv[i], lastAbgr, px[i], py[i], 0.0f };
					}
				}
				triangle++;
			}
		}
	}

	void LegacyGlDevice::DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices)
	{
		// The weapon wheel arrives as a textured line strip of 4-float vertices (position.xy, texcoords.uv)
		// - the layout the MeshSprite shader's attributes declare. GL has a native single-pixel line
		// primitive, so unlike the PowerVR (which expands every segment into a thin quad) the strip goes out
		// as it is, in one draw call.
		constexpr std::int32_t FloatsPerVertex = 4;
		if (numVertices < 2) {
			return;
		}

		const LegacyGlBuffer* vbo = _currentProgram->GetBoundVbo();
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

		const LegacyGlUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		LegacyGlTexture* texture = const_cast<LegacyGlTexture*>(_boundTextures[0]);
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
		const std::uint32_t abgr = PackAbgr(QuantizeChannel(color[0]), QuantizeChannel(color[1]),
			QuantizeChannel(color[2]), QuantizeChannel(color[3]));

		DrawState state;
		const std::uint32_t paletteVersion = _paletteGeneration;
		if (texture->NeedsPaletteBake()) {
			const LegacyGlTexture* paletteTex = _boundTextures[1];
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
			if (paletteTex == nullptr || paletteTex->GetPixels() == nullptr ||
				!texture->EnsureBakedStore(reinterpret_cast<const std::uint32_t*>(paletteTex->GetPixels()), 0,
					paletteVersion, paletteTex)) {
				return;
			}
		}

		const float texRect[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
		float uvScaleU = 1.0f, uvScaleV = 1.0f, uvBiasU = 0.0f, uvBiasV = 0.0f;
		if (!ResolveTextureState(state, texture, texRect, uvScaleU, uvScaleV, uvBiasU, uvBiasV)) {
			return;
		}

		state.BlendEnabled = _blending.Enabled;
		state.BlendSrc = MapBlendFactor(_blending.SrcRgb);
		state.BlendDst = MapBlendFactor(_blending.DstRgb);
		state.Prim = GL_LINE_STRIP;

		EnsureList();
		ApplyDrawTarget();
		ApplyScissor();

		const Recti viewport = (_viewport.W > 0 && _viewport.H > 0)
			? _viewport : Recti(0, 0, _logicalWidth, _logicalHeight);
		float scaleX, scaleY;
		GetTargetScale(scaleX, scaleY);
		const bool screenPass = (_currentRenderTarget == nullptr);

		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
		const float rasterBiasY = 0.5f * float(viewport.H) * scaleY + float(viewport.Y) * scaleY;
		const Transform2D raster = {
			mvp.Xx * rasterScaleX, mvp.Xy * rasterScaleY,
			mvp.Yx * rasterScaleX, mvp.Yy * rasterScaleY,
			mvp.Tx * rasterScaleX + rasterBiasX, mvp.Ty * rasterScaleY + rasterBiasY
		};

		// A line strip is one primitive, so it gets its own draw call
		FlushBatch();
		Vertex2D* v = AllocVertices(state, numVertices);
		if (v == nullptr) {
			return;
		}
		for (std::int32_t i = 0; i < numVertices; i++) {
			const float* src = vertices + std::size_t(i) * FloatsPerVertex;
			v[i].U = src[2] * uvScaleU - uvBiasU;
			v[i].V = src[3] * uvScaleV - uvBiasV;
			v[i].Color = abgr;
			v[i].X = raster.Xx * src[0] + raster.Yx * src[1] + raster.Tx;
			v[i].Y = raster.Xy * src[0] + raster.Yy * src[1] + raster.Ty;
			v[i].Z = 0.0f;
		}
		FlushBatch();
	}

	void LegacyGlDevice::Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		if (_currentProgram == nullptr || numVertices <= 0 || !_glInitialized) {
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
			DispatchTileMesh(primitive, firstVertex, numVertices);
			return;
		}

		// The weapon wheel is the one vertex-fed mesh on this tier, a textured line strip
		if (intrinsic == FixedFunctionIntrinsic::LineStripMesh) {
			if (primitive == PrimitiveType::LineStrip) {
				DispatchLineStrip(firstVertex, numVertices);
			} else if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Only the line-strip form of the mesh pipeline is supported by the legacy GL dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		// Everything else is the procedural sprite-quad family
		const bool isQuadFamily = (generated->Fn != nullptr);
		if (!isQuadFamily || (primitive != PrimitiveType::TriangleStrip && primitive != PrimitiveType::Triangles)) {
			frameSkippedDraws++;
			if (!_currentProgram->FetchUnsupportedWarned()) {
				LOGW("Skipping draws of program \"{}\": Effect not supported by the legacy GL dispatch", _currentProgram->GetObjectLabel());
			}
			return;
		}

		const std::uint8_t* projBytes = _currentProgram->GetResolvedProjection();
		const std::uint8_t* viewBytes = _currentProgram->GetResolvedView();
		const float* projMat = (projBytes != nullptr ? reinterpret_cast<const float*>(projBytes) : IdentityMatrix);
		const float* viewMat = (viewBytes != nullptr ? reinterpret_cast<const float*>(viewBytes) : IdentityMatrix);

		// Resolved once at introspection (see DispatchFacts) - this used to re-scan the
		// reflection's name strings on every RenderCommand
		const LegacyGlUniformBlock* block = _currentProgram->GetDispatchFacts().InstanceBlock;
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

		const LegacyGlShaderProgram::DispatchFacts& facts = _currentProgram->GetDispatchFacts();
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
		LegacyGlTexture* texture = const_cast<LegacyGlTexture*>(hasTexture
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
		const LegacyGlTexture* paletteTex = nullptr;
		if (isPaletteRemap || (texture != nullptr && (texture->IsIndexed() || texture->NeedsPaletteBake()))) {
			const std::int32_t paletteUnit = facts.PaletteUnit;
			paletteTex = (std::uint32_t(paletteUnit) < MaxTextureUnits ? _boundTextures[paletteUnit] : nullptr);
			if (paletteTex == nullptr || paletteTex == texture) {
				paletteTex = _paletteTexture;
			}
		}
		const std::uint32_t paletteVersion = (paletteTex == _paletteTexture
			? _paletteGeneration : (paletteTex != nullptr ? paletteTex->GetContentVersion() : 0));

		EnsureList();
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
		{

			material.BlendEnabled = _blending.Enabled;
			material.BlendSrc = MapBlendFactor(_blending.SrcRgb);
			material.BlendDst = MapBlendFactor(_blending.DstRgb);
		}

		// Texel sizes of the sprite texture in texture space (part of the EffectContext contract), derived
		// only for effects flagged with the texel-size facility
		const float texelWidth = (needsTexelStep && hasTexture && texture->GetWidth() > 0 ? 1.0f / float(texture->GetWidth()) : 0.0f);
		const float texelHeight = (needsTexelStep && hasTexture && texture->GetHeight() > 0 ? 1.0f / float(texture->GetHeight()) : 0.0f);

		// The engine's NDC orientation matches the software backend, whose top-down raster is flipped at
		// present time; here the screen is projected top-down instead (see ApplyDrawTarget), so screen
		// passes mirror NDC themselves (+1 = bottom row). A render-to-texture pass is projected the other
		// way up - so that what it draws at its top lands in the texture's row 0, which is where the
		// sampling passes look for it - and that is exactly the sign of the raster Y scale below.
		const bool screenPass = (_currentRenderTarget == nullptr);

		// Constant NDC-to-raster mapping, folded in once rather than reapplied for every sprite corner
		const float rasterScaleX = 0.5f * float(viewport.W) * scaleX;
		const float rasterBiasX = rasterScaleX + float(viewport.X) * scaleX;
		const float rasterScaleY = 0.5f * float(viewport.H) * scaleY * (screenPass ? 1.0f : -1.0f);
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

			// Select this instance's texture page (and its bake, if it is indexed) - texel texture coordinates are
			// texel indices of the bound page, so the conversion also carries the page origin
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
					// Index + per-pixel alpha has no paletted form: a CPU bake through the palette row is
					// what the other consoles do too
					if (paletteTex == nullptr || paletteTex->GetPixels() == nullptr) {
						continue;
					}
					const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(paletteTex->GetPixels())
						+ paletteOffset;
					if (!texture->EnsureBakedStore(entries, std::uint32_t(paletteOffset), paletteVersion, paletteTex)) {
						continue;
					}
				}
				if (!ResolveTextureState(state, texture, texRect, uvScaleU, uvScaleV, uvBiasU, uvBiasV)) {
					continue;
				}
			}

			// Synthesize the four sprite corners exactly like the software FetchVertex, with the constant
			// NDC-to-raster mapping folded into the transform so a corner costs one multiply-add per axis.
			// The corner weights are 0 or 1, so the sprite's extent in raster space is just the transformed
			// axes scaled by its size, and the corners are sums of those.
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

			// Nothing is clipped geometrically - GL has a real raster scissor and it is already
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
			// and the resolved GL state through the context
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
