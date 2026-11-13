#pragma once

#include "Gx2Memory.h"
#include "Gx2ShaderProgram.h"
#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>

#include <gx2/context.h>
#include <coreinit/time.h>
#include <gx2/surface.h>

namespace nCine::RHI::GX2
{
	class Gx2ShaderProgram;
	class Gx2RenderTarget;
	class Gx2Texture;

	/**
		@brief Pipeline-state and draw-call facade of the GX2 backend (aliased as `RHI::Device`)

		Exposes the OpenGL device's surface (blending, depth, cull, scissor, viewport, clear and the draw
		calls) so the backend-neutral render pipeline drives it unchanged, and owns the backend's side of the
		GX2 session: its context state, the intermediate screen colour buffer and the present.

		<b>GX2 itself belongs to SDL.</b> wiiu-sdl2's video driver calls `GX2Init()`, drives ProcUI, owns the
		TV and GamePad scan buffers and frees its MEM1 heap whenever the application goes to the background.
		The window backend keeps SDL's video subsystem and its window (for input and the ProcUI events) but
		creates no `SDL_Renderer`, so nothing else submits GX2 work. This device sets up its own
		`GX2ContextState` with shadowing enabled and renders with that.

		<b>"Screen" is an intermediate colour buffer</b> the size of the window (the drawable size, see
		@ref GetScreenWidth()). @ref PresentFrame() copies it into the TV and GamePad scan buffers with
		`GX2CopyColorBufferToScanBuffer()`, which also scales it to each display's own resolution.

		<b>Background.</b> Between @ref EnterBackground() and @ref EnterForeground() (the
		`SDL_APP_WILLENTERBACKGROUND` / `SDL_APP_DIDENTERFOREGROUND` events) nothing is submitted: clears,
		draws and presents return immediately, because SDL is about to release the foreground and its scan
		buffers and destroy its MEM1 heap. Before that, every block of this backend in MEM1 (the screen buffer and
		the render targets that asked for it, see @ref Gx2Memory) is freed; when the application comes back the
		screen buffer is allocated again, the render targets on their next use, and the context state is made
		current again. Everything in MEM2 survives.

		<b>Draws.</b> A draw binds the program's vertex, pixel and fetch shaders, fills the stages'
		loose-uniform blocks and copies the ranges the pipeline bound for the explicit blocks into a uniform
		ring (@ref AllocateUniformData()), binds the textures to the units the pixel shader samples and one
		attribute buffer per engine attribute slot.

		<b>Uniform blocks are little endian.</b> The GPU byte-swaps vertex attributes (`GX2_ENDIAN_SWAP_DEFAULT`)
		but reads uniform blocks as they are, so every block is copied into the ring with its 32-bit words
		swapped. Writing them in place in the engine's buffers is not safe, because a range may be flushed
		more than once.

		<b>The CPU runs ahead of the GPU.</b> GX2 does not rename buffers, so nothing the GPU may still read is
		written by the CPU before the GPU is done with it. The device numbers the command batches it submits
		(@ref GetUseToken()): the batch being recorded is flushed with `GX2Flush()` at the present (or when
		something has to wait for it), and the GX2 timestamp of that submission (`GX2GetLastSubmittedTimeStamp()`)
		is kept per batch, so a token is retired once `GX2GetRetiredTimeStamp()` has reached it. On top of that:
		- the streaming vertex and index buffers are RenderBuffersManager's persistently mapped section ring
		  (three sections), whose fences are such tokens (@ref InsertFence(), @ref ClientWaitFence());
		- the uniform ring is cut into chunks, each remembering the batch that last read it, and a chunk is
		  waited for before it is reused (@ref AllocateUniformData());
		- every texture and vertex/index buffer a draw reads is stamped with the token of that draw, and a CPU
		  write to one that is still in use either moves it to a fresh block (when the whole store is replaced:
		  the old block is retired) or waits for the token;
		- blocks of destroyed or reallocated resources are freed only when their token has retired
		  (@ref RetireBlock()).
		The present submits the frame's draws first and then waits for the previous swap to flip before it
		records the scan copies (@ref WaitForPreviousFlip()): the GPU draws one frame while the CPU records the
		next, and the CPU stays at most about a frame ahead of the scan-out.
	*/
	class Gx2Device
	{
	public:
		Gx2Device() = delete;
		~Gx2Device() = delete;

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

