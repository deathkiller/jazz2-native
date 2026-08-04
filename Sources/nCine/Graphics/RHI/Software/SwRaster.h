#pragma once

#if defined(WITH_RHI_SOFTWARE)

#include "SwTexture.h"
#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"

#include <cstdint>
#include <cstring>

#if defined(DEATH_TARGET_SSE2)
#	include <emmintrin.h>
#elif defined(DEATH_TARGET_NEON)
#	include <arm_neon.h>
#endif

namespace nCine::RHI::Software
{
	/** @brief Number of texture units the rasterizer can sample from in a single draw */
	static constexpr std::uint32_t MaxTextureUnits = SwTexture::MaxTextureUnits;

	/**
		@brief Upper bound on a fragment callback's parameter block

		The tile renderer defers draws and rasterizes them later (possibly on a worker thread), so it copies
		each draw's @ref DrawContext::fragmentShaderUserData block into per-command storage to make the
		deferred draw self-contained. This is the size of that inline storage; an effect whose parameter
		block is larger is declined for deferral and rasterized immediately instead. It must be at least as
		large as the biggest effect parameter struct the device fills.
	*/
	static constexpr std::uint32_t MaxFragmentShaderUserDataSize = 256;

	/**
		@brief Source or destination factor of the blending equation (rasterizer-local mirror)

		A compact mirror of the pipeline-neutral @ref nCine::BlendingFactor holding just the ten factors the
		CPU blenders specialize. The device layer maps a `nCine::BlendingFactor` pair onto this enum when it
		fills a @ref DrawContext, and having a dedicated enum lets the ported blend code stay verbatim without
		being shadowed by the pipeline-neutral one.
	*/
	enum class SwBlendFactor
	{
		Zero,				/**< Multiplies by zero */
		One,				/**< Multiplies by one */
		SrcColor,			/**< Multiplies by the source color */
		DstColor,			/**< Multiplies by the destination color */
		OneMinusSrcColor,	/**< Multiplies by one minus the source color */
		OneMinusDstColor,	/**< Multiplies by one minus the destination color */
		SrcAlpha,			/**< Multiplies by the source alpha */
		OneMinusSrcAlpha,	/**< Multiplies by one minus the source alpha */
		DstAlpha,			/**< Multiplies by the destination alpha */
		OneMinusDstAlpha	/**< Multiplies by one minus the destination alpha */
	};

	/**
		@brief Fixed-function per-draw parameters read by the rasterizer

		Without programmable shaders the sprite pipeline is described by this plain struct - the software
		counterpart of one instance's uniform block. @ref mvpMatrix transforms the procedural quad corners to
		clip space, @ref color modulates every sampled texel, @ref texRect selects the sampled sub-region and
		@ref spriteSize scales the synthesized corners. The rasterizer reads it directly.
	*/
	struct FFState
	{
		/** @brief Model-view-projection transform, 4x4 column-major */
		float mvpMatrix[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};
		/** @brief Constant color modulation (tint), normalized RGBA */
		float color[4] = { 1, 1, 1, 1 };
		/** @brief Sampled sub-rectangle of the texture as `(uScale, uOffset, vScale, vOffset)` */
		float texRect[4] = { 0, 0, 1, 1 };
		/** @brief Sprite size in pixels the procedural quad corners are scaled by */
		float spriteSize[2] = { 1, 1 };
		/** @brief Depth value (unused by the 2D rasterizer, carried for parity with the shader uniforms) */
		float depth = 0.0f;
		/** @brief Whether a texture is bound and should be sampled */
		bool hasTexture = false;
		/** @brief Texture unit the sampled texture is bound to */
		std::int32_t textureUnit = 0;
	};

	/**
		@brief Per-pixel inputs handed to an optional C++ fragment callback

		Filled once per destination pixel after texture sampling and before blending, so a callback can
		reproduce a shader's `fragment()` in C++. @ref rgba is the sampled straight-alpha color (4 bytes,
		`0..255`) the callback rewrites in place; @ref u / @ref v are the interpolated texture coordinates and
		@ref x / @ref y the destination pixel. The remaining fields expose the primary texture size, every
		bound texture, the instance color and the effect's own opaque parameter block.
	*/
	struct FragmentShaderInput
	{
		std::uint8_t* rgba;					/**< In/out pixel color (4 bytes, RGBA order), rewritten in place */
		float u, v;							/**< Interpolated texture coordinates */
		std::int32_t x, y;					/**< Destination pixel coordinates */
		std::int32_t texWidth, texHeight;	/**< Dimensions of the primary (unit `ff.textureUnit`) texture */
		const SwTexture* const* textures;	/**< The bound textures (@ref MaxTextureUnits entries) */
		const float* color;					/**< Instance color (4 floats, RGBA) */
		void* userData;						/**< Effect-owned parameter block, opaque to the rasterizer */
	};

