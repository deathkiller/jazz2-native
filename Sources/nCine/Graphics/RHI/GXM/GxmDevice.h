#pragma once

#include "GxmMemory.h"
#include "GxmShaderProgram.h"
#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>

#include <Containers/SmallVector.h>

#include <psp2/gxm.h>

using namespace Death::Containers;

namespace nCine::RHI::GXM
{
	class GxmShaderProgram;
	class GxmRenderTarget;
	class GxmTexture;

	/**
		@brief Pipeline-state and draw-call facade of the sceGxm backend (aliased as `RHI::Device`)

		Exposes the OpenGL device's surface (blending, depth, cull, scissor, viewport, clear and the draw
		calls) so the backend-neutral render pipeline drives it unchanged, and owns the whole sceGxm session
		behind it: the rendering context and its ring buffers, the shader patcher, the display buffers with
		their sync objects and the display queue that scans them out.

		Three properties of the hardware shape everything below.

		**sceGxm renders in scenes.** The PowerVR SGX is a tile-based deferred renderer: primitives are
		binned, then each 32x32 tile is shaded once and written out. A scene (`sceGxmBeginScene()` ...
		`sceGxmEndScene()`) is one such pass over one surface, so a scene is opened lazily by the first clear
		or draw that follows a target change (@ref EnsureScene()) and closed when the target changes again or
		the frame is presented. Because the tile buffer's colour starts undefined rather than loaded, there
		is no "clear the surface" operation to issue mid-scene: @ref Clear() draws a full-screen quad with a
		built-in shader, which is what the clear costs on this architecture (and what OpenGL|ES drivers do
		here internally too).

		That undefined start is also why re-entering a surface within a frame would silently discard what an
		earlier scene drew into it. The pipeline does not do this - measured on the console, the screen
		surface takes exactly one scene per frame, and a render target takes one per pass it is the target of
		- but it is the constraint any change to the viewport chain has to respect.

		**sceGxm only draws indexed.** There is no `glDrawArrays` equivalent - every `sceGxmDraw()` consumes
		index data - so the device keeps one shared, GPU-visible buffer of increasing indices (0, 1, 2, ...)
		and hands a window of it to the non-indexed draws, which reproduces `glDrawArrays(first, count)`
		exactly. The two vertex-ID-free sprite layouts are served the same way: the shaders the Cg emitter
		produces read the quad corner (and the batched instance index) from vertex attributes rather than
		`gl_VertexID`, so the device also owns the two small static streams that feed them (see
		@ref GetQuadCornerStream()).

		**The display scans out top-down.** The engine renders in the OpenGL convention, and this backend
		replays it faithfully: every viewport is programmed with a positive Y scale, so clip -Y lands on row
		0 and every surface - the screen and each off-screen render target - is stored bottom-up exactly like
		OpenGL. That is what keeps a texture's V axis and its viewport's Y axis pointing the same way, so an
		off-screen round trip does not flip the image (the property that makes the scene composite and the
		directly drawn HUD agree). The scan-out then needs the one correction that convention implies:
		"screen" (no render target bound) is an intermediate surface, and @ref PresentFrame() flips it into
		the display buffer with a built-in shader - the same single OpenGL-to-native flip the Direct3D 11
		backend does with its present blit, which also scales the logical resolution up to the panel for free.
	*/
	class GxmDevice
	{
	public:
		GxmDevice() = delete;
		~GxmDevice() = delete;

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

		// -- Backend extensions (called by the resource types) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(GxmShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static GxmShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const GxmTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~GxmTexture to avoid a dangling pointer) */
		static void UnbindTexture(const GxmTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const GxmTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Returns the host data range bound to a uniform binding point (read by the draw path) */
		static void GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size);
		/** @brief Clears a render target from the device if it is the current one (called from ~GxmRenderTarget) */
		static void UnbindRenderTarget(const GxmRenderTarget* renderTarget);
		/** @brief Records the current draw render target (its bound color attachments receive the pixels) */
		static void SetRenderTarget(GxmRenderTarget* renderTarget);
		/** @brief Clears a destroyed program from the device's current-program tracking (called from ~GxmShaderProgram) */
		static void OnProgramDestroyed(const GxmShaderProgram* program);

		/**
			@brief Ends the scene currently being recorded, if any

			Called whenever what the following draws render into changes - a render-target switch, the frame's
			presentation, or a resource the open scene still references going away.
		*/
		static void FinishScene();

		/** @brief Returns the shader patcher every program creates its vertex/fragment programs through */
		static SceGxmShaderPatcher* GetShaderPatcher();
		/** @brief Returns the rendering context, or `nullptr` before @ref CreateSwapchain() */
		static SceGxmContext* GetContext();

