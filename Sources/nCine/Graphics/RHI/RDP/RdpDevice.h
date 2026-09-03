#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nCine::RHI::RDP
{
	class RdpShaderProgram;
	class RdpRenderTarget;
	class RdpTexture;
	// Defined by the generated fixed-function effect table the device translation unit includes
	// (Shaders/Generated/RdpGeneratedEffects.h, exactly like the PVR/GX/GU backends consume theirs);
	// everyone else only ever holds an opaque entry pointer resolved at program load, so the incomplete
	// type is enough here
	struct FixedFunctionGeneratedEffect;

	/**
		@brief Pipeline-state and draw-call facade of the RDP backend (aliased as `RHI::Device`)

		The Nintendo 64 twin of the GX/PVR/GU devices: each draw decodes the bound program's instance
		block(s) exactly like `SwDevice::Dispatch`, CPU-transforms the four sprite corners to screen pixels
		and submits them through libdragon's rdpq as screen-space primitives - the RDP rasterizes exactly
		what it is given (there is no T&L stage in this backend; libdragon's OpenGL would run one on the
		RSP, but rdpq is driven directly with CPU transforms, the way the PVR backend drives KOS prims).
		Axis-aligned quads go out as `TEXTURE_RECTANGLE` commands, rotated ones and synthesized strips as
		`rdpq_triangle` pairs.

		The one piece of texturing machinery the other consoles do not have is TMEM: the RDP samples only
		out of a 4 KB on-chip buffer (2 KB for CI8 texels - the upper half holds the palette), so every
		textured primitive is preceded by an upload of the sub-window it samples out of the texture's
		RDRAM store, deduplicated against the window already resident (consecutive draws of the same tile
		or glyph reload nothing). An axis-aligned primitive whose window exceeds TMEM is split into bands
		that fit; a rotated one clamps its window with a one-time warning (nothing on this tier draws one
		that big today).

		Indexed textures are resolved by the hardware TLUT (`FMT_CI8` + `rdpq_tex_upload_tlut`), loaded
		per draw from whatever palette texture the material bound - the direct analogue of the PVR's
		palette banks and the GE's CLUT; RG8 index+alpha content takes the same per-palette-row CPU bake
		the other consoles use. The RDP has a real raster scissor, so scissored draws are cut by the
		hardware rather than clipped geometrically.

		The game is 2D in painter's order, so no Z buffer is attached (`rdpq_attach(surface, NULL)`) and
		depth compare stays off - which also saves 150 KB of the console's RDRAM. Because the RDP has no
		programmable shaders, the game runs the direct tier (see `RhiFwd.h`): the scene is rendered
		straight into the display surface at the logical resolution (the VI upscales 320x240 to the TV)
		and the CPU lightmap is handed to the device through @ref SetPendingSoftwareLighting().
	*/
	class RdpDevice
	{
	public:
		/** @brief Monotonic count of finished frames, used to detect "still referenced by the current frame" resources */
		static std::uint32_t GetSceneCounter() {
			return _sceneCounter;
		}

		/** @brief Returns `true` once InitializeRdp() has run (guards the rspq waits of the resource types) */
		static bool IsInitialized() {
			return _rdpInitialized;
		}

		/**
			@brief Returns `true` when the RDP has retired every command enqueued for the given frame

			Frames are numbered by @ref GetSceneCounter(); PresentFrame() plants an rspq syncpoint per
			frame, so retirement is answered exactly (a cheap check, no stall) whatever the display
			buffer count. The store-rewrite guards of the resource types call this with their
			used-in-frame stamps and only wait on a genuine conflict.
		*/
		static bool IsFrameRetired(std::uint32_t frame);
		/** @brief Temporary instrumentation hook: records a texel-store rebuild of @p bytes (see TraceDrawStatistics) */
		static void TraceStoreRebuild(std::uint32_t bytes, bool isBake);
		/** @brief Blocks until @ref IsFrameRetired(frame) holds (the frame being built drains the whole queue) */
		static void WaitForFrame(std::uint32_t frame);

		/**
			@brief Returns the generated fixed-function effect of a (program, variant) key, or `nullptr`

			Called once per program load from @ref RdpShaderProgram::SetProgramIdentity(). Scans the
			fixed-function effect table of this backend; a program that is absent from it has no
			`fixed_function` block in its `.shader` file (Lighting, Blur, the Resize* family, ...) and its
			draws are skipped with a one-time warning, exactly as on the other fixed-function backends.
		*/
		static const FixedFunctionGeneratedEffect* FindGeneratedEffect(const char* program, const char* variant);

		RdpDevice() = delete;
		~RdpDevice() = delete;

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
		/** @brief No-op (the logical resolution is driven by ResizeScreenFramebuffer() from the render pipeline) */
		static inline void ResizeSwapchain(std::int32_t width, std::int32_t height) {
			static_cast<void>(width);
			static_cast<void>(height);
		}
		/** @brief Closes the frame's RDP work and schedules the display flip (called by the N64 window backend once per frame) */
		static void PresentFrame();

		/**
			@brief Returns the maximum supported texture dimension (drives the tileset chunking)

			1024 is what the tile descriptors can address (10.2 fixed-point coordinates); the per-primitive
			TMEM window is what actually constrains sampling, and the device manages that itself. See the
			rationale in @ref RdpRhiCapabilities.
		*/
		static inline std::int32_t GetMaxTextureDimension() {
			return 1024;
		}

		// -- Direct-tier presentation contract (see RhiFwd.h) --

		/** @brief Sets the logical resolution the scene is rendered at (scaled to 320x240 at submit) */
		static void ResizeScreenFramebuffer(std::int32_t width, std::int32_t height);

		// -- RDP session, driven by the N64 window backend --

		/**
			@brief Brings up the rdpq layer (the window backend has already run display_init)

			Mirrors PvrDevice::InitializePvr(): the window backend owns the video mode and calls this once
			after it; per frame it then calls @ref PresentFrame() and @ref EndFrame().
		*/
		static void InitializeRdp();

		// -- RDP backend extensions (called by the resource types and read by the draw dispatch) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(RdpShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static RdpShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const RdpTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~RdpTexture) */
		static void UnbindTexture(const RdpTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const RdpTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Records the current draw render target (draws re-attach the RDP to its surface) */
		static void SetRenderTarget(RdpRenderTarget* renderTarget);
		/** @brief Clears a render target from the device if it is the current one (called from ~RdpRenderTarget) */
		static void UnbindRenderTarget(const RdpRenderTarget* renderTarget);

		/** @brief Registers the intercepted shared palette texture (rows become hardware TLUTs) */
		static void RegisterPaletteTexture(RdpTexture* texture);
		/** @brief Invalidates the TLUT copies (and RG8 bakes) built from the given palette rows after an upload */
		static void NotifyPaletteTextureChanged(RdpTexture* texture, std::int32_t firstRow, std::int32_t rowCount);

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

		// Only what ApplyPendingSoftwareLighting() actually reads is kept: the interface's lightmap scale
		// and the water wave parameters (time, camera Y) are accepted and dropped, because this backend's
		// water v1 draws constant tint bands - deliberately no wave animation, see the water pass there
		struct PendingSoftwareLight
		{
			const float* Lightmap = nullptr;
			std::int32_t LmW = 0, LmH = 0;
			std::int32_t VpX = 0, VpY = 0, VpW = 0, VpH = 0;
			float AmbR = 0.0f, AmbG = 0.0f, AmbB = 0.0f;
		};

		static BlendingState _blending;
		static DepthTestState _depthTest;
		static CullFaceState _cullFace;
		static ScissorState _scissor;
		static Recti _viewport;
		static Colorf _clearColor;

		static RdpShaderProgram* _currentProgram;
		static const RdpTexture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static RdpRenderTarget* _currentRenderTarget;

		static bool _rdpInitialized;
		static std::int32_t _logicalWidth;
		static std::int32_t _logicalHeight;
		static std::uint32_t _sceneCounter;

		static RdpTexture* _paletteTexture;
		static std::uint32_t _paletteGeneration;

		static std::vector<PendingSoftwareLight> _pendingSoftwareLights;

		/** @brief Makes sure the RDP is attached to the current draw surface (acquiring the display buffer on the frame's first draw) */
		static void ApplyDrawTarget();
		/** @brief Programs the hardware scissor for the current target from the tracked engine state */
		static void ApplyScissor();
		/** @brief Maps the logical space onto the current target (scale of the 320x240 display, or 1:1 for a target) */
		static void GetTargetScale(float& scaleX, float& scaleY);

		// Shared preamble of the dispatch paths below
		/** @brief Returns the current program's instance-block data (and its clamped binding), or `nullptr` */
		static const std::uint8_t* ResolveInstanceBlockData(std::int32_t& binding);
		/** @brief Returns the current projection*view product (16 floats), rebuilt only when either matrix's values changed */
		static const float* ResolveProjView();
		/** @brief Folds the viewport, the target scale and the NDC mirroring into the constant NDC-to-raster mapping */
		static void ComputeRasterFold(Recti& viewport, float& rasterScaleX, float& rasterBiasX,
			float& rasterScaleY, float& rasterBiasY, float* maxScale = nullptr);

		/** @brief Resolves the index range of a `DrawElements()` into a host pointer, or `nullptr` if it cannot be read */
		static const std::uint16_t* ResolveHostIndices(IndexFormat indexFormat, std::uintptr_t indexOffset, std::uint32_t numIndices);

		static void Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
			const std::uint16_t* indices = nullptr, std::int32_t indexCount = 0);
		// Draws a whole tile-layer mesh (a triangle list of position/texcoord/colour vertices)
		static void DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
			const std::uint16_t* indices, std::int32_t indexCount);
		// Draws a vertex-fed textured line strip (the weapon wheel); the RDP has no line primitive, so
		// every segment goes out as a thin screen-space quad, exactly like on the PVR
		static void DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices);
		static void ApplyPendingSoftwareLighting();
	};
}