	/** @brief Optional per-pixel fragment callback; runs after sampling, before blending */
	using FragmentShaderFn = void (*)(const FragmentShaderInput& input);

#if defined(RHI_USE_FB16)
	/**
		@brief Packs one 4-byte RGBA working pixel into an RGB565 framebuffer texel (alpha is dropped)

		Part of the optional 16-bit screen-framebuffer mode (`NCINE_RHI_USE_FB16`): the screen buffer
		stores native-endian RGB565 (2 bytes per pixel - half the memory and present bandwidth of RGBA8),
		while render-target textures and all intermediate rasterization stay 4-byte RGBA. The rasterizers
		stage each touched framebuffer row through @ref SwLoadFbSpan565 / @ref SwStoreFbSpan565, so their
		inner loops are unchanged. The mode has no destination alpha - blend factors reading it see 255.
	*/
	inline std::uint16_t SwPack565(const std::uint8_t* rgba)
	{
		return std::uint16_t(((rgba[0] & 0xF8) << 8) | ((rgba[1] & 0xFC) << 3) | (rgba[2] >> 3));
	}

	/** @brief Unpacks one RGB565 framebuffer texel to 4-byte RGBA (bit-replicated, so a load-store round trip is lossless; alpha reads 255) */
	inline void SwUnpack565(std::uint16_t px, std::uint8_t* rgba)
	{
		const std::uint32_t r5 = (px >> 11) & 0x1F;
		const std::uint32_t g6 = (px >> 5) & 0x3F;
		const std::uint32_t b5 = px & 0x1F;
		rgba[0] = std::uint8_t((r5 << 3) | (r5 >> 2));
		rgba[1] = std::uint8_t((g6 << 2) | (g6 >> 4));
		rgba[2] = std::uint8_t((b5 << 3) | (b5 >> 2));
		rgba[3] = 255;
	}

	/** @brief Expands a run of RGB565 framebuffer texels into a 4-byte RGBA staging row */
	inline void SwLoadFbSpan565(std::uint8_t* DEATH_RESTRICT rgba, const std::uint8_t* DEATH_RESTRICT fb16, std::int32_t count)
	{
		for (std::int32_t i = 0; i < count; i++, rgba += 4, fb16 += 2) {
			std::uint16_t px;
			std::memcpy(&px, fb16, 2);
			SwUnpack565(px, rgba);
		}
	}

	/** @brief Packs a 4-byte RGBA staging row back into a run of RGB565 framebuffer texels */
	inline void SwStoreFbSpan565(std::uint8_t* DEATH_RESTRICT fb16, const std::uint8_t* DEATH_RESTRICT rgba, std::int32_t count)
	{
		for (std::int32_t i = 0; i < count; i++, rgba += 4, fb16 += 2) {
			const std::uint16_t px = SwPack565(rgba);
			std::memcpy(fb16, &px, 2);
		}
	}
#endif

	/**
		@brief Quantizes a normalized channel to a byte exactly like the shader runtime's `packColor()`

		Kept bit-identical to `sw::packColor` (clamp to `[0, 1]`, scale, round) so the palette-LUT fast
		path below reproduces the transpiled fragment's output byte for byte.
	*/
	inline std::uint8_t SwQuantizeColor(float v)
	{
		v = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
		const std::int32_t i = static_cast<std::int32_t>(v * 255.0f + 0.5f);
		return static_cast<std::uint8_t>((i < 0) ? 0 : (i > 255 ? 255 : i));
	}