		/** @brief Monotonic count of presented frames (used to detect resources still referenced by the current frame) */
		static std::uint32_t GetSceneCounter();

		/**
			@brief Returns the static vertex stream feeding `aQuadCorner` for the 4-vertex sprite strip

			The corners are in the order of the single-quad `TRIANGLE_STRIP` draw - {(1,0), (1,1), (0,0),
			(0,1)} - which is what the Cg emitter's rewrite of the engine's `gl_VertexID` corner formula
			expects (see `ShaderCompiler::VertexIdRewrite`). Two floats per vertex.
		*/
		static const void* GetQuadCornerStream();
		/**
			@brief Returns the static vertex stream feeding `aQuadCorner` + `aInstanceIndex` for batched sprites

			Six vertices per sprite (two triangles) carrying the batched corner order {(1,1), (0,1), (0,0),
			(0,0), (1,0), (1,1)} and the sprite's index within the batch, so a batched draw of `6 * n`
			vertices reproduces the `gl_VertexID / 6` instance lookup and the corner formula the emitter
			rewrote. Three floats per vertex; covers @ref MaxBatchSize sprites.
		*/
		static const void* GetBatchedCornerStream();
		/** @brief Largest batch the @ref GetBatchedCornerStream() covers (the 64 KB uniform-block budget divided by the smallest instance stride) */
		static constexpr std::uint32_t MaxBatchSize = 682;

		// -- sceGxm session lifecycle (called by the window backend) --

		/**
			@brief Brings up the whole sceGxm session and the display queue

			@param windowHandle Ignored (the Vita has one fixed display, so there is no window to attach to);
			                    accepted only to keep the uniform swap-chain contract of the other backends
			@param width        Ignored, the panel is always 960x544
			@param height       Ignored
			@param vsync        Whether @ref PresentFrame() waits for the display swap to be picked up
			@returns `true` if sceGxm, the context, the surfaces and the built-in shaders all came up
		*/
		static bool CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync);
		/** @brief Tears the session down (finishes the display queue, then releases everything in creation order) */
		static void DestroySwapchain();
		/** @brief No-op: the panel resolution is fixed (the logical resolution is a render-target size, not a swap-chain one) */
		static void ResizeSwapchain(std::int32_t width, std::int32_t height);
		/** @brief Flips the intermediate screen surface into the next display buffer and queues it for scan-out */
		static void PresentFrame();

		// The Vita has no multi-window support at all, so the ImGui multi-viewport hooks are inert here
		/** @brief Not supported on this platform (returns `nullptr`) */
		static inline void* CreateSecondarySwapchain(void* windowHandle, std::int32_t width, std::int32_t height) {
			static_cast<void>(windowHandle);
			static_cast<void>(width);
			static_cast<void>(height);
			return nullptr;
		}
		/** @brief Not supported on this platform */
		static inline void DestroySecondarySwapchain(void* handle) {
			static_cast<void>(handle);
		}
		/** @brief Not supported on this platform */
		static inline void ResizeSecondarySwapchain(void* handle, std::int32_t width, std::int32_t height) {
			static_cast<void>(handle);
			static_cast<void>(width);
			static_cast<void>(height);
		}
		/** @brief Not supported on this platform */
		static inline void BeginSecondaryFrame(void* handle, bool clear) {
			static_cast<void>(handle);
			static_cast<void>(clear);
		}
		/** @brief Not supported on this platform */
		static inline void EndSecondaryFrame() {}
		/** @brief Not supported on this platform */
		static inline void PresentSecondaryFrame(void* handle) {
			static_cast<void>(handle);
		}

		/** @brief The panel's fixed width in pixels */
		static constexpr std::int32_t DisplayWidth = 960;
		/** @brief The panel's fixed height in pixels */
		static constexpr std::int32_t DisplayHeight = 544;
		/** @brief Stride of a display buffer in pixels (the display controller requires a 64-pixel aligned stride) */
		static constexpr std::int32_t DisplayStride = 960;

		/**
			@brief Returns the largest supported 2D texture dimension

			4096 is the SGX543's limit; it only feeds the tileset chunking in `ContentResolver` and the
			texture-load assert, both of which the console's content stays well below.
		*/
		static constexpr std::int32_t GetMaxTextureDimension() {
			return 4096;
		}
		/** @brief Byte alignment the engine suballocates bound uniform ranges with (one std140 `vec4`) */
		static constexpr std::int32_t GetUniformBufferOffsetAlignment() {
			return 16;
		}

