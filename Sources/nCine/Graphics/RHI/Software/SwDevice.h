#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>
#include <vector>

namespace nCine::RhiSoftware
{
	class SwShaderProgram;
	class SwRenderTarget;
	class SwTexture;

	/**
		@brief Destination framebuffer the device presents and resolves draws into

		A caller-owned, tightly- or loosely-packed RGBA8 surface. The bytes are laid out as
		`[R, G, B, A]` per texel, rows are stored top to bottom and @ref strideBytes is the byte
		distance between two consecutive rows (allowing padded rows or rendering into a sub-window of a
		larger surface). The device never allocates or frees this memory (except the screen back-buffer it
		owns through @ref SwDevice::ResizeScreenFramebuffer()).
	*/
	struct Framebuffer
	{
		/** @brief Pointer to the first byte (top-left texel) of the surface */
		std::uint8_t* pixels = nullptr;
		/** @brief Width of the surface in pixels */
		std::int32_t width = 0;
		/** @brief Height of the surface in pixels */
		std::int32_t height = 0;
		/** @brief Byte distance between two consecutive rows (usually `width * 4`) */
		std::int32_t strideBytes = 0;
	};

	/**
		@brief Pipeline-state and draw-call facade of the software backend (aliased as `Rhi::Device`)

		Mirrors the OpenGL device's surface (blending, depth, cull, scissor, viewport, clear and the draw
		calls) but instead of talking to a GPU it is a small immediate-mode state machine: `Bind*` calls
		record the current program, textures and uniform ranges, `Set*` calls record blend/scissor/viewport
		state, and each `Draw*` call gathers all of it and runs the hand-written C++ effect selected by the
		bound program, rasterizing into the current render target's color texture with @ref SwRaster.
		Depth is a no-op (the renderer is 2D) and fences signal immediately.

		The blend factors on this API are the pipeline-neutral `nCine::BlendingFactor` (fully qualified so
		they are never shadowed by the rasterizer's identically-named mirror enum).
	*/
	class SwDevice
	{
	public:
		SwDevice() = delete;
		~SwDevice() = delete;

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
		// The software backend presents through the WINDOW backend's CPU blit (SDL_Renderer streaming
		// texture): the window backend calls @ref FlushSoftwareRenderer() + @ref GetScreenFramebuffer() +
		// @ref EndFrame() itself, so this quartet is inert here. It exists so window backends and shared
		// code can drive any backend through the same calls instead of branching on WITH_RHI_*.

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
		/** @brief No-op (the CPU screen buffer is sized by @ref ResizeScreenFramebuffer() from the render pipeline, not by the window) */
		static inline void ResizeSwapchain(std::int32_t width, std::int32_t height) {
			static_cast<void>(width);
			static_cast<void>(height);
		}
		/** @brief No-op (the window backend blits @ref GetScreenFramebuffer() itself) */
		static inline void PresentFrame() {}