	/**
		@brief Expands one stored texel to the rasterizer's 4-byte RGBA working form

		@ref SwTexture stores R8 / RG8 index textures natively (1 / 2 bytes per texel); every gather
		expands the fetched texel through this helper, filling the missing channels with 255 - the exact
		bytes the former promoted-to-RGBA8 store held, so everything downstream of a gather (tint, palette
		LUT, fragment callbacks, blending) is bit-identical whatever the store width. A 4-byte store is a
		single 32-bit copy, kept first as the branch the RGBA8 paths take every time.
	*/
	DEATH_ALWAYS_INLINE void SwExpandTexel(std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src, std::int32_t bpp)
	{
		if DEATH_LIKELY(bpp == 4) {
			std::memcpy(dst, src, 4);
		} else {
			dst[0] = src[0];
			dst[1] = (bpp >= 2 ? src[1] : std::uint8_t(255));
			dst[2] = 255;
			dst[3] = 255;
		}
	}

	/**
		@brief Expands a contiguous run of stored texels to the 4-byte RGBA working form

		The vector form of @ref SwExpandTexel for the 1:1-mapped scanline gather (source texels are
		consecutive in the store). R8 expands to `[v,255,255,255]` and RG8 to `[v,a,255,255]` - SSE2 /
		NEON widen 16 resp. 8 texels per iteration by interleaving with all-ones lanes; the scalar tail
		(and the scalar-only build) emits one 32-bit store per texel. A 4-byte store is a plain copy.
	*/
	inline void SwExpandTexelRun(std::uint8_t* DEATH_RESTRICT dst, const std::uint8_t* DEATH_RESTRICT src, std::int32_t count, std::int32_t bpp)
	{
		if (bpp == 4) {
			std::memcpy(dst, src, std::size_t(count) * 4);
			return;
		}
		std::int32_t i = 0;
		if (bpp == 1) {
#if defined(DEATH_TARGET_SSE2)
			const __m128i ones = _mm_set1_epi8(char(0xFF));
			for (; i + 16 <= count; i += 16, src += 16, dst += 64) {
				const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
				// [v,FF] byte pairs, then [v,FF,FF,FF] dwords
				const __m128i loVF = _mm_unpacklo_epi8(v, ones);
				const __m128i hiVF = _mm_unpackhi_epi8(v, ones);
				const __m128i ffff = ones;
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_unpacklo_epi16(loVF, ffff));
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 16), _mm_unpackhi_epi16(loVF, ffff));
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 32), _mm_unpacklo_epi16(hiVF, ffff));
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 48), _mm_unpackhi_epi16(hiVF, ffff));
			}
#elif defined(DEATH_TARGET_NEON)
			const uint8x8_t ones8 = vdup_n_u8(0xFF);
			for (; i + 8 <= count; i += 8, src += 8, dst += 32) {
				const uint8x8_t v = vld1_u8(src);
				const uint8x8x2_t vf = vzip_u8(v, ones8);				// [v,FF] byte pairs
				const uint16x4_t ffff = vdup_n_u16(0xFFFF);
				const uint16x4x2_t lo = vzip_u16(vreinterpret_u16_u8(vf.val[0]), ffff);
				const uint16x4x2_t hi = vzip_u16(vreinterpret_u16_u8(vf.val[1]), ffff);
				vst1_u8(dst,      vreinterpret_u8_u16(lo.val[0]));
				vst1_u8(dst + 8,  vreinterpret_u8_u16(lo.val[1]));
				vst1_u8(dst + 16, vreinterpret_u8_u16(hi.val[0]));
				vst1_u8(dst + 24, vreinterpret_u8_u16(hi.val[1]));
			}
#endif
			for (; i < count; i++, src++, dst += 4) {
				dst[0] = src[0];
				dst[1] = 255;
				dst[2] = 255;
				dst[3] = 255;
			}
		} else {
			// RG8: [index, alpha] pairs widen to [index, alpha, 255, 255]
#if defined(DEATH_TARGET_SSE2)
			const __m128i ones = _mm_set1_epi8(char(0xFF));
			for (; i + 8 <= count; i += 8, src += 16, dst += 32) {
				const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));	// 8 x [v,a]
				const __m128i vaFF = ones;
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dst),
					_mm_unpacklo_epi16(v, vaFF));
				_mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 16),
					_mm_unpackhi_epi16(v, vaFF));
			}
#elif defined(DEATH_TARGET_NEON)
			const uint16x4_t ffff = vdup_n_u16(0xFFFF);
			for (; i + 4 <= count; i += 4, src += 8, dst += 16) {
				const uint16x4_t v = vreinterpret_u16_u8(vld1_u8(src));	// 4 x [v,a]
				const uint16x4x2_t widened = vzip_u16(v, ffff);
				vst1_u8(dst,     vreinterpret_u8_u16(widened.val[0]));
				vst1_u8(dst + 8, vreinterpret_u8_u16(widened.val[1]));
			}