		/**
			@brief What the display-queue callback needs to hand one finished buffer to the display controller

			sceGxm copies this into its own queue and passes it back to the callback on the queue thread, so it
			has to be a plain trivially-copyable structure - and public, because the callback is a free function.
		*/
		struct DisplayQueueCallbackData
		{
			void* Address;
			bool Vsync;
		};

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;
		// Display buffers cycled by the display queue. Two is enough to keep the GPU and the scan-out from
		// fighting over one surface; a third only helps when a frame occasionally overruns its vblank
		static constexpr std::uint32_t DisplayBufferCount = 3;

		struct UniformRange
		{
			const std::uint8_t* Data = nullptr;
			std::uint32_t Size = 0;
		};

		static BlendingState blending_;
		static DepthTestState depthTest_;
		static CullFaceState cullFace_;
		static ScissorState scissor_;
		static Recti viewport_;
		static Colorf clearColor_;

		static GxmShaderProgram* currentProgram_;
		static const GxmTexture* boundTextures_[MaxTextureUnits];
		static UniformRange boundUniformRanges_[MaxUniformBindings];
		static GxmRenderTarget* currentRenderTarget_;

		// -- Real sceGxm objects (owned) --

		static SceGxmContext* context_;
		static SceGxmShaderPatcher* shaderPatcher_;
		// One render target covers both the intermediate screen surface and the display buffers: they share
		// the panel's dimensions, and a sceGxmRenderTarget only describes the tiling of a size, not a surface
		static SceGxmRenderTarget* displayRenderTarget_;

		static GxmMemory::Block contextHostMem_;
		static GxmMemory::Block vdmRingBuffer_;
		static GxmMemory::Block vertexRingBuffer_;
		static GxmMemory::Block fragmentRingBuffer_;
		static GxmMemory::Block fragmentUsseRingBuffer_;
		static GxmMemory::Block patcherBufferMem_;
		static GxmMemory::Block patcherVertexUsseMem_;
		static GxmMemory::Block patcherFragmentUsseMem_;

		static GxmMemory::Block displayBuffers_[DisplayBufferCount];
		static SceGxmColorSurface displaySurfaces_[DisplayBufferCount];
		static SceGxmSyncObject* displaySyncObjects_[DisplayBufferCount];
		static std::uint32_t backBufferIndex_;
		static std::uint32_t frontBufferIndex_;

		// The intermediate surface every draw that is not aimed at a render target lands in, kept bottom-up
		// like OpenGL and flipped into the display buffer at present time (see the class documentation)
		static GxmMemory::Block screenBuffer_;
		static SceGxmColorSurface screenSurface_;
		static SceGxmTexture screenTexture_;
		// Serializes the frame's writes to the screen surface against the present blit that samples them
		static SceGxmSyncObject* screenSyncObject_;

		static GxmMemory::Block depthBuffer_;
		static SceGxmDepthStencilSurface depthSurface_;

		static bool initialized_;
		static bool vsync_;
		static bool sceneOpen_;
		// Texels the open scene renders into, which is what identifies it: a target change that lands on the
		// same surface goes on adding to this scene rather than starting one that would discard it (see
		// EnsureScene())
		static void* sceneSurfaceData_;
		static std::uint32_t sceneCounter_;
		// State that has to be re-applied after a scene begins (sceGxmBeginScene resets the pipeline state)
		static bool sceneStateApplied_;

		// Dimensions of the surface the open scene renders into, recorded when it begins. The viewport and
		// region clip are derived from them, and reading them back from the render target instead would mean
		// re-entering GxmRenderTarget::GetSceneTarget() - which is allowed to end the very scene being set up
		static std::int32_t sceneWidth_;
		static std::int32_t sceneHeight_;

		// -- Built-in shaders --

		// Clear: a full-screen quad in clip space filled with a uniform colour, the tile-based renderer's
		// equivalent of glClear (see the class documentation)
		static SceGxmShaderPatcherId clearVertexId_;
		static SceGxmShaderPatcherId clearFragmentId_;
		static SceGxmVertexProgram* clearVertexProgram_;
		static SceGxmFragmentProgram* clearFragmentProgram_;
		// Next slot of the clear quad ring, so concurrent clears in one frame keep their own colours
		static std::uint32_t clearQuadIndex_;
		static GxmMemory::Block clearVertices_;

		// Present: the screen surface sampled with a flipped V into the display buffer
		static SceGxmShaderPatcherId presentVertexId_;
		static SceGxmShaderPatcherId presentFragmentId_;
		static SceGxmVertexProgram* presentVertexProgram_;
		static SceGxmFragmentProgram* presentFragmentProgram_;
		static GxmMemory::Block presentVertices_;

