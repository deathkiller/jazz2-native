#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>
#include <vector>

#include <gccore.h>

namespace nCine::RHI::GX
{
	class GxShaderProgram;
	class GxRenderTarget;
	class GxTexture;
	// Defined by the generated GxGeneratedEffects.h inside the device translation unit; everyone
	// else only ever holds an opaque entry pointer resolved at program load
	struct FixedFunctionGeneratedEffect;

	/**
		@brief Pipeline-state and draw-call facade of the GX backend (aliased as `RHI::Device`)

		Mirrors the software device's surface, but instead of rasterizing on the CPU each draw decodes the
		bound program's instance block(s) exactly like `SwDevice::Dispatch` and emits fixed-function GX
		quads: the four sprite corners are CPU-transformed to logical screen pixels (the same math as the
		software `FetchVertex`), an orthographic projection over the logical resolution maps them onto the
		full EFB (which is how the logical-resolution scene is upscaled to the display for free), and the
		effect selected from the program label picks the texture/TEV configuration:
		- `DefaultSprite` family: textured quad, `GX_MODULATE` with the instance color
		- `PaletteRemap` family: CI8 texture with the TLUT of the instance's `palOffset` row; RG8 index+alpha
		  textures use a per-row CPU-baked RGBA copy (see @ref GxTexture::EnsureBakedRgba)
		- `NoTexture` family: flat color quad (`GX_PASSCLR`)
		- `TileMapMesh` family: a whole tile layer arrives as one triangle-list mesh (see
		  @ref DispatchTileMesh) and is submitted as batched `GX_QUADS` runs sharing one texture/TEV setup
		- `TexturedBackground` family: rendered as a plain textured quad (the per-pixel tunnel warp needs
		  shaders; the flat fallback keeps those layers visible)
		- `Combine`: intercepted - applies the queued CPU lightmap as a multiply quad plus the water tint
		  bands (the direct-tier lighting contract shared with the software backend)
		Unknown effects log once and skip their draws.

		Render targets are EFB copies: drawing into a bound @ref GxRenderTarget renders to the EFB with the
		target's dimensions and is copied out (`GX_CopyTex`) into the attached texture's tiled store when the
		target is switched away, so the textured-background passes work. Render-target passes must precede
		the screen pass within a frame (the engine's viewport chain already orders them that way).
	*/
	class GxDevice
	{
	public:
		/** @brief Monotonic count of presented frames, used to detect "still referenced by the in-flight FIFO" resources */
		static std::uint32_t GetFrameCounter() {
			return _frameCounter;
		}

		/**
			@brief Returns the generated fixed-function effect of a (program, variant) key, or `nullptr`

			Scans the table transpiled from the shaders' `fixed_function` blocks
			(`Shaders/Generated/GxGeneratedEffects.h`). Called once per program load from
			@ref GxShaderProgram::SetObjectLabel(), which maps the object label onto the key
			through its exact-name table - the draw path only ever reads the stored pointer.
		*/
		static const FixedFunctionGeneratedEffect* FindGeneratedEffect(const char* program, const char* variant);

		GxDevice() = delete;
		~GxDevice() = delete;

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
		// The Ogc window backend owns VIDEO/XFB and drives the GX session through InitializeGx() and
		// PresentToXfb() below, so the neutral quartet is inert here (mirrors the software backend).

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
		/** @brief No-op (the window backend calls PresentToXfb() itself) */
		static inline void PresentFrame() {}

		/** @brief Returns the maximum supported texture dimension (drives the tileset chunking) */
		static inline std::int32_t GetMaxTextureDimension() {
			return 1024;
		}

		// -- Direct-tier presentation contract (see RhiFwd.h) --

		/**
			@brief Sets the logical resolution the scene is rendered at

			Called by the render pipeline (UpscaleRenderPass) on the direct tier. Draw coordinates arrive in
			this logical space; the orthographic projection maps it onto the full EFB, whose display copy the
			video hardware scales to the TV - the logical-to-display upscale costs nothing.
		*/
		static void ResizeScreenFramebuffer(std::int32_t width, std::int32_t height);

		// -- GX session, driven by the Ogc window backend --

		/** @brief Brings up the GX pipe (FIFO, vertex descriptors, TEV baseline) for the given render mode */
		static void InitializeGx(GXRModeObj* rmode);
		/** @brief Drains the GX pipe and releases the FIFO, so no GP work is left pending at exit */
		static void ShutdownGx();
		/** @brief Finishes the frame's draws, copies the EFB to the given external framebuffer and flushes */
		static void PresentToXfb(void* xfb);