#endif
			for (; i < count; i++, src += 2, dst += 4) {
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = 255;
				dst[3] = 255;
			}
		}
	}

	/**
		@brief Per-command lookup table replacing the PaletteRemap fragment on the deferred tile path

		The PaletteRemap / BatchedPaletteRemap fragment is fundamentally a 256-entry table lookup: the
		palette row (`vPaletteOffset`) and the instance tint are constant for a whole draw command, so the
		fully tinted, quantized output of every possible index byte can be precomputed once per command
		instead of running the generic transpiled fragment (function-pointer call, redundant re-sample,
		float palette math) per pixel. The alpha channel additionally depends on the texel's own source
		alpha when the index texture is RG8 (its sampling swizzle maps `.a` to a texel byte); that case
		keeps the RGB lookup and folds the per-pixel factor in with the same float math the fragment uses,
		so the fast path stays bit-identical in every case.

		Built by @ref SwTileRenderer::SubmitCommand() when @ref DrawContext::paletteRemapHint is set and
		every constraint holds (nearest-sampled index texture, byte-addressable swizzle); consumed by the
		tile rasterizer in place of the fragment callback.
	*/
	struct SwPaletteLut
	{
		/** @brief Final packed RGBA per index (palette texel x tint); alpha assumes a `1.0` (or constant) source alpha */
		std::uint8_t packed[256][4];
		/** @brief Palette texel alpha byte per index, for the per-pixel source-alpha case */
		std::uint8_t palAlphaByte[256];
		/** @brief Tint (instance color) alpha the fragment multiplies last */
		float tintAlpha;
		/** @brief Byte of the gathered (4-byte expanded) texel carrying the palette index (the unit-0 sampling swizzle's `.r` source) */
		std::int32_t indexByteOffset;
		/** @brief Byte of the gathered texel carrying source alpha (swizzle `.a` source), or `-1` (constant 1) / `-2` (constant 0) */
		std::int32_t alphaByteOffset;
	};

	/**
		@brief One transformed screen-space vertex

		The output of the vertex stage (@ref FFState transform + viewport map): pixel-space position, texture
		coordinates and an RGBA color forwarded to the fragment stage.
	*/
	struct Vertex2D
	{
		float x, y;			/**< Screen-space pixel position */
		float u, v;			/**< Texture coordinates */
		float r, g, b, a;	/**< Vertex color */
	};

	/**
		@brief Everything one draw call needs beyond the persistent render state

		Gathers the bound textures, the fixed-function parameters (@ref ff), an optional fragment callback and
		a snapshot of the blend / scissor state. The device fills one of these per draw and hands it to
		@ref SwRaster::SetDrawContext(). When @ref vertexData is null the four sprite-quad corners are
		synthesized procedurally from @ref ff (the path the sprite pipeline uses); otherwise @ref vertexData
		points at interleaved, already clip-space vertices for the general primitive path.
	*/
	struct DrawContext
	{
		/** @brief Textures bound to each unit (indexed by `ff.textureUnit` and by a fragment callback) */
		const SwTexture* textures[MaxTextureUnits] = {};
		/** @brief Fixed-function per-draw parameters */
		FFState ff;
		/** @brief Optional per-pixel fragment callback (null for the plain textured / tinted path) */
		FragmentShaderFn fragmentShader = nullptr;
		/** @brief Opaque parameter block passed to @ref fragmentShader */
		void* fragmentShaderUserData = nullptr;
		/**
		 * @brief Size in bytes of the @ref fragmentShaderUserData block (0 when there is none)
		 *
		 * Set by the device so the tile renderer can snapshot the block into per-command storage when a draw
		 * is deferred. The block must be trivially copyable. Leave 0 for the immediate-only path.
		 */
		std::uint32_t fragmentShaderUserDataSize = 0;

		/**
		 * @brief Marks a PaletteRemap / BatchedPaletteRemap draw (set by the device)
		 *
		 * A hint only: it promises @ref fragmentShader is the transpiled PaletteRemap fragment (whose
		 * parameter block is a single `vPaletteOffset` float), so the tile renderer may build a
		 * @ref SwPaletteLut for the deferred command. Every other constraint is validated at build time;
		 * when any fails, the draw simply keeps the generic fragment callback.
		 */
		bool paletteRemapHint = false;
		/**
		 * @brief Palette LUT consumed in place of @ref fragmentShader (deferred tile path only)
		 *
		 * Never set by the device: the tile renderer builds the table per deferred command and points its
		 * command snapshot here before the flush. Stays null on the immediate path.
		 */
		const SwPaletteLut* paletteLut = nullptr;

		/**
		 * @brief Marks a solid no-texture sprite draw (DefaultSpriteNoTexture family, set by the device)
		 *
		 * A hint only: it promises @ref fragmentShader writes a per-draw constant (`packColor(vColor)`,
		 * independent of position and texel), so the tile renderer may evaluate the fragment once per
		 * command and fill/blend the constant color instead of calling it per pixel.
		 */
		bool constantColorHint = false;

		/** @brief Whether blending is enabled for this draw */
		bool blendingEnabled = false;
		/** @brief Source blend factor */
		SwBlendFactor blendSrc = SwBlendFactor::SrcAlpha;
		/** @brief Destination blend factor */
		SwBlendFactor blendDst = SwBlendFactor::OneMinusSrcAlpha;

		/** @brief Whether the scissor test is enabled for this draw */
		bool scissorEnabled = false;
		/** @brief Scissor rectangle in bottom-up (OpenGL) window coordinates */
		Recti scissorRect = Recti(0, 0, 0, 0);

		/** @brief Interleaved general vertices `[x, y, u, v]` in clip space, or null to synthesize the sprite quad from @ref ff */
		const void* vertexData = nullptr;
		/** @brief Byte stride between general vertices (0 == four tightly packed floats) */
		std::int32_t vertexStride = 0;
	};

	/**
		@brief Fixed-function CPU rasterizer of the software backend

		Rasterizes the sprite pipeline into a caller-supplied RGBA8 surface without any GPU. It keeps a small
		amount of persistent state (the destination color buffer, the viewport, and the blend / scissor state)
		set through the `Set*` calls, then rasterizes each draw against the @ref DrawContext installed with
		@ref SetDrawContext(). Three inner paths cover the pipeline: a direct memcpy / stretch blit for
		full-screen copies, a SIMD axis-aligned quad fast path for upright sprites, and affine-quad /
		general-triangle paths for rotated or sheared geometry. Every path supports the same set of blend
		modes, all @ref nCine::SamplerWrapping wrap modes, nearest / bilinear sampling and an optional per-pixel
		fragment callback.

		The rasterizer never owns the destination memory or the textures; the device layer resolves both and
		hands them in. Depth is a no-op (the renderer is 2D).
	*/
	class SwRaster
	{
	public:
		SwRaster() = delete;
		~SwRaster() = delete;

		/**
			@brief Points the rasterizer at the destination RGBA8 surface

			@param pixels			Base of a tightly packed `width * height * 4` RGBA8 buffer (owned by the caller)
			@param width			Surface width in pixels
			@param height			Surface height in pixels
			@param isFboTarget		`true` when the surface is a render-target texture (rows are written bottom-up
									to match the OpenGL framebuffer convention); `false` for the top-down screen buffer
		*/
		static void SetColorBuffer(std::uint8_t* pixels, std::int32_t width, std::int32_t height, bool isFboTarget);

		/** @brief Sets the viewport used to map clip space to destination pixels */
		static void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
		/** @brief Sets the scissor test in bottom-up (OpenGL) window coordinates */
		static void SetScissor(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);
		/** @brief Sets the blend state applied by every draw path */
		static void SetBlending(bool enabled, SwBlendFactor src, SwBlendFactor dst);
		/** @brief Fills the whole color buffer with the given normalized RGBA (ignores the scissor) */
		static void Clear(float r, float g, float b, float a);

		/** @brief Installs the per-draw context read by the next @ref Draw() call */
		static void SetDrawContext(const DrawContext& ctx);
		/** @brief Detaches the per-draw context */
		static void ClearDrawContext();

		/** @brief Rasterizes @p count vertices from @p firstVertex as @p primitive into the current color buffer */
		static void Draw(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t count);

		/**
			@brief Renders every draw the tile renderer has deferred for the current color buffer

			Forwards to the tile-based deferred layer's flush. The device calls this before the color buffer
			is read for presentation, so all queued draws have landed in it. A no-op when nothing is queued;
			it does not return until every worker thread has finished writing.
		*/
		static void Flush();
	};
}

#endif