		// Shared 0, 1, 2, ... index buffer standing in for the missing non-indexed draw. Grown on demand by
		// the largest DrawArrays() the frame issues
		static GxmMemory::Block sequentialIndices_;
		static std::uint32_t sequentialIndexCount_;
		// The same trick for the missing line-strip primitive: pairs (0,1), (1,2), (2,3), ... so a window of
		// this buffer turns a strip of N vertices into the N-1 independent line segments it means
		static GxmMemory::Block lineStripIndices_;
		static std::uint32_t lineStripVertexCount_;

		// The two static streams feeding the vertex-ID-free sprite shaders (see GetQuadCornerStream())
		static GxmMemory::Block quadCornerStream_;
		static GxmMemory::Block batchedCornerStream_;

		/** @brief Opens a scene on the current target if none is open, and (re)applies the pipeline state it reset */
		static bool EnsureScene();
		/** @brief Programs the viewport and region clip of the current target from the tracked engine state */
		static void ApplyViewportAndScissor();
		/**
			@brief Programs the depth function and depth write mask for **both** facings

			sceGxm keeps the depth (and stencil, and fragment-program-enable) state separately per facing,
			while OpenGL - whose semantics this backend reproduces - has no per-face depth state at all. The
			engine leaves face culling off, so both facings rasterize, and which one a quad lands on is not
			something the pipeline controls: it follows from the winding the geometry happens to have after the
			bottom-up viewport transform, and the two sprite layouts (a 4-vertex strip, six vertices per
			batched sprite) do not even agree on it.

			Programming only the front face therefore leaves half the geometry on sceGxm's defaults -
			`LESS_EQUAL` with depth writes *enabled* - which is a silent, order-dependent depth test the engine
			never asked for. With everything drawn a few layer steps apart inside a 2D scene, that rejects
			whatever loses the comparison by a rounding error: individual glyphs of a string vanishing and
			reappearing between frames, a lighting pass that flickers between correct, black and blown out, and
			half of a full-screen quad (one triangle of the pair) missing along the diagonal.
		*/
		static void SetDepthStateBothFaces(SceGxmDepthFunc func, SceGxmDepthWriteMode write);
		/** @brief Enables or disables the fragment stage for **both** facings (see @ref SetDepthStateBothFaces()) */
		static void SetFragmentProgramEnabledBothFaces(SceGxmFragmentProgramMode mode);
		/** @brief Returns the color surface, depth surface and dimensions the current target renders into */
		static void GetCurrentTarget(SceGxmRenderTarget*& renderTarget, SceGxmColorSurface*& colorSurface,
			SceGxmDepthStencilSurface*& depthSurface, SceGxmSyncObject*& syncObject, std::int32_t& width, std::int32_t& height);
		// Written by the GPU when a scene's fragment phase completes, so the next scene can be held off until
		// the surface it may sample is really there (see FinishScene())
		static SceGxmNotification sceneNotification_;

		/** @brief Blocks displaced by a grown buffer, freed once the frame that may still read them is done */
		static constexpr std::uint32_t RetiredBlockCount = 8;
		static GxmMemory::Block retiredBlocks_[RetiredBlockCount];
		/** @brief Holds @p block until the end of the frame instead of releasing it now, and invalidates it */
		static void RetireBlock(GxmMemory::Block& block);
		/** @brief Frees every retired block (called after the frame's barrier) */
		static void ReleaseRetiredBlocks();

		/** @brief Returns `true` if @ref sequentialIndices_ covers @p count indices, growing it if it does not */
		static bool EnsureSequentialIndices(std::uint32_t count);
		/** @brief Returns `true` if @ref lineStripIndices_ covers a strip of @p vertexCount vertices, growing it if it does not */
		static bool EnsureLineStripIndices(std::uint32_t vertexCount);
		/** @brief Compiles a built-in Cg shader pair and registers it with the patcher */
		static bool CreateBuiltinShaders();
		/** @brief Writes a stage's loose uniforms and bound uniform-block ranges into its default uniform buffer */
		static void UploadUniforms(void* uniformBuffer, GxmShaderProgram* program,
			const SmallVector<GxmUniformSlot, 0>& slots, const SmallVector<GxmShaderProgram::GxmBlockUpload, 0>& blocks);
		/** @brief Points a stage's uniform-buffer containers at the ranges the pipeline bound (no copy) */
		static void BindUniformBufferContainers(bool vertexStage, const SmallVector<GxmShaderProgram::GxmBlockUpload, 0>& blocks);
		/** @brief Shared body of the draw calls: binds the program, its resources and state, then issues the draw */
		static void DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
			bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex);
		/** @brief Draws the full-screen quad of a built-in shader pair into the current scene */
		static void DrawFullscreenQuad(SceGxmVertexProgram* vertexProgram, SceGxmFragmentProgram* fragmentProgram,
			const void* vertices, const Colorf* color, const SceGxmTexture* texture);
	};
}
