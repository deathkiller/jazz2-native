#if defined(WITH_RHI_GX2)

#include "Gx2Device.h"
#include "Gx2RenderTarget.h"
#include "Gx2Texture.h"
#include "Gx2ShaderProgram.h"
#include "Gx2BufferObject.h"

#include "../../../../Main.h"

#include <cstring>
#include <malloc.h>

#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/clear.h>
#include <gx2/display.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/swap.h>

namespace nCine::RHI::GX2
{
	namespace
	{
		/**
			@brief Copies @p size bytes of 32-bit words, swapping each to little endian

			The GPU reads uniform blocks without the byte swap it applies to vertex attributes, so a block
			has to hold little-endian words, while the engine fills its uniform buffers in the CPU's big-endian
			order. Every uniform member is a 32-bit scalar or a vector of them, so a word-wise swap is exact.
		*/
		void CopyWordsSwapped(std::uint8_t* dst, const std::uint8_t* src, std::uint32_t size)
		{
			std::uint32_t* d = reinterpret_cast<std::uint32_t*>(dst);
			const std::uint32_t* s = reinterpret_cast<const std::uint32_t*>(src);
			for (std::uint32_t i = 0; i < size / 4; i++) {
				d[i] = __builtin_bswap32(s[i]);
			}
		}

		GX2PrimitiveMode TranslatePrimitive(PrimitiveType primitive)
		{
			switch (primitive) {
				case PrimitiveType::Points: return GX2_PRIMITIVE_MODE_POINTS;
				case PrimitiveType::Lines: return GX2_PRIMITIVE_MODE_LINES;
				case PrimitiveType::LineLoop: return GX2_PRIMITIVE_MODE_LINE_LOOP;
				case PrimitiveType::LineStrip: return GX2_PRIMITIVE_MODE_LINE_STRIP;
				case PrimitiveType::Triangles: return GX2_PRIMITIVE_MODE_TRIANGLES;
				case PrimitiveType::TriangleStrip: return GX2_PRIMITIVE_MODE_TRIANGLE_STRIP;
				case PrimitiveType::TriangleFan: return GX2_PRIMITIVE_MODE_TRIANGLE_FAN;
				default: return GX2_PRIMITIVE_MODE_TRIANGLES;
			}
		}

