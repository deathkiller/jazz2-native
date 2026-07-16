#pragma once

#if defined(WITH_RHI_SOFTWARE)

#include "SwTexture.h"
#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"

#include <cstdint>

namespace nCine::RhiSoftware
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
