#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

namespace nCine::RhiGL
{
	/**
		@brief Pipeline-state and draw-call facade of the OpenGL backend

		Exposes everything the render pipeline sets between draws — blending, depth, scissor, viewport,
		clear — plus the draw calls themselves, with backend-neutral signatures (aliased as `Rhi::Device`).
		Delegates to the individual OpenGL state-cache wrappers, so redundant state changes keep being
		filtered exactly as before.
	*/
	class GLDevice
	{
	public:
		GLDevice() = delete;
		~GLDevice() = delete;

		/**
		 * @brief Snapshot of the scissor test state, used to save and restore it around a draw
		 */
		struct ScissorState
		{
			/** @brief Whether the scissor test is enabled */
			bool Enabled = false;
			/** @brief Scissor rectangle */
			Recti Rect = Recti(0, 0, 0, 0);
		};

		/**
		 * @brief Snapshot of the blending state, used to save and restore it around a draw
		 */
		struct BlendingState
		{
			/** @brief Whether blending is enabled */
			bool Enabled = false;
			/** @brief Source RGB blend factor */
			BlendingFactor SrcRgb = BlendingFactor::One;
			/** @brief Destination RGB blend factor */
			BlendingFactor DstRgb = BlendingFactor::Zero;
			/** @brief Source alpha blend factor */
			BlendingFactor SrcAlpha = BlendingFactor::One;
			/** @brief Destination alpha blend factor */
			BlendingFactor DstAlpha = BlendingFactor::Zero;
		};

		/**
		 * @brief Snapshot of the depth test state, used to save and restore it around a draw
		 */
		struct DepthTestState
		{
			/** @brief Whether the depth test is enabled */
			bool TestEnabled = false;
			/** @brief Whether writing to the depth buffer is enabled */
			bool MaskEnabled = true;
		};

		/**
		 * @brief Snapshot of the face culling state, used to save and restore it around a draw
		 */
		struct CullFaceState
		{
			/** @brief Whether face culling is enabled */
			bool Enabled = false;
			/** @brief Culled face mode */
			CullFaceMode Mode = CullFaceMode::Back;
		};

		/** @brief Enables or disables blending */
		static void SetBlendingEnabled(bool enabled);
		/** @brief Sets separate blending factors for color and alpha */
		static void SetBlendingFactors(BlendingFactor srcRgb, BlendingFactor dstRgb, BlendingFactor srcAlpha, BlendingFactor dstAlpha);
		/** @brief Returns the whole blending state */
		static BlendingState GetBlendingState();
		/** @brief Restores the whole blending state */
		static void SetBlendingState(const BlendingState& state);

		/** @brief Enables or disables the depth test */
		static void SetDepthTestEnabled(bool enabled);
		/** @brief Enables or disables writing to the depth buffer */
		static void SetDepthMaskEnabled(bool enabled);
		/** @brief Returns the whole depth test state */
		static DepthTestState GetDepthTestState();
		/** @brief Restores the whole depth test state */
		static void SetDepthTestState(const DepthTestState& state);

		/** @brief Enables or disables face culling */
		static void SetCullFaceEnabled(bool enabled);
		/** @brief Returns the whole face culling state */
		static CullFaceState GetCullFaceState();
		/** @brief Restores the whole face culling state */
		static void SetCullFaceState(const CullFaceState& state);

		/** @brief Returns the whole scissor test state */
		static ScissorState GetScissorState();
		/** @brief Restores the whole scissor test state */
		static void SetScissorState(const ScissorState& state);
		/** @brief Enables the scissor test with the specified rectangle */
		static void SetScissor(const Recti& rect);
		/** @brief Enables (reusing the last rectangle) or disables the scissor test */
		static void SetScissorTestEnabled(bool enabled);

		/** @brief Returns the current viewport rectangle */
		static Recti GetViewport();
		/** @brief Sets the viewport rectangle */
		static void SetViewport(const Recti& rect);
		/** @brief Initializes the cached viewport rectangle right after context creation */
		static void InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);

		/** @brief Returns the current clear color */
		static Colorf GetClearColor();
		/** @brief Sets the clear color */
		static void SetClearColor(const Colorf& color);
		/** @brief Clears the specified buffers of the current render target */
		static void Clear(ClearFlags flags);

		/** @brief Draws non-indexed geometry */
		static void DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices);
		/** @brief Draws non-indexed geometry with instancing */
		static void DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances);
		/**
		 * @brief Draws indexed geometry
		 *
		 * @param indexOffset	Byte offset of the first index within the bound index buffer
		 * @param baseVertex	Added to each index — ignored by backends without base-vertex support,
		 *						where the caller offsets the vertex format instead
		 */
		static void DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex);
		/** @brief Draws indexed geometry with 16-bit indices */
		static inline void DrawElements(PrimitiveType primitive, std::uint32_t numIndices, std::uintptr_t indexOffset, std::int32_t baseVertex) {
			DrawElements(primitive, numIndices, IndexFormat::UInt16, indexOffset, baseVertex);
		}
		/** @brief Draws indexed geometry with instancing */
		static void DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex);
		/** @brief Draws indexed geometry (16-bit indices) with instancing */
		static inline void DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex) {
			DrawElementsInstanced(primitive, numIndices, IndexFormat::UInt16, indexOffset, numInstances, baseVertex);
		}

		/** @brief Inserts a fence that signals once all previously submitted commands complete */
		static FenceHandle InsertFence();
		/** @brief Deletes a fence and resets the handle to `nullptr` (accepts a null handle) */
		static void DeleteFence(FenceHandle& fence);
		/** @brief Blocks until the fence signals, flushing pending commands; returns `false` on timeout or failure */
		static bool ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs);

		/** @brief Applies the initial pipeline state once after context creation */
		static void SetupInitialState();

		// -- Swap-chain / presentation surface (uniform across every backend) --
		// The OpenGL backend renders through a GL context the WINDOW backend owns (SDL / GLFW / EGL), which
		// also performs the buffer swap (e.g. SDL_GL_SwapWindow) - so this whole quartet is inert here. It
		// exists so window backends and shared code can drive any backend through the same calls instead of
		// branching on WITH_RHI_* (the D3D11 / Vulkan devices own a real swap chain behind the same surface).

		/** @brief No-op (the window backend owns the GL context; there is no backend swap chain) */
		static inline bool CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync) {
			static_cast<void>(windowHandle);
			static_cast<void>(width);
			static_cast<void>(height);
			static_cast<void>(vsync);
			return true;
		}
		/** @brief No-op */
		static inline void DestroySwapchain() {}
		/** @brief No-op (the GL default framebuffer resizes with the window) */
		static inline void ResizeSwapchain(std::int32_t width, std::int32_t height) {
			static_cast<void>(width);
			static_cast<void>(height);
		}
		/** @brief No-op (the window backend performs the buffer swap) */
		static inline void PresentFrame() {}
	};
}
