#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nCine::RHI::LegacyGL
{
	class LegacyGlShaderProgram;
	class LegacyGlRenderTarget;
	class LegacyGlTexture;
	// Defined by the generated fixed-function effect table the device translation unit includes
	// (Shaders/Generated/LegacyGlGeneratedEffects.h, exactly like the PVR and GX backends consume theirs);
	// everyone else only ever holds an opaque entry pointer resolved at program load, so the incomplete
	// type is enough here
	struct FixedFunctionGeneratedEffect;

	/**
		@brief Pipeline-state and draw-call facade of the legacy-OpenGL backend (aliased as `RHI::Device`)

		The fixed-function GL twin of the console devices: each draw decodes the bound program's instance
		block(s) exactly like `SwDevice::Dispatch`, CPU-transforms the four sprite corners to raster pixels
		and submits them as triangles through `glDrawArrays`, under an ortho projection over the target in
		pixels - so nothing is ever transformed by GL and a vertex means what it says. Texture coordinates
		are emitted in TEXELS, like the console backends do, and a texture matrix set once per binding
		scales them into GL's normalized range.

		Vertices come out of a per-frame arena and consecutive primitives that share their whole state are
		accumulated into one `glDrawArrays`, so a tile layer or a text run costs one draw call.

		What this backend is for is the machines whose only OpenGL is a fixed-function one: MorphOS, whose
		TinyGL carries the texture combiners and framebuffer objects, AmigaOS 4, whose MiniGL over Warp3D
		has the combiners but no framebuffer objects at all, and by extension any other legacy GL. There are no shaders, so effects come from the same transpiled `fixed_function` tables
		the consoles consume (`Shaders/Generated/LegacyGlGeneratedEffects.h`), with the presets mapped onto
		`GL_COMBINE` programs; indexed textures have no hardware palette here, so they are baked to RGBA by
		@ref LegacyGlTexture exactly as the RG8 content is on the consoles.

		Because there are no shaders, the game runs the direct tier (see `RhiFwd.h`): the scene is rendered
		straight to the display at the logical resolution and the CPU lightmap is handed to the device
		through @ref SetPendingSoftwareLighting() instead of a compositing shader pass.
	*/
	class LegacyGlDevice
	{
	public:
		/** @brief Monotonic count of finished frames, used to detect "still referenced by the current frame" resources */
		static std::uint32_t GetSceneCounter() {
			return _sceneCounter;
		}

		/**
			@brief Returns the generated fixed-function effect of a (program, variant) key, or `nullptr`

			Called once per program load from @ref LegacyGlShaderProgram::SetProgramIdentity(). Scans the
			fixed-function effect table of this backend; a program that is absent from it has no
			`fixed_function` block in its `.shader` file (Lighting, Blur, the Resize* family, ...) and its
			draws are skipped with a one-time warning, exactly as on the other fixed-function backends.
		*/
		static const FixedFunctionGeneratedEffect* FindGeneratedEffect(const char* program, const char* variant);

		LegacyGlDevice() = delete;
		~LegacyGlDevice() = delete;

		struct ScissorState
		{
			bool Enabled = false;
			Recti Rect = Recti(0, 0, 0, 0);
		};

		struct BlendingState
		{
			bool Enabled = false;
			nCine::BlendingFactor SrcRgb = nCine::BlendingFactor::One;
			nCine::BlendingFactor DstRgb = nCine::BlendingFactor::Zero;
			nCine::BlendingFactor SrcAlpha = nCine::BlendingFactor::One;
			nCine::BlendingFactor DstAlpha = nCine::BlendingFactor::Zero;
		};

		struct DepthTestState
		{
			bool TestEnabled = false;
			bool MaskEnabled = true;
		};

		struct CullFaceState
		{
			bool Enabled = false;
			CullFaceMode Mode = CullFaceMode::Back;
		};

		static void SetBlendingEnabled(bool enabled);
		static void SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha);
		static BlendingState GetBlendingState();
		static void SetBlendingState(const BlendingState& state);

		static void SetDepthTestEnabled(bool enabled);
		static void SetDepthMaskEnabled(bool enabled);
		static DepthTestState GetDepthTestState();
		static void SetDepthTestState(const DepthTestState& state);

		static void SetCullFaceEnabled(bool enabled);
		static CullFaceState GetCullFaceState();
		static void SetCullFaceState(const CullFaceState& state);

		static ScissorState GetScissorState();
		static void SetScissorState(const ScissorState& state);
		static void SetScissor(const Recti& rect);
		static void SetScissorTestEnabled(bool enabled);

		static Recti GetViewport();
		static void SetViewport(const Recti& rect);
		static void InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);

		static Colorf GetClearColor();
		static void SetClearColor(const Colorf& color);
		static void Clear(ClearFlags flags);

		static void DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices);
		static void DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances);
		static void DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex);
		static inline void DrawElements(PrimitiveType primitive, std::uint32_t numIndices, std::uintptr_t indexOffset, std::int32_t baseVertex) {
			DrawElements(primitive, numIndices, IndexFormat::UInt16, indexOffset, baseVertex);
		}
		static void DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex);
		static inline void DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex) {
			DrawElementsInstanced(primitive, numIndices, IndexFormat::UInt16, indexOffset, numInstances, baseVertex);
		}

		static FenceHandle InsertFence();
		static void DeleteFence(FenceHandle& fence);
		static bool ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs);

		static void SetupInitialState();

		// -- Swap-chain / presentation surface (uniform across every backend) --

		/** @brief No-op (the window backend owns the presentation path) */
		static inline bool CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync) {
			static_cast<void>(windowHandle);
			static_cast<void>(width);
			static_cast<void>(height);
			static_cast<void>(vsync);
			return true;
		}
		/** @brief No-op */
		static inline void DestroySwapchain() {}
		/** @brief Records the raster size of the drawable, which the screen pass renders into */
		static void ResizeSwapchain(std::int32_t width, std::int32_t height);
		/** @brief Flushes what the frame batched; the window backend swaps the buffers itself */
		static void PresentFrame();

		/**
			@brief Ceiling on the texture dimension this backend will report, whatever the driver says

			Not a hardware limit - it is what keeps ONE image's worth of memory bounded on the machines
			this backend exists for. A desktop GL answers `GL_MAX_TEXTURE_SIZE` with 16384, and a tileset
			atlas cut to fit that is a single allocation of tens of megabytes; a Radeon-era MorphOS box
			has no business making one. Anything larger is split into pages by @ref LegacyGlTexture.
		*/
		static constexpr std::int32_t MaxTextureDimension = 2048;

		/**
			@brief Returns the maximum supported texture dimension (drives the tileset chunking)

			`GL_MAX_TEXTURE_SIZE` as the driver reports it, capped at @ref MaxTextureDimension. It is what
			`ContentResolver` cuts tileset atlases into chunks to fit, and it is also the size one texture
			page is built at, so content sized against this number is never paged.

			Prebaked content that is larger anyway (the small font atlas is 128x529) is NOT rejected -
			@ref LegacyGlTexture splits such an image into pages internally and the draw path picks the
			page a primitive samples - so `Texture::Initialize()` only warns about it on this backend
			instead of asserting.
		*/
		static std::int32_t GetMaxTextureDimension();

		/**
			@brief Returns `true` when the GL samples non-power-of-two textures

			Legacy GL is not required to: it is a GL 2.0 feature (`GL_ARB_texture_non_power_of_two`
			before that), so where it is missing every page is stored padded up to powers of two and the
			draw path scales texel coordinates by the padded size. Probed by @ref InitializeGl().
		*/
		static inline bool SupportsNonPowerOfTwo() {
			return _supportsNpot;
		}

		// -- Direct-tier presentation contract (see RhiFwd.h) --

		/** @brief Sets the logical resolution the scene is rendered at (scaled to the drawable at submit) */
		static void ResizeScreenFramebuffer(std::int32_t width, std::int32_t height);

		// -- GL session, driven by the window backend that owns the context --

		/** @brief Programs the once-per-context pipeline state this backend relies on */
		static void InitializeGl();
		/** @brief Flushes anything outstanding, so the exit path leaves the context idle */
		static void ShutdownGl();

		// -- Backend extensions (called by the resource types and read by the draw dispatch) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(LegacyGlShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static LegacyGlShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const LegacyGlTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~LegacyGlTexture) */
		static void UnbindTexture(const LegacyGlTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const LegacyGlTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Records the current draw render target (draws are redirected into it) */
		static void SetRenderTarget(LegacyGlRenderTarget* renderTarget);
		/** @brief Clears a render target from the device if it is the current one (called from ~LegacyGlRenderTarget) */
		static void UnbindRenderTarget(const LegacyGlRenderTarget* renderTarget);

		/**
			@brief Reports what the GL context calls itself

			The three strings are the driver's own (they live as long as the context does, which outlives
			anything that reads them here) and the version is parsed out of `GL_VERSION`. Zeros and null
			strings are answered before the context exists.
		*/
		static void DescribeContext(std::int32_t& majorVersion, std::int32_t& minorVersion, const char*& vendor,
			const char*& renderer, const char*& apiVersion);
		/** @brief Forgets the GL state the device caches, after something outside it changed a binding */
		static void InvalidateStateCache();
		/** @brief Returns `true` when framebuffer objects were found to work (probed by @ref InitializeGl()) */
		static inline bool SupportsFramebufferObjects() {
			return _supportsFbo;
		}
		/** @brief Shrinks a size to what the drawable can hold, which is what a copy-back target is limited to */
		static void ClampToDrawable(std::int32_t& width, std::int32_t& height);

		/** @brief Registers the intercepted shared palette texture (its rows are what indexed content is baked through) */
		static void RegisterPaletteTexture(LegacyGlTexture* texture);
		/** @brief Invalidates the bakes built from the given palette rows after an upload */
		static void NotifyPaletteTextureChanged(LegacyGlTexture* texture, std::int32_t firstRow, std::int32_t rowCount);


		/**
			@brief Queues the CPU lightmap combine for the next `Combine` draw (the direct-tier lighting contract)

			The water half of the compositor is NOT queued here: it is a fixed_function block of the
			CombineWithWater programs (see CombineWithWater.shader), so the `water*` parameters of the shared
			signature are ignored by this backend - only the software one still reads them.
		*/
		static void SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
			std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
			bool waterActive = false, float waterLevelPx = 0.0f, float waterTime = 0.0f, float waterCamY = 0.0f);
		/** @brief Drops any lighting entries not consumed this frame (called by the window backend at present) */
		static void EndFrame();

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;

		struct UniformRange
		{
			const std::uint8_t* Data = nullptr;
			std::uint32_t Size = 0;
		};

		struct PendingSoftwareLight
		{
			const float* Lightmap = nullptr;
			std::int32_t LmW = 0, LmH = 0, Scale = 1;
			std::int32_t VpX = 0, VpY = 0, VpW = 0, VpH = 0;
			float AmbR = 0.0f, AmbG = 0.0f, AmbB = 0.0f;
		};

		static BlendingState _blending;
		static DepthTestState _depthTest;
		static CullFaceState _cullFace;
		static ScissorState _scissor;
		static Recti _viewport;
		static Colorf _clearColor;

		static LegacyGlShaderProgram* _currentProgram;
		static const LegacyGlTexture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static LegacyGlRenderTarget* _currentRenderTarget;

		static bool _glInitialized;
		/** @brief Whether framebuffer objects work here, which decides how a render target is filled */
		static bool _supportsFbo;
		/** @brief `GL_MAX_TEXTURE_SIZE` capped at @ref MaxTextureDimension */
		static std::int32_t _maxTextureSize;
		/** @brief Whether a texture may keep its own dimensions instead of being padded up */
		static bool _supportsNpot;
		static std::int32_t _logicalWidth;
		static std::int32_t _logicalHeight;
		/** @brief Raster size of the drawable, which the screen pass renders into */
		static std::int32_t _rasterWidth;
		static std::int32_t _rasterHeight;
		static std::uint32_t _sceneCounter;

		static LegacyGlTexture* _paletteTexture;
		static std::uint32_t _paletteGeneration;

		static std::vector<PendingSoftwareLight> _pendingSoftwareLights;

		/** @brief Brings the GL session up if it is not up yet (the draw paths and Clear() call this) */
		static void EnsureList();
		/** @brief Points GL at the current draw surface (the drawable or a render target) and projects onto it */
		static void ApplyDrawTarget();
		/** @brief Programs the GL scissor for the current target from the tracked engine state */
		static void ApplyScissor();
		/** @brief Maps the logical space onto the current target (the drawable's scale, or 1:1 for a target) */
		static void GetTargetScale(float& scaleX, float& scaleY);

		/** @brief Resolves the index range of a `DrawElements()` into a host pointer, or `nullptr` if it cannot be read */
		static const std::uint16_t* ResolveHostIndices(IndexFormat indexFormat, std::uintptr_t indexOffset, std::uint32_t numIndices);

		static void Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
			const std::uint16_t* indices = nullptr, std::int32_t indexCount = 0);
		// Draws a whole tile-layer mesh (a triangle list of position/texcoord/colour vertices)
		static void DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
			const std::uint16_t* indices, std::int32_t indexCount);
		// Draws a vertex-fed textured line strip (the weapon wheel) through GL's native line primitive
		static void DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices);
		static void ApplyPendingSoftwareLighting();
	};
}
