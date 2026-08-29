#include "GxmDevice.h"
#include "GxmBufferObject.h"
#include "GxmRenderTarget.h"
#include "GxmShaderProgram.h"
#include "GxmTexture.h"
#include "GxmDebug.h"
#include "GxmShaderProgram.h"

#include "../../../../Main.h"

#include <cstdlib>
#include <cstring>

#include <Containers/String.h>

#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vitashark.h>

namespace nCine::RHI::GXM
{
	namespace
	{
		// Ring buffers the context streams its command lists and per-draw uniform data through. Everything but
		// the vertex ring keeps the SDK default; that one is enlarged because every batched draw reserves the
		// whole declared size of its instance array (~64 KB) out of it, and the ring must not wrap back onto
		// data the GPU has not consumed yet within one frame
		constexpr std::uint32_t VdmRingBufferSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
		// sceGxm reserves a draw's *declared* default uniform buffer, so a batched draw costs its whole
		// instance array (~64 KB at BATCH_SIZE 585) however few instances it uses. Measured peak demand of a
		// busy frame is ~320 KB, so this is roughly 25x headroom - enough that the ring cannot wrap within a
		// frame and overwrite uniforms of draws the GPU has not run yet.
		constexpr std::uint32_t VertexRingBufferSize = 8 * 1024 * 1024;
		constexpr std::uint32_t FragmentRingBufferSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
		constexpr std::uint32_t FragmentUsseRingBufferSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
		constexpr std::uint32_t ContextHostMemSize = 16 * 1024;

		// The shader patcher's pools. Every program variant the game loads is patched into these, so they are
		// sized for the whole generated shader set rather than the SDK sample defaults
		constexpr std::uint32_t PatcherBufferSize = 2 * 1024 * 1024;
		constexpr std::uint32_t PatcherVertexUsseSize = 512 * 1024;
		constexpr std::uint32_t PatcherFragmentUsseSize = 512 * 1024;

		// Where the console keeps the Cg compiler. It is part of the firmware rather than the SDK, so a user
		// who has not extracted it gets the explicit diagnostic in CreateSwapchain() instead of a black screen
		constexpr const char* ShaderCompilerModulePath = "app0:/module/libshacccg.suprx";
		constexpr const char* ShaderCompilerModulePathAlt = "ur0:/data/libshacccg.suprx";

		// Built-in clear shader: a full-screen quad in clip space filled with one colour. A tile-based renderer
		// has no "clear the surface" operation - the tile buffer simply starts undefined - so this is what a
		// clear costs here, and what the console's OpenGL|ES drivers do internally as well
		// Vertex inputs are declared as flat parameters rather than gathered into a struct, which is the form
		// sceGxm's own samples use and the one `sceGxmProgramFindParameterByName()` is documented against - a
		// struct member's reflected name is the compiler's choice, and the built-ins are not the place to bet on it
		// The colour travels with the quad rather than in a uniform. A clear is the one draw whose colour has
		// no other way to be checked - every clear in a frame is black except the lighting buffer's, so a colour
		// that never arrives is invisible everywhere else - and this way it depends on nothing but the vertex
		// stream the clear already has to bind.
		constexpr const char* ClearVertexSource = R"(
void main(
	float2 aPosition,
	float4 aColor,
	out float4 gl_Position : POSITION,
	out float4 vColor : TEXCOORD0)
{
	// Depth is written at the far plane, which is what makes this quad clear the depth buffer too
	gl_Position = float4(aPosition, 1.0, 1.0);
	vColor = aColor;
}
)";
		constexpr const char* ClearFragmentSource = R"(
float4 main(float4 vColor : TEXCOORD0) : COLOR
{
	return vColor;
}
)";

		// Built-in present shader: the intermediate screen surface stretched over the display buffer. The V
		// flip that turns the engine's OpenGL-convention (bottom-up) surface into the top-down one the display
		// controller scans out is baked into the quad's texture coordinates
		constexpr const char* PresentVertexSource = R"(
void main(
	float2 aPosition,
	float2 aTexCoords,
	out float4 gl_Position : POSITION,
	out float2 vTexCoords : TEXCOORD0)
{
	gl_Position = float4(aPosition, 0.0, 1.0);
	vTexCoords = aTexCoords;
}
)";
		constexpr const char* PresentFragmentSource = R"(
uniform sampler2D uTexture : TEXUNIT0;

