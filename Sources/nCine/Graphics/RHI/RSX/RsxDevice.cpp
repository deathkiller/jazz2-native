#if defined(WITH_RHI_RSX)

#include "RsxDevice.h"
#include "RsxRenderTarget.h"
#include "RsxTexture.h"
#include "RsxShaderProgram.h"
#include "RsxBufferObject.h"
#include "../../Material.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/RsxGeneratedShaders.h"

#include <cstring>
#include <malloc.h>
#include <unistd.h>

#include <rsx/rsx.h>
#include <rsx/commands.h>
#include <sysutil/video.h>

namespace nCine::RHI::RSX
{
	namespace
	{
		/**
			@brief Size of the command FIFO

			Far larger than the 512 KB the SDK's samples reserve, because a frame of this game emits far more
			than they do: a batched draw writes its whole instance array as constant registers, which is tens
			of words per sprite. The ring still wraps within a frame at this size - that is what the flush in
			DrawCommon() is for - but a bigger ring means the GPU has more slack before the write pointer
			catches up with it.
		*/
		constexpr std::uint32_t CommandBufferSize = 4 * 1024 * 1024;
		/**
			@brief Host region mapped into the GPU's IO window

			Everything the PPE writes and the GPU reads comes out of here - the streamed vertex and index
			buffers, the uniform staging - so it is sized for the pipeline's ring buffers with room to spare.
			The console has 256 MB of XDR, so 16 is not a meaningful cost.
		*/
		constexpr std::uint32_t HostMemorySize = 16 * 1024 * 1024;

		/** @brief Label index used to wait for the GPU to drain (255 is the one the SDK's samples reserve) */
		constexpr std::uint8_t FinishLabelIndex = 255;
		std::uint32_t _finishLabelValue = 1;

		/**
			@brief Display modes tried in order of preference

			720p first: it is the mode the game's logical resolution upscales into most cleanly and the one
			virtually every display attached to a PS3 accepts. The standard-definition modes follow for a CRT,
			and 960x1080 is last because it is an unusual anamorphic mode that only some displays report.
		*/
		constexpr std::uint32_t PreferredResolutions[] = {
			VIDEO_RESOLUTION_720,
			VIDEO_RESOLUTION_480,
			VIDEO_RESOLUTION_576,
			VIDEO_RESOLUTION_960x1080
		};

		std::uint16_t TranslateBlendFactor(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero: return GCM_ZERO;
				case nCine::BlendingFactor::One: return GCM_ONE;
				case nCine::BlendingFactor::SrcColor: return GCM_SRC_COLOR;
				case nCine::BlendingFactor::OneMinusSrcColor: return GCM_ONE_MINUS_SRC_COLOR;
				case nCine::BlendingFactor::SrcAlpha: return GCM_SRC_ALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha: return GCM_ONE_MINUS_SRC_ALPHA;
				case nCine::BlendingFactor::DstAlpha: return GCM_DST_ALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha: return GCM_ONE_MINUS_DST_ALPHA;
				case nCine::BlendingFactor::DstColor: return GCM_DST_COLOR;
				case nCine::BlendingFactor::OneMinusDstColor: return GCM_ONE_MINUS_DST_COLOR;
				case nCine::BlendingFactor::SrcAlphaSaturate: return GCM_SRC_ALPHA_SATURATE;
				case nCine::BlendingFactor::ConstantColor: return GCM_CONSTANT_COLOR;
				case nCine::BlendingFactor::OneMinusConstantColor: return GCM_ONE_MINUS_CONSTANT_COLOR;
				case nCine::BlendingFactor::ConstantAlpha: return GCM_CONSTANT_ALPHA;
				case nCine::BlendingFactor::OneMinusConstantAlpha: return GCM_ONE_MINUS_CONSTANT_ALPHA;
				default: return GCM_ONE;
			}
		}

		std::uint32_t TranslatePrimitive(PrimitiveType primitive)
		{
			switch (primitive) {
				case PrimitiveType::Points: return GCM_TYPE_POINTS;
				case PrimitiveType::Lines: return GCM_TYPE_LINES;
				case PrimitiveType::LineLoop: return GCM_TYPE_LINE_LOOP;
				case PrimitiveType::LineStrip: return GCM_TYPE_LINE_STRIP;
				case PrimitiveType::Triangles: return GCM_TYPE_TRIANGLES;
				case PrimitiveType::TriangleStrip: return GCM_TYPE_TRIANGLE_STRIP;
				case PrimitiveType::TriangleFan: return GCM_TYPE_TRIANGLE_FAN;
				default: return GCM_TYPE_TRIANGLES;
			}
		}

