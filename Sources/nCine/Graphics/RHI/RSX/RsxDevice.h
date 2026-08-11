#pragma once

#include "RsxVram.h"
#include "RsxShaderProgram.h"
#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>

#include <Containers/SmallVector.h>

#include <rsx/gcm_sys.h>

using namespace Death::Containers;

namespace nCine::RHI::RSX
{
	class RsxShaderProgram;
	class RsxRenderTarget;
	class RsxTexture;

	/**
		@brief Pipeline-state and draw-call facade of the RSX backend (aliased as `RHI::Device`)

		Exposes the OpenGL device's surface (blending, depth, cull, scissor, viewport, clear and the draw
		calls) so the backend-neutral render pipeline drives it unchanged, and owns the whole RSX session
		behind it: the command FIFO, the negotiated video mode, the display buffers and the shared depth
		buffer.

		Three properties of the hardware shape everything below.

		**The RSX is immediate-mode, which is what makes this simpler than the sceGxm backend.** There is no
		scene to begin and end and no tile buffer whose contents start undefined: a render target is a set of
		addresses in a state register, `rsxSetSurface()` changes it, and `rsxClearSurface()` is a real
		hardware clear rather than a full-screen quad standing in for one. A target can be re-entered
		mid-frame without discarding what an earlier pass drew, so the pipeline's viewport chain needs no
		special handling at all.

		**The output resolution is negotiated with the display.** Unlike every other console backend here,
		the panel is not fixed: @ref CreateSwapchain() walks a preference list through
		`videoGetResolutionAvailability()` and takes the first mode the attached display accepts, so
		@ref GetDisplayWidth() and @ref GetDisplayHeight() are only known after the session is up. The
		logical (game) resolution remains a render-target size driven separately by the render pipeline.

		**The display scans out top-down.** The engine renders in the OpenGL convention, and this backend
		replays it faithfully: every viewport is programmed so clip -Y lands on row 0 and every surface - the
		screen and each off-screen render target - is stored bottom-up exactly like OpenGL. That is what
		keeps a texture's V axis and its viewport's Y axis pointing the same way, so an off-screen round trip
		does not flip the image (the property that makes the scene composite and the directly drawn HUD
		agree). The scan-out then needs the one correction that convention implies: "screen" (no render
		target bound) is an intermediate surface, and @ref PresentFrame() flips it into the display buffer
		with a built-in shader - the same single OpenGL-to-native flip the sceGxm and Direct3D 11 backends
		do, which also scales the logical resolution up to the panel for free.
	*/
	class RsxDevice
	{
	public:
		RsxDevice() = delete;
		~RsxDevice() = delete;

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
		static void BindProgram(RsxShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static RsxShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const RsxTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~RsxTexture to avoid a dangling pointer) */
		static void UnbindTexture(const RsxTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const RsxTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Returns the host data range bound to a uniform binding point (read by the draw path) */
		static void GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size);
		/** @brief Clears a render target from the device if it is the current one (called from ~RsxRenderTarget) */
		static void UnbindRenderTarget(const RsxRenderTarget* renderTarget);
		/** @brief Records the current draw render target (its bound colour attachment receives the pixels) */
		static void SetRenderTarget(RsxRenderTarget* renderTarget);
		/** @brief Clears a destroyed program from the device's current-program tracking (called from ~RsxShaderProgram) */
		static void OnProgramDestroyed(const RsxShaderProgram* program);

		/** @brief Returns the command context, or `nullptr` before @ref CreateSwapchain() */
		static gcmContextData* GetContext();

		/**
			@brief Blocks until the GPU has consumed everything submitted so far

			Used by the two operations that read what the GPU wrote - a render target's readback and the
			teardown - and by nothing on a frame path.
		*/
		static void Finish();

		/**
			@brief Holds @p block until the frame that may still reference it is done, and invalidates it

			The command FIFO runs behind the CPU, so memory freed the moment a resource is destroyed may
			still be read by commands already queued. Retired blocks are released after the next present.
		*/
		static void RetireBlock(RsxVram::Block& block);

		/** @brief Monotonic count of presented frames */
		static std::uint32_t GetFrameCounter();

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
		/** @brief Offset of the quad corner stream, as `rsxBindVertexArrayAttrib()` takes it */
		static std::uint32_t GetQuadCornerStreamOffset();
		/** @brief Offset of the batched corner stream */
		static std::uint32_t GetBatchedCornerStreamOffset();