float4 main(float2 vTexCoords : TEXCOORD0) : COLOR
{
	return tex2D(uTexture, vTexCoords);
}
)";

		/** @brief Clip-space quad of the built-in shaders, in the order of a 4-vertex triangle strip */
		constexpr float ClearQuad[] = {
			-1.0f, -1.0f,
			 1.0f, -1.0f,
			-1.0f,  1.0f,
			 1.0f,  1.0f
		};

		/** @brief One vertex of the clear quad: its clip-space corner and the colour being cleared to */
		struct ClearVertex
		{
			float X, Y;
			float R, G, B, A;
		};
		/**
			@brief Clear quads a frame can have in flight

			Each clear writes its own quad, because the GPU consumes a scene long after the call that recorded
			it - overwriting one buffer per clear would hand every clear in the frame the last one's colour. A
			frame of this pipeline issues about seven; the barrier at present time is what makes reuse safe
			across frames.
		*/
		constexpr std::uint32_t ClearQuadRingSize = 16;
		/**
			@brief The same quad with the texture coordinates that flip it vertically

			Clip -Y lands on row 0 of the target (the positive-Y-scale viewport this backend programs
			everywhere), and row 0 is the top of the scanned-out image - so the top of the display has to
			sample the *last* row of the bottom-up screen surface, which is V = 1.
		*/
		constexpr float PresentQuad[] = {
			-1.0f, -1.0f,  0.0f, 1.0f,
			 1.0f, -1.0f,  1.0f, 1.0f,
			-1.0f,  1.0f,  0.0f, 0.0f,
			 1.0f,  1.0f,  1.0f, 0.0f
		};

		/** @brief Corner attribute of the 4-vertex sprite strip (see @ref GxmDevice::GetQuadCornerStream()) */
		constexpr float QuadCorners[] = {
			1.0f, 0.0f,
			1.0f, 1.0f,
			0.0f, 0.0f,
			0.0f, 1.0f
		};
		/** @brief Corner order of the batched six-vertices-per-sprite layout, repeated per sprite */
		constexpr float BatchedCorners[6][2] = {
			{1.0f, 1.0f},
			{0.0f, 1.0f},
			{0.0f, 0.0f},
			{0.0f, 0.0f},
			{1.0f, 0.0f},
			{1.0f, 1.0f}
		};

		inline SceGxmPrimitiveType TranslatePrimitive(PrimitiveType primitive)
		{
			switch (primitive) {
				case PrimitiveType::Points: return SCE_GXM_PRIMITIVE_POINTS;
				case PrimitiveType::Lines: return SCE_GXM_PRIMITIVE_LINES;
				case PrimitiveType::LineLoop:
				case PrimitiveType::LineStrip:
					// The GPU has no line-strip primitive, so a strip is drawn as the independent segments it
					// means, fed by the line-strip index buffer (see EnsureLineStripIndices)
					return SCE_GXM_PRIMITIVE_LINES;
				case PrimitiveType::TriangleStrip: return SCE_GXM_PRIMITIVE_TRIANGLE_STRIP;
				case PrimitiveType::TriangleFan: return SCE_GXM_PRIMITIVE_TRIANGLE_FAN;
				default: return SCE_GXM_PRIMITIVE_TRIANGLES;
			}
		}

		inline SceGxmBlendFactor TranslateBlendFactor(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero: return SCE_GXM_BLEND_FACTOR_ZERO;
				case nCine::BlendingFactor::One: return SCE_GXM_BLEND_FACTOR_ONE;
				case nCine::BlendingFactor::SrcColor: return SCE_GXM_BLEND_FACTOR_SRC_COLOR;
				case nCine::BlendingFactor::OneMinusSrcColor: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
				case nCine::BlendingFactor::SrcAlpha: return SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				case nCine::BlendingFactor::DstAlpha: return SCE_GXM_BLEND_FACTOR_DST_ALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				case nCine::BlendingFactor::DstColor: return SCE_GXM_BLEND_FACTOR_DST_COLOR;
				case nCine::BlendingFactor::OneMinusDstColor: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
				case nCine::BlendingFactor::SrcAlphaSaturate: return SCE_GXM_BLEND_FACTOR_SRC_ALPHA_SATURATE;
				default: return SCE_GXM_BLEND_FACTOR_ONE;
			}
		}

		/** @brief Hands one finished display buffer to the display controller (runs on sceGxm's own queue thread) */
		void DisplayQueueCallback(const void* callbackData)
		{
			const auto* data = static_cast<const GxmDevice::DisplayQueueCallbackData*>(callbackData);

			SceDisplayFrameBuf frameBuf = {};
			frameBuf.size = sizeof(SceDisplayFrameBuf);
			frameBuf.base = data->Address;
			frameBuf.pitch = GxmDevice::DisplayStride;
			frameBuf.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
			frameBuf.width = GxmDevice::DisplayWidth;
			frameBuf.height = GxmDevice::DisplayHeight;
			sceDisplaySetFrameBuf(&frameBuf, SCE_DISPLAY_SETBUF_NEXTFRAME);

			if (data->Vsync) {
				sceDisplayWaitSetFrameBuf();
			}
		}
	}

	// -- Static state --

	GxmDevice::BlendingState GxmDevice::_blending;
	GxmDevice::DepthTestState GxmDevice::_depthTest;
	GxmDevice::CullFaceState GxmDevice::_cullFace;
	GxmDevice::ScissorState GxmDevice::_scissor;
	Recti GxmDevice::_viewport(0, 0, GxmDevice::DisplayWidth, GxmDevice::DisplayHeight);
	Colorf GxmDevice::_clearColor(0.0f, 0.0f, 0.0f, 1.0f);

	GxmShaderProgram* GxmDevice::_currentProgram = nullptr;
	const GxmTexture* GxmDevice::_boundTextures[GxmDevice::MaxTextureUnits] = {};
	GxmDevice::UniformRange GxmDevice::_boundUniformRanges[GxmDevice::MaxUniformBindings];
	GxmRenderTarget* GxmDevice::_currentRenderTarget = nullptr;

	SceGxmContext* GxmDevice::_context = nullptr;
	SceGxmShaderPatcher* GxmDevice::_shaderPatcher = nullptr;
	SceGxmRenderTarget* GxmDevice::_displayRenderTarget = nullptr;

	GxmMemory::Block GxmDevice::_contextHostMem;
	GxmMemory::Block GxmDevice::_vdmRingBuffer;
	GxmMemory::Block GxmDevice::_vertexRingBuffer;
	GxmMemory::Block GxmDevice::_fragmentRingBuffer;
	GxmMemory::Block GxmDevice::_fragmentUsseRingBuffer;
	GxmMemory::Block GxmDevice::_patcherBufferMem;
	GxmMemory::Block GxmDevice::_patcherVertexUsseMem;
	GxmMemory::Block GxmDevice::_patcherFragmentUsseMem;

	GxmMemory::Block GxmDevice::_displayBuffers[GxmDevice::DisplayBufferCount];
	SceGxmColorSurface GxmDevice::_displaySurfaces[GxmDevice::DisplayBufferCount];
	SceGxmSyncObject* GxmDevice::_displaySyncObjects[GxmDevice::DisplayBufferCount] = {};
	std::uint32_t GxmDevice::_backBufferIndex = 0;
	std::uint32_t GxmDevice::_frontBufferIndex = 0;

	GxmMemory::Block GxmDevice::_screenBuffer;
	SceGxmColorSurface GxmDevice::_screenSurface;
	SceGxmTexture GxmDevice::_screenTexture;
	SceGxmSyncObject* GxmDevice::_screenSyncObject = nullptr;

	GxmMemory::Block GxmDevice::_depthBuffer;
	SceGxmDepthStencilSurface GxmDevice::_depthSurface;

	bool GxmDevice::_initialized = false;
	bool GxmDevice::_vsync = true;
	bool GxmDevice::_sceneOpen = false;
	void* GxmDevice::_sceneSurfaceData = nullptr;
	std::uint32_t GxmDevice::_sceneCounter = 0;
	GxmDevice::Telemetry GxmDevice::_telemetry;
	const char* GxmDevice::_sceneLastProgram = nullptr;
	const char* GxmDevice::_lastFinishedSceneProgram = nullptr;
	const char* GxmDevice::_telemetryNextDrawLabel = nullptr;
	bool GxmDevice::_sceneStateApplied = false;
	std::int32_t GxmDevice::_sceneWidth = GxmDevice::DisplayWidth;
	std::int32_t GxmDevice::_sceneHeight = GxmDevice::DisplayHeight;

	SceGxmShaderPatcherId GxmDevice::_clearVertexId = nullptr;
	SceGxmShaderPatcherId GxmDevice::_clearFragmentId = nullptr;
	SceGxmProgram* GxmDevice::_clearVertexStage = nullptr;
	SceGxmProgram* GxmDevice::_clearFragmentStage = nullptr;
	SceGxmVertexProgram* GxmDevice::_clearVertexProgram = nullptr;
	SceGxmFragmentProgram* GxmDevice::_clearFragmentProgram = nullptr;
	std::uint32_t GxmDevice::_clearQuadIndex = 0;
	GxmMemory::Block GxmDevice::_clearVertices;

	SceGxmShaderPatcherId GxmDevice::_presentVertexId = nullptr;
	SceGxmShaderPatcherId GxmDevice::_presentFragmentId = nullptr;
	SceGxmProgram* GxmDevice::_presentVertexStage = nullptr;
	SceGxmProgram* GxmDevice::_presentFragmentStage = nullptr;
	SceGxmVertexProgram* GxmDevice::_presentVertexProgram = nullptr;
	SceGxmFragmentProgram* GxmDevice::_presentFragmentProgram = nullptr;
	GxmMemory::Block GxmDevice::_presentVertices;

	GxmMemory::Block GxmDevice::_sequentialIndices;
	std::uint32_t GxmDevice::_sequentialIndexCount = 0;
	GxmMemory::Block GxmDevice::_lineStripIndices;
	std::uint32_t GxmDevice::_lineStripVertexCount = 0;

	GxmMemory::Block GxmDevice::_quadCornerStream;
	GxmMemory::Block GxmDevice::_batchedCornerStream;
	GxmMemory::Block GxmDevice::_retiredBlocks[GxmDevice::RetiredBlockCount];
	SceGxmNotification GxmDevice::_sceneNotification = {};

	// -- Pipeline state --

	void GxmDevice::SetBlendingEnabled(bool enabled)
	{
		_blending.Enabled = enabled;
	}

	void GxmDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		_blending.SrcRgb = srcRgb;
		_blending.DstRgb = dstRgb;
		_blending.SrcAlpha = srcAlpha;
		_blending.DstAlpha = dstAlpha;
	}

	GxmDevice::BlendingState GxmDevice::GetBlendingState()
	{
		return _blending;
	}

	void GxmDevice::SetBlendingState(const BlendingState& state)
	{
		_blending = state;
	}

	void GxmDevice::SetDepthTestEnabled(bool enabled)
	{
		_depthTest.TestEnabled = enabled;
	}

	void GxmDevice::SetDepthMaskEnabled(bool enabled)
	{
		_depthTest.MaskEnabled = enabled;
	}

	GxmDevice::DepthTestState GxmDevice::GetDepthTestState()
	{
		return _depthTest;
	}

	void GxmDevice::SetDepthTestState(const DepthTestState& state)
	{
		_depthTest = state;
	}

	void GxmDevice::SetCullFaceEnabled(bool enabled)
	{
		_cullFace.Enabled = enabled;
	}

	GxmDevice::CullFaceState GxmDevice::GetCullFaceState()
	{
		return _cullFace;
	}

	void GxmDevice::SetCullFaceState(const CullFaceState& state)
	{
		_cullFace = state;
	}

	GxmDevice::ScissorState GxmDevice::GetScissorState()
	{
		return _scissor;
	}

	void GxmDevice::SetScissorState(const ScissorState& state)
	{
		_scissor = state;
		_sceneStateApplied = false;
	}

	void GxmDevice::SetScissor(const Recti& rect)
	{
		// Handing over a rectangle also *enables* the test, which is what the OpenGL backend's
		// `GLScissorTest::Enable(rect)` does and what the D3D11 and software backends copy: the pipeline gives a
		// command its clip rectangle (RenderCommand::Issue()) without ever enabling the test separately, so a
		// backend that only stored the rectangle here would clip nothing at all
		_scissor.Rect = rect;
		_scissor.Enabled = true;
		_sceneStateApplied = false;
	}

	void GxmDevice::SetScissorTestEnabled(bool enabled)
	{
		_scissor.Enabled = enabled;
		_sceneStateApplied = false;
	}

	Recti GxmDevice::GetViewport()
	{
		return _viewport;
	}

	void GxmDevice::SetViewport(const Recti& rect)
	{
		_viewport = rect;
		_sceneStateApplied = false;
	}

	void GxmDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_viewport = Recti(x, y, width, height);
		_sceneStateApplied = false;
	}

	Colorf GxmDevice::GetClearColor()
	{
		return _clearColor;
	}

	void GxmDevice::SetClearColor(const Colorf& color)
	{
		_clearColor = color;
	}

	void GxmDevice::SetupInitialState()
	{
		_blending = BlendingState();
		_depthTest = DepthTestState();
		_cullFace = CullFaceState();
		_scissor = ScissorState();
		_sceneStateApplied = false;
	}

	// -- Resource binding --

	void GxmDevice::BindProgram(GxmShaderProgram* program)
	{
		_currentProgram = program;
	}

	GxmShaderProgram* GxmDevice::CurrentProgram()
	{
		return _currentProgram;
	}

	void GxmDevice::BindTexture(std::uint32_t unit, const GxmTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			_boundTextures[unit] = texture;
		}
	}

	void GxmDevice::UnbindTexture(const GxmTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (_boundTextures[i] == texture) {
				_boundTextures[i] = nullptr;
			}
		}
	}

	const GxmTexture* GxmDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? _boundTextures[unit] : nullptr);
	}

	void GxmDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			_boundUniformRanges[index].Data = data;
			_boundUniformRanges[index].Size = size;
		}
	}

	void GxmDevice::GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size)
	{
		if (index < MaxUniformBindings) {
			data = _boundUniformRanges[index].Data;
			size = _boundUniformRanges[index].Size;
		} else {
			data = nullptr;
			size = 0;
		}
	}

	void GxmDevice::SetRenderTarget(GxmRenderTarget* renderTarget)
	{
		// No scene is closed here. What matters is the *surface* a scene renders into, not which framebuffer
		// object was bound to reach it, and whether that surface really changed is decided when the next draw
		// needs a scene (see EnsureScene()) - so layering a pass by binding several framebuffers over one
		// texture, which is how the pipeline draws its scene, clipped and overlay layers, keeps adding to the
		// scene already open on it instead of starting one that would discard the layers underneath
		_currentRenderTarget = renderTarget;
	}

	void GxmDevice::UnbindRenderTarget(const GxmRenderTarget* renderTarget)
	{
		// Any open scene is closed, not only one belonging to this target: closing is deferred now (see
		// SetRenderTarget()), so the scene still recording may be the one this target's surface owns even when
		// something else has since been bound - and this runs because that target is being destroyed
		FinishScene();
		if (_currentRenderTarget == renderTarget) {
			_currentRenderTarget = nullptr;
		}
	}

	void GxmDevice::OnProgramDestroyed(const GxmShaderProgram* program)
	{
		if (_currentProgram == program) {
			_currentProgram = nullptr;
		}
	}

	SceGxmShaderPatcher* GxmDevice::GetShaderPatcher()
	{
		return _shaderPatcher;
	}

	SceGxmContext* GxmDevice::GetContext()
	{
		return _context;
	}

	void GxmDevice::SetTelemetryDrawLabel(const char* label)
	{
		_telemetryNextDrawLabel = label;
	}

	GxmDevice::Telemetry GxmDevice::GetAndResetTelemetry()
	{
		Telemetry telemetry = _telemetry;
		_telemetry = {};
		return telemetry;
	}

	const void* GxmDevice::GetQuadCornerStream()
	{
		return _quadCornerStream.Base;
	}

	const void* GxmDevice::GetBatchedCornerStream()
	{
		return _batchedCornerStream.Base;
	}

	void GxmDevice::RetireBlock(GxmMemory::Block& block)
	{
		if (!block.IsValid()) {
			return;
		}
		// Held until the frame's barrier has passed, because a scene already recorded may still read it. The
		// table is small and a grow-only buffer stops growing almost immediately; if it ever fills, the block
		// is released the safe way instead.
		for (GxmMemory::Block& retired : _retiredBlocks) {
			if (!retired.IsValid()) {
				retired = block;
				block = GxmMemory::Block();
				return;
			}
		}
		FinishScene();
		sceGxmFinish(_context);
		GxmMemory::Free(block);
	}

	void GxmDevice::ReleaseRetiredBlocks()
	{
		for (GxmMemory::Block& retired : _retiredBlocks) {
			GxmMemory::Free(retired);
		}
	}

	void GxmDevice::SetDepthStateBothFaces(SceGxmDepthFunc func, SceGxmDepthWriteMode write)
	{
		sceGxmSetFrontDepthFunc(_context, func);
		sceGxmSetBackDepthFunc(_context, func);
		sceGxmSetFrontDepthWriteEnable(_context, write);
		sceGxmSetBackDepthWriteEnable(_context, write);
	}

	void GxmDevice::SetFragmentProgramEnabledBothFaces(SceGxmFragmentProgramMode mode)
	{
		sceGxmSetFrontFragmentProgramEnable(_context, mode);
		sceGxmSetBackFragmentProgramEnable(_context, mode);
	}

	// -- Scene management --

	void GxmDevice::GetCurrentTarget(SceGxmRenderTarget*& renderTarget, SceGxmColorSurface*& colorSurface,
		SceGxmDepthStencilSurface*& depthSurface, SceGxmSyncObject*& syncObject, std::int32_t& width, std::int32_t& height)
	{
		if (_currentRenderTarget != nullptr &&
			_currentRenderTarget->GetSceneTarget(renderTarget, colorSurface, syncObject, width, height)) {
			// Jazz2's off-screen viewports have no depth/stencil attachment. Reusing the display-sized
			// depth surface here corrupts memory when a level background target is larger than 960x544.
			depthSurface = nullptr;
			return;
		}

		// The screen surface is written by the frame and sampled by the present blit, so it is a
		// render-to-texture hand-off like any other and carries its own sync object (see
		// GxmRenderTarget::GetSceneTarget())
		renderTarget = _displayRenderTarget;
		colorSurface = &_screenSurface;
		depthSurface = &_depthSurface;
		syncObject = _screenSyncObject;
		width = DisplayWidth;
		height = DisplayHeight;
	}

	bool GxmDevice::EnsureScene()
	{
		if (_context == nullptr) {
			return false;
		}

		SceGxmRenderTarget* renderTarget = nullptr;
		SceGxmColorSurface* colorSurface = nullptr;
		SceGxmDepthStencilSurface* depthSurface = nullptr;
		SceGxmSyncObject* syncObject = nullptr;
		std::int32_t width = 0, height = 0;
		GetCurrentTarget(renderTarget, colorSurface, depthSurface, syncObject, width, height);
		if (renderTarget == nullptr || colorSurface == nullptr) {
			return false;
		}

		// Which texels the target writes is what tells one scene from another, because a scene cannot be
		// reopened without losing what is already in it - its colour tiles start undefined rather than loaded,
		// so a second scene over a surface publishes garbage where the first one had drawn. The pipeline hands
		// each layer of a pass its own framebuffer object over the *same* texture and expects every layer to
		// accumulate (they are all ClearMode::Never), which is only true here while they share one scene.
		void* surfaceData = sceGxmColorSurfaceGetData(colorSurface);
		if (_sceneOpen) {
			if (surfaceData == _sceneSurfaceData) {
				if (!_sceneStateApplied) {
					ApplyViewportAndScissor();
					_sceneStateApplied = true;
				}
				return true;
			}
			FinishScene();
		}

		const std::int32_t result = sceGxmBeginScene(_context, 0, renderTarget, nullptr, nullptr, syncObject,
			colorSurface, depthSurface);
		if (result < 0) {
			LOGE("sceGxmBeginScene({}x{}) failed with 0x{:.8x}", width, height, std::uint32_t(result));
			return false;
		}

		_sceneOpen = true;
		_telemetry.SceneBegins++;
		_sceneSurfaceData = surfaceData;
		_sceneLastProgram = nullptr;
		_sceneWidth = width;
		_sceneHeight = height;
		ApplyViewportAndScissor();
		_sceneStateApplied = true;
		return true;
	}

	void GxmDevice::FinishScene()
	{
		if (!_sceneOpen || _context == nullptr) {
			return;
		}
		// A scene's output is sampled by a later scene in the same frame all along the pipeline's chain (the
		// blur passes read the scene view, the composite reads all of them), and sharing one context is not
		// enough to make that hand-over safe: this scene's tile writeback is still in flight while the next one
		// records, so the reader can sample what the surface held before. Measured on the console - waiting
		// here is what makes the level view arrive intact, and not waiting leaves it holding just its clear.
		//
		// The notification is sceGxm's own answer to that: the GPU writes `value` to `address` when this
		// scene's fragment phase completes, so the wait is for this one scene rather than for the whole
		// pipeline the way sceGxmFinish() would be. A tighter version would wait only when the next scene
		// really samples this surface, which needs a scene's bindings to be known before it begins.
		_sceneNotification.value++;
		sceGxmEndScene(_context, nullptr, &_sceneNotification);
		_telemetry.SceneFinishes++;
		const std::uint64_t waitStart = sceKernelGetProcessTimeWide();
		sceGxmNotificationWait(&_sceneNotification);
		_telemetry.NotificationWaits++;
		const std::uint64_t waitMicroseconds = sceKernelGetProcessTimeWide() - waitStart;
		_telemetry.NotificationWaitMicroseconds += waitMicroseconds;
		if (_sceneLastProgram != nullptr) {
			_lastFinishedSceneProgram = _sceneLastProgram;
			for (Telemetry::ShaderDraw& draw : _telemetry.ShaderDraws) {
				if (draw.ProgramName != nullptr && std::strcmp(draw.ProgramName, _sceneLastProgram) == 0) {
					draw.SceneEnds++;
					draw.WaitMicroseconds += waitMicroseconds;
					break;
				}
			}
		}
		_sceneOpen = false;
		_sceneSurfaceData = nullptr;
		_sceneLastProgram = nullptr;
		_sceneStateApplied = false;
	}

	void GxmDevice::ApplyViewportAndScissor()
	{
		if (_context == nullptr) {
			return;
		}

		const std::int32_t targetWidth = _sceneWidth;
		const std::int32_t targetHeight = _sceneHeight;

		// Every surface is stored bottom-up like OpenGL (see the class documentation), so a positive Y scale
		// is what maps clip -Y onto row 0 - and the viewport's Y, being an OpenGL one measured from the
		// bottom, is then already a row index
		const float halfWidth = float(_viewport.W) * 0.5f;
		const float halfHeight = float(_viewport.H) * 0.5f;
		sceGxmSetViewport(_context,
			float(_viewport.X) + halfWidth, halfWidth,
			float(_viewport.Y) + halfHeight, halfHeight,
			0.5f, 0.5f);

		if (_scissor.Enabled) {
			// The maximum of a region clip is inclusive
			const std::int32_t xMin = (_scissor.Rect.X > 0 ? _scissor.Rect.X : 0);
			const std::int32_t yMin = (_scissor.Rect.Y > 0 ? _scissor.Rect.Y : 0);
			std::int32_t xMax = _scissor.Rect.X + _scissor.Rect.W - 1;
			std::int32_t yMax = _scissor.Rect.Y + _scissor.Rect.H - 1;
			if (xMax >= targetWidth) { xMax = targetWidth - 1; }
			if (yMax >= targetHeight) { yMax = targetHeight - 1; }
			if (xMax < xMin || yMax < yMin) {
				// An empty scissor rectangle has to reject everything, which "clip outside an empty region"
				// cannot express - a 1x1 region outside the surface is the closest equivalent
				sceGxmSetRegionClip(_context, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, 0, 0);
			} else {
				// Region clips use the same top-down coordinates as the engine's scissor rectangles. The viewport
				// transform handles the surface's bottom-up storage; mirroring here clips the lower menu rows early.
				sceGxmSetRegionClip(_context, SCE_GXM_REGION_CLIP_OUTSIDE,
					std::uint32_t(xMin), std::uint32_t(yMin), std::uint32_t(xMax), std::uint32_t(yMax));
			}
		} else {
			sceGxmSetRegionClip(_context, SCE_GXM_REGION_CLIP_NONE, 0, 0, 0, 0);
		}
	}

	bool GxmDevice::EnsureSequentialIndices(std::uint32_t count)
	{
		if (count <= _sequentialIndexCount) {
			return true;
		}
		// 16-bit indexing is limited to values below 64000 by the hardware; a single draw never comes close
		if (count > 63999) {
			LOGW("A draw of {} vertices exceeds the 16-bit index range and was skipped", count);
			return false;
		}

		std::uint32_t newCount = (_sequentialIndexCount > 0 ? _sequentialIndexCount : 4096);
		while (newCount < count) {
			newCount *= 2;
		}
		if (newCount > 63999) {
			newCount = 63999;
		}

		GxmMemory::Block block = GxmMemory::Alloc("Jazz2:SequentialIndices", newCount * sizeof(std::uint16_t), SCE_GXM_MEMORY_ATTRIB_READ);
		if (!block.IsValid()) {
			return false;
		}

		std::uint16_t* indices = static_cast<std::uint16_t*>(block.Base);
		for (std::uint32_t i = 0; i < newCount; i++) {
			indices[i] = std::uint16_t(i);
		}

		// The old buffer may still be referenced by a submitted scene, so it is retired rather than released
		// (see RetireBlock()) - ending the scene to make freeing it safe is what used to throw away everything
		// the pass had drawn, because the draw that follows opens a new scene over the same surface and a
		// colour tile buffer starts undefined
		RetireBlock(_sequentialIndices);
		_sequentialIndices = block;
		_sequentialIndexCount = newCount;
		return true;
	}

	bool GxmDevice::EnsureLineStripIndices(std::uint32_t vertexCount)
	{
		if (vertexCount <= _lineStripVertexCount) {
			return true;
		}
		if (vertexCount > 32000) {
			LOGW("A line strip of {} vertices exceeds the 16-bit index range and was skipped", vertexCount);
			return false;
		}

		std::uint32_t newCount = (_lineStripVertexCount > 0 ? _lineStripVertexCount : 256);
		while (newCount < vertexCount) {
			newCount *= 2;
		}
		if (newCount > 32000) {
			newCount = 32000;
		}

		// Two indices per vertex, so the window starting at 2 * firstVertex reads (first, first+1),
		// (first+1, first+2), ... - the segments of a strip that begins at firstVertex
		GxmMemory::Block block = GxmMemory::Alloc("Jazz2:LineStripIndices",
			newCount * 2u * sizeof(std::uint16_t), SCE_GXM_MEMORY_ATTRIB_READ);
		if (!block.IsValid()) {
			return false;
		}

		std::uint16_t* indices = static_cast<std::uint16_t*>(block.Base);
		for (std::uint32_t i = 0; i < newCount; i++) {
			indices[i * 2 + 0] = std::uint16_t(i);
			indices[i * 2 + 1] = std::uint16_t(i + 1);
		}

		RetireBlock(_lineStripIndices);
		_lineStripIndices = block;
		_lineStripVertexCount = newCount;
		return true;
	}

	// -- Clear --

	void GxmDevice::Clear(ClearFlags flags)
	{
		if (flags == ClearFlags::None || !EnsureScene() || _clearVertexProgram == nullptr || _clearFragmentProgram == nullptr) {
			return;
		}
		if (!EnsureSequentialIndices(4)) {
			return;
		}

		const bool clearColor = ((std::uint32_t(flags) & std::uint32_t(ClearFlags::Color)) != 0);
		const bool clearDepth = ((std::uint32_t(flags) & std::uint32_t(ClearFlags::Depth)) != 0);

		const std::int32_t targetWidth = _sceneWidth;
		const std::int32_t targetHeight = _sceneHeight;

		// glClear covers the whole surface (modulated by the scissor test) rather than the viewport, so the
		// quad is drawn through a full-target viewport and the tracked one is restored afterwards
		sceGxmSetViewport(_context, float(targetWidth) * 0.5f, float(targetWidth) * 0.5f,
			float(targetHeight) * 0.5f, float(targetHeight) * 0.5f, 0.5f, 0.5f);

		sceGxmSetVertexProgram(_context, _clearVertexProgram);
		sceGxmSetFragmentProgram(_context, _clearFragmentProgram);
		sceGxmSetCullMode(_context, SCE_GXM_CULL_NONE);
		SetDepthStateBothFaces(SCE_GXM_DEPTH_FUNC_ALWAYS,
			clearDepth ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
		// A colour-less clear (depth only) still has to rasterize the quad, with the colour write masked off -
		// which the fragment program's blend info carries, so the masked variant is a separate program
		SetFragmentProgramEnabledBothFaces(clearColor
			? SCE_GXM_FRAGMENT_PROGRAM_ENABLED : SCE_GXM_FRAGMENT_PROGRAM_DISABLED);

		// This clear's own quad, carrying the colour with it (see ClearVertex)
		ClearVertex* quad = static_cast<ClearVertex*>(_clearVertices.Base)
			+ (_clearQuadIndex % ClearQuadRingSize) * 4u;
		_clearQuadIndex++;
		for (std::uint32_t i = 0; i < 4; i++) {
			quad[i].X = ClearQuad[i * 2 + 0];
			quad[i].Y = ClearQuad[i * 2 + 1];
			quad[i].R = _clearColor.R;
			quad[i].G = _clearColor.G;
			quad[i].B = _clearColor.B;
			quad[i].A = _clearColor.A;
		}

		sceGxmSetVertexStream(_context, 0, quad);
		sceGxmDraw(_context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, _sequentialIndices.Base, 4);

		SetFragmentProgramEnabledBothFaces(SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
		ApplyViewportAndScissor();
	}

	// -- Draw --

	void GxmDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, 1, 0);
	}

	void GxmDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
		DrawCommon(primitive, firstVertex, std::uint32_t(numVertices), false, IndexFormat::UInt16, 0, numInstances, 0);
	}

	void GxmDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, 1, baseVertex);
	}

	void GxmDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		DrawCommon(primitive, 0, numIndices, true, indexFormat, indexOffset, numInstances, baseVertex);
	}

	void GxmDevice::DrawCommon(PrimitiveType primitive, std::int32_t firstVertex, std::uint32_t count,
		bool indexed, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
		if (count == 0 || _currentProgram == nullptr || !EnsureScene()) {
			return;
		}

		GxmShaderProgram* program = _currentProgram;
		SceGxmVertexProgram* vertexProgram = program->GetVertexProgram();
		if (vertexProgram == nullptr) {
			return;
		}

		SceGxmBlendInfo blendInfo = {};
		const SceGxmBlendInfo* blendInfoPtr = nullptr;
		if (_blending.Enabled) {
			blendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
			blendInfo.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
			blendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
			blendInfo.colorSrc = TranslateBlendFactor(_blending.SrcRgb);
			blendInfo.colorDst = TranslateBlendFactor(_blending.DstRgb);
			blendInfo.alphaSrc = TranslateBlendFactor(_blending.SrcAlpha);
			blendInfo.alphaDst = TranslateBlendFactor(_blending.DstAlpha);
			blendInfoPtr = &blendInfo;
		}
		const std::uint32_t blendKey = GxmShaderProgram::PackBlendKey(_blending.Enabled,
			_blending.SrcRgb, _blending.DstRgb, _blending.SrcAlpha, _blending.DstAlpha);
		SceGxmFragmentProgram* fragmentProgram = program->GetFragmentProgram(blendKey, blendInfoPtr);
		if (fragmentProgram == nullptr) {
			return;
		}

		sceGxmSetVertexProgram(_context, vertexProgram);
		sceGxmSetFragmentProgram(_context, fragmentProgram);
		// A depth-only Clear() switches the fragment stage off; make sure a real draw always has it back on
		SetFragmentProgramEnabledBothFaces(SCE_GXM_FRAGMENT_PROGRAM_ENABLED);

		// OpenGL ties depth writes to the depth test: with GL_DEPTH_TEST disabled, glDepthMask(true) still
		// writes nothing. Keeping that coupling here matters, because a later depth-tested draw would otherwise
		// see values this one was not supposed to leave behind
		const bool depthWrites = (_depthTest.TestEnabled && _depthTest.MaskEnabled);
		SetDepthStateBothFaces(_depthTest.TestEnabled ? SCE_GXM_DEPTH_FUNC_LESS_EQUAL : SCE_GXM_DEPTH_FUNC_ALWAYS,
			depthWrites ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
		if (_cullFace.Enabled) {
			// The engine winds its front faces counter-clockwise in the OpenGL convention, which this backend
			// preserves by storing every surface bottom-up - so the winding reaches the GPU unchanged
			sceGxmSetCullMode(_context, _cullFace.Mode == CullFaceMode::Front ? SCE_GXM_CULL_CCW : SCE_GXM_CULL_CW);
		} else {
			sceGxmSetCullMode(_context, SCE_GXM_CULL_NONE);
		}

		// Uniforms: sceGxm addresses a default uniform buffer in 32-bit components, so a slot's bytes land at
		// its resource index times four. The buffer is per draw and comes out of the context's vertex ring
		void* vertexUniformBuffer = nullptr;
		if (program->GetVertexUniformBufferSize() > 0) {
			if (sceGxmReserveVertexDefaultUniformBuffer(_context, &vertexUniformBuffer) >= 0 && vertexUniformBuffer != nullptr) {
				UploadUniforms(vertexUniformBuffer, program, program->GetVertexUniformSlots(), program->GetVertexBlockUploads());
			} else {
				// Drawing anyway would use whatever the ring buffer still held, which is worse than not drawing
				static bool warned = false;
				if (!warned) {
					warned = true;
					LOGW("Ran out of vertex ring buffer reserving {} bytes of uniforms; some draws were skipped",
						program->GetVertexUniformBufferSize());
				}
				return;
			}
		}

		void* fragmentUniformBuffer = nullptr;
		if (program->GetFragmentUniformBufferSize() > 0) {
			if (sceGxmReserveFragmentDefaultUniformBuffer(_context, &fragmentUniformBuffer) >= 0 && fragmentUniformBuffer != nullptr) {
				UploadUniforms(fragmentUniformBuffer, program, program->GetFragmentUniformSlots(), program->GetFragmentBlockUploads());
			} else {
				static bool warned = false;
				if (!warned) {
					warned = true;
					LOGW("Ran out of fragment ring buffer reserving {} bytes of uniforms; some draws were skipped",
						program->GetFragmentUniformBufferSize());
				}
				return;
			}
		}

		BindUniformBufferContainers(true, program->GetVertexBlockUploads());
		BindUniformBufferContainers(false, program->GetFragmentBlockUploads());

		for (const GxmShaderProgram::GxmSamplerSlot& slot : program->GetVertexSamplerSlots()) {
			const GxmTexture* texture = GetBoundTexture(slot.EngineUnit);
			if (texture != nullptr) {
				if (const SceGxmTexture* gxmTexture = texture->GetGxmTexture()) {
					sceGxmSetVertexTexture(_context, slot.TextureIndex, gxmTexture);
				}
			}
		}
		for (const GxmShaderProgram::GxmSamplerSlot& slot : program->GetFragmentSamplerSlots()) {
			const GxmTexture* texture = GetBoundTexture(slot.EngineUnit);
			if (texture != nullptr) {
				if (const SceGxmTexture* gxmTexture = texture->GetGxmTexture()) {
					sceGxmSetFragmentTexture(_context, slot.TextureIndex, gxmTexture);
				}
			}
		}

		// Vertex streams. The static corner stream is where the vertex-ID-free sprite shaders read their quad
		// corner (and batched instance index) from; everything else comes from the pipeline's vertex buffer
		if (program->UsesStaticCornerStream()) {
			const void* stream = (program->UsesBatchedCornerStream() ? GetBatchedCornerStream() : GetQuadCornerStream());
			if (stream == nullptr) {
				return;
			}
			sceGxmSetVertexStream(_context, program->GetStaticStreamIndex(), stream);
		}
		// A non-indexed draw is reproduced with a window of the shared increasing-index buffer, and the window
		// can start at the first vertex or the stream can - the pipeline suballocates its geometry out of one
		// large buffer, so `firstVertex` is where this command's vertices begin in it and is routinely large.
		// Advancing the *stream* keeps the indices at 0..count-1, so a draw only ever needs as many indices as
		// it has vertices instead of as many as its offset into the buffer; the alternative reaches sceGxm's
		// 16-bit index ceiling (63999) on a big enough buffer and silently drops the draw. Only the geometry
		// stream can absorb it - the static corner streams are indexed from zero by definition, and the draws
		// that read them always pass a first vertex of zero anyway.
		const bool foldFirstVertexIntoStream = (!indexed && !program->UsesStaticCornerStream() && firstVertex > 0);
		if (program->UsesGeometryStream()) {
			const GxmBufferObject* vbo = program->GetBoundVbo();
			const void* vertexData = (vbo != nullptr ? vbo->GetGpuData() : nullptr);
			if (vertexData == nullptr) {
				return;
			}
			// sceGxmDraw has no base-vertex parameter, so a base vertex shifts the stream address instead
			const std::uint32_t stride = program->GetGeometryStride();
			const std::uint8_t* base = static_cast<const std::uint8_t*>(vertexData) + program->GetVboOffset()
				+ std::size_t(baseVertex > 0 ? baseVertex : 0) * stride
				+ std::size_t(foldFirstVertexIntoStream ? firstVertex : 0) * stride;
			sceGxmSetVertexStream(_context, 0, base);
		}

		const SceGxmPrimitiveType primitiveType = TranslatePrimitive(primitive);
		const bool isLineStrip = (primitive == PrimitiveType::LineStrip || primitive == PrimitiveType::LineLoop);
		const void* indexData = nullptr;
		std::uint32_t indexCount = count;
		SceGxmIndexFormat gxmIndexFormat = SCE_GXM_INDEX_FORMAT_U16;
		if (indexed) {
			const GxmBufferObject* ibo = program->GetBoundIbo();
			const void* indexBase = (ibo != nullptr ? ibo->GetGpuData() : nullptr);
			if (indexBase == nullptr) {
				return;
			}
			indexData = static_cast<const std::uint8_t*>(indexBase) + indexOffset;
			gxmIndexFormat = (indexFormat == IndexFormat::UInt32 ? SCE_GXM_INDEX_FORMAT_U32 : SCE_GXM_INDEX_FORMAT_U16);
		} else if (isLineStrip) {
			// A strip of N vertices is N-1 segments, which the paired index buffer spells out - from the pair
			// that starts at the first vertex, or from the very first pair when the stream has already been
			// advanced to it (see foldFirstVertexIntoStream above). Counting the offset in both places shifts
			// the strip twice and it is drawn from whatever vertices happen to sit there.
			const std::uint32_t first = (foldFirstVertexIntoStream ? 0u : std::uint32_t(firstVertex > 0 ? firstVertex : 0));
			if (count < 2 || !EnsureLineStripIndices(first + count)) {
				return;
			}
			indexData = static_cast<const std::uint16_t*>(_lineStripIndices.Base) + first * 2u;
			indexCount = (count - 1) * 2u;
		} else {
			// There is no non-indexed draw: a window of the shared increasing-index buffer reproduces
			// glDrawArrays(first, count) exactly, indices included - starting at zero when the stream has
			// already been advanced to the first vertex (see foldFirstVertexIntoStream above)
			const std::uint32_t first = (foldFirstVertexIntoStream ? 0u : std::uint32_t(firstVertex > 0 ? firstVertex : 0));
			if (!EnsureSequentialIndices(first + count)) {
				return;
			}
			indexData = static_cast<const std::uint16_t*>(_sequentialIndices.Base) + first;
		}

		if (numInstances > 1) {
			// GPU instancing would need both an index buffer whose pattern repeats per instance and a vertex
			// stream declared with SCE_GXM_INDEX_SOURCE_INSTANCE_*, and the engine's layouts provide neither -
			// it batches with the vertex-ID trick instead and never asks for an instanced draw. Rather than
			// silently render one instance and call it done, say so.
			static bool warned = false;
			if (!warned) {
				warned = true;
				LOGW("Instanced draws are not implemented on this backend; {} instances were drawn as one", numInstances);
			}
		}
		sceGxmDraw(_context, primitiveType, gxmIndexFormat, indexData, indexCount);

		const char* programName = (_telemetryNextDrawLabel != nullptr ? _telemetryNextDrawLabel : program->GetProgramName());
		_telemetryNextDrawLabel = nullptr;
		if (programName != nullptr) {
			_sceneLastProgram = programName;
			for (Telemetry::ShaderDraw& draw : _telemetry.ShaderDraws) {
				if (draw.ProgramName == nullptr) {
					draw.ProgramName = programName;
					draw.Calls = 1;
					draw.Indices = indexCount;
					break;
				}
				if (std::strcmp(draw.ProgramName, programName) == 0) {
					draw.Calls++;
					draw.Indices += indexCount;
					break;
				}
			}
		}
	}

	void GxmDevice::UploadUniforms(void* uniformBuffer, GxmShaderProgram* program,
		const SmallVector<GxmUniformSlot, 0>& slots, const SmallVector<GxmShaderProgram::GxmBlockUpload, 0>& blocks)
	{
		// A default uniform buffer is addressed in 32-bit components, so a resolved resource index times four
		// is the byte offset the value belongs at (see GxmUniformSlot)
		std::uint8_t* base = static_cast<std::uint8_t*>(uniformBuffer);
		for (const GxmUniformSlot& slot : slots) {
			if (const std::uint8_t* value = program->ResolveUniform(slot.Name)) {
				std::memcpy(base + slot.ResourceIndex * 4u, value, slot.ByteSize);
			}
		}

		for (const GxmShaderProgram::GxmBlockUpload& upload : blocks) {
			if (upload.Where != GxmShaderProgram::GxmBlockUpload::Destination::DefaultUniformBuffer) {
				continue;		// bound by address instead, see BindUniformBufferContainers()
			}
			const std::uint8_t* data = nullptr;
			std::uint32_t size = 0;
			GetUniformRange(upload.BindingIndex, data, size);
			if (data == nullptr || size <= upload.SourceOffset) {
				continue;
			}

			// Only what the pipeline actually bound is copied - a batched draw binds just the instances it
			// filled, not the whole declared array
			std::uint32_t bytes = size - upload.SourceOffset;
			if (bytes > upload.MaxByteSize) {
				bytes = upload.MaxByteSize;
			}

			if (upload.CompiledStride != 0 && upload.SourceStride != 0
				&& upload.CompiledStride != upload.SourceStride) {
				// A batched instance array the compiler packed tighter than std140 (see GxmBlockUpload): copied
				// as one run only its first element would land where the shader reads it, so it goes element by
				// element, from the pipeline's stride to the shader's
				const std::uint32_t elements = bytes / upload.SourceStride;
				const std::uint32_t elementBytes = (upload.SourceStride < upload.CompiledStride
					? upload.SourceStride : upload.CompiledStride);
				for (std::uint32_t element = 0; element < elements; element++) {
					std::memcpy(base + upload.Index * 4u + element * upload.CompiledStride,
						data + upload.SourceOffset + element * upload.SourceStride, elementBytes);
				}
				continue;
			}

			std::memcpy(base + upload.Index * 4u, data + upload.SourceOffset, bytes);
		}
	}

	void GxmDevice::BindUniformBufferContainers(bool vertexStage, const SmallVector<GxmShaderProgram::GxmBlockUpload, 0>& blocks)
	{
		// A block too large for the uniform registers is not written anywhere - the range the pipeline filled
		// is handed to sceGxm as an address, which is why those buffers are GPU-visible memory. This is a
		// context call rather than a buffer write, so it happens whether or not a default buffer was reserved.
		for (const GxmShaderProgram::GxmBlockUpload& upload : blocks) {
			if (upload.Where != GxmShaderProgram::GxmBlockUpload::Destination::UniformBufferContainer) {
				continue;
			}
			const std::uint8_t* data = nullptr;
			std::uint32_t size = 0;
			GetUniformRange(upload.BindingIndex, data, size);
			if (data == nullptr || size <= upload.SourceOffset) {
				continue;
			}
			const void* base = data + upload.SourceOffset;
			if (vertexStage) {
				sceGxmSetVertexUniformBuffer(_context, upload.Index, base);
			} else {
				sceGxmSetFragmentUniformBuffer(_context, upload.Index, base);
			}
		}
	}

	// -- Fences --

	FenceHandle GxmDevice::InsertFence()
	{
		// sceGxm has no lightweight fence object; the pipeline only uses these to know when a buffer range it
		// wrote has been consumed, which sceGxmFinish() answers conservatively (see ClientWaitFence)
		return reinterpret_cast<FenceHandle>(std::uintptr_t(1));
	}

	void GxmDevice::DeleteFence(FenceHandle& fence)
	{
		fence = nullptr;
	}

	bool GxmDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
		static_cast<void>(timeoutNs);
		if (fence == nullptr || _context == nullptr) {
			return true;
		}
		// Waiting for everything is stronger than the caller asked for, but correct - and this is only reached
		// when the pipeline's ring buffers have wrapped, which the enlarged vertex ring makes rare
		FinishScene();
		const std::uint64_t finishStart = sceKernelGetProcessTimeWide();
		sceGxmFinish(_context);
		_telemetry.FinishMicroseconds += sceKernelGetProcessTimeWide() - finishStart;
		return true;
	}

	// -- Session lifecycle --

	bool GxmDevice::CreateBuiltinShaders()
	{
		struct BuiltinStage
		{
			const char* Source;
			shark_type Type;
			SceGxmProgram** Output;
			SceGxmShaderPatcherId* Id;
			const char* Name;
		};

		const BuiltinStage stages[] = {
			{ ClearVertexSource, SHARK_VERTEX_SHADER, &_clearVertexStage, &_clearVertexId, "clear vertex" },
			{ ClearFragmentSource, SHARK_FRAGMENT_SHADER, &_clearFragmentStage, &_clearFragmentId, "clear fragment" },
			{ PresentVertexSource, SHARK_VERTEX_SHADER, &_presentVertexStage, &_presentVertexId, "present vertex" },
			{ PresentFragmentSource, SHARK_FRAGMENT_SHADER, &_presentFragmentStage, &_presentFragmentId, "present fragment" }
		};

		for (const BuiltinStage& stage : stages) {
			// A previous attempt that got this far and then failed left a copy behind (the failure paths tear
			// the session down and the window backend may try again), so release it before overwriting it
			std::free(*stage.Output);
			*stage.Output = nullptr;

			std::uint32_t size = 0;
			bool loadedFromCache = false;
			*stage.Output = GxmShaderProgram::CompileCgStage(stage.Source, stage.Type == SHARK_VERTEX_SHADER, size, loadedFromCache);
			if (*stage.Output == nullptr) {
				LOGE("Failed to compile the built-in {} shader", stage.Name);
				return false;
			}
			LOGD("{} the built-in {} shader ({} bytes of GXP)", loadedFromCache ? "Loaded" : "Compiled", stage.Name, size);
			if (sceGxmShaderPatcherRegisterProgram(_shaderPatcher, *stage.Output, stage.Id) < 0) {
				LOGE("Failed to register the built-in {} shader with the shader patcher", stage.Name);
				return false;
			}
			
		}

		// Resolving an attribute that is not there hands sceGxm a null parameter, which faults rather than
		// failing - so every lookup below goes through the resolver that reports what the stage really declares.
		// When reflection cannot resolve it, the declaration order is used instead: sceGxm assigns attribute
		// registers in that order, and unlike the generated shaders these two have a fixed, known signature -
		// so a built-in shader is the one case where the convention is safe to lean on rather than a guess.
		const auto resolveAttribute = [](const SceGxmProgram* program, const char* name,
			std::uint32_t declarationIndex, std::uint16_t& regIndex) {
			if (const SceGxmProgramParameter* parameter = GxmShaderProgram::FindAttribute(program, name)) {
				regIndex = std::uint16_t(sceGxmProgramParameterGetResourceIndex(parameter));
				return;
			}
			LOGW("Falling back to register {} for the built-in shader's \"{}\" attribute", declarationIndex, name);
			regIndex = std::uint16_t(declarationIndex);
		};

		// Clear: one float2 position per vertex, no blending (it overwrites the surface)
		SceGxmVertexAttribute clearAttribute = {};
		clearAttribute.streamIndex = 0;
		clearAttribute.offset = 0;
		clearAttribute.format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		clearAttribute.componentCount = 2;
		resolveAttribute(_clearVertexStage, "aPosition", 0, clearAttribute.regIndex);
		SceGxmVertexAttribute clearColorAttribute = {};
		clearColorAttribute.streamIndex = 0;
		clearColorAttribute.offset = offsetof(ClearVertex, R);
		clearColorAttribute.format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		clearColorAttribute.componentCount = 4;
		resolveAttribute(_clearVertexStage, "aColor", 1, clearColorAttribute.regIndex);
		const SceGxmVertexAttribute clearAttributes[2] = { clearAttribute, clearColorAttribute };
		SceGxmVertexStream clearStream = {};
		clearStream.stride = sizeof(ClearVertex);
		clearStream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
		if (sceGxmShaderPatcherCreateVertexProgram(_shaderPatcher, _clearVertexId, clearAttributes, 2,
				&clearStream, 1, &_clearVertexProgram) < 0 ||
			sceGxmShaderPatcherCreateFragmentProgram(_shaderPatcher, _clearFragmentId,
				SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, nullptr, _clearVertexStage,
				&_clearFragmentProgram) < 0) {
			LOGE("Failed to create the built-in clear programs");
			return false;
		}

		// Present: position + texture coordinates, no blending
		SceGxmVertexAttribute presentAttributes[2] = {};
		presentAttributes[0].streamIndex = 0;
		presentAttributes[0].offset = 0;
		presentAttributes[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		presentAttributes[0].componentCount = 2;
		presentAttributes[1].streamIndex = 0;
		presentAttributes[1].offset = sizeof(float) * 2;
		presentAttributes[1].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		presentAttributes[1].componentCount = 2;
		resolveAttribute(_presentVertexStage, "aPosition", 0, presentAttributes[0].regIndex);
		resolveAttribute(_presentVertexStage, "aTexCoords", 1, presentAttributes[1].regIndex);
		SceGxmVertexStream presentStream = {};
		presentStream.stride = sizeof(float) * 4;
		presentStream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
		if (sceGxmShaderPatcherCreateVertexProgram(_shaderPatcher, _presentVertexId, presentAttributes, 2,
				&presentStream, 1, &_presentVertexProgram) < 0 ||
			sceGxmShaderPatcherCreateFragmentProgram(_shaderPatcher, _presentFragmentId,
				SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, nullptr, _presentVertexStage,
				&_presentFragmentProgram) < 0) {
			LOGE("Failed to create the built-in present programs");
			return false;
		}

		// Their geometry, in GPU-visible memory like every other stream
		_clearVertices = GxmMemory::Alloc("Jazz2:ClearQuad",
			ClearQuadRingSize * 4u * sizeof(ClearVertex), SCE_GXM_MEMORY_ATTRIB_READ);
		_presentVertices = GxmMemory::Alloc("Jazz2:PresentQuad", sizeof(PresentQuad), SCE_GXM_MEMORY_ATTRIB_READ);
		if (!_clearVertices.IsValid() || !_presentVertices.IsValid()) {
			return false;
		}
		std::memcpy(_presentVertices.Base, PresentQuad, sizeof(PresentQuad));
		return true;
	}

	bool GxmDevice::CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync)
	{
		static_cast<void>(windowHandle);
		static_cast<void>(width);
		static_cast<void>(height);

		if (_initialized) {
			return true;
		}
		_vsync = vsync;
		LOGI("Initializing native GXM RHI (requested {}x{}, vsync {})", width, height, vsync);

		SceGxmInitializeParams initializeParams = {};
		initializeParams.flags = 0;
		initializeParams.displayQueueMaxPendingCount = DisplayBufferCount - 1;
		initializeParams.displayQueueCallback = DisplayQueueCallback;
		initializeParams.displayQueueCallbackDataSize = sizeof(DisplayQueueCallbackData);
		initializeParams.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
		std::int32_t result = sceGxmInitialize(&initializeParams);
		if (result < 0) {
			LOGE("sceGxmInitialize() failed with 0x{:.8x}", std::uint32_t(result));
			return false;
		}

		// From here on every failure exits through DestroySwapchain(), which needs this set to do its work. A
		// half-initialized device would otherwise keep opening scenes it can never present - a black screen
		// with no clue why, instead of the diagnostic that got us here
		_initialized = true;

		// The context's ring buffers, then the context itself
		_contextHostMem = GxmMemory::Alloc("Jazz2:GxmContextHost", ContextHostMemSize, SCE_GXM_MEMORY_ATTRIB_RW);
		_vdmRingBuffer = GxmMemory::Alloc("Jazz2:GxmVdmRing", VdmRingBufferSize, SCE_GXM_MEMORY_ATTRIB_READ);
		_vertexRingBuffer = GxmMemory::Alloc("Jazz2:GxmVertexRing", VertexRingBufferSize, SCE_GXM_MEMORY_ATTRIB_READ);
		_fragmentRingBuffer = GxmMemory::Alloc("Jazz2:GxmFragmentRing", FragmentRingBufferSize, SCE_GXM_MEMORY_ATTRIB_READ);
		_fragmentUsseRingBuffer = GxmMemory::AllocFragmentUsse("Jazz2:GxmFragmentUsseRing", FragmentUsseRingBufferSize);
		if (!_contextHostMem.IsValid() || !_vdmRingBuffer.IsValid() || !_vertexRingBuffer.IsValid() ||
			!_fragmentRingBuffer.IsValid() || !_fragmentUsseRingBuffer.IsValid()) {
			LOGE("Failed to allocate the sceGxm context ring buffers");
			DestroySwapchain();
			return false;
		}

		SceGxmContextParams contextParams = {};
		contextParams.hostMem = _contextHostMem.Base;
		contextParams.hostMemSize = _contextHostMem.Size;
		contextParams.vdmRingBufferMem = _vdmRingBuffer.Base;
		contextParams.vdmRingBufferMemSize = _vdmRingBuffer.Size;
		contextParams.vertexRingBufferMem = _vertexRingBuffer.Base;
		contextParams.vertexRingBufferMemSize = _vertexRingBuffer.Size;
		contextParams.fragmentRingBufferMem = _fragmentRingBuffer.Base;
		contextParams.fragmentRingBufferMemSize = _fragmentRingBuffer.Size;
		contextParams.fragmentUsseRingBufferMem = _fragmentUsseRingBuffer.Base;
		contextParams.fragmentUsseRingBufferMemSize = _fragmentUsseRingBuffer.Size;
		contextParams.fragmentUsseRingBufferOffset = _fragmentUsseRingBuffer.UsseOffset;
		result = sceGxmCreateContext(&contextParams, &_context);
		if (result < 0) {
			LOGE("sceGxmCreateContext() failed with 0x{:.8x}", std::uint32_t(result));
			DestroySwapchain();
			return false;
		}

		// The render target describing the panel's tiling, shared by the screen surface and the display buffers
		SceGxmRenderTargetParams renderTargetParams = {};
		renderTargetParams.flags = 0;
		renderTargetParams.width = DisplayWidth;
		renderTargetParams.height = DisplayHeight;
		renderTargetParams.scenesPerFrame = 8;
		renderTargetParams.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
		renderTargetParams.multisampleLocations = 0;
		renderTargetParams.driverMemBlock = GxmMemory::InvalidUid;
		result = sceGxmCreateRenderTarget(&renderTargetParams, &_displayRenderTarget);
		if (result < 0) {
			LOGE("sceGxmCreateRenderTarget({}x{}) failed with 0x{:.8x}", DisplayWidth, DisplayHeight, std::uint32_t(result));
			DestroySwapchain();
			return false;
		}

		// The display buffers the controller scans out of, with the sync object that keeps the GPU from
		// overwriting one still on screen
		const std::uint32_t displayBufferSize = std::uint32_t(DisplayStride) * std::uint32_t(DisplayHeight) * 4u;
		for (std::uint32_t i = 0; i < DisplayBufferCount; i++) {
			_displayBuffers[i] = GxmMemory::AllocCdram("Jazz2:DisplayBuffer", displayBufferSize, SCE_GXM_MEMORY_ATTRIB_RW);
			if (!_displayBuffers[i].IsValid()) {
				LOGE("Failed to allocate display buffer {}", i);
				DestroySwapchain();
			return false;
			}
			std::memset(_displayBuffers[i].Base, 0, displayBufferSize);

			result = sceGxmColorSurfaceInit(&_displaySurfaces[i], SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
				SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
				DisplayWidth, DisplayHeight, DisplayStride, _displayBuffers[i].Base);
			if (result < 0) {
				LOGE("sceGxmColorSurfaceInit(display {}) failed with 0x{:.8x}", i, std::uint32_t(result));
				DestroySwapchain();
			return false;
			}
			result = sceGxmSyncObjectCreate(&_displaySyncObjects[i]);
			if (result < 0) {
				LOGE("sceGxmSyncObjectCreate({}) failed with 0x{:.8x}", i, std::uint32_t(result));
				DestroySwapchain();
			return false;
			}
		}

		// The intermediate surface every screen-targeted draw lands in, kept bottom-up like OpenGL and
		// flipped into a display buffer at present time
		_screenBuffer = GxmMemory::AllocCdram("Jazz2:ScreenSurface", displayBufferSize, SCE_GXM_MEMORY_ATTRIB_RW);
		if (!_screenBuffer.IsValid()) {
			LOGE("Failed to allocate the intermediate screen surface");
			DestroySwapchain();
			return false;
		}
		std::memset(_screenBuffer.Base, 0, displayBufferSize);
		result = sceGxmColorSurfaceInit(&_screenSurface, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
			SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
			DisplayWidth, DisplayHeight, DisplayStride, _screenBuffer.Base);
		if (result < 0) {
			LOGE("sceGxmColorSurfaceInit(screen) failed with 0x{:.8x}", std::uint32_t(result));
			DestroySwapchain();
			return false;
		}
		result = sceGxmSyncObjectCreate(&_screenSyncObject);
		if (result < 0) {
			LOGE("sceGxmSyncObjectCreate(screen) failed with 0x{:.8x}", std::uint32_t(result));
			_screenSyncObject = nullptr;
			DestroySwapchain();
			return false;
		}
		result = sceGxmTextureInitLinear(&_screenTexture, _screenBuffer.Base, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR,
			DisplayWidth, DisplayHeight, 0);
		if (result < 0) {
			LOGE("sceGxmTextureInitLinear(screen) failed with 0x{:.8x}", std::uint32_t(result));
			DestroySwapchain();
			return false;
		}
		sceGxmTextureSetMinFilter(&_screenTexture, SCE_GXM_TEXTURE_FILTER_POINT);
		sceGxmTextureSetMagFilter(&_screenTexture, SCE_GXM_TEXTURE_FILTER_POINT);
		sceGxmTextureSetUAddrMode(&_screenTexture, SCE_GXM_TEXTURE_ADDR_CLAMP);
		sceGxmTextureSetVAddrMode(&_screenTexture, SCE_GXM_TEXTURE_ADDR_CLAMP);

		// One depth/stencil surface shared by every scene: the renderer is 2D and never needs a depth buffer's
		// contents to outlive a pass, and this stride covers any render target the pipeline creates
		const std::uint32_t depthBufferSize = std::uint32_t(DisplayStride) * std::uint32_t(DisplayHeight) * 4u;
		_depthBuffer = GxmMemory::Alloc("Jazz2:DepthSurface", depthBufferSize, SCE_GXM_MEMORY_ATTRIB_RW);
		if (!_depthBuffer.IsValid()) {
			LOGE("Failed to allocate the depth/stencil surface");
			DestroySwapchain();
			return false;
		}
		result = sceGxmDepthStencilSurfaceInit(&_depthSurface, SCE_GXM_DEPTH_STENCIL_FORMAT_DF32,
			SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, DisplayStride, _depthBuffer.Base, nullptr);
		if (result < 0) {
			LOGE("sceGxmDepthStencilSurfaceInit() failed with 0x{:.8x}", std::uint32_t(result));
			DestroySwapchain();
			return false;
		}
		// Every scene initializes its on-chip tile depth from this value, so setting it to the far plane is
		// what gives each pass the cleared depth buffer an OpenGL frame starts with
		sceGxmDepthStencilSurfaceSetBackgroundDepth(&_depthSurface, 1.0f);

		// The shader patcher every program's vertex/fragment programs are created through
		_patcherBufferMem = GxmMemory::Alloc("Jazz2:PatcherBuffer", PatcherBufferSize, SCE_GXM_MEMORY_ATTRIB_RW);
		_patcherVertexUsseMem = GxmMemory::AllocVertexUsse("Jazz2:PatcherVertexUsse", PatcherVertexUsseSize);
		_patcherFragmentUsseMem = GxmMemory::AllocFragmentUsse("Jazz2:PatcherFragmentUsse", PatcherFragmentUsseSize);
		if (!_patcherBufferMem.IsValid() || !_patcherVertexUsseMem.IsValid() || !_patcherFragmentUsseMem.IsValid()) {
			LOGE("Failed to allocate the sceGxm shader patcher pools");
			DestroySwapchain();
			return false;
		}

		SceGxmShaderPatcherParams patcherParams = {};
		patcherParams.userData = nullptr;
		patcherParams.hostAllocCallback = nullptr;
		patcherParams.hostFreeCallback = nullptr;
		patcherParams.bufferAllocCallback = nullptr;
		patcherParams.bufferFreeCallback = nullptr;
		patcherParams.bufferMem = _patcherBufferMem.Base;
		patcherParams.bufferMemSize = _patcherBufferMem.Size;
		patcherParams.vertexUsseAllocCallback = nullptr;
		patcherParams.vertexUsseFreeCallback = nullptr;
		patcherParams.vertexUsseMem = _patcherVertexUsseMem.Base;
		patcherParams.vertexUsseMemSize = _patcherVertexUsseMem.Size;
		patcherParams.vertexUsseOffset = _patcherVertexUsseMem.UsseOffset;
		patcherParams.fragmentUsseAllocCallback = nullptr;
		patcherParams.fragmentUsseFreeCallback = nullptr;
		patcherParams.fragmentUsseMem = _patcherFragmentUsseMem.Base;
		patcherParams.fragmentUsseMemSize = _patcherFragmentUsseMem.Size;
		patcherParams.fragmentUsseOffset = _patcherFragmentUsseMem.UsseOffset;
		// sceGxm needs its host allocator when no callbacks are given; passing plain malloc/free keeps the
		// patcher's bookkeeping on the C++ heap, which is where the rest of the engine's small allocations live
		patcherParams.hostAllocCallback = [](void* userData, SceSize size) -> void* {
			static_cast<void>(userData);
			return std::malloc(size);
		};
		patcherParams.hostFreeCallback = [](void* userData, void* mem) {
			static_cast<void>(userData);
			std::free(mem);
		};
		result = sceGxmShaderPatcherCreate(&patcherParams, &_shaderPatcher);
		if (result < 0) {
			LOGE("sceGxmShaderPatcherCreate() failed with 0x{:.8x}", std::uint32_t(result));
			DestroySwapchain();
			return false;
		}

		// sceGxm consumes compiled GXP binaries and the SDK ships no offline compiler for them, so the whole
		// shader set is compiled on the console by the firmware's own Cg compiler. A build that bundles the
		// module in its own VPK is served first, the standard shared location second
		const char* shaderCompilerPath = ShaderCompilerModulePath;
		std::int32_t shaderCompilerResult = shark_init(shaderCompilerPath);
		if (shaderCompilerResult < 0) {
			shaderCompilerPath = ShaderCompilerModulePathAlt;
			shaderCompilerResult = shark_init(shaderCompilerPath);
		}
		if (shaderCompilerResult < 0) {
			LOGE("Cannot initialize the runtime Cg compiler: \"libshacccg.suprx\" was not found. This backend "
				"compiles its shaders on the console, so that firmware module has to be extracted and placed "
				"in \"ur0:/data/\" (see the PS Vita section of the console documentation)");
			DestroySwapchain();
			return false;
		}
		LOGI("Runtime Cg compiler loaded from \"{}\"", shaderCompilerPath);

		// A scene's completion notification has to be written into the driver's own notification region
		// (see FinishScene())
		_sceneNotification.address = sceGxmGetNotificationRegion();
		_sceneNotification.value = 0;
		if (_sceneNotification.address == nullptr) {
			LOGE("sceGxmGetNotificationRegion() returned nothing, so scene completion cannot be waited on");
			DestroySwapchain();
			return false;
		}
		*_sceneNotification.address = 0;

		// Before the first compile of anything, so a built-in shader that fails says why
		GxmShaderProgram::InstallCompilerLogCallback();

		if (!CreateBuiltinShaders()) {
			DestroySwapchain();
			return false;
		}

		// The two static streams feeding the vertex-ID-free sprite layouts
		_quadCornerStream = GxmMemory::Alloc("Jazz2:QuadCorners", sizeof(QuadCorners), SCE_GXM_MEMORY_ATTRIB_READ);
		_batchedCornerStream = GxmMemory::Alloc("Jazz2:BatchedCorners", MaxBatchSize * 6u * 3u * sizeof(float), SCE_GXM_MEMORY_ATTRIB_READ);
		if (!_quadCornerStream.IsValid() || !_batchedCornerStream.IsValid()) {
			LOGE("Failed to allocate the static vertex streams");
			DestroySwapchain();
			return false;
		}
		std::memcpy(_quadCornerStream.Base, QuadCorners, sizeof(QuadCorners));
		float* batched = static_cast<float*>(_batchedCornerStream.Base);
		for (std::uint32_t instance = 0; instance < MaxBatchSize; instance++) {
			for (std::uint32_t corner = 0; corner < 6; corner++) {
				float* vertex = batched + (instance * 6u + corner) * 3u;
				vertex[0] = BatchedCorners[corner][0];
				vertex[1] = BatchedCorners[corner][1];
				vertex[2] = float(instance);
			}
		}

		if (!EnsureSequentialIndices(4096)) {
			LOGE("Failed to allocate the shared index buffer");
			DestroySwapchain();
			return false;
		}

		_backBufferIndex = 0;
		_frontBufferIndex = DisplayBufferCount - 1;
		LOGI("sceGxm initialized ({}x{}, {} display buffers, {} KB of GPU memory reserved)",
			DisplayWidth, DisplayHeight, DisplayBufferCount, GxmMemory::GetAllocatedBytes() / 1024);
		return true;
	}

	void GxmDevice::DestroySwapchain()
	{
		if (!_initialized) {
			return;
		}

		FinishScene();
		if (_context != nullptr) {
			sceGxmFinish(_context);
		}
		// A swapchain can be torn down before the next present, leaving grown index buffers held in this
		// table. The finish above guarantees that no submitted draw still references them.
		ReleaseRetiredBlocks();
		sceGxmDisplayQueueFinish();

		shark_end();

		if (_shaderPatcher != nullptr) {
			if (_clearVertexProgram != nullptr) { sceGxmShaderPatcherReleaseVertexProgram(_shaderPatcher, _clearVertexProgram); }
			if (_clearFragmentProgram != nullptr) { sceGxmShaderPatcherReleaseFragmentProgram(_shaderPatcher, _clearFragmentProgram); }
			if (_presentVertexProgram != nullptr) { sceGxmShaderPatcherReleaseVertexProgram(_shaderPatcher, _presentVertexProgram); }
			if (_presentFragmentProgram != nullptr) { sceGxmShaderPatcherReleaseFragmentProgram(_shaderPatcher, _presentFragmentProgram); }
			sceGxmShaderPatcherDestroy(_shaderPatcher);
			_shaderPatcher = nullptr;
		}
		_clearVertexProgram = nullptr;
		_clearFragmentProgram = nullptr;
		_presentVertexProgram = nullptr;
		_presentFragmentProgram = nullptr;
		std::free(_clearVertexStage);
		std::free(_clearFragmentStage);
		std::free(_presentVertexStage);
		std::free(_presentFragmentStage);
		_clearVertexStage = nullptr;
		_clearFragmentStage = nullptr;
		_presentVertexStage = nullptr;
		_presentFragmentStage = nullptr;
		_clearQuadIndex = 0;

		for (std::uint32_t i = 0; i < DisplayBufferCount; i++) {
			if (_displaySyncObjects[i] != nullptr) {
				sceGxmSyncObjectDestroy(_displaySyncObjects[i]);
				_displaySyncObjects[i] = nullptr;
			}
			GxmMemory::Free(_displayBuffers[i]);
		}
		if (_screenSyncObject != nullptr) {
			sceGxmSyncObjectDestroy(_screenSyncObject);
			_screenSyncObject = nullptr;
		}

		if (_displayRenderTarget != nullptr) {
			sceGxmDestroyRenderTarget(_displayRenderTarget);
			_displayRenderTarget = nullptr;
		}
		if (_context != nullptr) {
			sceGxmDestroyContext(_context);
			_context = nullptr;
		}

		GxmMemory::Free(_screenBuffer);
		GxmMemory::Free(_depthBuffer);
		GxmMemory::Free(_clearVertices);
		GxmMemory::Free(_presentVertices);
		GxmMemory::Free(_quadCornerStream);
		GxmMemory::Free(_batchedCornerStream);
		GxmMemory::Free(_sequentialIndices);
		GxmMemory::Free(_lineStripIndices);
		GxmMemory::Free(_patcherBufferMem);
		GxmMemory::Free(_patcherVertexUsseMem);
		GxmMemory::Free(_patcherFragmentUsseMem);
		GxmMemory::Free(_fragmentUsseRingBuffer);
		GxmMemory::Free(_fragmentRingBuffer);
		GxmMemory::Free(_vertexRingBuffer);
		GxmMemory::Free(_vdmRingBuffer);
		GxmMemory::Free(_contextHostMem);
		GxmMemory::ReleaseRetainedSurfaces();
		_sequentialIndexCount = 0;
		_lineStripVertexCount = 0;

		sceGxmTerminate();
		_initialized = false;
	}

	void GxmDevice::ResizeSwapchain(std::int32_t width, std::int32_t height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void GxmDevice::PresentFrame()
	{
		_telemetry.Presents++;
		if (!_initialized || _context == nullptr) {
			return;
		}

		// Close whatever the frame was still drawing into, then flip the screen surface into the buffer the
		// display controller will pick up next
		_currentRenderTarget = nullptr;
		FinishScene();

		if (_presentVertexProgram != nullptr && _presentFragmentProgram != nullptr && EnsureSequentialIndices(4)) {
			const std::int32_t result = sceGxmBeginScene(_context, 0, _displayRenderTarget, nullptr, nullptr,
				_displaySyncObjects[_backBufferIndex], &_displaySurfaces[_backBufferIndex], &_depthSurface);
			if (result >= 0) {
				sceGxmSetViewport(_context, float(DisplayWidth) * 0.5f, float(DisplayWidth) * 0.5f,
					float(DisplayHeight) * 0.5f, float(DisplayHeight) * 0.5f, 0.5f, 0.5f);
				sceGxmSetRegionClip(_context, SCE_GXM_REGION_CLIP_NONE, 0, 0, 0, 0);
				sceGxmSetVertexProgram(_context, _presentVertexProgram);
				sceGxmSetFragmentProgram(_context, _presentFragmentProgram);
				sceGxmSetCullMode(_context, SCE_GXM_CULL_NONE);
				SetDepthStateBothFaces(SCE_GXM_DEPTH_FUNC_ALWAYS, SCE_GXM_DEPTH_WRITE_DISABLED);
				SetFragmentProgramEnabledBothFaces(SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
				sceGxmSetFragmentTexture(_context, 0, &_screenTexture);
				sceGxmSetVertexStream(_context, 0, _presentVertices.Base);
				sceGxmDraw(_context, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, _sequentialIndices.Base, 4);
				sceGxmEndScene(_context, nullptr, nullptr);
			} else {
				LOGE("sceGxmBeginScene(present) failed with 0x{:.8x}", std::uint32_t(result));
			}
		}

		sceGxmPadHeartbeat(&_displaySurfaces[_backBufferIndex], _displaySyncObjects[_backBufferIndex]);

		DisplayQueueCallbackData callbackData;
		callbackData.Address = _displayBuffers[_backBufferIndex].Base;
		callbackData.Vsync = _vsync;
		sceGxmDisplayQueueAddEntry(_displaySyncObjects[_frontBufferIndex], _displaySyncObjects[_backBufferIndex], &callbackData);

		// Wait for the GPU to finish reading this frame before the CPU starts writing the next one.
		//
		// This is not a nicety. The pipeline's vertex, index and uniform ring buffers ARE GPU-visible memory
		// that the engine writes into directly - there is no upload step and therefore no driver in between to
		// serialize those writes against reads still in flight, which is exactly what an OpenGL driver does for
		// the same code. Without this barrier the CPU runs up to `displayQueueMaxPendingCount` frames ahead and
		// overwrites data the GPU has not consumed, which shows up as geometry that is *almost* right and
		// flickers differently every frame. The per-draw default uniform buffer makes it worse: a batched draw
		// reserves its whole declared instance array (~64 KB) however few instances it actually uses, so the
		// ring turns over in a frame or two.
		//
		// The cost is real - CPU and GPU no longer overlap. Removing it means giving every per-frame buffer as
		// many copies as there are frames in flight and cycling them with the display queue, which is a
		// pipeline-wide change rather than a backend one.
		const std::uint64_t finishStart = sceKernelGetProcessTimeWide();
		sceGxmFinish(_context);
		const std::uint64_t finishMicroseconds = sceKernelGetProcessTimeWide() - finishStart;
		_telemetry.FinishMicroseconds += finishMicroseconds;
		if (_lastFinishedSceneProgram != nullptr) {
			for (Telemetry::ShaderDraw& draw : _telemetry.ShaderDraws) {
				if (draw.ProgramName != nullptr && std::strcmp(draw.ProgramName, _lastFinishedSceneProgram) == 0) {
					draw.PresentFinishMicroseconds += finishMicroseconds;
					break;
				}
			}
		}

		// Everything recorded this frame has been consumed, so anything a growing buffer displaced can go
		ReleaseRetiredBlocks();

		_frontBufferIndex = _backBufferIndex;
		_backBufferIndex = (_backBufferIndex + 1) % DisplayBufferCount;
		_sceneCounter++;

		// The next frame starts with the screen surface again, and every scene re-applies the state it needs
		_sceneStateApplied = false;
	}

	// -- Debug markers --

	void GxmDebug::PushGroup(StringView message)
	{
		if (SceGxmContext* context = GxmDevice::GetContext()) {
			sceGxmPushUserMarker(context, String::nullTerminatedView(message).data());
		}
	}

	void GxmDebug::PopGroup()
	{
		if (SceGxmContext* context = GxmDevice::GetContext()) {
			sceGxmPopUserMarker(context);
		}
	}

	void GxmDebug::MessageInsert(StringView message)
	{
		if (SceGxmContext* context = GxmDevice::GetContext()) {
			sceGxmSetUserMarker(context, String::nullTerminatedView(message).data());
		}
	}
}