		// -- Software backend extensions (called by the resource types and read by the effects) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(SwShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static SwShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const SwTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~SwTexture to avoid a dangling pointer) */
		static void UnbindTexture(const SwTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const SwTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Records the current draw render target (its color attachment 0 receives the pixels) */
		static void SetRenderTarget(SwRenderTarget* renderTarget);
		/** @brief Clears a render target from the device if it is the current one (called from ~SwRenderTarget) */
		static void UnbindRenderTarget(const SwRenderTarget* renderTarget);
		/** @brief Sets the framebuffer used when no render target is bound (present/back-buffer path) */
		static void SetDefaultFramebuffer(const Framebuffer& framebuffer);

		/**
			@brief Resizes the owned screen back-buffer to the window size and installs it as the default framebuffer

			The root screen viewport binds no render target, so its clears and draws land in the default
			framebuffer. On desktop the window backend calls this once at window creation and on every resize
			to give that framebuffer a CPU surface sized to the drawable, then reads it back each frame with
			@ref GetScreenFramebuffer() to present it (the software counterpart of the GL default framebuffer +
			buffer swap). The backend owns the pixel memory; it stays valid until the next resize.
		*/
		static void ResizeScreenFramebuffer(std::int32_t width, std::int32_t height);
		/** @brief Returns the owned screen back-buffer (pixels/size/stride) for the window backend to present */
		static Framebuffer GetScreenFramebuffer();

		/**
			@brief Renders any draws the tile renderer has deferred into the current color buffer

			The window backend calls this before reading @ref GetScreenFramebuffer() to present, so every
			queued draw has landed in the screen buffer. Forwards to the rasterizer's deferred-layer flush,
			which waits for all worker threads to finish; a no-op when nothing is queued.
		*/
		static void FlushSoftwareRenderer();

		/**
			@brief Ends the presented frame, dropping any per-frame device state

			The window backend calls this after presenting. Discards software-lighting entries left in the queue
			(their Combine draw was culled or never dispatched, e.g. an off-screen viewport): without this the
			leftovers would survive into later frames, permanently shifting the viewport-to-lightmap pairing in
			splitscreen — and their caller-owned lightmap pointers would dangle past the compositor's frame.
		*/
		static void EndFrame();

		/**
			@brief Queues a dynamic lightmap to be composited over a viewport during the next matching Combine draw

			Software-lighting entry point used by the viewport compositor (@c Jazz2::Rendering::CombineRenderer). The
			scene renders straight to the screen buffer with no shader post-processing, so the dynamic lighting is
			applied on the CPU here instead: the compositor builds the half-resolution lightmap in the Visit
			(queue-building) phase and calls this, then the Combine draw it queues is intercepted in @ref Dispatch()
			during the later Draw phase - after the scene has been rasterized into the screen buffer and before the
			HUD - where the map is blended in place over the viewport rectangle. Entries are consumed
			first-in-first-out, one per Combine draw, so splitscreen viewports each receive their own map in
			submission order.

			@param lightmap  Half-resolution accumulation buffer, 2 floats per texel (R = intensity incl. the ambient
			                 base, G = brightness core), row-major. Caller-owned; must stay valid until the matching
			                 Combine draw is dispatched (the compositor keeps it in a per-viewport member).
			@param lmW       Lightmap width in texels
			@param lmH       Lightmap height in texels
			@param scale     Lightmap-to-screen downscale factor (a screen pixel samples texel `pixel / scale`)
			@param vpX       Viewport left in screen-buffer pixels
			@param vpY       Viewport top in screen-buffer pixels
			@param vpW       Viewport width in screen-buffer pixels (clamped against the buffer at apply time)
			@param vpH       Viewport height in screen-buffer pixels (clamped against the buffer at apply time)
			@param ambR      Ambient colour red the unlit scene is blended toward
			@param ambG      Ambient colour green the unlit scene is blended toward
			@param ambB      Ambient colour blue the unlit scene is blended toward
		*/
		static void SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
			std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB);

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

		static SwShaderProgram* currentProgram_;
		static const SwTexture* boundTextures_[MaxTextureUnits];
		static UniformRange boundUniformRanges_[MaxUniformBindings];
		static SwRenderTarget* currentRenderTarget_;
		static std::uint8_t* defaultFbPixels_;
		static std::int32_t defaultFbWidth_;
		static std::int32_t defaultFbHeight_;
		static std::int32_t defaultFbStride_;
		/** @brief Backend-owned pixel store for the screen back-buffer (only used by the present path) */
		static std::vector<std::uint8_t> screenPixels_;

		/** @brief One queued software-lighting combine, submitted by the compositor and applied at the next Combine draw */
		struct PendingSoftwareLight
		{
			const float* Lightmap = nullptr;
			std::int32_t LmW = 0, LmH = 0, Scale = 1;
			std::int32_t VpX = 0, VpY = 0, VpW = 0, VpH = 0;
			float AmbR = 0.0f, AmbG = 0.0f, AmbB = 0.0f;
		};
		/** @brief FIFO of pending software-lighting combines (one per viewport, in submission order) */
		static std::vector<PendingSoftwareLight> pendingSoftwareLights_;

		/** @brief Resolves the color framebuffer that draws and clears write into (RT color 0, else default) */
		static bool ResolveFramebuffer(Framebuffer& out);
		/** @brief Runs the correct C++ effect for the bound program over the given draw range */
		static void Dispatch(PrimitiveType primitive, std::int32_t numVertices);
		/** @brief Consumes the front queued software lightmap and blends it in place over its viewport rectangle */
		static void ApplyPendingSoftwareLighting();
	};
}
