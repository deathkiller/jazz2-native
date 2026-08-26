#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"
#include "GsVram.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nCine::RHI::GS
{
	class GsShaderProgram;
	class GsRenderTarget;
	class GsTexture;
	// Defined by the generated GsGeneratedEffects.h inside the device translation unit; everyone else only
	// ever holds an opaque entry pointer resolved at program load
	struct FixedFunctionGeneratedEffect;

	/**
		@brief Pipeline-state and draw-call facade of the GS backend (aliased as `RHI::Device`)

		The PlayStation 2 twin of the PVR and GX devices: each draw decodes the bound program's instance
		block(s) exactly like `SwDevice::Dispatch`, CPU-transforms the four sprite corners to display
		coordinates and writes them into a GIF packet as Graphics Synthesizer primitives. The GS takes screen
		space directly - there is no transform unit in the rasterizer and no matrix register - so the whole
		model-view-projection is applied on the EE, the same way the PowerVR backend does it on the SH4.

		Register encoding is left to PS2SDK's `libdraw` rather than hand-assembled, in the same way the PVR
		backend builds on KallistiOS's `pvr.h`: the backend's own logic is corner synthesis, effect dispatch
		and residency, not GIF tag layout. Two traps of that library are load-bearing here and are repeated
		wherever they apply, because neither fails loudly:
		- `draw_texture_transfer()` builds a DMA **chain**, so its packet must be sent with
		  `dma_channel_send_chain()`; sending it as a normal transfer wedges the channel silently.
		- `texbuffer_t::width` and `framebuffer_t::width` are in **texels**, not `TBW` units.

		Because the GS rasterizes as packets arrive rather than deferring a whole scene like the tile
		accelerator, a texture whose pages are reused mid-frame could be sampled by a primitive that was
		already submitted. What makes that safe here is ORDER rather than planning: the GIF consumes
		everything on its DMA path strictly in sequence, so as long as the re-upload is issued *after* the
		draws that sample the previous contents - which @ref FlushPendingPackets() is what guarantees - the
		GS has finished with those draws before the new texels land. A store may still be evicted while it is
		resident, but never while a submitted primitive is still reading it.

		Palette handling has no bank limit to work around: a CLUT is a 1 KB region of local memory selected
		per draw through `TEX0.CBP`, so the layout's slab holds many at once and the PowerVR's four-bank LRU
		becomes a straight slot cache keyed by palette row.
	*/
	class GsDevice
	{
	public:
		/** @brief Monotonic count of finished frames, used to detect "still referenced by the current frame" resources */
		static std::uint32_t GetFrameCounter() {
			return _frameCounter;
		}

		/**
			@brief Returns the generated fixed-function effect of a (program, variant) key, or `nullptr`

			Scans the table transpiled from the shaders' `fixed_function` blocks
			(`Shaders/Generated/GsGeneratedEffects.h`). Called once per program load from
			@ref GsShaderProgram::SetObjectLabel(), which maps the object label onto the key through its
			exact-name table - the draw path only ever reads the stored pointer.
		*/
		static const FixedFunctionGeneratedEffect* FindGeneratedEffect(const char* program, const char* variant);

		GsDevice() = delete;
		~GsDevice() = delete;

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
		/** @brief Flushes the frame's remaining GIF packets and flips the display buffer */
		static void PresentFrame();

		/** @brief Returns the maximum supported texture dimension (drives the tileset chunking) */
		static inline std::int32_t GetMaxTextureDimension() {
			// `TEX0.TW`/`TH` are log2 fields capped at 10
			return 1024;
		}

		// -- Direct-tier presentation contract (see RhiFwd.h) --

		/** @brief Sets the logical resolution the scene is rendered at (scaled to the display at submit) */
		static void ResizeScreenFramebuffer(std::int32_t width, std::int32_t height);

		// -- GS session, driven by the Ps2 window backend --

		/**
			@brief Brings up the Graphics Synthesizer

			Places the static regions through @ref GsVram::Initialize(), sets the video mode and hands the
			display buffers' addresses to `libgraph`. The depth test is configured ALWAYS with writes masked:
			the engine's queue is already in painter's order, so a Z buffer would cost 560 KB of local memory
			for nothing.
		*/
		static void InitializeGs();

		// -- GS backend extensions (called by the resource types and read by the draw dispatch) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(GsShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static GsShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const GsTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~GsTexture) */
		static void UnbindTexture(const GsTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const GsTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Records the current draw render target (points `FRAME.FBP` at its attachment) */
		static void SetRenderTarget(GsRenderTarget* renderTarget);
		/** @brief Clears a render target from the device if it is the current one (called from ~GsRenderTarget) */
		static void UnbindRenderTarget(const GsRenderTarget* renderTarget);

		/**
			@brief Sends whatever draws and register writes are still queued, so a later transfer cannot overtake them

			The GS consumes GIF data strictly in order, which is the *only* thing that makes a mid-frame
			re-upload safe: the transfer must reach it AFTER the draws that sample the store's previous
			contents. Those draws sit in this device's pending packet until it is flushed, so any code path
			that hands the GIF a packet of its own - a texture or CLUT transfer, both of which are DMA chains
			and cannot be appended here - has to call this first. Skipping it inverts the order: the upload
			lands, and then the draws that were queued before it sample the new data.
		*/
		static void FlushPendingPackets();

		/**
			@brief Writes the EE data cache back so a DMA can read @p bytes bytes at @p start

			The second thing every path that hands the GIF a packet of its own has to do, and the one with no
			symptom until it bites. PS2SDK synchronises the buffer it is *handed* - `dma_channel_send_chain()`
			calls `SyncDCache()` over the qwords of the chain itself - but `draw_texture_transfer()` builds a
			chain whose `REF` tags point somewhere else entirely, at the staging image in main memory, and
			nothing synchronises that. The EE's data cache is 8 KB and **write-back**, so the tail of an image
			the CPU has just filled is still sitting in it, and the DMA - which reads memory directly, never
			the cache - transfers whatever was in those addresses beforehand.

			The corruption is therefore intermittent by nature: how much of the image is still dirty depends
			on what ran between filling it and sending it, and the stale bytes are the *previous* upload
			through the same staging buffer. A texture comes out garbled for as long as it stays resident and
			is correct the next time it happens to be re-transferred, which is what it looked like - a frame
			glitching at random and coming good by itself.
		*/
		static void WritebackForDma(const void* start, std::size_t bytes);

		/** @brief Registers the intercepted shared palette texture (rows become CLUTs) */
		static void RegisterPaletteTexture(GsTexture* texture);
		/** @brief Invalidates the CLUT slots (and RG8 bakes) of the given palette rows after an upload */
		static void NotifyPaletteTextureChanged(GsTexture* texture, std::int32_t firstRow, std::int32_t rowCount);

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
		/** @brief CLUT slots tracked by the device (the slab's capacity, see `GsVramLayout::ClutSlotCount`) */
		static constexpr std::uint32_t MaxClutSlots = 32;
		/**
			@brief Rows of the shared palette texture whose changes are tracked individually

			`ContentResolver` builds a 256x256 palette texture and uploads only the rows it dirtied, and the
			game dirties a few of them most frames (the animated water and lava ramps). One global "the palette
			changed" counter therefore invalidates every resident CLUT **and every RG8 bake** on almost every
			frame - and a bake costs a page allocation plus a DMA per sprite on this hardware, which is enough
			to exhaust local memory and collapse the frame rate. Stamping rows separately keeps an invalidation
			as narrow as the upload that caused it.
		*/
		static constexpr std::int32_t MaxPaletteRows = 256;

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

		/** @brief One resident CLUT: a 1 KB slab slot holding 256 entries of one palette row */
		struct ClutSlot
		{
			std::int32_t PaletteOffset = -1;
			const GsTexture* Palette = nullptr;
			std::uint32_t PaletteVersion = 0;
			std::uint32_t LastUse = 0;
			/** @brief Block address of the slot, the unit of `TEX0.CBP` */
			std::uint32_t Block = GsVram::InvalidBlock;
			/**
				@brief Whether the slot holds the row's COVERAGE rather than its colours

				A silhouette pass needs a flat colour where the texture has alpha, which the GS's four
				texture functions cannot produce (see @ref AcquireCoverageClut()). The coverage form of a
				row - white in every entry, the row's own alphas kept - turns MODULATE into exactly that,
				and it is a different 1 KB payload for the same row, so it needs its own slot.
			*/
			bool Coverage = false;
		};

		static BlendingState _blending;
		static DepthTestState _depthTest;
		static CullFaceState _cullFace;
		static ScissorState _scissor;
		static Recti _viewport;
		static Colorf _clearColor;

		static GsShaderProgram* _currentProgram;
		static const GsTexture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static GsRenderTarget* _currentRenderTarget;
		/**
			@brief Whether the current render target has no colour surface in local memory

			`FRAME` still points at whatever was bound before it, so every draw and clear of the pass has to
			be dropped rather than issued - they would land on the previous target (the display, usually)
			while being transformed and scissored for this one.
		*/
		static bool _renderTargetSurfaceMissing;

		static bool _gsInitialized;
		static std::int32_t _logicalWidth;
		static std::int32_t _logicalHeight;
		static std::int32_t _displayBufferIndex;

		static GsTexture* _paletteTexture;
		static std::uint32_t _paletteGeneration;
		/** @brief Value of @ref _paletteGeneration when each palette row was last uploaded */
		static std::uint32_t _paletteRowStamp[MaxPaletteRows];
		static ClutSlot _clutSlots[MaxClutSlots];
		static std::uint32_t _clutUseCounter;
		static std::uint32_t _frameCounter;

		static std::vector<PendingSoftwareLight> _pendingSoftwareLights;

		static void Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices);
		/** @brief Draws a whole tile-layer mesh (a triangle list of position/texcoord/colour vertices) */
		static void DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices);
		/**
			@brief Draws a vertex-fed textured line strip (the weapon wheel)

			The GS does have a LINE primitive, but it has no texture coordinates worth using across a strip,
			so every segment goes out as a thin screen-space quad exactly as on the PVR.
		*/
		static void DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices);
		static void ApplyPendingSoftwareLighting();
		/**
			@brief Programs `SCISSOR` for the current scissor state and render target

			Called by every clear and every draw. The register is context state that outlives a packet, so
			the pass that does not scissor has to say so explicitly - otherwise it keeps the rectangle of
			whichever pass scissored last.
		*/
		static void ApplyScissor();
		/** @brief Loads 256 entries into a CLUT slab slot, reusing the slot if they are already there */
		/**
			@brief The 256 palette entries at @p paletteOffset, or `nullptr` when the offset is out of range

			The offset carried in an instance is a **flat** index into the palette texture and need not be
			row-aligned - the gem gradients pack two palettes into one 256-entry row, so theirs are 128, 256,
			384 and 512. Reading it as a row index walks off the end of the palette.
		*/
		static const std::uint32_t* ResolvePaletteEntries(const GsTexture* palette, std::int32_t paletteOffset);
		/**
			@brief Version of the 256 entries at @p paletteOffset of @p palette

			For the shared palette texture this is the newest stamp of the (at most two) rows those entries
			span, so a CLUT or a bake is only invalidated when the entries it actually read were rewritten.
			Any other palette texture is versioned as a whole by its own content version.
		*/
		static std::uint32_t PaletteVersionForOffset(const GsTexture* palette, std::int32_t paletteOffset);
		static std::uint32_t AcquireClutForOffset(const GsTexture* palette, std::int32_t paletteOffset);
		static std::uint32_t AcquireClut(const GsTexture* palette, std::int32_t paletteOffset,
			std::uint32_t version, const std::uint32_t* entries, bool coverage = false);
		/**
			@brief Loads the COVERAGE form of a palette row - white in every entry, its own alphas kept

			The mechanism behind `FixedFunctionPass::TevPreset::Silhouette` for an indexed store. A
			silhouette is "the pass colour wherever the texture has alpha", and none of the GS's four
			texture functions produces it: MODULATE multiplies the texel in, DECAL ignores the fragment
			colour entirely, and both HIGHLIGHT forms can only add the fragment ALPHA (achromatic) on top.
			But a CLUT is a per-draw table, so replacing every entry's colour with white while keeping its
			alpha makes MODULATE compute `white * Cf = Cf` and `At * Af` - the silhouette, exactly, with no
			extra pass and no per-texel arithmetic.
		*/
		static std::uint32_t AcquireCoverageClutForOffset(const GsTexture* palette, std::int32_t paletteOffset);
		/**
			@brief Loads the static alpha-to-coverage CLUT the true-colour stores sample through

			The same silhouette trick as @ref AcquireCoverageClutForOffset(), for a store that has no CLUT of
			its own. A PSMCT32 store can also be sampled as **PSMT8H**, which reads the high byte of each
			32-bit cell - the alpha - as an 8-bit index through a CLUT, over identical addressing (8H is a
			sub-field of the same cells, so the pitch, the page swizzle and `TW`/`TH` are unchanged). Index
			`i` is therefore the texel's alpha, and one table mapping `i` to (white, alpha `i`) turns
			MODULATE into the silhouette for EVERY such store - so unlike the per-row form this is a single
			table for the whole backend, built once and kept in its slot.
		*/
		static std::uint32_t AcquireAlphaCoverageClut();
		static void GetTargetScale(float& scaleX, float& scaleY, float& offsetX, float& offsetY);
	};
}
