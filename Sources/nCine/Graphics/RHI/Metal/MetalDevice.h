#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>

// metal-cpp (Metal.hpp, Foundation.hpp, QuartzCore.hpp) is pulled in only by the backend's own .cpp files -
// every Metal object (MTL::Device, MTL::CommandQueue, MTL::Texture, ...) is kept as a file-static there or as
// an opaque pointer in the contract classes, so this header (transitively included across the whole pipeline
// through Rhi.h) stays free of metal-cpp and does not force its include directory onto unrelated
// translation units.

namespace nCine::RHI::Metal
{
	class MetalShaderProgram;
	class MetalRenderTarget;
	class MetalTexture;

	/**
		@brief Pipeline-state and draw-call facade of the Metal backend (aliased as `RHI::Device`)

		Exposes the OpenGL device's surface (blending, depth, cull, scissor, viewport, clear and the draw
		calls) so the backend-neutral render pipeline drives it unchanged.

		The device also owns the real `MTL::Device`, `MTL::CommandQueue` and the `CAMetalLayer` SDL attaches to
		the window (via @ref CreateSwapchain(), called by the SDL window backend). Every frame is one
		`MTL::CommandBuffer`; draws are recorded into a render command encoder per render target, and
		@ref PresentFrame() acquires a drawable, draws the rendered screen texture into it and commits (the
		buffer-swap equivalent).

		The scene is rendered GL-style: the offline MSL negates clip-space Y in every vertex shader so a
		render target's memory keeps the GL row order (row 0 = the GL bottom row), which is what lets
		CPU-uploaded and rendered textures agree exactly as on the OpenGL and Vulkan backends; the one
		scan-out correction is the present pass, whose fullscreen triangle maps the drawable's top row onto the
		screen texture's last row. GL viewports and scissors therefore map to Metal's with no translation, and
		the front-face winding is set to clockwise so GL's counter-clockwise front faces survive the flip.
	*/
	class MetalDevice
	{
	public:
		MetalDevice() = delete;
		~MetalDevice() = delete;

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
		static void BindProgram(MetalShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static MetalShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const MetalTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~MetalTexture to avoid a dangling pointer) */
		static void UnbindTexture(const MetalTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const MetalTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Clears a render target from the device if it is the current one (called from ~MetalRenderTarget) */
		static void UnbindRenderTarget(const MetalRenderTarget* renderTarget);
		/** @brief Records the current draw render target (its color attachments receive the pixels) */
		static void SetRenderTarget(MetalRenderTarget* renderTarget);
		/** @brief Ends the open render pass if it draws into @p renderTarget, whose attachments just changed */
		static void OnRenderTargetChanged(const MetalRenderTarget* renderTarget);
		/** @brief Drops any cached render pipelines keyed on a shader program being destroyed/reset */
		static void OnShaderProgramDestroyed(MetalShaderProgram* program);

		// Internal accessors used by the backend's draw path (no Metal types, so callable from the .cpp's
		// anonymous-namespace draw helpers without exposing the private static state)
		/** @brief Returns the currently bound draw render target (nullptr = the screen) */
		static MetalRenderTarget* currentRenderTargetInternal();
		/** @brief Number of uniform binding slots tracked by @ref BindUniformRange() */
		static std::uint32_t MaxUniformBindingsPublic();
		/** @brief Returns the host data range bound to a uniform binding slot */
		static void GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size);

		// -- Metal device / layer lifecycle (called by the window backend) --

		/**
			@brief Creates the Metal device, command queue and the window's layer

			@param windowHandle  The `SDL_Window*` (created with `SDL_WINDOW_METAL`), passed as a `void*` so the
			                     window backend does not need the Metal headers. The backend attaches a Metal view
			                     to it through the SDL Metal API and configures the view's `CAMetalLayer`.
			@param width         Drawable width in pixels
			@param height        Drawable height in pixels
			@param vsync         Whether @ref PresentFrame() presents with vertical sync (`displaySyncEnabled`)
			@returns `true` if the device and the layer were created
		*/
		static bool CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync);
		/** @brief Releases the layer, the screen texture and the device */
		static void DestroySwapchain();
		/** @brief Resizes the layer's drawables and the screen texture to the new drawable size (waits for the GPU to go idle first) */
		static void ResizeSwapchain(std::int32_t width, std::int32_t height);
		/** @brief Acquires a drawable, draws the rendered screen texture into it, presents it and commits the frame (the buffer-swap equivalent) */
		static void PresentFrame();
		/** @brief No-op (a command buffer records its own GPU times, which @ref PresentFrame() reports once it completes) */
		static inline void BeginGpuTiming() {}
		/** @brief No-op */
		static inline void EndGpuTiming() {}

		/**
			@brief Creates an additional Metal view and layer for a secondary window

			Used by the ImGui multi-viewport support for the windows it spawns when a panel is dragged out of the
			main one. Only their presentation lives in the backend: the contents are rendered through the ordinary
			RHI path into an off-screen render target, whose texture is handed over for the frame with
			@ref QueueSecondaryPresent(). @ref PresentFrame() then draws each queued texture into its window's
			drawable and presents it with the frame's single commit.

			@param windowHandle  `SDL_Window*` of the secondary window, passed as a `void*`
			@param width         Drawable width in pixels
			@param height        Drawable height in pixels
			@returns An opaque handle to pass to the other `*Secondary*` functions, or `nullptr` if the view failed
		*/
		static void* CreateSecondarySwapchain(void* windowHandle, std::int32_t width, std::int32_t height);
		/** @brief Releases a secondary view and layer created by @ref CreateSecondarySwapchain() */
		static void DestroySecondarySwapchain(void* handle);
		/** @brief Resizes a secondary layer's drawables */
		static void ResizeSecondarySwapchain(void* handle, std::int32_t width, std::int32_t height);
		/** @brief Hands the texture holding a secondary window's contents over to the next @ref PresentFrame() */
		static void QueueSecondaryPresent(void* handle, const MetalTexture* source);

		// -- Device limits (consumed by MetalRhiCapabilities to publish the backend's values) --

		/** @brief Returns the largest 2D texture dimension (16384 on every Mac GPU family) */
		static std::int32_t GetMaxTextureDimension();
		/** @brief Returns the constant-buffer bind-offset alignment the uniform ring honors (256 bytes on macOS) */
		static std::int32_t GetUniformBufferOffsetAlignment();
		/** @brief Returns the uniform range the batch size is derived from (64 KB, the budget the offline MSL baked its BATCH_SIZE from) */
		static std::int32_t GetMaxUniformBufferRange();

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;

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

		static MetalShaderProgram* _currentProgram;
		static const MetalTexture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static MetalRenderTarget* _currentRenderTarget;
	};
}
