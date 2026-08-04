#pragma once

#include "../RhiTypes.h"
#include "../../../Primitives/Rect.h"
#include "../../../Primitives/Colorf.h"

#include <cstdint>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

// Direct3D 11 / DXGI COM interfaces referenced only as opaque pointers here, so <d3d11.h> stays out of the
// contract headers the whole pipeline pulls in through Rhi.h — it is included only by D3D11Device.cpp.
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11Buffer;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;
struct ID3D11RasterizerState;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;

namespace nCine::RHI::D3D11
{
	class D3D11ShaderProgram;
	class D3D11RenderTarget;
	class D3D11Texture;

	/**
		@brief Pipeline-state and draw-call facade of the Direct3D 11 backend (aliased as `RHI::Device`)

		Exposes the OpenGL device's surface (blending, depth, cull, scissor, viewport, clear and the draw
		calls) so the backend-neutral render pipeline drives it unchanged.

		The device also owns the real `ID3D11Device`, immediate `ID3D11DeviceContext` and the DXGI
		`IDXGISwapChain` created from the SDL window (via @ref CreateSwapchain(), called by the SDL window
		backend). @ref PresentFrame() blits the rendered scene into the back-buffer and presents it (the
		buffer-swap equivalent).
	*/
	class D3D11Device
	{
	public:
		D3D11Device() = delete;
		~D3D11Device() = delete;

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
		static void BindProgram(D3D11ShaderProgram* program);
		/** @brief Returns the currently bound shader program */
		static D3D11ShaderProgram* CurrentProgram();
		/** @brief Records the texture bound to a texture unit */
		static void BindTexture(std::uint32_t unit, const D3D11Texture* texture);
		/** @brief Clears a texture from every unit it is bound to (called from ~D3D11Texture to avoid a dangling pointer) */
		static void UnbindTexture(const D3D11Texture* texture);
		/** @brief Returns the texture bound to a texture unit */
		static const D3D11Texture* GetBoundTexture(std::uint32_t unit);
		/** @brief Records the host data range bound to a uniform binding point */
		static void BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size);
		/** @brief Clears a render target from the device if it is the current one (called from ~D3D11RenderTarget) */
		static void UnbindRenderTarget(const D3D11RenderTarget* renderTarget);
		/** @brief Scrubs a released render target view from the last-bound shadow so a recycled pointer can't be mistaken for still bound (called by D3D11RenderTarget whenever it releases an RTV) */
		static void OnRtvReleased(const ID3D11RenderTargetView* rtv);
		/** @brief Records the current draw render target (its bound color attachments receive the pixels) */
		static void SetRenderTarget(D3D11RenderTarget* renderTarget);
		/** @brief Clears a destroyed program from the device's current-program and shadow-state tracking (called from ~D3D11ShaderProgram) */
		static void OnProgramDestroyed(const D3D11ShaderProgram* program);

		// -- Direct3D 11 device / swap-chain lifecycle (called by the window backend) --