		/**
			@brief Largest batch the @ref GetBatchedCornerStream() covers

			Far smaller than the sceGxm backend's 682, and for a hardware reason rather than a policy one: a
			batched instance array reaches the vertex program through its constant registers, and the `vp40`
			profile allows a program 544 of them in total. The compiler's own accounting is not a simple
			product of that and the element width - measured against the batched shaders, they stop compiling
			somewhere above 40 instances - so 32 is the value they all accept with margin.
		*/
		static constexpr std::uint32_t MaxBatchSize = 32;

		// -- RSX session lifecycle (called by the window backend) --

		/**
			@brief Brings up the whole RSX session

			@param windowHandle Ignored (the console has one display, so there is no window to attach to);
			                    accepted only to keep the uniform swap-chain contract of the other backends
			@param width        Ignored, the mode is negotiated with the attached display
			@param height       Ignored
			@param vsync        Whether the present waits for vertical blank
			@returns `true` if the FIFO, the video mode, the surfaces and the built-in shaders all came up
		*/
		static bool CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync);
		/** @brief Tears the session down (waits for the GPU, then releases everything in creation order) */
		static void DestroySwapchain();
		/** @brief No-op: the display mode is negotiated once (the logical resolution is a render-target size) */
		static void ResizeSwapchain(std::int32_t width, std::int32_t height);
		/** @brief Flips the intermediate screen surface into the next display buffer and queues it for scan-out */
		static void PresentFrame();

		// The console has no multi-window support, so the ImGui multi-viewport hooks are inert here
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

		/** @brief Width in pixels of the negotiated display mode (0 before @ref CreateSwapchain()) */
		static std::int32_t GetDisplayWidth();
		/** @brief Height in pixels of the negotiated display mode */
		static std::int32_t GetDisplayHeight();

		/**
			@brief Returns the largest supported 2D texture dimension

			4096 is the NV47's limit; it only feeds the tileset chunking in `ContentResolver` and the
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
			@brief Byte budget of one uniform block, which here is the vertex constant-register file

			The RSX gives a vertex program constant registers and nothing else to read uniforms from - there
			is no uniform buffer to bind - and the `vp40` profile allows a program 544 of them. The engine's
			batcher divides the figure published here by the instance stride to decide how large a batch may
			be, so it is set conservatively rather than at the ceiling; @ref MaxBatchSize bounds the result
			again in any case. What matters is only that it never lets the batcher build a batch whose later
			elements the shader cannot address.
		*/
		static constexpr std::int32_t GetMaxUniformBlockSize() {
			return 460 * 16;
		}

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;
		/** @brief Display buffers cycled by the flip; two keeps the GPU and the scan-out off one surface */
		static constexpr std::uint32_t DisplayBufferCount = 2;
		/** @brief Blocks retired by a destroyed resource, released once the GPU is past the frame that used them */
		static constexpr std::uint32_t RetiredBlockCount = 64;
		/**
			@brief Frames a retired block is held for before its memory is handed back

			Presenting only *queues* a frame - the GPU is still executing it, and usually the one before it -
			so a block freed at present time can still be being read. Two frames covers the depth this
			backend keeps in flight (see @ref DisplayBufferCount) with a frame to spare.

			Getting this wrong is not a subtle corruption: a fragment program's microcode is fetched from
			exactly this kind of block, so reissuing its memory to a texture while the GPU is still fetching
			from it makes the hardware execute whatever the new owner wrote as shader instructions.
		*/
		static constexpr std::uint32_t RetiredBlockFrameDelay = 2;

		struct UniformRange
		{
			const std::uint8_t* Data = nullptr;
			std::uint32_t Size = 0;
		};

		static BlendingState _blending;
		static DepthTestState _depthTest;
		static CullFaceState _cullFace;
		static ScissorState _scissor;
		static Recti _viewport;
		static Colorf _clearColor;

		static RsxShaderProgram* _currentProgram;
		static const RsxTexture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static RsxRenderTarget* _currentRenderTarget;

		// -- The RSX session (owned) --

		static gcmContextData* _context;
		/** @brief Host region `rsxInit()` maps into the GPU's IO window, which RsxVram suballocates */
		static void* _hostMemory;

		static std::int32_t _displayWidth;
		static std::int32_t _displayHeight;
		static RsxVram::Block _displayBuffers[DisplayBufferCount];
		static std::uint32_t _displayPitch;
		static std::uint32_t _backBufferIndex;

