#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstddef>
#include <cstdint>
#include <vector>

typedef struct C3D_RenderTarget_tag C3D_RenderTarget;

namespace nCine::RHI::PICA
{
	class PicaShaderProgram;
	class PicaRenderTarget;
	class PicaTexture;
	// Defined by the generated fixed-function effect table the device translation unit includes
	// (Shaders/Generated/PicaGeneratedEffects.h, exactly like the PVR, GX and GU backends consume theirs);
	// everyone else only ever holds an opaque entry pointer resolved at program load, so the incomplete
	// type is enough here
	struct FixedFunctionGeneratedEffect;

	/**
		@brief Pipeline-state and draw-call facade of the PICA backend (aliased as `RHI::Device`)

		The Nintendo 3DS twin of the GU, GX and PVR devices: each draw decodes the bound program's instance
		block(s) exactly like `SwDevice::Dispatch`, CPU-transforms the four sprite corners to screen pixels and
		submits them to the PICA200 as triangles under a passthrough vertex program (Shaders/Pica/Sprite.v.pica),
		whose one uniform is the orthographic matrix that maps screen pixels into clip space - `Mtx_OrthoTilt`
		for the top screen, whose framebuffer is rotated by 90 degrees, `Mtx_Ortho` for a render target.

		Everything the frame draws goes through citro3d's command buffer, which the GPU consumes when the frame
		is closed, and vertices come out of a per-frame arena in the linear heap that is written back from the
		data cache before each draw call - the GPU reads main memory without seeing the ARM11's cache, which is
		the classic source of "nothing renders" on this hardware. Two arenas alternate between frames, because
		the GPU is still reading one frame's vertices while the next one is being built. Consecutive primitives
		that share their whole GPU state are accumulated into a single `C3D_DrawArrays`, so a tile layer or a
		text run costs one draw call.

		The PICA200 has a real raster scissor, so scissored draws are not clipped geometrically - the rect is
		programmed (in the rotated framebuffer's coordinates) and the hardware cuts the primitives. Its texture
		combiner is the GX's TEV in miniature: six stages with the same operand set, an interpolate function and
		the x2/x4 output scales, so the `TintMix` and `ModulateX*` presets are single-stage programs here where
		the GE had to reject them. What it has NO trace of is a colour lookup table, so indexed textures are
		baked through their palette row on the CPU (see @ref PicaTexture).

		Because the fragment stage has no programs, the game runs the direct tier (see `RhiFwd.h`): the scene
		is rendered straight to the display at the logical resolution and the CPU lightmap is handed to the
		device through @ref SetPendingSoftwareLighting() instead of a compositing shader pass.
	*/
	class PicaDevice
	{
	public:
		/** @brief The top screen, which is what the game renders to (the bottom one keeps the boot console) */
		static constexpr std::int32_t ScreenWidth = 400;
		static constexpr std::int32_t ScreenHeight = 240;

		/** @brief Monotonic count of finished frames, used to detect "still referenced by the current frame" resources */
		static std::uint32_t GetSceneCounter() {
			return _sceneCounter;
		}

		/**
			@brief Returns the generated fixed-function effect of a (program, variant) key, or `nullptr`

			Called once per program load from @ref PicaShaderProgram::SetProgramIdentity(). Scans the
			fixed-function effect table of this backend; a program that is absent from it has no
			`fixed_function` block in its `.shader` file (Lighting, Blur, the Resize* family, ...) and its
			draws are skipped with a one-time warning, exactly as on the other fixed-function backends.
		*/
		static const FixedFunctionGeneratedEffect* FindGeneratedEffect(const char* program, const char* variant);

		PicaDevice() = delete;
		~PicaDevice() = delete;

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
		/** @brief Closes the frame's command list and queues the display transfer to the top screen (called by the Ctr window backend once per frame) */
		static void PresentFrame();

		/** @brief The dimension a single PICA200 texture cannot exceed (a hardware limit) */
		static constexpr std::int32_t HardwareTextureDimension = 1024;

		/**
			@brief Returns the maximum supported texture dimension (drives the tileset chunking)

			The GPU's true limit, reported honestly: it makes `ContentResolver` cut tileset atlases into chunks
			that each fit one texture (its own preference of 512 rows is below this anyway). Prebaked content
			that is larger anyway is NOT rejected - @ref PicaTexture splits such an image into pages internally
			and the draw path picks the page a primitive samples - so `Texture::Initialize()` only warns about
			it on this backend instead of asserting.
		*/
		static inline std::int32_t GetMaxTextureDimension() {
			return HardwareTextureDimension;
		}