		/** @brief Returns a fence for everything recorded so far: the token of the open batch (see @ref GetUseToken()) */
		static FenceHandle InsertFence();
		static void DeleteFence(FenceHandle& fence);
		static bool ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs);

		static void SetupInitialState();

		// -- Backend extensions (called by the resource types) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(Gx2ShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static Gx2ShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const Gx2Texture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~Gx2Texture to avoid a dangling pointer) */
		static void UnbindTexture(const Gx2Texture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const Gx2Texture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Returns the host data range bound to a uniform binding point (read by the draw path) */
		static void GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size);
		/** @brief Clears a render target from the device if it is the current one (called from ~Gx2RenderTarget) */
		static void UnbindRenderTarget(const Gx2RenderTarget* renderTarget);
		/** @brief Records the current draw render target (its bound colour attachment receives the pixels) */
		static void SetRenderTarget(Gx2RenderTarget* renderTarget);
		/** @brief Clears a destroyed program from the device's current-program tracking (called from ~Gx2ShaderProgram) */
		static void OnProgramDestroyed(const Gx2ShaderProgram* program);
		/** @brief Makes the next draw with @p program bind its shaders again, because its binaries were released */
		static void OnProgramBinariesReleased(const Gx2ShaderProgram* program);

		/** @brief Blocks until the GPU has executed everything submitted so far (`GX2DrawDone()`) */
		static void Finish();
		/**
			@brief Returns the token of the command batch being recorded
			A resource stores it when a draw reads it; the token retires once the GPU has executed the batch.
			Tokens are never 0, so 0 means "never used".
		*/
		static std::uint32_t GetUseToken();
		/** @brief Whether the GPU has finished the batch of @p token (without waiting; an unsubmitted batch is not) */
		static bool IsUseRetired(std::uint32_t token);
		/** @brief Waits until the GPU has finished the batch of @p token, submitting it first if it is still open */
		static bool WaitForUse(std::uint32_t token);

		/**
			@brief Holds @p block until the frames that may still reference it are done, then frees it

			The GPU runs behind the CPU, so memory freed the moment a resource is destroyed may still be read
			by commands already queued. A retired block is freed once the batch that was open when it was retired
			has retired (checked at every present).
		*/
		static void RetireBlock(Gx2Memory::Block& block);
		/** @brief Whether MEM1 may be allocated right now: only in the foreground, while SDL's MEM1 heap exists (see @ref Gx2Memory) */
		static inline bool CanAllocMem1() {
			return CanSubmit();
		}

		/** @brief Monotonic count of presented frames */
		static std::uint32_t GetFrameCounter();

		/**
			@brief Largest batch a batched sprite shader can address

			GX2 uniform blocks are limited to 64 KB (`GetMaxUniformBlockSize()`), and the sprite instance
			stride is 112 bytes, so 585 instances fit; the generated batched shaders are compiled with the same
			`BATCH_SIZE`.
		*/
		static constexpr std::uint32_t MaxBatchSize = 585;

		// -- GX2 session lifecycle (called by the window backend) --

