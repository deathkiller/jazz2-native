#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>

// The Vulkan headers (vulkan/vulkan.h) and the loader are pulled in only by VulkanDevice.cpp — every Vulkan
// handle (VkInstance, VkDevice, VkSwapchainKHR, ...) is kept as a file-static there, so this contract header
// (transitively included across the whole pipeline through Rhi.h) stays free of the Vulkan headers and does
// not force the Vulkan-Headers include directory onto unrelated translation units.

namespace nCine::RhiVulkan
{
	class VulkanShaderProgram;
	class VulkanRenderTarget;
	class VulkanTexture;

	/**
		@brief Pipeline-state and draw-call facade of the Vulkan backend (aliased as `Rhi::Device`)

		Exposes the OpenGL device's surface (blending, depth, cull, scissor, viewport, clear and the draw
		calls) so the backend-neutral render pipeline drives it unchanged. For slice 2a the pipeline-state
		setters record their values and the draw calls are no-ops (no SPIR-V pipelines / descriptor sets are
		built yet — that is slice 2b).

		The device also owns the real `VkInstance`, `VkDevice`, graphics/present `VkQueue` and the
		`VkSwapchainKHR` created from the SDL window (via @ref CreateSwapchain(), called by the SDL window
		backend). @ref PresentFrame() acquires a swap-chain image, clears it to the slice 2a marker color with
		`vkCmdClearColorImage` and presents it — the slice 2a milestone.
	*/
	class VulkanDevice
	{
	public:
		VulkanDevice() = delete;
		~VulkanDevice() = delete;

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
		static void DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex);

		static FenceHandle InsertFence();
		static void DeleteFence(FenceHandle& fence);
		static bool ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs);

		static void SetupInitialState();

		// -- Backend extensions (called by the resource types) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(VulkanShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static VulkanShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const VulkanTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~VulkanTexture to avoid a dangling pointer) */
		static void UnbindTexture(const VulkanTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const VulkanTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Clears a render target from the device if it is the current one (called from ~VulkanRenderTarget) */
		static void UnbindRenderTarget(const VulkanRenderTarget* renderTarget);
		/** @brief Records the current draw render target (its color attachment 0 receives the pixels) */
		static void SetRenderTarget(VulkanRenderTarget* renderTarget);
		/** @brief Drops any cached graphics pipelines keyed on a shader program being destroyed/reset */
		static void OnShaderProgramDestroyed(VulkanShaderProgram* program);

		// Internal accessors used by the backend's draw path (no Vulkan types, so callable from the .cpp's
		// anonymous-namespace draw helpers without exposing the private static state)
		/** @brief Returns the currently bound draw render target (nullptr = the screen) */
		static VulkanRenderTarget* currentRenderTargetInternal();
		/** @brief Number of uniform binding slots tracked by @ref BindUniformRange() */
		static std::uint32_t MaxUniformBindingsPublic();
		/** @brief Returns the host data range bound to a uniform binding slot */
		static void GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size);

		// -- Vulkan device / swap-chain lifecycle (called by the window backend) --

		/**
			@brief Creates the Vulkan instance, device and swap chain for the given SDL window

			@param windowHandle  The `SDL_Window*` (created with `SDL_WINDOW_VULKAN`), passed as a `void*` so the
			                     window backend does not need the Vulkan headers. The backend queries the required
			                     instance extensions and the presentation surface from it through the SDL Vulkan API.
			@param width         Swap-chain image width in pixels
			@param height        Swap-chain image height in pixels
			@param vsync         Whether @ref PresentFrame() presents with vertical sync (FIFO vs. MAILBOX/IMMEDIATE)
			@returns `true` if the instance, device and swap chain were created
		*/
		static bool CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync);
		/** @brief Releases the swap chain, device and instance */
		static void DestroySwapchain();
		/** @brief Recreates the swap chain at the new drawable size (waits for the device to go idle first) */
		static void ResizeSwapchain(std::int32_t width, std::int32_t height);
		/** @brief Acquires a swap-chain image, clears it to the slice 2a marker color and presents it (the buffer-swap equivalent) */
		static void PresentFrame();

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;

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

		static VulkanShaderProgram* currentProgram_;
		static const VulkanTexture* boundTextures_[MaxTextureUnits];
		static UniformRange boundUniformRanges_[MaxUniformBindings];
		static VulkanRenderTarget* currentRenderTarget_;

		// Loader bootstrap (resolves the entry points declared in VulkanCommon.h). Void signatures, so these
		// stay declarable in this vulkan.h-free contract header.
		static void LoadGlobalFunctions();
		static void LoadInstanceFunctions();
		static void LoadDeviceFunctions();
	};
}