		/**
			@brief Creates the D3D11 device, immediate context and DXGI swap chain for the given native window

			Two window kinds are supported, selected at compile time by @ref DEATH_TARGET_WINDOWS_RT:
			 - Desktop (SDL2): @p windowHandle is a native `HWND`; a windowed bitblt swap chain is created via
			   `D3D11CreateDeviceAndSwapChain`.
			 - UWP (Windows Store / Xbox): @p windowHandle is the `CoreWindow` as an opaque `IUnknown*`; a
			   flip-model swap chain is created via `IDXGIFactory2::CreateSwapChainForCoreWindow` (UWP forbids
			   the desktop bitblt swap chain).

			@param windowHandle  Native `HWND` (desktop) or `CoreWindow` `IUnknown*` (UWP), passed as a `void*`
			                     so the window backend does not need the D3D11/DXGI headers
			@param width         Back-buffer width in pixels
			@param height        Back-buffer height in pixels
			@param vsync         Whether @ref PresentFrame() presents with vertical sync
			@returns `true` if the device and swap chain were created
		*/
		static bool CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync);
		/** @brief Releases the swap chain, context and device */
		static void DestroySwapchain();
		/** @brief Resizes the swap-chain back-buffer (releases and recreates the back-buffer RTV around `ResizeBuffers`) */
		static void ResizeSwapchain(std::int32_t width, std::int32_t height);
		/** @brief Blits the rendered scene into the back-buffer and presents it (the buffer-swap equivalent) */
		static void PresentFrame();

		/**
			@brief Creates an additional swap chain for a secondary window

			Used by the ImGui multi-viewport support to render into the platform windows it spawns when a panel
			is dragged out of the main window. Unlike the main swap chain, a secondary one is rendered into and
			presented directly - there is no intermediate present texture, and hence no flip-blit - so
			@ref BeginSecondaryFrame() also suppresses the projection Y flip for its draws.

			@param windowHandle  Native `HWND` of the secondary window, passed as a `void*`
			@param width         Back-buffer width in pixels
			@param height        Back-buffer height in pixels
			@returns An opaque handle to pass to the other `*Secondary*` functions, or `nullptr` on failure
		*/
		static void* CreateSecondarySwapchain(void* windowHandle, std::int32_t width, std::int32_t height);
		/** @brief Releases a secondary swap chain created by @ref CreateSecondarySwapchain() */
		static void DestroySecondarySwapchain(void* handle);
		/** @brief Resizes a secondary swap chain's back-buffer */
		static void ResizeSecondarySwapchain(void* handle, std::int32_t width, std::int32_t height);
		/** @brief Directs the following draws (and clears) into a secondary swap chain's back-buffer, optionally clearing it first */
		static void BeginSecondaryFrame(void* handle, bool clear);
		/** @brief Ends the redirection started by @ref BeginSecondaryFrame() (the next draw targets the main window again) */
		static void EndSecondaryFrame();
		/** @brief Presents a secondary swap chain */
		static void PresentSecondaryFrame(void* handle);

		/** @brief Returns the D3D11 device (for resource creation), or `nullptr` before creation */
		static ID3D11Device* GetD3DDevice();
		/** @brief Returns the D3D11 immediate context, or `nullptr` before creation */
		static ID3D11DeviceContext* GetD3DContext();
		/** @brief Returns the largest supported 2D texture dimension of the obtained feature level (16384 on 11_0, 8192 on 10.x; consumed by D3D11RhiCapabilities) */
		static std::int32_t GetMaxTextureDimension();

	private:
		static constexpr std::uint32_t MaxTextureUnits = 8;
		static constexpr std::uint32_t MaxUniformBindings = 8;
		// Simultaneously bound color render targets (matches D3D11RenderTarget::MaxColorAttachments and
		// D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; asserted in BindCurrentRenderTarget)
		static constexpr std::uint32_t MaxRenderTargets = 8;

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

		static D3D11ShaderProgram* _currentProgram;
		static const D3D11Texture* _boundTextures[MaxTextureUnits];
		static UniformRange _boundUniformRanges[MaxUniformBindings];
		static D3D11RenderTarget* _currentRenderTarget;

		// Real Direct3D 11 objects (owned)
		static ID3D11Device* _device;
		static ID3D11DeviceContext* _context;
		static IDXGISwapChain* _swapchain;
		static ID3D11RenderTargetView* _backbufferRtv;
		static bool _vsync;
		// DXGI_SWAP_CHAIN_FLAGs the swap chain was actually created with (the presentation model is negotiated
		// with fallbacks, see CreateSwapchain): ResizeBuffers() must repeat them, and tearing may only be
		// requested at present time when the chain carries DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
		static std::uint32_t _swapchainFlags;
		static std::int32_t _backbufferWidth;
		static std::int32_t _backbufferHeight;
		// Largest 2D texture dimension of the obtained feature level (set by CreateSwapchain)
		static std::int32_t _maxTextureDimension;

		// The engine renders in the OpenGL convention. The D3D backend replays that faithfully: every draw's
		// clip-space Y is flipped (projection matrix, see BindConstantBuffers) so all targets - the back-buffer and
		// every off-screen render target - are stored bottom-up exactly like GL, keeping the scene composite and the
		// direct-drawn HUD/menu consistent regardless of how many off-screen round-trips a path makes. "Screen"
		// (no render target) is drawn into this intermediate texture and PresentFrame() flip-blits it into the DXGI
		// back-buffer (the single GL bottom-up -> D3D top-down scan-out correction, the software backend's
		// SDL_FLIP_VERTICAL equivalent). A negative-height viewport, the usual remedy, is ignored by the runtime here.
		static ID3D11Texture2D* _presentTexture;
		static ID3D11RenderTargetView* _presentRtv;
		static ID3D11ShaderResourceView* _presentSrv;

		// Set while BeginSecondaryFrame() redirects drawing into a secondary swap chain (an ImGui platform
		// window): its back-buffer view stands in for the screen target, and because it is presented directly -
		// without the intermediate texture and its flip-blit - that surface is the one place the backend renders
		// TOP-DOWN. Both exceptions the orientation implies are keyed on these: no projection Y flip
		// (BindConstantBuffers) and a flipped scissor rectangle (ApplyRenderState, hence the height).
		static ID3D11RenderTargetView* _secondaryTargetRtv;
		static std::int32_t _secondaryTargetHeight;
		static ID3D11VertexShader* _presentVs;
		static ID3D11PixelShader* _presentPs;
		static ID3D11SamplerState* _presentSampler;

		// Dynamic constant-buffer pool: one reusable DYNAMIC buffer per pool slot, updated with WRITE_DISCARD
		// when its bytes actually change (the driver versions it so previously recorded draws keep their data).
		// ContentHash/ContentSize cache the bytes last written to the buffer so a draw whose cbuffer is
		// byte-identical to the pool slot's current contents (e.g. the _Globals/projection block, unchanged
		// across every draw of a pass) skips the Map/Unmap entirely and only re-binds.
		struct PooledCBuffer
		{
			std::uint32_t Size = 0;			// backing buffer capacity (16-aligned), grow-only
			ID3D11Buffer* Buffer = nullptr;
			std::uint64_t ContentHash = 0;	// xxHash3 of the bytes last uploaded to Buffer
			std::uint32_t ContentSize = 0;	// number of those bytes (0 = buffer just (re)created, cache invalid)
		};
		static SmallVector<PooledCBuffer, 0> _cbufferPool;
		// Reusable scratch for gathering a _Globals cbuffer's scattered loose uniforms into one contiguous
		// block so it can be hashed and uploaded in a single copy
		static SmallVector<std::uint8_t, 0> _cbufferStaging;

		// Cached pipeline-state objects, created on demand from the recorded blend/cull/scissor state
		struct BlendStateEntry
		{
			std::uint64_t Key;
			ID3D11BlendState* State;
		};
		static SmallVector<BlendStateEntry, 8> _blendStates;
		struct RasterStateEntry
		{
			std::uint32_t Key;
			ID3D11RasterizerState* State;
		};
		static SmallVector<RasterStateEntry, 8> _rasterStates;
		static ID3D11DepthStencilState* _depthDisabledState;

		// Last-applied context state (redundant-bind elimination). DrawCommon re-applies everything each draw
		// and D3D11 does not filter redundant calls itself, so these shadows skip the API call when the value
		// is unchanged. They are invalidated whenever something else touches the context (the present blit,
		// resize, device creation) and scrubbed when an object they point to dies (texture SRVs, render-target
		// RTVs, program shaders), so a recycled pointer can never be mistaken for the one still bound.
		struct CachedRect
		{
			std::int32_t L = 0, T = 0, R = 0, B = 0;
		};
		static ID3D11BlendState* _lastBlendState;
		static ID3D11RasterizerState* _lastRasterState;
		static bool _depthStateApplied;
		static ID3D11VertexShader* _lastVs;
		static ID3D11PixelShader* _lastPs;
		static std::uint32_t _lastTopology;			// D3D11_PRIMITIVE_TOPOLOGY value; 0 (UNDEFINED) = unknown
		// The RTV set last passed to OMSetRenderTargets (all bound color attachments, in slot order)
		static ID3D11RenderTargetView* _lastRtvs[MaxRenderTargets];
		static std::uint32_t _lastRtvCount;
		static bool _lastRtvValid;
		static Recti _lastViewport;
		static bool _lastViewportValid;
		static CachedRect _lastScissorRect;
		static bool _lastScissorValid;
		static ID3D11ShaderResourceView* _lastSrvs[2][MaxTextureUnits];		// [0] = PS stage, [1] = VS stage
		static ID3D11SamplerState* _lastSamplers[2][MaxTextureUnits];
		static bool _srvShadowValid;

		/** @brief Forgets all last-applied context state so the next draw re-issues every binding */
		static void InvalidateCachedState();

		/** @brief Returns the view standing in for the "screen" target: the present texture, or a secondary swap chain's back-buffer while one is bound */
		static ID3D11RenderTargetView* ScreenRtv();

		/** @brief (Re)creates @ref _backbufferRtv from the current swap-chain back-buffer and binds it */
		static bool CreateBackbufferRtv();
		/** @brief (Re)creates the intermediate present texture (RTV+SRV) and the flip-blit shaders/sampler at @p width x @p height */
		static bool CreatePresentResources(std::int32_t width, std::int32_t height);
		/** @brief Releases the intermediate present texture and its views (keeps the blit shaders/sampler) */
		static void ReleasePresentResources();
		/** @brief Returns the pool slot index of a reusable dynamic constant buffer at least @p size bytes (16-aligned); resets that slot's content cache if the buffer had to be (re)created */
		static std::uint32_t AcquireConstantBuffer(std::uint32_t size);
		/** @brief Builds each stage's constant buffers from the current program + bound ranges and binds them */
		static void BindConstantBuffers();
		/** @brief Binds the bound textures' SRVs and samplers to the VS and PS stages */
		static void BindTextures();
		/** @brief Applies the recorded blend/depth/rasterizer/scissor state to the context */
		static void ApplyRenderState();
		/** @brief Binds the currently bound render target (or the back-buffer) as the output */
		static void BindCurrentRenderTarget();
		/** @brief Shared body of the draw calls: binds program/state/resources then issues the draw */
		static void DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
			bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex);
		/** @brief Releases the pooled constant buffers and cached state objects */
		static void ReleasePipelineObjects();
	};
}