		// -- Direct-tier presentation contract (see RhiFwd.h) --

		/** @brief Sets the logical resolution the scene is rendered at (scaled to 400x240 at submit) */
		static void ResizeScreenFramebuffer(std::int32_t width, std::int32_t height);

		// -- GPU session, driven by the Ctr window backend --

		/** @brief Brings up citro3d, the screen target, the vertex program and the initial pipeline state; `false` if the GPU cannot be used */
		static bool InitializePica();
		/** @brief Tears the GPU session down again, so the exit path leaves the hardware idle */
		static void ShutdownPica();

		// -- PICA backend extensions (called by the resource types and read by the draw dispatch) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(PicaShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static PicaShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const PicaTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~PicaTexture) */
		static void UnbindTexture(const PicaTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const PicaTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Records the current draw render target (draws redirect the GPU's colour buffer to it) */
		static void SetRenderTarget(PicaRenderTarget* renderTarget);
		/** @brief Clears a render target from the device if it is the current one (called from ~PicaRenderTarget) */
		static void UnbindRenderTarget(const PicaRenderTarget* renderTarget);
		/** @brief Forgets a citro3d target that is about to be deleted (called from the texture that owns it) */
		static void UnbindRenderTargetSurface(const C3D_RenderTarget* target);
		/** @brief Fills a freshly created render target's colour buffer with black, sequenced with the frame */
		static void ClearRenderTargetSurface(C3D_RenderTarget* target);

		/**
			@brief Frees a linear-heap block once the GPU can no longer be reading it

			The commands referencing a store sit in a command list that is only run when the frame is closed,
			and the GPU works on it while the next frame is being built - so a store a texture drops mid-frame
			(a level load replaces textures while the loading screen is being drawn) stays allocated for two
			more presents and is freed by the device then. Exactly the hazard that froze the PSP port (see
			`GuDevice::SyncBeforeStoreRelease`), solved by deferring instead of by waiting.
		*/
		static void DeferredLinearFree(void* block);
		/** @brief Like @ref DeferredLinearFree() for a block that may live in VRAM instead */
		static void DeferredFree(void* block, bool inVram);

		/** @brief Registers the intercepted shared palette texture (its rows are what the bakes resolve through) */
		static void RegisterPaletteTexture(PicaTexture* texture);
		/** @brief Invalidates the bakes built from the given palette rows after an upload */
		static void NotifyPaletteTextureChanged(PicaTexture* texture, std::int32_t firstRow, std::int32_t rowCount);

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

		static PicaShaderProgram* _currentProgram;
		static const PicaTexture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static PicaRenderTarget* _currentRenderTarget;

		static bool _picaInitialized;
		static bool _frameOpen;
		static std::int32_t _logicalWidth;
		static std::int32_t _logicalHeight;
		static std::uint32_t _sceneCounter;

		static PicaTexture* _paletteTexture;
		static std::uint32_t _paletteGeneration;

		static std::vector<PendingSoftwareLight> _pendingSoftwareLights;

		/** @brief Opens the frame if it is not open yet (the draw paths and Clear() call this) */
		static void EnsureFrame();
		/** @brief Points the GPU at the current draw surface (the screen target or a render target) */
		static void ApplyDrawTarget();
		/** @brief Programs the hardware scissor for the current target from the tracked engine state */
		static void ApplyScissor();
		/** @brief Maps the logical space onto the current target (scale of the 400x240 panel, or 1:1 for a target) */
		static void GetTargetScale(float& scaleX, float& scaleY);
		/** @brief Frees the blocks whose deferral ran out (called once per present) */
		static void ProcessDeferredFrees();

		/** @brief Resolves the index range of a `DrawElements()` into a host pointer, or `nullptr` if it cannot be read */
		static const std::uint16_t* ResolveHostIndices(IndexFormat indexFormat, std::uintptr_t indexOffset, std::uint32_t numIndices);

		static void Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
			const std::uint16_t* indices = nullptr, std::int32_t indexCount = 0);
		// Draws a whole tile-layer mesh (a triangle list of position/texcoord/colour vertices)
		static void DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
			const std::uint16_t* indices, std::int32_t indexCount);
		// Draws a vertex-fed textured line strip (the weapon wheel) as one-pixel-wide quads
		static void DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices);
		static void ApplyPendingSoftwareLighting();
	};
}
