#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nCine::RHI::GU
{
	class GuShaderProgram;
	class GuRenderTarget;
	class GuTexture;
	// Defined by the generated fixed-function effect table the device translation unit includes
	// (Shaders/Generated/GuGeneratedEffects.h, exactly like the PVR and GX backends consume theirs);
	// everyone else only ever holds an opaque entry pointer resolved at program load, so the incomplete
	// type is enough here
	struct FixedFunctionGeneratedEffect;

	/**
		@brief Pipeline-state and draw-call facade of the GU backend (aliased as `RHI::Device`)

		The PlayStation Portable twin of the GX and PVR devices: each draw decodes the bound program's
		instance block(s) exactly like `SwDevice::Dispatch`, CPU-transforms the four sprite corners to
		screen pixels and submits them to the Allegrex GE as `GU_TRANSFORM_2D` primitives - the GE's
		"through" mode, which skips transform and lighting and takes screen coordinates and texel-space
		texture coordinates directly. Axis-aligned quads (nearly everything: UI, tiles, unrotated sprites)
		go out as `GU_SPRITES`, two vertices per quad; rotated ones as triangle pairs.

		Everything the frame draws is written into one display list the GE consumes while it is being
		filled, and vertices come out of a per-frame arena that is written back from the data cache before
		each draw call - the GE reads main memory without seeing the CPU's cache, which is the classic
		source of "nothing renders" on this hardware. Consecutive primitives that share their whole GE
		state are accumulated into a single `sceGuDrawArray`, so a tile layer or a text run costs one draw
		call.

		Unlike the PowerVR, the GE has a real raster scissor (`sceGuScissor`), so scissored draws are not
		clipped geometrically - the rect is programmed and the hardware cuts the primitives. Indexed
		textures are resolved by the hardware CLUT (`GU_PSM_T8`), loaded per draw from whatever palette
		texture the material bound, which is the direct analogue of the PVR's palette banks and the GX's
		TLUTs; RG8 index+alpha content has no paletted form and takes the same per-palette-row CPU bake the
		other consoles use.

		Because the GE has no programmable shaders, the game runs the direct tier (see `RhiFwd.h`): the
		scene is rendered straight to the display at the logical resolution and the CPU lightmap is handed
		to the device through @ref SetPendingSoftwareLighting() instead of a compositing shader pass.
	*/
	class GuDevice
	{
	public:
		/** @brief Monotonic count of finished frames, used to detect "still referenced by the current frame" resources */
		static std::uint32_t GetSceneCounter() {
			return _sceneCounter;
		}

		/**
			@brief Returns the generated fixed-function effect of a (program, variant) key, or `nullptr`

			Called once per program load from @ref GuShaderProgram::SetProgramIdentity(). Scans the
			fixed-function effect table of this backend; a program that is absent from it has no
			`fixed_function` block in its `.shader` file (Lighting, Blur, the Resize* family, ...) and its
			draws are skipped with a one-time warning, exactly as on the other fixed-function backends.
		*/
		static const FixedFunctionGeneratedEffect* FindGeneratedEffect(const char* program, const char* variant);

		GuDevice() = delete;
		~GuDevice() = delete;

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
		/** @brief Closes the frame's display list and flips the display buffers (called by the Psp window backend once per frame) */
		static void PresentFrame();

		/** @brief The dimension a single GE texture cannot exceed (9-bit u/v addressing - a hardware limit) */
		static constexpr std::int32_t HardwareTextureDimension = 512;

		/**
			@brief Returns the maximum supported texture dimension (drives the tileset chunking)

			The GE's true limit, reported honestly: it makes `ContentResolver` cut tileset atlases into
			chunks that each fit one GE texture, which is what the number is for. Prebaked content that is
			larger anyway (the small font atlas is 128x529) is NOT rejected - @ref GuTexture splits such an
			image into 512x512 pages internally and the draw path picks the page a primitive samples - so
			`Texture::Initialize()` only warns about it on this backend instead of asserting.
		*/
		static inline std::int32_t GetMaxTextureDimension() {
			return HardwareTextureDimension;
		}

		// -- Direct-tier presentation contract (see RhiFwd.h) --

		/** @brief Sets the logical resolution the scene is rendered at (scaled to 480x272 at submit) */
		static void ResizeScreenFramebuffer(std::int32_t width, std::int32_t height);

		// -- GU session, driven by the Psp window backend --

		/** @brief Brings up the GE (sceGuInit, the double-buffered 480x272 display, the initial pipeline state) */
		static void InitializeGu();
		/** @brief Tears the GE session down again (sceGuTerm), so the exit path leaves the hardware idle */
		static void ShutdownGu();

		// -- GU backend extensions (called by the resource types and read by the draw dispatch) --

		/** @brief Records the currently bound shader program */
		static void BindProgram(GuShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static GuShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const GuTexture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~GuTexture) */
		static void UnbindTexture(const GuTexture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const GuTexture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Records the current draw render target (draws redirect the GE's draw buffer to it) */
		static void SetRenderTarget(GuRenderTarget* renderTarget);
		/** @brief Clears a render target from the device if it is the current one (called from ~GuRenderTarget) */
		static void UnbindRenderTarget(const GuRenderTarget* renderTarget);

		/** @brief Registers the intercepted shared palette texture (rows become hardware CLUTs) */
		static void RegisterPaletteTexture(GuTexture* texture);
		/** @brief Invalidates the CLUT copies (and RG8 bakes) built from the given palette rows after an upload */
		static void NotifyPaletteTextureChanged(GuTexture* texture, std::int32_t firstRow, std::int32_t rowCount);

		/**
			@brief Allocates a block of video memory, or `nullptr` when none is left

			The 2 MB of embedded video memory is laid out by hand (there is no allocator behind
			`sceGeEdramGetAddr()`): the display buffers and the depth buffer sit at the front, and whatever
			follows them is handed out through this first-fit allocator to the surfaces that are worth
			keeping there - render targets. Sprite atlases deliberately stay in main memory, which the GE
			samples too.
		*/
		static void* AllocateVram(std::size_t size);
		/** @brief Returns a block obtained from @ref AllocateVram() */
		static void FreeVram(void* ptr);

		/** @brief Queues the CPU lightmap/water combine for the next `Combine` draw (the direct-tier lighting contract) */
		static void SetPendingSoftwareLighting(const float* lightmap, std::int32_t lmW, std::int32_t lmH, std::int32_t scale,
			std::int32_t vpX, std::int32_t vpY, std::int32_t vpW, std::int32_t vpH, float ambR, float ambG, float ambB,
			bool waterActive = false, float waterLevelPx = 0.0f, float waterTime = 0.0f, float waterCamY = 0.0f);
		/** @brief Drops any lighting entries not consumed this frame (called by the window backend at present) */
		static void EndFrame();

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;

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

		static BlendingState _blending;
		static DepthTestState _depthTest;
		static CullFaceState _cullFace;
		static ScissorState _scissor;
		static Recti _viewport;
		static Colorf _clearColor;

		static GuShaderProgram* _currentProgram;
		static const GuTexture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static GuRenderTarget* _currentRenderTarget;

		static bool _guInitialized;
		static bool _listOpen;
		static std::int32_t _logicalWidth;
		static std::int32_t _logicalHeight;
		static std::uint32_t _sceneCounter;

		static GuTexture* _paletteTexture;
		static std::uint32_t _paletteGeneration;

		static std::vector<PendingSoftwareLight> _pendingSoftwareLights;

		/** @brief Opens the frame's display list if it is not open yet (the draw paths and Clear() call this) */
		static void EnsureList();
		/** @brief Points the GE at the current draw surface (the flipped display buffer or a render target) */
		static void ApplyDrawTarget();
		/** @brief Programs the hardware scissor for the current target from the tracked engine state */
		static void ApplyScissor();
		/** @brief Maps the logical space onto the current target (scale of the 480x272 panel, or 1:1 for a target) */
		static void GetTargetScale(float& scaleX, float& scaleY);

		static void Dispatch(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices);
		// Draws a whole tile-layer mesh (a triangle list of position/texcoord/colour vertices)
		static void DispatchTileMesh(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices);
		// Draws a vertex-fed textured line strip (the weapon wheel) through the GE's native line primitive
		static void DispatchLineStrip(std::int32_t firstVertex, std::int32_t numVertices);
		static void ApplyPendingSoftwareLighting();
	};
}