		// -- GX backend extensions (called by the resource types and read by the draw dispatch) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(GxShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static GxShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const GxTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~GxTexture) */
		static void UnbindTexture(const GxTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const GxTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Records the current draw render target (draws render to the EFB and are copied out on switch) */
		static void SetRenderTarget(GxRenderTarget* renderTarget);
		/** @brief Clears a render target from the device if it is the current one (called from ~GxRenderTarget) */
		static void UnbindRenderTarget(const GxRenderTarget* renderTarget);

		/** @brief Registers the intercepted shared palette texture (rows become TLUTs) */
		static void RegisterPaletteTexture(GxTexture* texture);
		/** @brief Invalidates the TLUTs (and RG8 bakes) of the given palette rows after an upload */
		static void NotifyPaletteTextureChanged(GxTexture* texture, std::int32_t firstRow, std::int32_t rowCount);

		/**
			@brief Queues the CPU lightmap combine for the next `Combine` draw

			The direct-tier lighting contract shared with the software backend (see CombineRenderer): the GX
			device consumes each entry by drawing the lightmap as a multiply quad. The water half of the
			compositor is NOT queued here - it is a fixed_function block of the CombineWithWater programs (see
			CombineWithWater.shader), so the `water*` parameters of the shared signature are ignored by this
			backend - only the software one still reads them.
		*/
		static void SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
			std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
			bool waterActive = false, float waterLevelPx = 0.0f, float waterTime = 0.0f, float waterCamY = 0.0f);
		/** @brief Drops any lighting entries not consumed this frame (called by the window backend at present) */
		static void EndFrame();

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;
		// GX_TLUT0..15 are the 16 low-bank 256-entry TLUT slots; palette rows are mapped onto them LRU
		static constexpr std::uint32_t MaxTlutSlots = 16;

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

		struct TlutSlot
		{
			std::int32_t PaletteOffset = -1;
			const GxTexture* Palette = nullptr;
			std::uint32_t PaletteVersion = 0;
			std::uint32_t LastUse = 0;
			std::uint16_t* Data = nullptr;		// 256 RGB5A3 entries, 32-byte aligned
		};

		static BlendingState _blending;
		static DepthTestState _depthTest;
		static CullFaceState _cullFace;
		static ScissorState _scissor;
		static Recti _viewport;
		static Colorf _clearColor;

		static GxShaderProgram* _currentProgram;
		static const GxTexture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static GxRenderTarget* _currentRenderTarget;

		static GXRModeObj* _rmode;
		static void* _gxFifo;
		static bool _gxInitialized;
		static std::int32_t _logicalWidth;
		static std::int32_t _logicalHeight;

		static GxTexture* _paletteTexture;
		static std::uint32_t _paletteGeneration;
		static TlutSlot _tlutSlots[MaxTlutSlots];
		static std::uint32_t _tlutUseCounter;
		static std::uint32_t _frameCounter;

		static std::vector<PendingSoftwareLight> _pendingSoftwareLights;

		// Lightmap combine texture (rebuilt per Combine draw from the queued float lightmap)
		static std::uint8_t* _lightmapStore;
		static std::size_t _lightmapStoreSize;
		static GXTexObj _lightmapTexObj;
		// Linear staging buffer the lightmap texels are combined into before tiling; persists across
		// frames and grows monotonically, because reallocating (and zero-filling) it per lit frame is
		// wasted work - every byte is overwritten anyway
		static std::uint8_t* _lightmapLinear;
		static std::size_t _lightmapLinearSize;

		/** @brief Resolves the index range of a `DrawElements()` into a host pointer, or `nullptr` if it cannot be read */
		static const std::uint16_t* ResolveHostIndices(IndexFormat indexFormat, std::uintptr_t indexOffset, std::uint32_t numIndices);

		static void Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
			const std::uint16_t* indices = nullptr, std::int32_t indexCount = 0);
		static void DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices,
			const std::uint16_t* indices, std::int32_t indexCount);
		// Draws a vertex-fed textured line strip (the weapon wheel) through the GP's native line primitive
		static void DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices);
		static void ApplyPendingSoftwareLighting();
		static void ApplyRenderState();
		static void ApplyProjection();
		static void FlushCurrentRenderTarget();
		static std::int32_t AcquireTlutForRow(const GxTexture* palette, std::int32_t paletteRow);
	};
}