		/**
			@brief Sets up the backend's GX2 context state and the screen colour buffer

			SDL's video driver must already be initialised (it owns `GX2Init()` and the scan buffers).

			@param windowHandle Ignored, there is one TV and one GamePad
			@param width        Width of the screen colour buffer (the window's)
			@param height       Height of the screen colour buffer
			@param vsync        Whether the present waits for vertical blank
		*/
		static bool CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync);
		/** @brief Waits for the GPU, then releases everything @ref CreateSwapchain() created */
		static void DestroySwapchain();
		/** @brief No-op: the screen colour buffer keeps the window size, which does not change on the console */
		static void ResizeSwapchain(std::int32_t width, std::int32_t height);
		/** @brief Copies the screen colour buffer to the TV and GamePad scan buffers and queues the swap */
		static void PresentFrame();
		/** @brief No-op */
		static inline void BeginGpuTiming() {}
		/** @brief No-op */
		static inline void EndGpuTiming() {}

		/** @brief Stops all GPU submission (`SDL_APP_WILLENTERBACKGROUND`), after letting the GPU finish */
		static void EnterBackground();
		/** @brief Resumes GPU submission (`SDL_APP_DIDENTERFOREGROUND`) */
		static void EnterForeground();

		/** @brief Width of the screen colour buffer, which is the drawable size the window backend reports */
		static std::int32_t GetScreenWidth();
		/** @brief Height of the screen colour buffer */
		static std::int32_t GetScreenHeight();
		/** @brief Same as @ref GetScreenWidth(); the TV resolution itself is SDL's business */
		static std::int32_t GetDisplayWidth();
		/** @brief Same as @ref GetScreenHeight() */
		static std::int32_t GetDisplayHeight();

		/** @brief Largest supported 2D texture dimension */
		static constexpr std::int32_t GetMaxTextureDimension() {
			return 8192;
		}
		/** @brief Byte alignment the engine suballocates bound uniform ranges with (`GX2_UNIFORM_BLOCK_ALIGNMENT`) */
		static constexpr std::int32_t GetUniformBufferOffsetAlignment() {
			return 256;
		}
		/** @brief Byte budget of one uniform block */
		static constexpr std::int32_t GetMaxUniformBlockSize() {
			return 65536;
		}

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;
		/** @brief Blocks retired by a destroyed resource, released once the GPU is past the frame that used them */
		static constexpr std::uint32_t RetiredBlockCount = 64;
		/** @brief Number of submitted batches whose GX2 timestamps are remembered */
		static constexpr std::uint32_t BatchStampCount = 16;

		struct UniformRange
		{
			const std::uint8_t* Data = nullptr;
			std::uint32_t Size = 0;
		};

		/** @brief A retired block together with the frame it stopped being referenced in */
		struct RetiredBlock
		{
			Gx2Memory::Block Block;
			std::uint32_t Token = 0;
		};

		static BlendingState _blending;
		static DepthTestState _depthTest;
		static CullFaceState _cullFace;
		static ScissorState _scissor;
		static Recti _viewport;
		static Colorf _clearColor;

		static Gx2ShaderProgram* _currentProgram;
		static const Gx2Texture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static Gx2RenderTarget* _currentRenderTarget;

		static GX2ContextState* _contextState;
		static GX2ColorBuffer _screenBuffer;
		static Gx2Memory::Block _screenMemory;
		static std::int32_t _screenWidth;
		static std::int32_t _screenHeight;

		static bool _initialized;
		static bool _inForeground;
		static bool _vsync;
		static bool _firstFrame;
		/** @brief Token of the batch that carried the last `GX2SwapScanBuffers()`, 0 if none */
		static std::uint32_t _swapToken;
		static std::uint32_t _frameCounter;
		/** @brief Set whenever the colour buffer, viewport or scissor may have changed since the last draw */
		static bool _targetDirty;
		/** @brief What was last programmed on the GPU, so a draw re-sends only what changed (the pipeline sets the
			viewport before every draw); `AppliedColorBuffer == nullptr` means unknown, after the state was reset */
		struct AppliedTarget {
			const GX2ColorBuffer* ColorBuffer;
			const void* Image;
			std::int32_t Width, Height;
			Recti Viewport;
			Recti Scissor;
		};
		static AppliedTarget _applied;

		static RetiredBlock _retiredBlocks[RetiredBlockCount];

		/** @brief Size of the ring the loose-uniform blocks of a frame's draws are taken from */
		static constexpr std::uint32_t UniformRingSize = 4 * 1024 * 1024;
		/** @brief Number of chunks the uniform ring is cut into; a chunk is the unit that is waited for before reuse */
		static constexpr std::uint32_t UniformRingChunkCount = 8;
		static constexpr std::uint32_t UniformRingChunkSize = UniformRingSize / UniformRingChunkCount;
		static Gx2Memory::Block _uniformRing;
		/** @brief Chunk the uniform ring allocates from and the offset within it */
		static std::uint32_t _uniformChunk;
		static std::uint32_t _uniformChunkOffset;
		/** @brief Per chunk, the token of the last batch that may read it (0 if none) */
		static std::uint32_t _uniformChunkTokens[UniformRingChunkCount];
		/** @brief Token of the batch being recorded */
		static std::uint32_t _openBatch;
		/** @brief GX2 timestamp of each submitted batch, indexed by token modulo @ref BatchStampCount */
		static OSTime _batchStamps[BatchStampCount];
		/** @brief Program whose shaders are currently set on the GPU */
		static const Gx2ShaderProgram* _lastProgram;

		/** @brief Allocates the screen colour buffer's image for `_screenBuffer.surface`, in MEM1 if it fits */
		static bool AllocScreenMemory();
		/** @brief Whether GX2 work may be submitted right now */
		static inline bool CanSubmit() {
			return (_initialized && _inForeground);
		}
		/**
			@brief Returns the colour buffer of the current target (the screen if none is bound) and its size

			Returns `nullptr` if the bound target has no colour buffer (its texture could not get GPU storage),
			so a clear or draw meant for it doesn't land on the screen.
		*/
		static const GX2ColorBuffer* GetCurrentColorBuffer(std::int32_t& width, std::int32_t& height);
		/**
			@brief Binds the current target's colour buffer and programs viewport and scissor, if they changed

			Returns `false` if the current target has no colour buffer; it stays pending, so every draw
			checks it again.
		*/
		static bool ApplyTarget();
		/** @brief Programs the viewport and scissor of the current target from the tracked engine state */
		static void ApplyViewportAndScissor(std::int32_t targetWidth, std::int32_t targetHeight);
		/** @brief Makes the backend's context state current again after GX2 clobbered the pipeline state */
		static void RestoreContextState();
		/** @brief Forgets what was programmed, so the next draw sets the colour buffer, viewport and scissor again */
		static inline void InvalidateTarget() {
			_targetDirty = true;
			_applied.ColorBuffer = nullptr;
		}
		/** @brief Programs blending from the tracked state */
		static void ApplyBlending();
		/** @brief Programs culling from the tracked state */
		static void ApplyCulling();
		/**
			@brief Frees the retired blocks the GPU can no longer be reading

			@param force Release everything regardless of age; only correct after a @ref Finish()
		*/
		static void ReleaseRetiredBlocks(bool force = false);
		/** @brief Submits the open batch (`GX2Flush()`), remembers its timestamp and opens the next one */
		static void FlushBatch();
		/** @brief Returns the GX2 timestamp that covers the submitted batch of @p token */
		static OSTime GetBatchStamp(std::uint32_t token);
		/** @brief Waits until the swap queued by the previous present has flipped, so its scan buffer is free again */
		static void WaitForPreviousFlip();
		/**
			@brief Returns 256-byte aligned memory for one draw's uniform block

			Taken from the current chunk of the uniform ring. When it is full, the ring moves on to the next
			chunk after waiting for the batch that last read that one, which has normally retired long ago.
			Returns `nullptr` if that wait times out (a GPU hang), leaving the ring where it was.
		*/
		static std::uint8_t* AllocateUniformData(std::uint32_t size);
		/** @brief Uploads a stage's loose uniforms and binds its explicit uniform blocks, returns `false` if a block could not be allocated */
		static bool ApplyStageUniforms(const Gx2ShaderProgram& program, const Gx2ShaderProgram::StageBindings& bindings, bool vertexStage);
		/**
			@brief Binds one attribute buffer per enabled attribute of the program's vertex format

			Returns `false` if an enabled attribute has no usable buffer, so the draw doesn't read
			the stream an earlier draw left in that slot.
		*/
		static bool ApplyAttributeBuffers(Gx2ShaderProgram& program);
		/** @brief Common draw path; skips the draw while the bound program has no binaries */
		static void DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
			bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex);
	};
}