		/** @brief Packs a colour into the ARGB word `rsxSetClearColor()` takes */
		std::uint32_t PackClearColor(const Colorf& color)
		{
			const auto clamp = [](float v) -> std::uint32_t {
				const float scaled = v * 255.0f;
				return std::uint32_t(scaled < 0.0f ? 0.0f : (scaled > 255.0f ? 255.0f : scaled));
			};
			return (clamp(color.A) << 24) | (clamp(color.R) << 16) | (clamp(color.G) << 8) | clamp(color.B);
		}
	}

	RsxDevice::BlendingState RsxDevice::_blending;
	RsxDevice::DepthTestState RsxDevice::_depthTest;
	RsxDevice::CullFaceState RsxDevice::_cullFace;
	RsxDevice::ScissorState RsxDevice::_scissor;
	Recti RsxDevice::_viewport;
	Colorf RsxDevice::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	RsxShaderProgram* RsxDevice::_currentProgram = nullptr;
	const RsxTexture* RsxDevice::_boundTextures[RsxDevice::MaxTextureUnits] = {};
	RsxDevice::UniformRange RsxDevice::_boundUniformRanges[RsxDevice::MaxUniformBindings] = {};
	RsxRenderTarget* RsxDevice::_currentRenderTarget = nullptr;

	gcmContextData* RsxDevice::_context = nullptr;
	void* RsxDevice::_hostMemory = nullptr;

	std::int32_t RsxDevice::_displayWidth = 0;
	std::int32_t RsxDevice::_displayHeight = 0;
	RsxVram::Block RsxDevice::_displayBuffers[RsxDevice::DisplayBufferCount];
	std::uint32_t RsxDevice::_displayPitch = 0;
	std::uint32_t RsxDevice::_backBufferIndex = 0;

	RsxVram::Block RsxDevice::_screenBuffer;
	std::uint32_t RsxDevice::_screenPitch = 0;
	gcmTexture RsxDevice::_screenTexture;

	RsxVram::Block RsxDevice::_depthBuffer;
	std::uint32_t RsxDevice::_depthPitch = 0;

	bool RsxDevice::_initialized = false;
	bool RsxDevice::_vsync = true;
	bool RsxDevice::_firstFlip = true;
	std::uint32_t RsxDevice::_frameCounter = 0;
	bool RsxDevice::_surfaceDirty = true;
	const rsxVertexProgram* RsxDevice::_lastVertexProgram = nullptr;
	const void* RsxDevice::_lastVertexUcode = nullptr;
	const rsxFragmentProgram* RsxDevice::_lastFragmentProgram = nullptr;
	std::uint32_t RsxDevice::_lastFragmentUcodeOffset = 0;
	std::uint32_t RsxDevice::_boundAttributeRegisters = 0;

	RsxDevice::RetiredBlock RsxDevice::_retiredBlocks[RsxDevice::RetiredBlockCount];

	const rsxVertexProgram* RsxDevice::_presentVertexProgram = nullptr;
	const rsxFragmentProgram* RsxDevice::_presentFragmentProgram = nullptr;
	const void* RsxDevice::_presentVertexUcode = nullptr;
	RsxVram::Block RsxDevice::_presentFragmentUcode;
	RsxVram::Block RsxDevice::_presentVertices;

	RsxVram::Block RsxDevice::_quadCornerStream;
	RsxVram::Block RsxDevice::_batchedCornerStream;

	// -- Session lifecycle ------------------------------------------------------------------------

	bool RsxDevice::ConfigureVideo()
	{
		// The console is attached to whatever display the user owns, so the mode is negotiated rather than
		// assumed: the first entry of the preference list the display accepts wins
		videoResolution resolution;
		std::uint32_t chosenId = 0;
		for (std::uint32_t id : PreferredResolutions) {
			if (videoGetResolutionAvailability(VIDEO_PRIMARY, id, VIDEO_ASPECT_AUTO, 0) != 1) {
				continue;
			}
			if (videoGetResolution(id, &resolution) == 0) {
				chosenId = id;
				break;
			}
		}
		if (chosenId == 0) {
			LOGE("No usable video mode was reported by the display");
			return false;
		}

		_displayWidth = std::int32_t(resolution.width);
		_displayHeight = std::int32_t(resolution.height);

		videoConfiguration config;
		std::memset(&config, 0, sizeof(config));
		config.resolution = std::uint8_t(chosenId);
		// XRGB rather than ARGB: the scan-out ignores alpha, and asking for a format the display controller
		// has to interpret differently from what the GPU wrote would only cost a conversion
		config.format = VIDEO_BUFFER_FORMAT_XRGB;
		config.aspect = VIDEO_ASPECT_AUTO;
		config.pitch = std::uint32_t(_displayWidth) * 4;

		if (videoConfigure(VIDEO_PRIMARY, &config, nullptr, 0) != 0) {
			LOGE("Cannot configure the video output for {}x{}", _displayWidth, _displayHeight);
			return false;
		}

		LOGI("Negotiated video mode: {}x{}", _displayWidth, _displayHeight);
		return true;
	}

	bool RsxDevice::CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync)
	{
		static_cast<void>(windowHandle);
		static_cast<void>(width);
		static_cast<void>(height);

		if (_initialized) {
			return true;
		}
		_vsync = vsync;

		// The IO region has to be page-aligned for the mapping the lv2 side performs, and it is handed to
		// rsxInit() rather than allocated by it - which is also what makes it addressable by the PPE
		_hostMemory = ::memalign(1024 * 1024, HostMemorySize);
		if (_hostMemory == nullptr) {
			LOGE("Cannot reserve {} MB of host memory for the RSX IO window", HostMemorySize / (1024 * 1024));
			return false;
		}

		if (rsxInit(&_context, CommandBufferSize, HostMemorySize, _hostMemory) != 0 || _context == nullptr) {
			LOGE("Cannot initialize the RSX command context");
			::free(_hostMemory);
			_hostMemory = nullptr;
			return false;
		}

		if (!ConfigureVideo()) {
			DestroySwapchain();
			return false;
		}

		// The command FIFO is carved out of the FRONT of the region handed to rsxInit() - that is libgcm's
		// contract - so the suballocator must start behind it. Handing out the whole region instead lets the
		// first vertex buffer land on top of the FIFO, which the GPU then reads as garbage commands: RPCS3
		// reports it as "Dead FIFO commands queue state has been detected" once the first frame is submitted.
		std::uint8_t* heapBase = static_cast<std::uint8_t*>(_hostMemory) + CommandBufferSize;
		if (!RsxVram::Initialize(heapBase, HostMemorySize - CommandBufferSize)) {
			DestroySwapchain();
			return false;
		}

		gcmSetFlipMode(_vsync ? GCM_FLIP_VSYNC : GCM_FLIP_HSYNC);

		// Display buffers the scan-out cycles between, and the intermediate screen surface the frame is
		// actually drawn into (bottom-up, see the class documentation)
		for (std::uint32_t i = 0; i < DisplayBufferCount; i++) {
			_displayBuffers[i] = RsxVram::AllocSurface(std::uint32_t(_displayWidth), std::uint32_t(_displayHeight), 4, _displayPitch);
			if (!_displayBuffers[i].IsValid()) {
				LOGE("Cannot allocate display buffer {}", i);
				DestroySwapchain();
				return false;
			}
			gcmSetDisplayBuffer(std::uint8_t(i), _displayBuffers[i].Offset, _displayPitch,
				std::uint32_t(_displayWidth), std::uint32_t(_displayHeight));
		}

		_screenBuffer = RsxVram::AllocSurface(std::uint32_t(_displayWidth), std::uint32_t(_displayHeight), 4, _screenPitch);
		if (!_screenBuffer.IsValid()) {
			LOGE("Cannot allocate the intermediate screen surface");
			DestroySwapchain();
			return false;
		}

		// One depth surface, shared by the screen and every render target (see RsxRenderTarget). Z24S8 even
		// though nothing here uses stencil: the 16-bit format would halve the memory but costs precision the
		// layer ordering depends on, and 5 MB is nothing against 256.
		_depthBuffer = RsxVram::AllocSurface(std::uint32_t(_displayWidth), std::uint32_t(_displayHeight), 4, _depthPitch);
		if (!_depthBuffer.IsValid()) {
			LOGE("Cannot allocate the depth surface");
			DestroySwapchain();
			return false;
		}

		// The screen surface is sampled by the present shader, so it also needs a texture description
		std::memset(&_screenTexture, 0, sizeof(_screenTexture));
		_screenTexture.format = (GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN);
		_screenTexture.mipmap = 1;
		_screenTexture.dimension = GCM_TEXTURE_DIMS_2D;
		_screenTexture.cubemap = GCM_FALSE;
		_screenTexture.remap = ((GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
			(GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
			(GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
			(GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
			(GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT) |
			(GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
			(GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
			(GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT));
		_screenTexture.width = std::uint16_t(_displayWidth);
		_screenTexture.height = std::uint16_t(_displayHeight);
		_screenTexture.depth = 1;
		_screenTexture.location = _screenBuffer.GetGcmLocation();
		_screenTexture.pitch = _screenPitch;
		_screenTexture.offset = _screenBuffer.Offset;

		if (!CreateBuiltinResources() || !CreatePresentShader()) {
			DestroySwapchain();
			return false;
		}

		_initialized = true;
		_firstFlip = true;
		_backBufferIndex = 0;
		_surfaceDirty = true;
		_viewport = Recti(0, 0, _displayWidth, _displayHeight);

		SetupInitialState();
		return true;
	}

	void RsxDevice::DestroySwapchain()
	{
		if (_context != nullptr) {
			Finish();
		}

		ReleaseRetiredBlocks(true);
		RsxVram::Free(_presentVertices);
		RsxVram::Free(_presentFragmentUcode);
		RsxVram::Free(_quadCornerStream);
		RsxVram::Free(_batchedCornerStream);
		RsxVram::Free(_depthBuffer);
		RsxVram::Free(_screenBuffer);
		for (std::uint32_t i = 0; i < DisplayBufferCount; i++) {
			RsxVram::Free(_displayBuffers[i]);
		}
		RsxVram::Shutdown();

		if (_hostMemory != nullptr) {
			::free(_hostMemory);
			_hostMemory = nullptr;
		}
		_context = nullptr;
		_initialized = false;
		_currentProgram = nullptr;
		_currentRenderTarget = nullptr;
	}

	void RsxDevice::ResizeSwapchain(std::int32_t width, std::int32_t height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	gcmContextData* RsxDevice::GetContext()
	{
		return _context;
	}

	std::int32_t RsxDevice::GetDisplayWidth()
	{
		return _displayWidth;
	}

	std::int32_t RsxDevice::GetDisplayHeight()
	{
		return _displayHeight;
	}

	std::uint32_t RsxDevice::GetFrameCounter()
	{
		return _frameCounter;
	}

	void RsxDevice::Finish()
	{
		if (_context == nullptr) {
			return;
		}
		// Writes a label the GPU only reaches once it has consumed everything before it, then spins on the
		// label's memory. The sleep keeps the PPE off the bus while it waits, which matters because the GPU
		// is reading through it.
		rsxSetWriteBackendLabel(_context, FinishLabelIndex, _finishLabelValue);
		rsxFlushBuffer(_context);
		while (*reinterpret_cast<volatile std::uint32_t*>(gcmGetLabelAddress(FinishLabelIndex)) != _finishLabelValue) {
			::usleep(30);
		}
		_finishLabelValue++;
	}

	void RsxDevice::RetireBlock(RsxVram::Block& block)
	{
		if (!block.IsValid()) {
			return;
		}
		for (std::uint32_t i = 0; i < RetiredBlockCount; i++) {
			if (!_retiredBlocks[i].Block.IsValid()) {
				_retiredBlocks[i].Block = block;
				_retiredBlocks[i].RetiredAtFrame = _frameCounter;
				block.Base = nullptr;
				block.Offset = 0;
				block.Size = 0;
				return;
			}
		}
		// The list is full, which means more resources were destroyed within the retirement window than it
		// was sized for. Waiting is correct rather than convenient: freeing memory the GPU is still reading
		// from is what the delay exists to prevent, so the only safe way to reclaim a slot is to make the
		// wait real first.
		Finish();
		ReleaseRetiredBlocks(true);
		_retiredBlocks[0].Block = block;
		_retiredBlocks[0].RetiredAtFrame = _frameCounter;
		block.Base = nullptr;
		block.Offset = 0;
		block.Size = 0;
	}

	void RsxDevice::ReleaseRetiredBlocks(bool force)
	{
		for (std::uint32_t i = 0; i < RetiredBlockCount; i++) {
			RetiredBlock& retired = _retiredBlocks[i];
			if (!retired.Block.IsValid()) {
				continue;
			}
			// Presenting queues a frame rather than completing it, so a block is only safe to reuse once the
			// GPU is demonstrably past the frame that last referenced it
			if (!force && _frameCounter - retired.RetiredAtFrame < RetiredBlockFrameDelay) {
				continue;
			}
			RsxVram::Free(retired.Block);
		}
	}

	// -- State ------------------------------------------------------------------------------------

	void RsxDevice::SetupInitialState()
	{
		if (_context == nullptr) {
			return;
		}

		// The engine is a 2D renderer: nothing is culled (which one of a sprite's two triangles faces the
		// camera follows from the winding the bottom-up viewport transform gives it, not from anything the
		// pipeline controls), and the alpha test is unused because blending covers every case.
		rsxSetCullFaceEnable(_context, GCM_FALSE);
		rsxSetAlphaTestEnable(_context, GCM_FALSE);
		rsxSetDepthTestEnable(_context, GCM_FALSE);
		rsxSetDepthWriteEnable(_context, GCM_TRUE);
		rsxSetDepthFunc(_context, GCM_LEQUAL);
		rsxSetBlendEnable(_context, GCM_FALSE);
		rsxSetBlendEquation(_context, GCM_FUNC_ADD, GCM_FUNC_ADD);
		rsxSetColorMask(_context, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
		rsxSetShadeModel(_context, GCM_SHADE_MODEL_SMOOTH);
		rsxSetFrontFace(_context, GCM_FRONTFACE_CCW);
	}

	void RsxDevice::SetBlendingEnabled(bool enabled)
	{
		if (_blending.Enabled == enabled) {
			return;
		}
		_blending.Enabled = enabled;
		if (_context != nullptr) {
			rsxSetBlendEnable(_context, enabled ? GCM_TRUE : GCM_FALSE);
		}
	}

	void RsxDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		if (_blending.SrcRgb == srcRgb && _blending.DstRgb == dstRgb &&
			_blending.SrcAlpha == srcAlpha && _blending.DstAlpha == dstAlpha) {
			return;
		}
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
		if (_context != nullptr) {
			rsxSetBlendFunc(_context, TranslateBlendFactor(srcRgb), TranslateBlendFactor(dstRgb),
				TranslateBlendFactor(srcAlpha), TranslateBlendFactor(dstAlpha));
		}
	}

	RsxDevice::BlendingState RsxDevice::GetBlendingState()
	{
		return _blending;
	}

	void RsxDevice::SetBlendingState(const BlendingState& state)
	{
		SetBlendingEnabled(state.Enabled);
		SetBlendingFactors(state.SrcRgb, state.DstRgb, state.SrcAlpha, state.DstAlpha);
	}

	void RsxDevice::SetDepthTestEnabled(bool enabled)
	{
		if (_depthTest.TestEnabled == enabled) {
			return;
		}
		_depthTest.TestEnabled = enabled;
		if (_context != nullptr) {
			rsxSetDepthTestEnable(_context, enabled ? GCM_TRUE : GCM_FALSE);
		}
	}

	void RsxDevice::SetDepthMaskEnabled(bool enabled)
	{
		if (_depthTest.MaskEnabled == enabled) {
			return;
		}
		_depthTest.MaskEnabled = enabled;
		if (_context != nullptr) {
			rsxSetDepthWriteEnable(_context, enabled ? GCM_TRUE : GCM_FALSE);
		}
	}

	RsxDevice::DepthTestState RsxDevice::GetDepthTestState()
	{
		return _depthTest;
	}

	void RsxDevice::SetDepthTestState(const DepthTestState& state)
	{
		SetDepthTestEnabled(state.TestEnabled);
		SetDepthMaskEnabled(state.MaskEnabled);
	}

	void RsxDevice::SetCullFaceEnabled(bool enabled)
	{
		if (_cullFace.Enabled == enabled) {
			return;
		}
		_cullFace.Enabled = enabled;
		if (_context != nullptr) {
			rsxSetCullFaceEnable(_context, enabled ? GCM_TRUE : GCM_FALSE);
		}
	}

	RsxDevice::CullFaceState RsxDevice::GetCullFaceState()
	{
		return _cullFace;
	}

	void RsxDevice::SetCullFaceState(const CullFaceState& state)
	{
		SetCullFaceEnabled(state.Enabled);
		if (_cullFace.Mode != state.Mode) {
			_cullFace.Mode = state.Mode;
			if (_context != nullptr) {
				rsxSetCullFace(_context, state.Mode == CullFaceMode::Front ? GCM_CULL_FRONT : GCM_CULL_BACK);
			}
		}
	}

	RsxDevice::ScissorState RsxDevice::GetScissorState()
	{
		return _scissor;
	}

	void RsxDevice::SetScissorState(const ScissorState& state)
	{
		_scissor = state;
	}

	void RsxDevice::SetScissor(const Recti& rect)
	{
		_scissor.Enabled = true;
		_scissor.Rect = rect;
	}

	void RsxDevice::SetScissorTestEnabled(bool enabled)
	{
		_scissor.Enabled = enabled;
	}

	Recti RsxDevice::GetViewport()
	{
		return _viewport;
	}

	void RsxDevice::SetViewport(const Recti& rect)
	{
		_viewport = rect;
	}

	void RsxDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
	}

	Colorf RsxDevice::GetClearColor()
	{
		return _clearColor;
	}

	void RsxDevice::SetClearColor(const Colorf& color)
	{
		_clearColor = color;
	}

	void RsxDevice::Clear(ClearFlags flags)
	{
		if (_context == nullptr) {
			return;
		}
		ApplySurface();

		std::uint32_t mask = 0;
		if ((flags & ClearFlags::Color) == ClearFlags::Color) {
			rsxSetClearColor(_context, PackClearColor(_clearColor));
			mask |= (GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
		}
		if ((flags & ClearFlags::Depth) == ClearFlags::Depth) {
			rsxSetClearDepthStencil(_context, 0xFFFFFF00u);
			mask |= GCM_CLEAR_Z;
		}
		if ((flags & ClearFlags::Stencil) == ClearFlags::Stencil) {
			mask |= GCM_CLEAR_S;
		}
		if (mask != 0) {
			// A real hardware clear, unlike the full-screen quad the tile-based sceGxm backend has to draw
			rsxClearSurface(_context, mask);
			rsxFlushBuffer(_context);
		}
	}

	// -- Target and viewport ----------------------------------------------------------------------

	void RsxDevice::GetCurrentTargetSize(std::int32_t& width, std::int32_t& height)
	{
		if (_currentRenderTarget != nullptr) {
			if (RsxTexture* texture = _currentRenderTarget->GetColorTexture(0)) {
				width = texture->GetWidth();
				height = texture->GetHeight();
				return;
			}
		}
		width = _displayWidth;
		height = _displayHeight;
	}

	void RsxDevice::ApplySurface()
	{
		if (_context == nullptr) {
			return;
		}

		std::int32_t targetWidth = _displayWidth;
		std::int32_t targetHeight = _displayHeight;
		gcmSurface surface;

		if (_currentRenderTarget != nullptr && _currentRenderTarget->GetDrawSurface(surface, targetWidth, targetHeight)) {
			// The depth half is the device's shared surface rather than anything the target owns
			surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
			surface.depthLocation = _depthBuffer.GetGcmLocation();
			surface.depthOffset = _depthBuffer.Offset;
			surface.depthPitch = _depthPitch;
		} else {
			// No render target: the intermediate screen surface, which PresentFrame() flips out
			std::memset(&surface, 0, sizeof(surface));
			surface.colorFormat = GCM_SURFACE_A8R8G8B8;
			surface.colorTarget = GCM_SURFACE_TARGET_0;
			surface.colorLocation[0] = _screenBuffer.GetGcmLocation();
			surface.colorOffset[0] = _screenBuffer.Offset;
			surface.colorPitch[0] = _screenPitch;
			for (std::uint32_t i = 1; i < 4; i++) {
				surface.colorLocation[i] = GCM_LOCATION_RSX;
				surface.colorOffset[i] = 0;
				surface.colorPitch[i] = 64;
			}
			surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
			surface.depthLocation = _depthBuffer.GetGcmLocation();
			surface.depthOffset = _depthBuffer.Offset;
			surface.depthPitch = _depthPitch;
			surface.type = GCM_SURFACE_TYPE_LINEAR;
			surface.antiAlias = GCM_SURFACE_CENTER_1;
			surface.width = std::uint32_t(_displayWidth);
			surface.height = std::uint32_t(_displayHeight);
			surface.x = 0;
			surface.y = 0;
			targetWidth = _displayWidth;
			targetHeight = _displayHeight;
		}

		rsxSetSurface(_context, &surface);
		_surfaceDirty = false;
		ApplyViewportAndScissor(targetHeight);
	}

	void RsxDevice::ApplyViewportAndScissor(std::int32_t targetHeight)
	{
		if (_context == nullptr) {
			return;
		}

		const std::int32_t x = _viewport.X;
		const std::int32_t width = _viewport.W;
		const std::int32_t height = _viewport.H;
		// The engine's viewport origin is bottom-left (OpenGL); the RSX's is top-left, so the Y is flipped
		// against the target. Every surface here is stored bottom-up (see the class documentation), which is
		// what makes this the only place the two conventions have to be reconciled.
		const std::int32_t y = targetHeight - (_viewport.Y + height);

		// The scale/offset pair is the viewport transform proper: clip space maps onto the half-width and
		// half-height of the rectangle, centred on it. The Z half maps [-1, 1] onto [0, 1], the depth range
		// the hardware stores, which is what the engine's projection matrices assume.
		const float scale[4] = {
			float(width) * 0.5f,
			float(height) * 0.5f,
			0.5f,
			0.0f
		};
		const float offset[4] = {
			float(x) + float(width) * 0.5f,
			float(y) + float(height) * 0.5f,
			0.5f,
			0.0f
		};
		rsxSetViewport(_context, std::uint16_t(x), std::uint16_t(y), std::uint16_t(width), std::uint16_t(height),
			0.0f, 1.0f, scale, offset);

		if (_scissor.Enabled) {
			const std::int32_t scissorY = targetHeight - (_scissor.Rect.Y + _scissor.Rect.H);
			rsxSetScissor(_context, std::uint16_t(_scissor.Rect.X), std::uint16_t(scissorY),
				std::uint16_t(_scissor.Rect.W), std::uint16_t(_scissor.Rect.H));
		} else {
			// There is no scissor-enable bit on the RSX; a scissor covering the whole target is how it is
			// turned off, and 4095 is the largest rectangle the command's 12-bit fields can express
			rsxSetScissor(_context, 0, 0, 4095, 4095);
		}
	}

	void RsxDevice::SetRenderTarget(RsxRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			return;
		}
		_currentRenderTarget = renderTarget;
		_surfaceDirty = true;
	}

	void RsxDevice::UnbindRenderTarget(const RsxRenderTarget* renderTarget)
	{
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
			_surfaceDirty = true;
		}
	}

	// -- Resource binding -------------------------------------------------------------------------

	void RsxDevice::BindProgram(RsxShaderProgram* program)
	{
		_currentProgram = program;
	}

	RsxShaderProgram* RsxDevice::CurrentProgram()
	{
		return _currentProgram;
	}

	void RsxDevice::OnProgramDestroyed(const RsxShaderProgram* program)
	{
		if (_currentProgram == program) {
			_currentProgram = nullptr;
		}
	}

	void RsxDevice::BindTexture(std::uint32_t unit, const RsxTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void RsxDevice::UnbindTexture(const RsxTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
	}

	const RsxTexture* RsxDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void RsxDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void RsxDevice::GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size)
	{
		if (index < MaxUniformBindings) {
			data = _boundUniformRanges[index].Data;
			size = _boundUniformRanges[index].Size;
		} else {
			data = nullptr;
			size = 0;
		}
	}

	// -- Fences -----------------------------------------------------------------------------------

	FenceHandle RsxDevice::InsertFence()
	{
		// The engine only uses fences to avoid overwriting a buffer the GPU may still be reading. The RSX
		// has backend labels for exactly that, but this backend's ring buffers are sized so a frame never
		// laps itself, so a fence is reported as immediately signalled rather than costing a real sync.
		return FenceHandle{};
	}

	void RsxDevice::DeleteFence(FenceHandle& fence)
	{
		fence = FenceHandle{};
	}

	bool RsxDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(fence);
		static_cast<void>(timeoutNs);
		return true;
	}

	// -- Present ----------------------------------------------------------------------------------

	void RsxDevice::PresentFrame()
	{
		if (_context == nullptr) {
			return;
		}

		// The frame was drawn bottom-up into the screen surface; the flip into the display buffer is where
		// the OpenGL convention becomes the scan-out's, and where the logical resolution is scaled to the
		// panel. Rendering it as a quad rather than blitting is what buys the scale for free.
		gcmSurface surface;
		std::memset(&surface, 0, sizeof(surface));
		surface.colorFormat = GCM_SURFACE_A8R8G8B8;
		surface.colorTarget = GCM_SURFACE_TARGET_0;
		surface.colorLocation[0] = _displayBuffers[_backBufferIndex].GetGcmLocation();
		surface.colorOffset[0] = _displayBuffers[_backBufferIndex].Offset;
		surface.colorPitch[0] = _displayPitch;
		for (std::uint32_t i = 1; i < 4; i++) {
			surface.colorLocation[i] = GCM_LOCATION_RSX;
			surface.colorOffset[i] = 0;
			surface.colorPitch[i] = 64;
		}
		surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
		surface.depthLocation = _depthBuffer.GetGcmLocation();
		surface.depthOffset = _depthBuffer.Offset;
		surface.depthPitch = _depthPitch;
		surface.type = GCM_SURFACE_TYPE_LINEAR;
		surface.antiAlias = GCM_SURFACE_CENTER_1;
		surface.width = std::uint32_t(_displayWidth);
		surface.height = std::uint32_t(_displayHeight);
		surface.x = 0;
		surface.y = 0;
		rsxSetSurface(_context, &surface);

		const float scale[4] = { float(_displayWidth) * 0.5f, float(_displayHeight) * 0.5f, 0.5f, 0.0f };
		const float offset[4] = { float(_displayWidth) * 0.5f, float(_displayHeight) * 0.5f, 0.5f, 0.0f };
		rsxSetViewport(_context, 0, 0, std::uint16_t(_displayWidth), std::uint16_t(_displayHeight), 0.0f, 1.0f, scale, offset);
		rsxSetScissor(_context, 0, 0, 4095, 4095);

		rsxSetBlendEnable(_context, GCM_FALSE);
		rsxSetDepthTestEnable(_context, GCM_FALSE);

		if (_presentVertexProgram != nullptr && _presentFragmentProgram != nullptr && _presentVertices.IsValid()) {
			rsxInvalidateTextureCache(_context, GCM_INVALIDATE_TEXTURE);
			// The present shader replaces whatever the pipeline left bound, so the cache above no longer
			// describes what the RSX is running
			_lastVertexProgram = nullptr;
			_lastVertexUcode = nullptr;
			_lastFragmentProgram = nullptr;
			rsxLoadVertexProgram(_context, _presentVertexProgram, _presentVertexUcode);
			rsxLoadFragmentProgramLocation(_context, _presentFragmentProgram, _presentFragmentUcode.Offset,
				_presentFragmentUcode.GetGcmLocation());

			rsxLoadTexture(_context, 0, &_screenTexture);
			rsxTextureControl(_context, 0, GCM_TRUE, 0 << 8, 0 << 8, GCM_TEXTURE_MAX_ANISO_1);
			rsxTextureFilter(_context, 0, 0, GCM_TEXTURE_LINEAR, GCM_TEXTURE_LINEAR, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
			rsxTextureWrapMode(_context, 0, GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_CLAMP_TO_EDGE,
				GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);

			// Four vertices of (x, y, u, v), the V already flipped in the data so the shader stays trivial.
			// The registers are looked up through GetAttrib() rather than the GetAttribIndex() that
			// rsx_program.h also declares: librsx declares that one but implements no such symbol, so
			// calling it does not link (the same is true of rsxFragmentProgramGetConstIndex).
			const rsxProgramAttrib* positionAttrib = rsxVertexProgramGetAttrib(_presentVertexProgram, "aPosition");
			const rsxProgramAttrib* texCoordAttrib = rsxVertexProgramGetAttrib(_presentVertexProgram, "aTexCoords");
			const std::int32_t positionReg = (positionAttrib != nullptr ? std::int32_t(positionAttrib->index) : -1);
			const std::int32_t texCoordReg = (texCoordAttrib != nullptr ? std::int32_t(texCoordAttrib->index) : -1);
			if (positionReg >= 0) {
				rsxBindVertexArrayAttrib(_context, std::uint8_t(positionReg), 0, _presentVertices.Offset,
					16, 2, GCM_VERTEX_DATA_TYPE_F32, _presentVertices.GetGcmLocation());
			}
			if (texCoordReg >= 0) {
				rsxBindVertexArrayAttrib(_context, std::uint8_t(texCoordReg), 0, _presentVertices.Offset + 8,
					16, 2, GCM_VERTEX_DATA_TYPE_F32, _presentVertices.GetGcmLocation());
			}
			rsxDrawVertexArray(_context, GCM_TYPE_TRIANGLE_STRIP, 0, 4);
		}

		// Queue the flip and let the next frame's commands follow it; gcmSetWaitFlip() is what keeps the GPU
		// from drawing over a buffer the scan-out has not finished with
		if (!_firstFlip) {
			while (gcmGetFlipStatus() != 0) {
				::usleep(200);
			}
		}
		gcmResetFlipStatus();
		gcmSetFlip(_context, std::uint8_t(_backBufferIndex));
		// The wait has to be in the buffer BEFORE the write pointer is handed over, or it is not part of the
		// work this flush submits and only takes effect once the next frame happens to flush
		gcmSetWaitFlip(_context);
		rsxFlushBuffer(_context);

		_backBufferIndex = (_backBufferIndex + 1) % DisplayBufferCount;
		_firstFlip = false;
		_frameCounter++;

		// Blocks retired long enough ago that the GPU cannot still be reading them can go now
		ReleaseRetiredBlocks();

		// The next frame starts by re-programming whatever it renders into
		_surfaceDirty = true;
	}

	// -- Built-in resources -----------------------------------------------------------------------

	bool RsxDevice::CreateBuiltinResources()
	{
		// The quad corner stream: {(1,0), (1,1), (0,0), (0,1)}, the order of the single-quad TRIANGLE_STRIP
		// draw the Cg emitter's gl_VertexID rewrite expects
		static const float quadCorners[] = {
			1.0f, 0.0f,
			1.0f, 1.0f,
			0.0f, 0.0f,
			0.0f, 1.0f
		};
		_quadCornerStream = RsxVram::Alloc(sizeof(quadCorners), 128, RsxVram::Location::Main);
		if (!_quadCornerStream.IsValid()) {
			return false;
		}
		std::memcpy(_quadCornerStream.Base, quadCorners, sizeof(quadCorners));

		// The batched stream: six vertices per sprite carrying the corner and the sprite's index in the
		// batch, so a draw of 6*n vertices reproduces the gl_VertexID/6 instance lookup
		static const float batchedCorners[6][2] = {
			{ 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f },
			{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }
		};
		const std::uint32_t batchedSize = std::uint32_t(sizeof(float) * 3 * 6 * MaxBatchSize);
		_batchedCornerStream = RsxVram::Alloc(batchedSize, 128, RsxVram::Location::Main);
		if (!_batchedCornerStream.IsValid()) {
			return false;
		}
		{
			float* out = static_cast<float*>(_batchedCornerStream.Base);
			for (std::uint32_t instance = 0; instance < MaxBatchSize; instance++) {
				for (std::uint32_t corner = 0; corner < 6; corner++) {
					*out++ = batchedCorners[corner][0];
					*out++ = batchedCorners[corner][1];
					*out++ = float(instance);
				}
			}
		}

		// The present quad: (x, y, u, v) per vertex in the TRIANGLE_STRIP order, in clip space. The V is
		// flipped here rather than in the shader, which is what keeps the present shader a plain textured
		// quad and puts the one convention correction in a place a reader can see.
		static const float presentQuad[] = {
			-1.0f, -1.0f, 0.0f, 1.0f,
			-1.0f,  1.0f, 0.0f, 0.0f,
			 1.0f, -1.0f, 1.0f, 1.0f,
			 1.0f,  1.0f, 1.0f, 0.0f
		};
		_presentVertices = RsxVram::Alloc(sizeof(presentQuad), 128, RsxVram::Location::Main);
		if (!_presentVertices.IsValid()) {
			return false;
		}
		std::memcpy(_presentVertices.Base, presentQuad, sizeof(presentQuad));
		return true;
	}


	bool RsxDevice::CreatePresentShader()
	{
		const GeneratedRsxShader* generated = FindGeneratedRsxShader("__Present", "");
		if (generated == nullptr) {
			LOGE("The built-in present shader is missing from the generated RSX shader table");
			return false;
		}

		_presentVertexProgram = reinterpret_cast<const rsxVertexProgram*>(generated->VertexProgram);
		_presentFragmentProgram = reinterpret_cast<const rsxFragmentProgram*>(generated->FragmentProgram);

		std::uint32_t ucodeSize = 0;
		void* ucode = nullptr;
		rsxVertexProgramGetUCode(_presentVertexProgram, &ucode, &ucodeSize);
		_presentVertexUcode = ucode;

		rsxFragmentProgramGetUCode(_presentFragmentProgram, &ucode, &ucodeSize);
		if (ucode == nullptr || ucodeSize == 0) {
			return false;
		}
		_presentFragmentUcode = RsxVram::AllocFragmentProgram(ucodeSize);
		if (!_presentFragmentUcode.IsValid()) {
			return false;
		}
		std::memcpy(_presentFragmentUcode.Base, ucode, ucodeSize);
		return true;
	}

	const void* RsxDevice::GetQuadCornerStream()
	{
		return _quadCornerStream.Base;
	}

	const void* RsxDevice::GetBatchedCornerStream()
	{
		return _batchedCornerStream.Base;
	}

	std::uint32_t RsxDevice::GetQuadCornerStreamOffset()
	{
		return _quadCornerStream.Offset;
	}

	std::uint32_t RsxDevice::GetBatchedCornerStreamOffset()
	{
		return _batchedCornerStream.Offset;
	}

	// -- Draw -------------------------------------------------------------------------------------

	void RsxDevice::UploadUniforms()
	{
		RsxShaderProgram* program = _currentProgram;
		if (program == nullptr || _context == nullptr) {
			return;
		}

		// Loose uniforms: the value the uniform cache last committed, published through the program
		for (const RsxUniformSlot& slot : program->GetVertexUniformSlots()) {
			if (const std::uint8_t* data = program->ResolveUniform(slot.Name)) {
				rsxSetVertexProgramParameter(_context, program->GetVertexProgram(), slot.Param,
					reinterpret_cast<const float*>(data));
			}
		}
		for (const RsxUniformSlot& slot : program->GetFragmentUniformSlots()) {
			if (const std::uint8_t* data = program->ResolveUniform(slot.Name)) {
				// A fragment constant is patched into the microcode, which is why this needs the program's
				// private copy of it rather than the shared blob (see RsxShaderProgram)
				rsxSetFragmentProgramParameter(_context, program->GetFragmentProgram(), slot.Param,
					reinterpret_cast<const float*>(data), program->GetFragmentUcodeBlock().Offset,
					program->GetFragmentUcodeBlock().GetGcmLocation());
			}
		}

		// Uniform-block members, out of the range the pipeline bound to the block's binding point
		const auto uploadBlocks = [program](const SmallVector<RsxShaderProgram::RsxBlockUpload, 0>& uploads, bool vertexStage) {
			for (const RsxShaderProgram::RsxBlockUpload& upload : uploads) {
				const std::uint8_t* data = nullptr;
				std::uint32_t size = 0;
				GetUniformRange(upload.BindingIndex, data, size);
				if (data == nullptr || upload.SourceOffset >= size) {
					continue;
				}
				const std::uint8_t* source = data + upload.SourceOffset;
				if (vertexStage) {
					rsxSetVertexProgramParameter(_context, program->GetVertexProgram(), upload.Param,
						reinterpret_cast<const float*>(source));
				} else {
					rsxSetFragmentProgramParameter(_context, program->GetFragmentProgram(), upload.Param,
						reinterpret_cast<const float*>(source), program->GetFragmentUcodeBlock().Offset,
						program->GetFragmentUcodeBlock().GetGcmLocation());
				}
			}
		};
		uploadBlocks(program->GetVertexBlockUploads(), true);
		uploadBlocks(program->GetFragmentBlockUploads(), false);

		UploadInstanceArray();
	}

	void RsxDevice::UploadInstanceArray()
	{
		RsxShaderProgram* program = _currentProgram;
		if (program == nullptr || _context == nullptr) {
			return;
		}
		const RsxShaderProgram::RsxInstanceArray& array = program->GetInstanceArray();
		if (!array.Valid) {
			return;
		}

		const std::uint8_t* data = nullptr;
		std::uint32_t size = 0;
		GetUniformRange(array.BindingIndex, data, size);
		if (data == nullptr || size == 0) {
			return;
		}

		// Repack rather than write field by field. The two sides lay an element out differently - std140
		// packs a trailing vec2 and float into one 16-byte slot, the register file gives each its own - so
		// the element cannot be handed over as one block; but gathering it into a register-shaped scratch
		// buffer first turns what would be one command per FIELD into one command per element.
		float scratch[MaxInstanceRegisters * 4];
		const std::uint32_t registers = array.RegistersPerElement;
		if (registers == 0 || registers > MaxInstanceRegisters) {
			return;
		}

		// Only the elements the bound range actually covers are written: the batcher sizes its range to the
		// sprites it collected, which is usually fewer than the batch the shader was compiled for
		std::uint32_t elements = (array.SourceStride > 0 ? size / array.SourceStride : 0);
		if (elements > array.ElementCount) {
			elements = array.ElementCount;
		}

		for (std::uint32_t element = 0; element < elements; element++) {
			const std::uint8_t* source = data + std::size_t(element) * array.SourceStride;
			std::memset(scratch, 0, std::size_t(registers) * 4 * sizeof(float));
			for (const RsxShaderProgram::RsxInstanceField& field : array.Fields) {
				if (field.RegisterOffset + field.RegisterCount > registers) {
					continue;
				}
				std::memcpy(&scratch[field.RegisterOffset * 4], source + field.SourceOffset, field.ByteSize);
			}
			// The count is in FLOATS, not in constant registers - librsx splits it as `count >> 5` blocks of
			// eight registers plus a remainder, so passing the register count uploads a quarter of the
			// element and leaves the rest of it undefined
			rsxSetVertexProgramConstants(_context, array.BaseRegister + element * registers, registers * 4, scratch);
		}
	}

	const rsxProgramAttrib* RsxDevice::FindVertexAttribute(const rsxVertexProgram* program, const char* name)
	{
		if (program == nullptr || name == nullptr) {
			return nullptr;
		}
		// The generated stages reach cgcomp as members of the Cg entry point's input struct, so what ends up
		// in the attribute table is "_input.aQuadCorner" rather than the name the engine and the shader
		// source use. Asking librsx for the bare name finds nothing at all, which is silent: the stream never
		// gets bound and the stage reads whatever the last draw left in that register. The qualifier is
		// therefore matched as a suffix, which also keeps the hand-written present shader - whose attributes
		// are unqualified - working through the same path.
		const std::uint16_t count = rsxVertexProgramGetNumAttrib(program);
		const rsxProgramAttrib* attribs = rsxVertexProgramGetAttribs(program);
		if (attribs == nullptr) {
			return nullptr;
		}
		const char* base = reinterpret_cast<const char*>(program);
		const std::size_t nameLength = std::strlen(name);
		for (std::uint16_t i = 0; i < count; i++) {
			const char* attribName = base + attribs[i].name_off;
			const std::size_t attribLength = std::strlen(attribName);
			if (attribLength < nameLength) {
				continue;
			}
			if (std::strcmp(attribName + (attribLength - nameLength), name) != 0) {
				continue;
			}
			// Either the whole name or a "<qualifier>." prefix in front of it, never a partial word
			if (attribLength == nameLength || attribName[attribLength - nameLength - 1] == '.') {
				return &attribs[i];
			}
		}
		return nullptr;
	}

	void RsxDevice::ApplyVertexFormat(std::int32_t baseVertex)
	{
		RsxShaderProgram* program = _currentProgram;
		if (program == nullptr || _context == nullptr) {
			return;
		}

		// The sprite shaders have no vertex-ID input - Cg has none to give them - so the Cg lowering reads
		// `aQuadCorner` and, when batched, `aInstanceIndex` instead, and the per-vertex data behind those two
		// comes from the device's own static streams rather than from anything the pipeline bound. They
		// appear in no reflection, so they are looked up in the microcode's own attribute table, exactly as
		// the sceGxm backend does with the same shaders (see GxmShaderProgram::BindStageAttributes()).
		// Without this the corner attribute is fed by whatever the previous draw left in that register.
		std::uint32_t boundRegisters = 0;
		if (const rsxVertexProgram* vertexProgram = program->GetVertexProgram()) {
			const rsxProgramAttrib* cornerAttrib = FindVertexAttribute(vertexProgram, Material::QuadCornerAttributeName);
			const rsxProgramAttrib* instanceAttrib = FindVertexAttribute(vertexProgram, "aInstanceIndex");
			if (cornerAttrib != nullptr || instanceAttrib != nullptr) {
				// The batched stream carries the instance index alongside the corner, six vertices per
				// sprite; the unbatched one is the bare four-vertex strip corner
				const bool batched = (instanceAttrib != nullptr);
				const RsxVram::Block& stream = (batched ? _batchedCornerStream : _quadCornerStream);
				const std::uint8_t stride = (batched ? std::uint8_t(3 * sizeof(float)) : std::uint8_t(2 * sizeof(float)));
				if (stream.IsValid()) {
					if (cornerAttrib != nullptr) {
						rsxBindVertexArrayAttrib(_context, std::uint8_t(cornerAttrib->index), 0, stream.Offset,
							stride, 2, GCM_VERTEX_DATA_TYPE_F32, stream.GetGcmLocation());
						boundRegisters |= (1u << cornerAttrib->index);
					}
					if (instanceAttrib != nullptr) {
						rsxBindVertexArrayAttrib(_context, std::uint8_t(instanceAttrib->index), 0,
							stream.Offset + std::uint32_t(2 * sizeof(float)), stride, 1,
							GCM_VERTEX_DATA_TYPE_F32, stream.GetGcmLocation());
						boundRegisters |= (1u << instanceAttrib->index);
					}
				}
			}
		}

		RsxVertexFormat& format = program->GetVertexFormat();
		for (std::uint32_t i = 0; i < format.GetAttributeCount(); i++) {
			const RsxVertexFormat::Attribute& attribute = format[i];
			if (!attribute.IsEnabled()) {
				continue;
			}

			// The engine's attribute index is a location in the reflection's numbering; the register the
			// compiled stage actually reads comes from the microcode's own table
			const std::int32_t reg = std::int32_t(attribute.GetIndex());
			const RsxBufferObject* vbo = attribute.GetVbo();
			if (vbo == nullptr || vbo->GetGpuData() == nullptr) {
				continue;
			}

			std::uint32_t offset = 0;
			if (rsxAddressToOffset(vbo->GetGpuData(), &offset) != 0) {
				continue;
			}
			offset += attribute.GetBaseOffset() +
				std::uint32_t(reinterpret_cast<std::uintptr_t>(attribute.GetPointer())) +
				std::uint32_t(baseVertex) * std::uint32_t(attribute.GetStride());

			rsxBindVertexArrayAttrib(_context, std::uint8_t(reg), 0, offset,
				std::uint8_t(attribute.GetStride()), std::uint8_t(attribute.GetSize()),
				GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_CELL);
			boundRegisters |= (1u << (reg & 31));
		}

		// A register this draw did not bind still holds the previous draw's stream, and the buffer behind it
		// may since have been freed and handed out again. An element count of zero is how the hardware is
		// told an attribute is absent, so anything left over from the last draw is turned off rather than
		// left pointing somewhere it no longer owns.
		const std::uint32_t staleRegisters = _boundAttributeRegisters & ~boundRegisters;
		if (staleRegisters != 0) {
			for (std::uint32_t reg = 0; reg < MaxVertexAttributeRegisters; reg++) {
				if ((staleRegisters & (1u << reg)) != 0) {
					rsxBindVertexArrayAttrib(_context, std::uint8_t(reg), 0, 0, 0, 0,
						GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_CELL);
				}
			}
		}
		_boundAttributeRegisters = boundRegisters;
	}

	void RsxDevice::DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
		bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		RsxShaderProgram* program = _currentProgram;
		if (_context == nullptr || program == nullptr || !program->IsLinked()) {
			return;
		}

		if (_surfaceDirty) {
			ApplySurface();
		} else {
			// The viewport and scissor are engine state that changes far more often than the target, so they
			// are re-programmed per draw while the surface is not
			std::int32_t targetWidth, targetHeight;
			GetCurrentTargetSize(targetWidth, targetHeight);
			ApplyViewportAndScissor(targetHeight);
		}

		// The whole vertex microcode travels through the FIFO - the fragment side only sends a pointer, because
		// its microcode is fetched from local memory. That makes this by far the largest thing a draw writes,
		// into a ring of about 28 KB, so it is sent only when the program actually changes. Consecutive draws
		// with the same program are the common case in a batched frame, and re-sending a program the RSX is
		// already running would be most of that frame's FIFO traffic.
		if (_lastVertexProgram != program->GetVertexProgram() || _lastVertexUcode != program->GetVertexUcode()) {
			_lastVertexProgram = program->GetVertexProgram();
			_lastVertexUcode = program->GetVertexUcode();
			rsxLoadVertexProgram(_context, program->GetVertexProgram(), program->GetVertexUcode());
			// Which outputs the stage actually writes is not part of the program upload - librsx sends the
			// instructions and nothing else - so the mask cgcomp recorded has to be programmed separately.
			// Left unset it keeps whatever the last program needed, and a stage writing an output the previous
			// one did not has that output discarded; the emulator reports it as the program having no position
			// output and NOPs the shader, which is why the batched stages (mask 0x1c000, one bit wider than
			// anything else here) drew nothing at all.
			rsxSetVertexAttribOutputMask(_context, program->GetVertexProgram()->output_mask);
			rsxFlushBuffer(_context);
		}
		if (_lastFragmentProgram != program->GetFragmentProgram()
			|| _lastFragmentUcodeOffset != program->GetFragmentUcodeBlock().Offset) {
			_lastFragmentProgram = program->GetFragmentProgram();
			_lastFragmentUcodeOffset = program->GetFragmentUcodeBlock().Offset;
			rsxLoadFragmentProgramLocation(_context, program->GetFragmentProgram(),
				program->GetFragmentUcodeBlock().Offset, program->GetFragmentUcodeBlock().GetGcmLocation());
		}

		// Textures. The unit the shader samples comes from the microcode, the one the pipeline bound to from
		// the reflection; they normally agree (see RsxShaderProgram::RsxSamplerBinding).
		bool anyTexture = false;
		for (const RsxShaderProgram::RsxSamplerBinding& sampler : program->GetFragmentSamplers()) {
			const RsxTexture* texture = GetBoundTexture(sampler.EngineUnit);
			if (texture == nullptr) {
				continue;
			}
			if (const gcmTexture* gcmTex = texture->GetGcmTexture()) {
				rsxLoadTexture(_context, std::uint8_t(sampler.HardwareUnit), gcmTex);
				texture->ApplySamplerState(sampler.HardwareUnit);
				anyTexture = true;
			}
		}
		if (anyTexture) {
			// The texture cache does not observe writes the PPE made to texture memory, so it is dropped
			// whenever a draw is about to sample something that may have been uploaded since the last one
			rsxInvalidateTextureCache(_context, GCM_INVALIDATE_TEXTURE);
		}

		UploadUniforms();
		ApplyVertexFormat(indexed ? baseVertex : 0);

		const std::uint32_t type = TranslatePrimitive(primitive);
		if (indexed) {
			const RsxBufferObject* ibo = program->GetVertexFormat().GetIbo();
			if (ibo == nullptr || ibo->GetGpuData() == nullptr) {
				return;
			}
			std::uint32_t offset = 0;
			if (rsxAddressToOffset(ibo->GetGpuData(), &offset) != 0) {
				return;
			}
			offset += std::uint32_t(indexOffset);
			rsxDrawIndexArray(_context, std::uint8_t(type), offset, count,
				indexFormat == IndexFormat::UInt32 ? GCM_INDEX_TYPE_32B : GCM_INDEX_TYPE_16B,
				GCM_LOCATION_CELL);
		} else {
			rsxDrawVertexArray(_context, type, std::uint32_t(firstVertex), count);
		}

		// Hand the commands written so far to the GPU. This is not an optimization but a requirement: the
		// FIFO is a ring, and when the write pointer reaches the end librsx's context callback jumps back to
		// the start and waits for the GPU to have consumed what is there. The GPU only consumes up to the
		// PUT pointer, which nothing but this call advances - so a frame that fills the ring without ever
		// flushing either deadlocks or, as here, has its wrapped commands read as garbage ("Unexpected
		// command 0x..." from RPCS3). A frame of this game emits far more than the ring holds, chiefly
		// because a batched draw writes its whole instance array as constants.
		rsxFlushBuffer(_context);
	}

	void RsxDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, 0);
	}

	void RsxDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		// The RSX has no instanced draw, and the engine's batched shaders do not need one: the batcher
		// already expands each sprite into its own six vertices carrying the instance index (see
		// GetBatchedCornerStream()), so an instanced request is a plain draw of the expanded stream
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices) * std::uint32_t(numInstances),
			false, IndexFormat::UInt16, 0, 0);
	}

	void RsxDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, baseVertex);
	}

	void RsxDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices * std::uint32_t(numInstances), true, indexFormat, indexOffset, baseVertex);
	}
}

#endif
