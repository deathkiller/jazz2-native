#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>
#include <vector>

#include <dc/pvr.h>

namespace nCine::RHI::PVR
{
	class PvrShaderProgram;
	class PvrRenderTarget;
	class PvrTexture;
	// Defined by the generated PvrGeneratedEffects.h inside the device translation unit; everyone
	// else only ever holds an opaque entry pointer resolved at program load
	struct FixedFunctionGeneratedEffect;

	/**
		@brief Pipeline-state and draw-call facade of the PVR backend (aliased as `RHI::Device`)

		The Dreamcast twin of the GX device: each draw decodes the bound program's instance block(s)
		exactly like `SwDevice::Dispatch`, CPU-transforms the four sprite corners to logical pixels and
		submits them as PowerVR translucent-list quads in display coordinates (the logical-to-640x480
		scale is folded into the vertex positions). The TA renders one scene per frame, so the device runs
		a small scene state machine: the first draw of a frame opens the scene (`pvr_scene_begin` /
		`pvr_scene_begin_txr` for a render target) and its translucent list; switching targets or
		presenting finishes it. Autosort is disabled at init, so the translucent list preserves submission
		order - the engine's painter's-order queue maps 1:1.

		Per-effect texture selection mirrors the GX device: CI8-style draws pick one of the four hardware
		palette banks from the instance's `palOffset` (LRU), RG8 index+alpha textures use the per-row
		ARGB4444 bake, `NoTexture` draws submit untextured colored quads, and the `Combine` draw applies
		the queued CPU lightmap as a `dst * src` multiply quad plus the water tint bands.
	*/
	class PvrDevice
	{
	public:
		/** @brief Monotonic count of finished scenes, used to detect "still referenced by the current scene" resources */
		static std::uint32_t GetSceneCounter() {
			return _sceneCounter;
		}

		/**
			@brief Returns the generated fixed-function effect of a (program, variant) key, or `nullptr`

			Scans the table transpiled from the shaders' `fixed_function` blocks
			(`Shaders/Generated/PvrGeneratedEffects.h`). Called once per program load from
			@ref PvrShaderProgram::SetObjectLabel(), which maps the object label onto the key
			through its exact-name table - the draw path only ever reads the stored pointer.
		*/
		static const FixedFunctionGeneratedEffect* FindGeneratedEffect(const char* program, const char* variant);

		PvrDevice() = delete;
		~PvrDevice() = delete;

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
		/** @brief Finishes the frame's TA scene (called by the Dc window backend once per frame) */
		static void PresentFrame();

		/** @brief Returns the maximum supported texture dimension (drives the tileset chunking) */
		static inline std::int32_t GetMaxTextureDimension() {
			return 1024;
		}

		// -- Direct-tier presentation contract (see RhiFwd.h) --

		/** @brief Sets the logical resolution the scene is rendered at (scaled to 640x480 at submit) */
		static void ResizeScreenFramebuffer(std::int32_t width, std::int32_t height);

		// -- PVR session, driven by the Dc window backend --

		/** @brief Brings up the PVR (pvr_init with autosort disabled, background color, palette format) */
		static void InitializePvr();

		// -- PVR backend extensions (called by the resource types and read by the draw dispatch) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(PvrShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static PvrShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const PvrTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~PvrTexture) */
		static void UnbindTexture(const PvrTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const PvrTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Records the current draw render target (draws open a render-to-texture scene) */
		static void SetRenderTarget(PvrRenderTarget* renderTarget);
		/** @brief Clears a render target from the device if it is the current one (called from ~PvrRenderTarget) */
		static void UnbindRenderTarget(const PvrRenderTarget* renderTarget);

		/** @brief Registers the intercepted shared palette texture (rows become palette banks) */
		static void RegisterPaletteTexture(PvrTexture* texture);
		/** @brief Invalidates the palette banks (and RG8 bakes) of the given palette rows after an upload */
		static void NotifyPaletteTextureChanged(PvrTexture* texture, std::int32_t firstRow, std::int32_t rowCount);

		/** @brief Queues the CPU lightmap/water combine for the next `Combine` draw (the direct-tier lighting contract) */
		static void SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
			std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
			bool waterActive = false, float waterLevelPx = 0.0f, float waterTime = 0.0f, float waterCamY = 0.0f);
		/** @brief Drops any lighting entries not consumed this frame (called by the window backend at present) */
		static void EndFrame();

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;
		// The PowerVR has 1024 palette entries = four 256-entry banks; rows are mapped onto them LRU
		static constexpr std::uint32_t MaxPaletteBanks = 4;

		enum class SceneTarget
		{
			None,
			Screen,
			RenderTexture
		};

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
			bool WaterActive = false;
			float WaterLevelPx = 0.0f, WaterTime = 0.0f, WaterCamY = 0.0f;
		};

		struct PaletteBank
		{
			std::int32_t PaletteOffset = -1;
			const PvrTexture* Palette = nullptr;
			std::uint32_t PaletteVersion = 0;
			std::uint32_t LastUse = 0;
		};

		static BlendingState _blending;
		static DepthTestState _depthTest;
		static CullFaceState _cullFace;
		static ScissorState _scissor;
		static Recti _viewport;
		static Colorf _clearColor;

		static PvrShaderProgram* _currentProgram;
		static const PvrTexture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static PvrRenderTarget* _currentRenderTarget;

		static bool _pvrInitialized;
		static std::int32_t _logicalWidth;
		static std::int32_t _logicalHeight;
		static SceneTarget _sceneTarget;
		static PvrRenderTarget* _sceneRenderTarget;

		static PvrTexture* _paletteTexture;
		static std::uint32_t _paletteGeneration;
		static PaletteBank _paletteBanks[MaxPaletteBanks];
		static std::uint32_t _paletteUseCounter;
		static std::uint32_t _sceneCounter;

		static std::vector<PendingSoftwareLight> _pendingSoftwareLights;

		// Lightmap combine texture (rebuilt per Combine draw from the queued float lightmap)
		static pvr_ptr_t _lightmapVram;
		static std::size_t _lightmapVramSize;
		static std::int32_t _lightmapW;
		static std::int32_t _lightmapH;

		static void Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices);
		// Draws a whole tile-layer mesh (a triangle list of position/texcoord/colour vertices)
		static void DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices);
		// Draws a vertex-fed textured line strip (the weapon wheel); the TA has no line primitive,
		// so every segment goes out as a thin screen-space quad
		static void DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices);
		static void ApplyPendingSoftwareLighting();
		static void EnsureScene();
		static void FinishScene();
		static std::int32_t AcquirePaletteBankForRow(const PvrTexture* palette, std::int32_t paletteRow);
		/** @brief Loads 256 entries into one of the hardware palette banks, reusing the bank if they are already there */
		static std::int32_t AcquirePaletteBank(const PvrTexture* palette, std::int32_t paletteOffset,
			std::uint32_t version, const std::uint32_t* entries);
		static void GetTargetScale(float& scaleX, float& scaleY, float& offsetX, float& offsetY);
	};
}