		GX2BlendMode TranslateBlendFactor(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero: return GX2_BLEND_MODE_ZERO;
				case nCine::BlendingFactor::One: return GX2_BLEND_MODE_ONE;
				case nCine::BlendingFactor::SrcColor: return GX2_BLEND_MODE_SRC_COLOR;
				case nCine::BlendingFactor::OneMinusSrcColor: return GX2_BLEND_MODE_INV_SRC_COLOR;
				case nCine::BlendingFactor::SrcAlpha: return GX2_BLEND_MODE_SRC_ALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha: return GX2_BLEND_MODE_INV_SRC_ALPHA;
				case nCine::BlendingFactor::DstAlpha: return GX2_BLEND_MODE_DST_ALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha: return GX2_BLEND_MODE_INV_DST_ALPHA;
				case nCine::BlendingFactor::DstColor: return GX2_BLEND_MODE_DST_COLOR;
				case nCine::BlendingFactor::OneMinusDstColor: return GX2_BLEND_MODE_INV_DST_COLOR;
				case nCine::BlendingFactor::SrcAlphaSaturate: return GX2_BLEND_MODE_SRC_ALPHA_SAT;
				case nCine::BlendingFactor::ConstantColor: return GX2_BLEND_MODE_BLEND_FACTOR;
				case nCine::BlendingFactor::OneMinusConstantColor: return GX2_BLEND_MODE_INV_BLEND_FACTOR;
				case nCine::BlendingFactor::ConstantAlpha: return GX2_BLEND_MODE_CONSTANT_ALPHA;
				case nCine::BlendingFactor::OneMinusConstantAlpha: return GX2_BLEND_MODE_INV_CONSTANT_ALPHA;
				default: return GX2_BLEND_MODE_ONE;
			}
		}
	}

	Gx2Device::BlendingState Gx2Device::_blending;
	Gx2Device::DepthTestState Gx2Device::_depthTest;
	Gx2Device::CullFaceState Gx2Device::_cullFace;
	Gx2Device::ScissorState Gx2Device::_scissor;
	Recti Gx2Device::_viewport;
	Colorf Gx2Device::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	Gx2ShaderProgram* Gx2Device::_currentProgram = nullptr;
	const Gx2Texture* Gx2Device::_boundTextures[Gx2Device::MaxTextureUnits] = {};
	Gx2Device::UniformRange Gx2Device::_boundUniformRanges[Gx2Device::MaxUniformBindings] = {};
	Gx2RenderTarget* Gx2Device::_currentRenderTarget = nullptr;

	GX2ContextState* Gx2Device::_contextState = nullptr;
	GX2ColorBuffer Gx2Device::_screenBuffer;
	Gx2Memory::Block Gx2Device::_screenMemory;
	std::int32_t Gx2Device::_screenWidth = 1280;
	std::int32_t Gx2Device::_screenHeight = 720;

	bool Gx2Device::_initialized = false;
	bool Gx2Device::_inForeground = true;
	bool Gx2Device::_vsync = true;
	bool Gx2Device::_firstFrame = true;
	std::uint32_t Gx2Device::_swapToken = 0;
	std::uint32_t Gx2Device::_frameCounter = 0;
	bool Gx2Device::_targetDirty = true;
	Gx2Device::AppliedTarget Gx2Device::_applied = {};

	Gx2Device::RetiredBlock Gx2Device::_retiredBlocks[Gx2Device::RetiredBlockCount];

	Gx2Memory::Block Gx2Device::_uniformRing;
	std::uint32_t Gx2Device::_uniformChunk = 0;
	std::uint32_t Gx2Device::_uniformChunkOffset = 0;
	std::uint32_t Gx2Device::_uniformChunkTokens[Gx2Device::UniformRingChunkCount] = {};
	std::uint32_t Gx2Device::_openBatch = 1;
	OSTime Gx2Device::_batchStamps[Gx2Device::BatchStampCount] = {};
	const Gx2ShaderProgram* Gx2Device::_lastProgram = nullptr;

	// ── Session ──────────────────────────────────────────────────────────────────────────────────

	bool Gx2Device::CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync)
	{
		static_cast<void>(windowHandle);
		if (_initialized) {
			return true;
		}

		_vsync = vsync;
		if (width > 0 && height > 0) {
			_screenWidth = width;
			_screenHeight = height;
		}

		// Shadowing on (the TRUE): every register write made while this state is current is recorded in it,
		// so GX2SetContextState() restores the whole pipeline state after an operation that clobbers it -
		// the clears and the scan-buffer copies all do
		_contextState = static_cast<GX2ContextState*>(::memalign(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState)));
		if (_contextState == nullptr) {
			LOGE("Failed to allocate the GX2 context state");
			return false;
		}
		GX2SetupContextStateEx(_contextState, TRUE);
		GX2SetContextState(_contextState);

		// Every program CafeGLSL generates for this engine uses uniform blocks (see `--emit-gx2` in Docs/ShaderCompiler.dox)
		GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);

		std::memset(&_screenBuffer, 0, sizeof(_screenBuffer));
		GX2Surface& surface = _screenBuffer.surface;
		surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
		surface.width = std::uint32_t(_screenWidth);
		surface.height = std::uint32_t(_screenHeight);
		surface.depth = 1;
		surface.mipLevels = 1;
		surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
		surface.aa = GX2_AA_MODE1X;
		surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
		surface.tileMode = GX2_TILE_MODE_DEFAULT;
		GX2CalcSurfaceSizeAndAlignment(&surface);
		AllocScreenMemory();
		if (!_screenMemory.IsValid() && (_screenWidth > 1280 || _screenHeight > 720)) {
			// A 1080p buffer that fits neither in MEM1 nor in MEM2 falls back to 720p, the drawable size follows it
			// (see SdlGfxDevice::QueryDrawableSize())
			LOGW("Failed to allocate the {}x{} screen colour buffer ({} bytes), falling back to 1280x720", _screenWidth, _screenHeight, surface.imageSize);
			_screenWidth = 1280;
			_screenHeight = 720;
			surface.width = std::uint32_t(_screenWidth);
			surface.height = std::uint32_t(_screenHeight);
			GX2CalcSurfaceSizeAndAlignment(&surface);
			AllocScreenMemory();
		}
		if (!_screenMemory.IsValid()) {
			LOGE("Failed to allocate the {}x{} screen colour buffer ({} bytes)", _screenWidth, _screenHeight, surface.imageSize);
			std::free(_contextState);
			_contextState = nullptr;
			return false;
		}
		surface.image = _screenMemory.Base;
		_screenBuffer.viewNumSlices = 1;
		GX2InitColorBufferRegs(&_screenBuffer);

		_uniformRing = Gx2Memory::Alloc(UniformRingSize, GX2_UNIFORM_BLOCK_ALIGNMENT);
		if (!_uniformRing.IsValid()) {
			LOGE("Failed to allocate the uniform ring ({} bytes)", UniformRingSize);
			Gx2Memory::Free(_screenMemory);
			std::free(_contextState);
			_contextState = nullptr;
			return false;
		}
		_uniformChunk = 0;
		_uniformChunkOffset = 0;
		for (std::uint32_t i = 0; i < UniformRingChunkCount; i++) {
			_uniformChunkTokens[i] = 0;
		}
		_lastProgram = nullptr;

		GX2SetSwapInterval(_vsync ? 1 : 0);

		_initialized = true;
		_inForeground = true;
		_firstFrame = true;
		_swapToken = 0;
		InvalidateTarget();
		_viewport = Recti(0, 0, _screenWidth, _screenHeight);
		SetupInitialState();

		LOGI("GX2 device initialized with a {}x{} screen buffer", _screenWidth, _screenHeight);
		return true;
	}

	void Gx2Device::DestroySwapchain()
	{
		if (!_initialized) {
			return;
		}
		Finish();
		ReleaseRetiredBlocks(true);
		Gx2Texture::ReleaseMem1Copies();
		Gx2Memory::Free(_uniformRing);
		Gx2Memory::Free(_screenMemory);
		std::memset(&_screenBuffer, 0, sizeof(_screenBuffer));
		std::free(_contextState);
		_contextState = nullptr;
		_initialized = false;
	}

	void Gx2Device::ResizeSwapchain(std::int32_t width, std::int32_t height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void Gx2Device::EnterBackground()
	{
		if (!_initialized || !_inForeground) {
			return;
		}
		// SDL releases the foreground (and frees its scan buffers and its MEM1 heap) on its next event pump, so the
		// GPU has to be done with everything already queued before that happens
		GX2DrawDone();
		// Every block in MEM1 goes now, while SDL's heap still exists: retired ones, render targets (allocated again
		// when next drawn into, their contents are redrawn every frame) and the screen buffer (in EnterForeground())
		ReleaseRetiredBlocks(true);
		Gx2Texture::ReleaseMem1Copies();
		if (_screenMemory.IsMem1()) {
			Gx2Memory::Free(_screenMemory);
			_screenBuffer.surface.image = nullptr;
		}
		_inForeground = false;
		LOGI("GX2 device entering background");
	}

	void Gx2Device::EnterForeground()
	{
		if (!_initialized || _inForeground) {
			return;
		}
		// SDL has reinitialised its own side (scan buffers, MEM1 heap); what lives in MEM2 survived, the screen
		// buffer is allocated again if it was in MEM1 and the render targets follow on their next use
		RestoreContextState();
		_inForeground = true;
		if (!_screenMemory.IsValid()) {
			if (!AllocScreenMemory()) {
				// Nothing to draw into; draws and presents are skipped until the device is recreated
				LOGE("Failed to allocate the {}x{} screen colour buffer again", _screenWidth, _screenHeight);
				_inForeground = false;
				return;
			}
			_screenBuffer.surface.image = _screenMemory.Base;
			GX2InitColorBufferRegs(&_screenBuffer);
		}
		_firstFrame = true;
		_swapToken = 0;
		_targetDirty = true;
		LOGI("GX2 device returning to foreground");
	}

	void Gx2Device::RestoreContextState()
	{
		GX2SetContextState(_contextState);
		// The shader mode is a config register, which the context state does not shadow, and the clears and
		// scan-buffer copies run GX2's own register-mode shaders. Left alone, the next draw would read its
		// uniforms from the (empty) constant registers instead of the uniform blocks.
		GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
		_lastProgram = nullptr;
		// The shadowed state should bring back the colour buffer, viewport and scissor of before, but a clear is
		// rare enough to simply program them again rather than rely on it
		InvalidateTarget();
	}

	std::int32_t Gx2Device::GetScreenWidth()
	{
		return _screenWidth;
	}

	std::int32_t Gx2Device::GetScreenHeight()
	{
		return _screenHeight;
	}

	std::int32_t Gx2Device::GetDisplayWidth()
	{
		return _screenWidth;
	}

	std::int32_t Gx2Device::GetDisplayHeight()
	{
		return _screenHeight;
	}

	std::uint32_t Gx2Device::GetFrameCounter()
	{
		return _frameCounter;
	}

	void Gx2Device::Finish()
	{
		if (_initialized) {
			GX2DrawDone();
		}
	}

	bool Gx2Device::AllocScreenMemory()
	{
		// MEM1 first: the screen buffer is drawn into by the last pass and read by both scan-buffer copies
		const GX2Surface& surface = _screenBuffer.surface;
		_screenMemory = Gx2Memory::AllocColorSurfaceMem1(surface);
		if (!_screenMemory.IsValid()) {
			_screenMemory = Gx2Memory::Alloc(surface.imageSize, surface.alignment);
		}
		return _screenMemory.IsValid();
	}

	void Gx2Device::RetireBlock(Gx2Memory::Block& block)
	{
		if (!block.IsValid()) {
			return;
		}
		if (!_initialized) {
			// Nothing can be in flight without a session
			Gx2Memory::Free(block);
			return;
		}

		for (std::uint32_t i = 0; i < RetiredBlockCount; i++) {
			if (!_retiredBlocks[i].Block.IsValid()) {
				_retiredBlocks[i].Block = block;
				// Every command that can reference the block is in the open batch or an earlier one
				_retiredBlocks[i].Token = _openBatch;
				block = Gx2Memory::Block{};
				return;
			}
		}

		// Out of slots: wait for the GPU, after which every retired block is safe to release
		Finish();
		ReleaseRetiredBlocks(true);
		_retiredBlocks[0].Block = block;
		_retiredBlocks[0].Token = _openBatch;
		block = Gx2Memory::Block{};
	}

	void Gx2Device::ReleaseRetiredBlocks(bool force)
	{
		for (std::uint32_t i = 0; i < RetiredBlockCount; i++) {
			RetiredBlock& retired = _retiredBlocks[i];
			if (!retired.Block.IsValid()) {
				continue;
			}
			if (!force && !IsUseRetired(retired.Token)) {
				continue;
			}
			Gx2Memory::Free(retired.Block);
		}
	}

	// ── State ────────────────────────────────────────────────────────────────────────────────────

	void Gx2Device::SetupInitialState()
	{
		_blending = BlendingState{};
		_depthTest = DepthTestState{};
		_cullFace = CullFaceState{};
		if (!CanSubmit()) {
			return;
		}

		// No depth buffer exists on this backend (the engine never enables the depth test), so the depth
		// unit is off for good
		GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_LEQUAL);
		GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
		GX2SetTargetChannelMasks(GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA,
			GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA, GX2_CHANNEL_MASK_RGBA);
		ApplyBlending();
		ApplyCulling();
		InvalidateTarget();
	}

	void Gx2Device::ApplyBlending()
	{
		if (!CanSubmit()) {
			return;
		}
		GX2SetColorControl(GX2_LOGIC_OP_COPY, _blending.Enabled ? 0x01 : 0x00, FALSE, TRUE);
		GX2SetBlendControl(GX2_RENDER_TARGET_0,
			TranslateBlendFactor(_blending.SrcRgb), TranslateBlendFactor(_blending.DstRgb), GX2_BLEND_COMBINE_MODE_ADD,
			TRUE, TranslateBlendFactor(_blending.SrcAlpha), TranslateBlendFactor(_blending.DstAlpha), GX2_BLEND_COMBINE_MODE_ADD);
	}

	void Gx2Device::ApplyCulling()
	{
		if (!CanSubmit()) {
			return;
		}
		const bool cullFront = _cullFace.Enabled && (_cullFace.Mode == CullFaceMode::Front || _cullFace.Mode == CullFaceMode::FrontAndBack);
		const bool cullBack = _cullFace.Enabled && (_cullFace.Mode == CullFaceMode::Back || _cullFace.Mode == CullFaceMode::FrontAndBack);
		GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, cullFront ? TRUE : FALSE, cullBack ? TRUE : FALSE);
	}

	void Gx2Device::SetBlendingEnabled(bool enabled)
	{
		if (_blending.Enabled == enabled) {
			return;
		}
		_blending.Enabled = enabled;
		ApplyBlending();
	}

	void Gx2Device::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		if (_blending.SrcRgb == srcRgb && _blending.DstRgb == dstRgb && _blending.SrcAlpha == srcAlpha && _blending.DstAlpha == dstAlpha) {
			return;
		}
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
		ApplyBlending();
	}

	Gx2Device::BlendingState Gx2Device::GetBlendingState()
	{
		return _blending;
	}

	void Gx2Device::SetBlendingState(const BlendingState& state)
	{
		_blending = state;
		ApplyBlending();
	}

	void Gx2Device::SetDepthTestEnabled(bool enabled)
	{
		// Tracked for the pipeline's save/restore only, see SetupInitialState()
		_depthTest.TestEnabled = enabled;
	}

	void Gx2Device::SetDepthMaskEnabled(bool enabled)
	{
		_depthTest.MaskEnabled = enabled;
	}

	Gx2Device::DepthTestState Gx2Device::GetDepthTestState()
	{
		return _depthTest;
	}

	void Gx2Device::SetDepthTestState(const DepthTestState& state)
	{
		_depthTest = state;
	}

	void Gx2Device::SetCullFaceEnabled(bool enabled)
	{
		if (_cullFace.Enabled == enabled) {
			return;
		}
		_cullFace.Enabled = enabled;
		ApplyCulling();
	}

	Gx2Device::CullFaceState Gx2Device::GetCullFaceState()
	{
		return _cullFace;
	}

	void Gx2Device::SetCullFaceState(const CullFaceState& state)
	{
		_cullFace = state;
		ApplyCulling();
	}

	Gx2Device::ScissorState Gx2Device::GetScissorState()
	{
		return _scissor;
	}

	void Gx2Device::SetScissorState(const ScissorState& state)
	{
		_scissor = state;
		_targetDirty = true;
	}

	void Gx2Device::SetScissor(const Recti& rect)
	{
		_scissor.Enabled = true;
		_scissor.Rect = rect;
		_targetDirty = true;
	}

	void Gx2Device::SetScissorTestEnabled(bool enabled)
	{
		_scissor.Enabled = enabled;
		_targetDirty = true;
	}

	Recti Gx2Device::GetViewport()
	{
		return _viewport;
	}

	void Gx2Device::SetViewport(const Recti& rect)
	{
		_viewport = rect;
		_targetDirty = true;
	}

	void Gx2Device::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
		_targetDirty = true;
	}

	Colorf Gx2Device::GetClearColor()
	{
		return _clearColor;
	}

	void Gx2Device::SetClearColor(const Colorf& color)
	{
		_clearColor = color;
	}

	void Gx2Device::Clear(ClearFlags flags)
	{
		if (!CanSubmit() || (flags & ClearFlags::Color) != ClearFlags::Color) {
			// Depth and stencil have no storage on this backend
			return;
		}

		std::int32_t width, height;
		const GX2ColorBuffer* colorBuffer = GetCurrentColorBuffer(width, height);
		if (colorBuffer == nullptr) {
			return;
		}
		// A fast clear of the whole buffer, which is what the pipeline asks for (it clears a target before
		// drawing into it). GX2ClearColor() takes a non-const buffer but only reads it.
		GX2ClearColor(const_cast<GX2ColorBuffer*>(colorBuffer), _clearColor.R, _clearColor.G, _clearColor.B, _clearColor.A);
		// The clear clobbers the pipeline state; the shadowed context state puts it all back, including the
		// colour buffer, viewport and scissor that were bound before
		RestoreContextState();
	}

	// ── Target and Viewport ──────────────────────────────────────────────────────────────────────

	const GX2ColorBuffer* Gx2Device::GetCurrentColorBuffer(std::int32_t& width, std::int32_t& height)
	{
		if (_currentRenderTarget != nullptr) {
			return _currentRenderTarget->GetColorBuffer(width, height);
		}
		width = _screenWidth;
		height = _screenHeight;
		return &_screenBuffer;
	}

	bool Gx2Device::ApplyTarget()
	{
		if (!_targetDirty || !CanSubmit()) {
			return true;
		}
		std::int32_t width, height;
		const GX2ColorBuffer* colorBuffer = GetCurrentColorBuffer(width, height);
		if (colorBuffer == nullptr) {
			return false;
		}
		// The buffer's registers are read when it is set, so a target whose surface was rebuilt in place (same
		// object, new image or size) counts as a different one
		const void* image = colorBuffer->surface.image;
		if (_applied.ColorBuffer == nullptr || _applied.ColorBuffer != colorBuffer || _applied.Image != image ||
			_applied.Width != width || _applied.Height != height) {
			GX2SetColorBuffer(colorBuffer, GX2_RENDER_TARGET_0);
			_applied.ColorBuffer = colorBuffer;
			_applied.Image = image;
			_applied.Width = width;
			_applied.Height = height;
			// Viewport and scissor are sent again for a new buffer, whatever their values
			_applied.Viewport = Recti(-1, -1, -1, -1);
			_applied.Scissor = Recti(-1, -1, -1, -1);
		}
		ApplyViewportAndScissor(width, height);
		_targetDirty = false;
		return true;
	}

	void Gx2Device::ApplyViewportAndScissor(std::int32_t targetWidth, std::int32_t targetHeight)
	{
		// The engine's rectangles count from the top-left corner, and so do GX2's window coordinates; clip +Y
		// is the top row, as everywhere in the RHI (see RhiFwd.h)
		if (_viewport != _applied.Viewport) {
			GX2SetViewport(float(_viewport.X), float(_viewport.Y), float(_viewport.W), float(_viewport.H), 0.0f, 1.0f);
			_applied.Viewport = _viewport;
		}

		std::int32_t sx = 0, sy = 0, sw = targetWidth, sh = targetHeight;
		if (_scissor.Enabled) {
			// Clamped into the target: a clip rectangle may hang off the edge of what it clips (the menu's
			// does), and the hardware's fields are unsigned
			sx = _scissor.Rect.X;
			sy = _scissor.Rect.Y;
			sw = _scissor.Rect.W;
			sh = _scissor.Rect.H;
			if (sx < 0) { sw += sx; sx = 0; }
			if (sy < 0) { sh += sy; sy = 0; }
			if (sw > targetWidth - sx) { sw = targetWidth - sx; }
			if (sh > targetHeight - sy) { sh = targetHeight - sy; }
			if (sw < 0) { sw = 0; }
			if (sh < 0) { sh = 0; }
		}
		const Recti scissor(sx, sy, sw, sh);
		if (scissor != _applied.Scissor) {
			GX2SetScissor(std::uint32_t(sx), std::uint32_t(sy), std::uint32_t(sw), std::uint32_t(sh));
			_applied.Scissor = scissor;
		}
	}

	void Gx2Device::SetRenderTarget(Gx2RenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			return;
		}
		_currentRenderTarget = renderTarget;
		_targetDirty = true;
		// Bound right away rather than at the next draw, because a clear may come first
		ApplyTarget();
	}

	void Gx2Device::UnbindRenderTarget(const Gx2RenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
			_targetDirty = true;
		}
	}

	// ── Resource Binding ─────────────────────────────────────────────────────────────────────────

	void Gx2Device::BindProgram(Gx2ShaderProgram* program)
	{
		_currentProgram = program;
	}

	Gx2ShaderProgram* Gx2Device::CurrentProgram()
	{
		return _currentProgram;
	}

	void Gx2Device::OnProgramDestroyed(const Gx2ShaderProgram* program)
	{
		if (_currentProgram == program) {
			_currentProgram = nullptr;
		}
		if (_lastProgram == program) {
			_lastProgram = nullptr;
		}
	}

	void Gx2Device::OnProgramBinariesReleased(const Gx2ShaderProgram* program)
	{
		// The program stays current (a relink keeps using the same object), only the shader binds are redone
		if (_lastProgram == program) {
			_lastProgram = nullptr;
		}
	}

	void Gx2Device::BindTexture(std::uint32_t unit, const Gx2Texture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void Gx2Device::UnbindTexture(const Gx2Texture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
	}

	const Gx2Texture* Gx2Device::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void Gx2Device::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void Gx2Device::GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size)
	{
		if (index < MaxUniformBindings) {
			data = _boundUniformRanges[index].Data;
			size = _boundUniformRanges[index].Size;
		} else {
			data = nullptr;
			size = 0;
		}
	}

	// ── Fences ───────────────────────────────────────────────────────────────────────────────────

	// A batch is everything recorded between two FlushBatch() calls. Its token is a 32-bit sequence number (never
	// 0) compared with wrap-around arithmetic; the GX2 timestamp of its submission is remembered for the last
	// BatchStampCount batches, which is far more than can be in flight (one batch per frame plus the rare
	// flush for a wait). A token older than that is answered with the oldest remembered stamp, which is later
	// than its own and so only errs towards waiting.

	std::uint32_t Gx2Device::GetUseToken()
	{
		return _openBatch;
	}

	void Gx2Device::FlushBatch()
	{
		// Submits the commands recorded so far; an empty command buffer is not submitted, and then the last
		// submitted stamp still covers everything recorded before it
		GX2Flush();
		_batchStamps[_openBatch % BatchStampCount] = GX2GetLastSubmittedTimeStamp();
		_openBatch++;
		if (_openBatch == 0) {
			_openBatch = 1;
		}
	}

	OSTime Gx2Device::GetBatchStamp(std::uint32_t token)
	{
		const std::uint32_t age = _openBatch - token;
		if (age > BatchStampCount) {
			// The oldest remembered batch; its stamp is not earlier than this one's
			token = _openBatch - BatchStampCount;
		}
		return _batchStamps[token % BatchStampCount];
	}

	bool Gx2Device::IsUseRetired(std::uint32_t token)
	{
		if (token == 0 || !_initialized) {
			return true;
		}
		if (std::int32_t(token - _openBatch) >= 0) {
			// Still being recorded, the GPU has not even seen it
			return false;
		}
		return (GX2GetRetiredTimeStamp() >= GetBatchStamp(token));
	}

	bool Gx2Device::WaitForUse(std::uint32_t token)
	{
		if (token == 0 || !_initialized) {
			return true;
		}
		if (std::int32_t(token - _openBatch) >= 0) {
			FlushBatch();
		}
		const OSTime stamp = GetBatchStamp(token);
		if (GX2GetRetiredTimeStamp() >= stamp) {
			return true;
		}
		// Sleeps on GX2's timestamp event; it gives up (and returns false) only on a GPU timeout
		return (GX2WaitTimeStamp(stamp) != FALSE);
	}

	FenceHandle Gx2Device::InsertFence()
	{
		// The fence is the open batch: RenderBuffersManager::Remap() inserts it right after the frame's draws were
		// recorded, and the present submits them
		return reinterpret_cast<FenceHandle>(std::uintptr_t(_openBatch));
	}

	void Gx2Device::DeleteFence(FenceHandle& fence)
	{
		fence = FenceHandle{};
	}

	bool Gx2Device::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		const std::uint32_t token = std::uint32_t(reinterpret_cast<std::uintptr_t>(fence));
		if (timeoutNs == 0) {
			return IsUseRetired(token);
		}
		// GX2 has no timed wait; GX2WaitTimeStamp() returns false on its own GPU timeout instead
		return WaitForUse(token);
	}

	// ── Present ──────────────────────────────────────────────────────────────────────────────────

	void Gx2Device::PresentFrame()
	{
		if (!CanSubmit()) {
			if (_initialized) {
				// In the background nothing waits for a flip, so the main loop is throttled here instead
				OSSleepTicks(OSMillisecondsToTicks(16));
			}
			return;
		}

		// The frame is split in two submissions so the CPU can record the next frame while the GPU draws this one.
		// The draws go to the GPU first; only the scan copies have to wait. The scan buffers are double-buffered
		// (SDL sets them up), and nothing on the GPU side holds back a copy, so a copy recorded while the previous
		// swap has not flipped yet would draw into the buffer still being shown. Measured on the console, this took
		// four players with Zoom out from 20 to 32 fps (MEM2 targets) and four players without it from 51 to 60 fps.
		FlushBatch();
		if (!_firstFrame) {
			WaitForPreviousFlip();
		}

		// The copy scales the screen buffer to each scan buffer's own resolution (the TV mode SDL picked, and
		// 854x480 on the GamePad)
		GX2CopyColorBufferToScanBuffer(&_screenBuffer, GX2_SCAN_TARGET_TV);
		GX2CopyColorBufferToScanBuffer(&_screenBuffer, GX2_SCAN_TARGET_DRC);
		// The copies clobber the pipeline state like a clear does
		RestoreContextState();
		GX2SwapScanBuffers();
		// Submits the copies and the swap without waiting for them: what the next frame writes is kept off memory
		// this one still reads by the tokens (see the class documentation)
		_swapToken = _openBatch;
		FlushBatch();

		if (_firstFrame) {
			// The scan-out is turned on only once there is a frame of ours in the scan buffers
			GX2SetTVEnable(TRUE);
			GX2SetDRCEnable(TRUE);
			_firstFrame = false;
		}

		// No wait here: the next present waits for this swap to flip before its own scan copies, which keeps the
		// CPU at most about one frame ahead of the scan-out

		_frameCounter++;
		ReleaseRetiredBlocks();
		InvalidateTarget();
	}

	void Gx2Device::WaitForPreviousFlip()
	{
		// GX2 counts a swap only once the GPU has executed it, so the batch that carries the previous swap is waited
		// for first; GX2WaitForFlip() then returns at the next flip, or at once when none is pending. The loop is
		// bounded so that counts which never meet (e.g. while the scan-out is off) cannot hang the game.
		if (_swapToken != 0) {
			const OSTime stamp = GetBatchStamp(_swapToken);
			if (GX2GetRetiredTimeStamp() < stamp) {
				GX2WaitTimeStamp(stamp);
			}
		}
		for (std::int32_t i = 0; i < 4; i++) {
			std::uint32_t swapCount = 0, flipCount = 0;
			OSTime lastFlip = 0, lastVsync = 0;
			GX2GetSwapStatus(&swapCount, &flipCount, &lastFlip, &lastVsync);
			if (std::int32_t(swapCount - flipCount) <= 0) {
				break;
			}
			GX2WaitForFlip();
		}
	}

	// ── Draws ────────────────────────────────────────────────────────────────────────────────────

	std::uint8_t* Gx2Device::AllocateUniformData(std::uint32_t size)
	{
		const std::uint32_t alignedSize = (size + GX2_UNIFORM_BLOCK_ALIGNMENT - 1) & ~std::uint32_t(GX2_UNIFORM_BLOCK_ALIGNMENT - 1);
		if (alignedSize > UniformRingChunkSize) {
			return nullptr;
		}
		if (_uniformChunkOffset + alignedSize > UniformRingChunkSize) {
			// Every draw that reads the chunk being left is in the open batch or an earlier one
			const std::uint32_t leftChunk = _uniformChunk;
			const std::uint32_t leftOffset = _uniformChunkOffset;
			_uniformChunkTokens[leftChunk] = _openBatch;
			_uniformChunk = (_uniformChunk + 1) % UniformRingChunkCount;
			_uniformChunkOffset = 0;
			const std::uint32_t token = _uniformChunkTokens[_uniformChunk];
			if (!IsUseRetired(token)) {
				// The ring has come round to a chunk the GPU may still be reading: the CPU is far ahead, or one
				// frame needs more than the whole ring. Not an error, but worth knowing when it happens often.
				static std::uint32_t waits = 0;
				if ((waits++ % 600) == 0) {
					LOGW("The uniform ring caught up with the GPU, waiting ({} times so far)", waits);
				}
				if (!WaitForUse(token)) {
					// Only on a GPU timeout. The chunk may still be read, so it keeps its token and the ring stays
					// on the chunk it was leaving (still current, so its token goes back to 0); the next block
					// waits again, and this draw is skipped rather than submitted with stale uniforms.
					LOGW("Waiting for uniform ring chunk {} timed out", _uniformChunk);
					_uniformChunkTokens[leftChunk] = 0;
					_uniformChunk = leftChunk;
					_uniformChunkOffset = leftOffset;
					return nullptr;
				}
				// A wait for the open batch flushes it, and the draw asking for this block is then recorded in
				// the next one. Its earlier blocks may sit in the chunk just left (one draw's blocks are far
				// smaller than a chunk, so it never spans more than that one), which must outlive it as well.
				_uniformChunkTokens[leftChunk] = _openBatch;
			}
			_uniformChunkTokens[_uniformChunk] = 0;
		}
		std::uint8_t* data = static_cast<std::uint8_t*>(_uniformRing.Base) + _uniformChunk * UniformRingChunkSize + _uniformChunkOffset;
		_uniformChunkOffset += alignedSize;
		return data;
	}

	bool Gx2Device::ApplyStageUniforms(const Gx2ShaderProgram& program, const Gx2ShaderProgram::StageBindings& bindings, bool vertexStage)
	{
		if (bindings.LooseLocation >= 0 && bindings.LooseSize > 0) {
			std::uint8_t* block = AllocateUniformData(bindings.LooseSize);
			if (block == nullptr) {
				return false;
			}
			std::memset(block, 0, bindings.LooseSize);
			for (const Gx2ShaderProgram::LooseUniform& uniform : bindings.LooseUniforms) {
				const std::uint8_t* value = program.ResolveUniform(uniform.Name);
				if (value == nullptr) {
					continue;
				}
				for (std::uint32_t i = 0; i < uniform.Count; i++) {
					const std::uint32_t offset = uniform.Offset + i * uniform.Stride;
					if (offset + uniform.ElementSize > bindings.LooseSize) {
						break;
					}
					CopyWordsSwapped(block + offset, value + i * uniform.ElementSize, uniform.ElementSize);
				}
			}
			Gx2Memory::FlushCpuWrites(block, bindings.LooseSize, GX2_INVALIDATE_MODE_UNIFORM_BLOCK);
			if (vertexStage) {
				GX2SetVertexUniformBlock(std::uint32_t(bindings.LooseLocation), bindings.LooseSize, block);
			} else {
				GX2SetPixelUniformBlock(std::uint32_t(bindings.LooseLocation), bindings.LooseSize, block);
			}
		}

		for (const Gx2ShaderProgram::BlockBinding& binding : bindings.Blocks) {
			const std::uint8_t* data = nullptr;
			std::uint32_t size = 0;
			GetUniformRange(binding.BindingIndex, data, size);
			if (data == nullptr || size == 0) {
				continue;
			}
			// Byte-swapped into the ring rather than in place: the engine's buffer may be flushed more than once
			// for the same range, and the batched blocks are rewritten every frame anyway
			std::uint8_t* block = AllocateUniformData(size);
			if (block == nullptr) {
				return false;
			}
			CopyWordsSwapped(block, data, size);
			Gx2Memory::FlushCpuWrites(block, size, GX2_INVALIDATE_MODE_UNIFORM_BLOCK);
			if (vertexStage) {
				GX2SetVertexUniformBlock(binding.Location, size, block);
			} else {
				GX2SetPixelUniformBlock(binding.Location, size, block);
			}
		}
		return true;
	}

	bool Gx2Device::ApplyAttributeBuffers(Gx2ShaderProgram& program)
	{
		Gx2VertexFormat& format = program.GetVertexFormat();
		for (std::uint32_t i = 0; i < format.GetAttributeCount(); i++) {
			const Gx2VertexFormat::Attribute& attribute = format[i];
			if (!attribute.IsEnabled()) {
				continue;
			}
			const Gx2BufferObject* vbo = attribute.GetVbo();
			if (vbo == nullptr || vbo->GetGpuData() == nullptr || attribute.GetBaseOffset() >= vbo->GetSize()) {
				return false;
			}
			// The stream reads the attribute's offset within the vertex (see Gx2ShaderProgram::GetFetchShader()),
			// so the buffer starts at the first vertex of the mesh
			const std::uint8_t* base = static_cast<const std::uint8_t*>(vbo->GetGpuData()) + attribute.GetBaseOffset();
			std::uint32_t stride = std::uint32_t(attribute.GetStride());
			if (stride == 0) {
				stride = std::uint32_t(attribute.GetSize()) *
					(attribute.GetType() == std::uint32_t(VertexAttribType::UnsignedByte) ? 1u : 4u);
			}
			GX2SetAttribBuffer(attribute.GetIndex(), std::uint32_t(vbo->GetSize() - attribute.GetBaseOffset()), stride, base);
			vbo->MarkUsed(_openBatch);
		}
		return true;
	}

	void Gx2Device::DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
		bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		Gx2ShaderProgram* program = _currentProgram;
		if (!CanSubmit() || program == nullptr || !program->HasBinaries() || count == 0) {
			return;
		}
		const GX2FetchShader* fetchShader = program->GetFetchShader();
		if (fetchShader == nullptr) {
			return;
		}

		if (!ApplyTarget()) {
			return;
		}

		if (_lastProgram != program) {
			GX2SetVertexShader(program->GetVertexShader());
			GX2SetPixelShader(program->GetPixelShader());
			_lastProgram = program;
		}
		GX2SetFetchShader(fetchShader);

		if (!ApplyStageUniforms(*program, program->GetVertexBindings(), true) ||
			!ApplyStageUniforms(*program, program->GetPixelBindings(), false)) {
			return;
		}

		for (const Gx2ShaderProgram::SamplerBinding& sampler : program->GetSamplers()) {
			if (const Gx2Texture* texture = GetBoundTexture(sampler.EngineUnit)) {
				// The unit would keep the texture an earlier draw left on it, which may have been released since
				if (!texture->ApplySamplerState(sampler.HardwareUnit)) {
					return;
				}
				texture->MarkUsed(_openBatch);
			}
		}

		if (!ApplyAttributeBuffers(*program)) {
			return;
		}

		const GX2PrimitiveMode mode = TranslatePrimitive(primitive);
		const std::uint32_t instances = std::uint32_t(numInstances > 0 ? numInstances : 1);
		if (indexed) {
			const Gx2BufferObject* ibo = program->GetVertexFormat().GetIbo();
			if (ibo == nullptr || ibo->GetGpuData() == nullptr) {
				return;
			}
			const std::uint8_t* indices = static_cast<const std::uint8_t*>(ibo->GetGpuData()) + indexOffset;
			ibo->MarkUsed(_openBatch);
			// The base vertex is the index offset the vertex grouper adds to every fetched index
			GX2DrawIndexedEx2(mode, count, indexFormat == IndexFormat::UInt32 ? GX2_INDEX_TYPE_U32 : GX2_INDEX_TYPE_U16,
				indices, std::uint32_t(baseVertex), instances, 0);
		} else {
			// The first vertex is added to the generated index, so gl_VertexID counts from it as in OpenGL
			GX2DrawEx(mode, count, std::uint32_t(firstVertex), instances);
		}
	}

	void Gx2Device::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, 1, 0);
	}

	void Gx2Device::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, numInstances, 0);
	}

	void Gx2Device::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, 1, baseVertex);
	}

	void Gx2Device::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, numInstances, baseVertex);
	}
}

#endif