		// The intermediate surface every draw that is not aimed at a render target lands in, kept bottom-up
		// like OpenGL and flipped into the display buffer at present time (see the class documentation)
		static RsxVram::Block _screenBuffer;
		static std::uint32_t _screenPitch;
		static gcmTexture _screenTexture;

		/** @brief Depth/stencil surface shared by the screen and every render target (see @ref RsxRenderTarget) */
		static RsxVram::Block _depthBuffer;
		static std::uint32_t _depthPitch;

		static bool _initialized;
		static bool _vsync;
		static bool _firstFlip;
		static std::uint32_t _frameCounter;
		/** @brief Set whenever the surface state has to be re-programmed before the next draw */
		static bool _surfaceDirty;
		/** @brief Program whose microcode was last sent through the FIFO, to avoid re-sending it every draw */
		static const rsxVertexProgram* _lastVertexProgram;
		/** @brief Microcode that went with @ref _lastVertexProgram */
		static const void* _lastVertexUcode;
		/** @brief Fragment program last pointed at, which costs a pointer rather than a microcode upload */
		static const rsxFragmentProgram* _lastFragmentProgram;
		/** @brief Local-memory offset that went with @ref _lastFragmentProgram */
		static std::uint32_t _lastFragmentUcodeOffset;
		/** @brief Attribute registers the last draw bound, so the next one can turn off what it does not use */
		static std::uint32_t _boundAttributeRegisters;

		/** @brief Finds a stage attribute by name, tolerating the struct qualifier cgcomp records */
		static const rsxProgramAttrib* FindVertexAttribute(const rsxVertexProgram* program, const char* name);

		/** @brief A retired block together with the frame it stopped being referenced in */
		struct RetiredBlock
		{
			RsxVram::Block Block;
			std::uint32_t RetiredAtFrame = 0;
		};
		static RetiredBlock _retiredBlocks[RetiredBlockCount];

		// -- Built-in shaders --

		// Present: the screen surface sampled with a flipped V into the display buffer
		static const rsxVertexProgram* _presentVertexProgram;
		static const rsxFragmentProgram* _presentFragmentProgram;
		static const void* _presentVertexUcode;
		static RsxVram::Block _presentFragmentUcode;
		static RsxVram::Block _presentVertices;

		// The two static streams feeding the vertex-ID-free sprite shaders (see GetQuadCornerStream())
		static RsxVram::Block _quadCornerStream;
		static RsxVram::Block _batchedCornerStream;

		/** @brief Negotiates a video mode with the attached display and configures the scan-out */
		static bool ConfigureVideo();
		/** @brief Programs the surface (colour + depth) the current target renders into, if it changed */
		static void ApplySurface();
		/** @brief Programs the viewport and scissor of the current target from the tracked engine state */
		static void ApplyViewportAndScissor(std::int32_t targetWidth, std::int32_t targetHeight);
		/** @brief Returns the dimensions of whatever the current target is */
		static void GetCurrentTargetSize(std::int32_t& width, std::int32_t& height);
		/** @brief Creates the static corner streams and the present shader's geometry */
		static bool CreateBuiltinResources();
		/** @brief Resolves the built-in present shader pair from the generated table */
		static bool CreatePresentShader();
		/**
			@brief Frees the retired blocks the GPU can no longer be reading

			@param force Release everything regardless of age; only correct after a @ref Finish(), which is
			             what the teardown and the out-of-slots path in @ref RetireBlock() do
		*/
		static void ReleaseRetiredBlocks(bool force = false);
		/** @brief Uploads a stage's loose uniforms and bound uniform-block ranges into its constants */
		static void UploadUniforms();
		/** @brief Repacks and uploads the current program's batched instance array, if it has one */
		static void UploadInstanceArray();
		/**
			@brief Largest element the instance repacking scratch buffer covers, in constant registers

			The widest instance the engine writes is a mat4 plus four vec4-shaped fields, which cgcomp lays
			out as eight registers; sixteen leaves room for a wider one without putting the buffer on the heap.
		*/
		static constexpr std::uint32_t MaxInstanceRegisters = 16;
		/** @brief Vertex attribute registers the NV47 exposes, which bounds the stale-binding sweep */
		static constexpr std::uint32_t MaxVertexAttributeRegisters = 16;
		/** @brief Binds the current program's vertex attributes from its vertex format */
		static void ApplyVertexFormat(std::int32_t baseVertex);
		/** @brief Binds the current program, its resources and state, then issues the draw */
		static void DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
			bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex);
	};
}
