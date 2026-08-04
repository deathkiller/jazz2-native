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

	GxmDevice::BlendingState GxmDevice::blending_;
	GxmDevice::DepthTestState GxmDevice::depthTest_;
	GxmDevice::CullFaceState GxmDevice::cullFace_;
	GxmDevice::ScissorState GxmDevice::scissor_;
	Recti GxmDevice::viewport_(0, 0, GxmDevice::DisplayWidth, GxmDevice::DisplayHeight);
	Colorf GxmDevice::clearColor_(0.0f, 0.0f, 0.0f, 1.0f);

	GxmShaderProgram* GxmDevice::currentProgram_ = nullptr;
	const GxmTexture* GxmDevice::boundTextures_[GxmDevice::MaxTextureUnits] = {};
	GxmDevice::UniformRange GxmDevice::boundUniformRanges_[GxmDevice::MaxUniformBindings];
	GxmRenderTarget* GxmDevice::currentRenderTarget_ = nullptr;

	SceGxmContext* GxmDevice::context_ = nullptr;
	SceGxmShaderPatcher* GxmDevice::shaderPatcher_ = nullptr;
	SceGxmRenderTarget* GxmDevice::displayRenderTarget_ = nullptr;

	GxmMemory::Block GxmDevice::contextHostMem_;
	GxmMemory::Block GxmDevice::vdmRingBuffer_;
	GxmMemory::Block GxmDevice::vertexRingBuffer_;
	GxmMemory::Block GxmDevice::fragmentRingBuffer_;
	GxmMemory::Block GxmDevice::fragmentUsseRingBuffer_;
	GxmMemory::Block GxmDevice::patcherBufferMem_;
	GxmMemory::Block GxmDevice::patcherVertexUsseMem_;
	GxmMemory::Block GxmDevice::patcherFragmentUsseMem_;

	GxmMemory::Block GxmDevice::displayBuffers_[GxmDevice::DisplayBufferCount];
	SceGxmColorSurface GxmDevice::displaySurfaces_[GxmDevice::DisplayBufferCount];
	SceGxmSyncObject* GxmDevice::displaySyncObjects_[GxmDevice::DisplayBufferCount] = {};
	std::uint32_t GxmDevice::backBufferIndex_ = 0;
	std::uint32_t GxmDevice::frontBufferIndex_ = 0;

	GxmMemory::Block GxmDevice::screenBuffer_;
	SceGxmColorSurface GxmDevice::screenSurface_;
	SceGxmTexture GxmDevice::screenTexture_;
	SceGxmSyncObject* GxmDevice::screenSyncObject_ = nullptr;

	GxmMemory::Block GxmDevice::depthBuffer_;
	SceGxmDepthStencilSurface GxmDevice::depthSurface_;

	bool GxmDevice::initialized_ = false;
	bool GxmDevice::vsync_ = true;
	bool GxmDevice::sceneOpen_ = false;
	void* GxmDevice::sceneSurfaceData_ = nullptr;
	std::uint32_t GxmDevice::sceneCounter_ = 0;
	bool GxmDevice::sceneStateApplied_ = false;
	std::int32_t GxmDevice::sceneWidth_ = GxmDevice::DisplayWidth;
	std::int32_t GxmDevice::sceneHeight_ = GxmDevice::DisplayHeight;

	SceGxmShaderPatcherId GxmDevice::clearVertexId_ = nullptr;
	SceGxmShaderPatcherId GxmDevice::clearFragmentId_ = nullptr;
	SceGxmVertexProgram* GxmDevice::clearVertexProgram_ = nullptr;
	SceGxmFragmentProgram* GxmDevice::clearFragmentProgram_ = nullptr;
	std::uint32_t GxmDevice::clearQuadIndex_ = 0;
	GxmMemory::Block GxmDevice::clearVertices_;

	SceGxmShaderPatcherId GxmDevice::presentVertexId_ = nullptr;
	SceGxmShaderPatcherId GxmDevice::presentFragmentId_ = nullptr;
	SceGxmVertexProgram* GxmDevice::presentVertexProgram_ = nullptr;
	SceGxmFragmentProgram* GxmDevice::presentFragmentProgram_ = nullptr;
	GxmMemory::Block GxmDevice::presentVertices_;

	GxmMemory::Block GxmDevice::sequentialIndices_;
	std::uint32_t GxmDevice::sequentialIndexCount_ = 0;
	GxmMemory::Block GxmDevice::lineStripIndices_;
	std::uint32_t GxmDevice::lineStripVertexCount_ = 0;

	GxmMemory::Block GxmDevice::quadCornerStream_;
	GxmMemory::Block GxmDevice::batchedCornerStream_;
	GxmMemory::Block GxmDevice::retiredBlocks_[GxmDevice::RetiredBlockCount];
	SceGxmNotification GxmDevice::sceneNotification_ = {};

	// -- Pipeline state --

	void GxmDevice::SetBlendingEnabled(bool enabled)
	{
		blending_.Enabled = enabled;
	}

	void GxmDevice::SetBlendingFactors(nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb, nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		blending_.SrcRgb = srcRgb;
		blending_.DstRgb = dstRgb;
		blending_.SrcAlpha = srcAlpha;
		blending_.DstAlpha = dstAlpha;
	}

	GxmDevice::BlendingState GxmDevice::GetBlendingState()
	{
		return blending_;
	}

	void GxmDevice::SetBlendingState(const BlendingState& state)
	{
		blending_ = state;
	}

	void GxmDevice::SetDepthTestEnabled(bool enabled)
	{
		depthTest_.TestEnabled = enabled;
	}

	void GxmDevice::SetDepthMaskEnabled(bool enabled)
	{
		depthTest_.MaskEnabled = enabled;
	}

	GxmDevice::DepthTestState GxmDevice::GetDepthTestState()
	{
		return depthTest_;
	}

	void GxmDevice::SetDepthTestState(const DepthTestState& state)
	{
		depthTest_ = state;
	}

	void GxmDevice::SetCullFaceEnabled(bool enabled)
	{
		cullFace_.Enabled = enabled;
	}

	GxmDevice::CullFaceState GxmDevice::GetCullFaceState()
	{
		return cullFace_;
	}

	void GxmDevice::SetCullFaceState(const CullFaceState& state)
	{
		cullFace_ = state;
	}

	GxmDevice::ScissorState GxmDevice::GetScissorState()
	{
		return scissor_;
	}

	void GxmDevice::SetScissorState(const ScissorState& state)
	{
		scissor_ = state;
		sceneStateApplied_ = false;
	}

	void GxmDevice::SetScissor(const Recti& rect)
	{
		// Handing over a rectangle also *enables* the test, which is what the OpenGL backend's
		// `GLScissorTest::Enable(rect)` does and what the D3D11 and software backends copy: the pipeline gives a
		// command its clip rectangle (RenderCommand::Issue()) without ever enabling the test separately, so a
		// backend that only stored the rectangle here would clip nothing at all
		scissor_.Rect = rect;
		scissor_.Enabled = true;
		sceneStateApplied_ = false;
	}

	void GxmDevice::SetScissorTestEnabled(bool enabled)
	{
		scissor_.Enabled = enabled;
		sceneStateApplied_ = false;
	}

	Recti GxmDevice::GetViewport()
	{
		return viewport_;
	}

	void GxmDevice::SetViewport(const Recti& rect)
	{
		viewport_ = rect;
		sceneStateApplied_ = false;
	}

	void GxmDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		viewport_ = Recti(x, y, width, height);
		sceneStateApplied_ = false;
	}

	Colorf GxmDevice::GetClearColor()
	{
		return clearColor_;
	}

	void GxmDevice::SetClearColor(const Colorf& color)
	{
		clearColor_ = color;
	}

	void GxmDevice::SetupInitialState()
	{
		blending_ = BlendingState();
		depthTest_ = DepthTestState();
		cullFace_ = CullFaceState();
		scissor_ = ScissorState();
		sceneStateApplied_ = false;
	}

	// -- Resource binding --

	void GxmDevice::BindProgram(GxmShaderProgram* program)
	{
		currentProgram_ = program;
	}

	GxmShaderProgram* GxmDevice::CurrentProgram()
	{
		return currentProgram_;
	}

	void GxmDevice::BindTexture(std::uint32_t unit, const GxmTexture* texture)
	{
		if (unit < MaxTextureUnits) {
			boundTextures_[unit] = texture;
		}
	}

	void GxmDevice::UnbindTexture(const GxmTexture* texture)
	{
		for (std::uint32_t i = 0; i < MaxTextureUnits; i++) {
			if (boundTextures_[i] == texture) {
				boundTextures_[i] = nullptr;
			}
		}
	}

	const GxmTexture* GxmDevice::GetBoundTexture(std::uint32_t unit)
	{
		return (unit < MaxTextureUnits ? boundTextures_[unit] : nullptr);
	}

	void GxmDevice::BindUniformRange(std::uint32_t index, const std::uint8_t* data, std::uint32_t size)
	{
		if (index < MaxUniformBindings) {
			boundUniformRanges_[index].Data = data;
			boundUniformRanges_[index].Size = size;
		}
	}

	void GxmDevice::GetUniformRange(std::uint32_t index, const std::uint8_t*& data, std::uint32_t& size)
	{
		if (index < MaxUniformBindings) {
			data = boundUniformRanges_[index].Data;
			size = boundUniformRanges_[index].Size;
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
		currentRenderTarget_ = renderTarget;
	}

	void GxmDevice::UnbindRenderTarget(const GxmRenderTarget* renderTarget)
	{
		// Any open scene is closed, not only one belonging to this target: closing is deferred now (see
		// SetRenderTarget()), so the scene still recording may be the one this target's surface owns even when
		// something else has since been bound - and this runs because that target is being destroyed
		FinishScene();
		if (currentRenderTarget_ == renderTarget) {
			currentRenderTarget_ = nullptr;
		}
	}

	void GxmDevice::OnProgramDestroyed(const GxmShaderProgram* program)
	{
		if (currentProgram_ == program) {
			currentProgram_ = nullptr;
		}
	}

	SceGxmShaderPatcher* GxmDevice::GetShaderPatcher()
	{
		return shaderPatcher_;
	}

	SceGxmContext* GxmDevice::GetContext()
	{
		return context_;
	}

	std::uint32_t GxmDevice::GetSceneCounter()
	{
		return sceneCounter_;
	}

	const void* GxmDevice::GetQuadCornerStream()
	{
		return quadCornerStream_.Base;
	}

	const void* GxmDevice::GetBatchedCornerStream()
	{
		return batchedCornerStream_.Base;
	}

	void GxmDevice::RetireBlock(GxmMemory::Block& block)
	{
		if (!block.IsValid()) {
			return;
		}
		// Held until the frame's barrier has passed, because a scene already recorded may still read it. The
		// table is small and a grow-only buffer stops growing almost immediately; if it ever fills, the block
		// is released the safe way instead.
		for (GxmMemory::Block& retired : retiredBlocks_) {
			if (!retired.IsValid()) {
				retired = block;
				block = GxmMemory::Block();
				return;
			}
		}
		FinishScene();
		sceGxmFinish(context_);
		GxmMemory::Free(block);
	}

	void GxmDevice::ReleaseRetiredBlocks()
	{
		for (GxmMemory::Block& retired : retiredBlocks_) {
			GxmMemory::Free(retired);
		}
	}

	void GxmDevice::SetDepthStateBothFaces(SceGxmDepthFunc func, SceGxmDepthWriteMode write)
	{
		sceGxmSetFrontDepthFunc(context_, func);
		sceGxmSetBackDepthFunc(context_, func);
		sceGxmSetFrontDepthWriteEnable(context_, write);
		sceGxmSetBackDepthWriteEnable(context_, write);
	}

	void GxmDevice::SetFragmentProgramEnabledBothFaces(SceGxmFragmentProgramMode mode)
	{
		sceGxmSetFrontFragmentProgramEnable(context_, mode);
		sceGxmSetBackFragmentProgramEnable(context_, mode);
	}

	// -- Scene management --

	void GxmDevice::GetCurrentTarget(SceGxmRenderTarget*& renderTarget, SceGxmColorSurface*& colorSurface,
		SceGxmDepthStencilSurface*& depthSurface, SceGxmSyncObject*& syncObject, std::int32_t& width, std::int32_t& height)
	{
		depthSurface = &depthSurface_;
		if (currentRenderTarget_ != nullptr &&
			currentRenderTarget_->GetSceneTarget(renderTarget, colorSurface, syncObject, width, height)) {
			return;
		}

		// The screen surface is written by the frame and sampled by the present blit, so it is a
		// render-to-texture hand-off like any other and carries its own sync object (see
		// GxmRenderTarget::GetSceneTarget())
		renderTarget = displayRenderTarget_;
		colorSurface = &screenSurface_;
		syncObject = screenSyncObject_;
		width = DisplayWidth;
		height = DisplayHeight;
	}

	bool GxmDevice::EnsureScene()
	{
		if (context_ == nullptr) {
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
		if (sceneOpen_) {
			if (surfaceData == sceneSurfaceData_) {
				if (!sceneStateApplied_) {
					ApplyViewportAndScissor();
					sceneStateApplied_ = true;
				}
				return true;
			}
			FinishScene();
		}

		const std::int32_t result = sceGxmBeginScene(context_, 0, renderTarget, nullptr, nullptr, syncObject,
			colorSurface, depthSurface);
		if (result < 0) {
			LOGE("sceGxmBeginScene({}x{}) failed with 0x{:.8x}", width, height, std::uint32_t(result));
			return false;
		}

		sceneOpen_ = true;
		sceneSurfaceData_ = surfaceData;
		sceneWidth_ = width;
		sceneHeight_ = height;
		ApplyViewportAndScissor();
		sceneStateApplied_ = true;
		return true;
	}

	void GxmDevice::FinishScene()
	{
		if (!sceneOpen_ || context_ == nullptr) {
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
		sceneNotification_.value++;
		sceGxmEndScene(context_, nullptr, &sceneNotification_);
		sceGxmNotificationWait(&sceneNotification_);
		sceneOpen_ = false;
		sceneSurfaceData_ = nullptr;
		sceneStateApplied_ = false;
	}

	void GxmDevice::ApplyViewportAndScissor()
	{
		if (context_ == nullptr) {
			return;
		}

		const std::int32_t targetWidth = sceneWidth_;
		const std::int32_t targetHeight = sceneHeight_;

		// Every surface is stored bottom-up like OpenGL (see the class documentation), so a positive Y scale
		// is what maps clip -Y onto row 0 - and the viewport's Y, being an OpenGL one measured from the
		// bottom, is then already a row index
		const float halfWidth = float(viewport_.W) * 0.5f;
		const float halfHeight = float(viewport_.H) * 0.5f;
		sceGxmSetViewport(context_,
			float(viewport_.X) + halfWidth, halfWidth,
			float(viewport_.Y) + halfHeight, halfHeight,
			0.5f, 0.5f);

		if (scissor_.Enabled) {
			// The maximum of a region clip is inclusive
			const std::int32_t xMin = (scissor_.Rect.X > 0 ? scissor_.Rect.X : 0);
			const std::int32_t yMin = (scissor_.Rect.Y > 0 ? scissor_.Rect.Y : 0);
			std::int32_t xMax = scissor_.Rect.X + scissor_.Rect.W - 1;
			std::int32_t yMax = scissor_.Rect.Y + scissor_.Rect.H - 1;
			if (xMax >= targetWidth) { xMax = targetWidth - 1; }
			if (yMax >= targetHeight) { yMax = targetHeight - 1; }
			if (xMax < xMin || yMax < yMin) {
				// An empty scissor rectangle has to reject everything, which "clip outside an empty region"
				// cannot express - a 1x1 region outside the surface is the closest equivalent
				sceGxmSetRegionClip(context_, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, 0, 0);
			} else {
				// Measured on the console: the rows of a region clip run opposite to the way this backend stores
				// its surfaces, so the rectangle is mirrored vertically. The viewport above can express either
				// direction because its Y scale is signed - which is what puts clip -Y on row 0 and gives every
				// surface OpenGL's bottom-up layout - while an unsigned clip rectangle cannot, so anything handed
				// to sceGxm as plain surface rows has to be turned over to match the rectangle `glScissor()`
				// takes on the reference backend
				const std::int32_t yMinClip = targetHeight - 1 - yMax;
				const std::int32_t yMaxClip = targetHeight - 1 - yMin;
				sceGxmSetRegionClip(context_, SCE_GXM_REGION_CLIP_OUTSIDE,
					std::uint32_t(xMin), std::uint32_t(yMinClip), std::uint32_t(xMax), std::uint32_t(yMaxClip));
			}
		} else {
			sceGxmSetRegionClip(context_, SCE_GXM_REGION_CLIP_NONE, 0, 0, 0, 0);
		}
	}

	bool GxmDevice::EnsureSequentialIndices(std::uint32_t count)
	{
		if (count <= sequentialIndexCount_) {
			return true;
		}
		// 16-bit indexing is limited to values below 64000 by the hardware; a single draw never comes close
		if (count > 63999) {
			LOGW("A draw of {} vertices exceeds the 16-bit index range and was skipped", count);
			return false;
		}

		std::uint32_t newCount = (sequentialIndexCount_ > 0 ? sequentialIndexCount_ : 4096);
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
		RetireBlock(sequentialIndices_);
		sequentialIndices_ = block;
		sequentialIndexCount_ = newCount;
		return true;
	}

	bool GxmDevice::EnsureLineStripIndices(std::uint32_t vertexCount)
	{
		if (vertexCount <= lineStripVertexCount_) {
			return true;
		}
		if (vertexCount > 32000) {
			LOGW("A line strip of {} vertices exceeds the 16-bit index range and was skipped", vertexCount);
			return false;
		}

		std::uint32_t newCount = (lineStripVertexCount_ > 0 ? lineStripVertexCount_ : 256);
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

		RetireBlock(lineStripIndices_);
		lineStripIndices_ = block;
		lineStripVertexCount_ = newCount;
		return true;
	}

	// -- Clear --

	void GxmDevice::Clear(ClearFlags flags)
	{
		if (flags == ClearFlags::None || !EnsureScene() || clearVertexProgram_ == nullptr || clearFragmentProgram_ == nullptr) {
			return;
		}
		if (!EnsureSequentialIndices(4)) {
			return;
		}

		const bool clearColor = ((std::uint32_t(flags) & std::uint32_t(ClearFlags::Color)) != 0);
		const bool clearDepth = ((std::uint32_t(flags) & std::uint32_t(ClearFlags::Depth)) != 0);

		const std::int32_t targetWidth = sceneWidth_;
		const std::int32_t targetHeight = sceneHeight_;

		// glClear covers the whole surface (modulated by the scissor test) rather than the viewport, so the
		// quad is drawn through a full-target viewport and the tracked one is restored afterwards
		sceGxmSetViewport(context_, float(targetWidth) * 0.5f, float(targetWidth) * 0.5f,
			float(targetHeight) * 0.5f, float(targetHeight) * 0.5f, 0.5f, 0.5f);

		sceGxmSetVertexProgram(context_, clearVertexProgram_);
		sceGxmSetFragmentProgram(context_, clearFragmentProgram_);
		sceGxmSetCullMode(context_, SCE_GXM_CULL_NONE);
		SetDepthStateBothFaces(SCE_GXM_DEPTH_FUNC_ALWAYS,
			clearDepth ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
		// A colour-less clear (depth only) still has to rasterize the quad, with the colour write masked off -
		// which the fragment program's blend info carries, so the masked variant is a separate program
		SetFragmentProgramEnabledBothFaces(clearColor
			? SCE_GXM_FRAGMENT_PROGRAM_ENABLED : SCE_GXM_FRAGMENT_PROGRAM_DISABLED);

		// This clear's own quad, carrying the colour with it (see ClearVertex)
		ClearVertex* quad = static_cast<ClearVertex*>(clearVertices_.Base)
			+ (clearQuadIndex_ % ClearQuadRingSize) * 4u;
		clearQuadIndex_++;
		for (std::uint32_t i = 0; i < 4; i++) {
			quad[i].X = ClearQuad[i * 2 + 0];
			quad[i].Y = ClearQuad[i * 2 + 1];
			quad[i].R = clearColor_.R;
			quad[i].G = clearColor_.G;
			quad[i].B = clearColor_.B;
			quad[i].A = clearColor_.A;
		}

		sceGxmSetVertexStream(context_, 0, quad);
		sceGxmDraw(context_, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, sequentialIndices_.Base, 4);

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
		if (count == 0 || currentProgram_ == nullptr || !EnsureScene()) {
			return;
		}

		GxmShaderProgram* program = currentProgram_;
		SceGxmVertexProgram* vertexProgram = program->GetVertexProgram();
		if (vertexProgram == nullptr) {
			return;
		}

		SceGxmBlendInfo blendInfo = {};
		const SceGxmBlendInfo* blendInfoPtr = nullptr;
		if (blending_.Enabled) {
			blendInfo.colorMask = SCE_GXM_COLOR_MASK_ALL;
			blendInfo.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
			blendInfo.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
			blendInfo.colorSrc = TranslateBlendFactor(blending_.SrcRgb);
			blendInfo.colorDst = TranslateBlendFactor(blending_.DstRgb);
			blendInfo.alphaSrc = TranslateBlendFactor(blending_.SrcAlpha);
			blendInfo.alphaDst = TranslateBlendFactor(blending_.DstAlpha);
			blendInfoPtr = &blendInfo;
		}
		const std::uint32_t blendKey = GxmShaderProgram::PackBlendKey(blending_.Enabled,
			blending_.SrcRgb, blending_.DstRgb, blending_.SrcAlpha, blending_.DstAlpha);
		SceGxmFragmentProgram* fragmentProgram = program->GetFragmentProgram(blendKey, blendInfoPtr);
		if (fragmentProgram == nullptr) {
			return;
		}

		sceGxmSetVertexProgram(context_, vertexProgram);
		sceGxmSetFragmentProgram(context_, fragmentProgram);
		// A depth-only Clear() switches the fragment stage off; make sure a real draw always has it back on
		SetFragmentProgramEnabledBothFaces(SCE_GXM_FRAGMENT_PROGRAM_ENABLED);

		// OpenGL ties depth writes to the depth test: with GL_DEPTH_TEST disabled, glDepthMask(true) still
		// writes nothing. Keeping that coupling here matters, because a later depth-tested draw would otherwise
		// see values this one was not supposed to leave behind
		const bool depthWrites = (depthTest_.TestEnabled && depthTest_.MaskEnabled);
		SetDepthStateBothFaces(depthTest_.TestEnabled ? SCE_GXM_DEPTH_FUNC_LESS_EQUAL : SCE_GXM_DEPTH_FUNC_ALWAYS,
			depthWrites ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
		if (cullFace_.Enabled) {
			// The engine winds its front faces counter-clockwise in the OpenGL convention, which this backend
			// preserves by storing every surface bottom-up - so the winding reaches the GPU unchanged
			sceGxmSetCullMode(context_, cullFace_.Mode == CullFaceMode::Front ? SCE_GXM_CULL_CCW : SCE_GXM_CULL_CW);
		} else {
			sceGxmSetCullMode(context_, SCE_GXM_CULL_NONE);
		}

		// Uniforms: sceGxm addresses a default uniform buffer in 32-bit components, so a slot's bytes land at
		// its resource index times four. The buffer is per draw and comes out of the context's vertex ring
		void* vertexUniformBuffer = nullptr;
		if (program->GetVertexUniformBufferSize() > 0) {
			if (sceGxmReserveVertexDefaultUniformBuffer(context_, &vertexUniformBuffer) >= 0 && vertexUniformBuffer != nullptr) {
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
			if (sceGxmReserveFragmentDefaultUniformBuffer(context_, &fragmentUniformBuffer) >= 0 && fragmentUniformBuffer != nullptr) {
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
					sceGxmSetVertexTexture(context_, slot.TextureIndex, gxmTexture);
				}
			}
		}
		for (const GxmShaderProgram::GxmSamplerSlot& slot : program->GetFragmentSamplerSlots()) {
			const GxmTexture* texture = GetBoundTexture(slot.EngineUnit);
			if (texture != nullptr) {
				if (const SceGxmTexture* gxmTexture = texture->GetGxmTexture()) {
					sceGxmSetFragmentTexture(context_, slot.TextureIndex, gxmTexture);
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
			sceGxmSetVertexStream(context_, program->GetStaticStreamIndex(), stream);
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
			sceGxmSetVertexStream(context_, 0, base);
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
			indexData = static_cast<const std::uint16_t*>(lineStripIndices_.Base) + first * 2u;
			indexCount = (count - 1) * 2u;
		} else {
			// There is no non-indexed draw: a window of the shared increasing-index buffer reproduces
			// glDrawArrays(first, count) exactly, indices included - starting at zero when the stream has
			// already been advanced to the first vertex (see foldFirstVertexIntoStream above)
			const std::uint32_t first = (foldFirstVertexIntoStream ? 0u : std::uint32_t(firstVertex > 0 ? firstVertex : 0));
			if (!EnsureSequentialIndices(first + count)) {
				return;
			}
			indexData = static_cast<const std::uint16_t*>(sequentialIndices_.Base) + first;
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
		sceGxmDraw(context_, primitiveType, gxmIndexFormat, indexData, indexCount);
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
				sceGxmSetVertexUniformBuffer(context_, upload.Index, base);
			} else {
				sceGxmSetFragmentUniformBuffer(context_, upload.Index, base);
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
		if (fence == nullptr || context_ == nullptr) {
			return true;
		}
		// Waiting for everything is stronger than the caller asked for, but correct - and this is only reached
		// when the pipeline's ring buffers have wrapped, which the enlarged vertex ring makes rare
		FinishScene();
		sceGxmFinish(context_);
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

		static SceGxmProgram* clearVertexStage = nullptr;
		static SceGxmProgram* clearFragmentStage = nullptr;
		static SceGxmProgram* presentVertexStage = nullptr;
		static SceGxmProgram* presentFragmentStage = nullptr;

		const BuiltinStage stages[] = {
			{ ClearVertexSource, SHARK_VERTEX_SHADER, &clearVertexStage, &clearVertexId_, "clear vertex" },
			{ ClearFragmentSource, SHARK_FRAGMENT_SHADER, &clearFragmentStage, &clearFragmentId_, "clear fragment" },
			{ PresentVertexSource, SHARK_VERTEX_SHADER, &presentVertexStage, &presentVertexId_, "present vertex" },
			{ PresentFragmentSource, SHARK_FRAGMENT_SHADER, &presentFragmentStage, &presentFragmentId_, "present fragment" }
		};

		for (const BuiltinStage& stage : stages) {
			// A previous attempt that got this far and then failed left a copy behind (the failure paths tear
			// the session down and the window backend may try again), so release it before overwriting it
			std::free(*stage.Output);
			*stage.Output = nullptr;

			std::uint32_t size = 0;
			*stage.Output = GxmShaderProgram::CompileCgStage(stage.Source, stage.Type == SHARK_VERTEX_SHADER, size);
			if (*stage.Output == nullptr) {
				LOGE("Failed to compile the built-in {} shader", stage.Name);
				return false;
			}
			LOGD("Compiled the built-in {} shader ({} bytes of GXP)", stage.Name, size);
			if (sceGxmShaderPatcherRegisterProgram(shaderPatcher_, *stage.Output, stage.Id) < 0) {
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
		resolveAttribute(clearVertexStage, "aPosition", 0, clearAttribute.regIndex);
		SceGxmVertexAttribute clearColorAttribute = {};
		clearColorAttribute.streamIndex = 0;
		clearColorAttribute.offset = offsetof(ClearVertex, R);
		clearColorAttribute.format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
		clearColorAttribute.componentCount = 4;
		resolveAttribute(clearVertexStage, "aColor", 1, clearColorAttribute.regIndex);
		const SceGxmVertexAttribute clearAttributes[2] = { clearAttribute, clearColorAttribute };
		SceGxmVertexStream clearStream = {};
		clearStream.stride = sizeof(ClearVertex);
		clearStream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
		if (sceGxmShaderPatcherCreateVertexProgram(shaderPatcher_, clearVertexId_, clearAttributes, 2,
				&clearStream, 1, &clearVertexProgram_) < 0 ||
			sceGxmShaderPatcherCreateFragmentProgram(shaderPatcher_, clearFragmentId_,
				SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, nullptr, clearVertexStage,
				&clearFragmentProgram_) < 0) {
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
		resolveAttribute(presentVertexStage, "aPosition", 0, presentAttributes[0].regIndex);
		resolveAttribute(presentVertexStage, "aTexCoords", 1, presentAttributes[1].regIndex);
		SceGxmVertexStream presentStream = {};
		presentStream.stride = sizeof(float) * 4;
		presentStream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
		if (sceGxmShaderPatcherCreateVertexProgram(shaderPatcher_, presentVertexId_, presentAttributes, 2,
				&presentStream, 1, &presentVertexProgram_) < 0 ||
			sceGxmShaderPatcherCreateFragmentProgram(shaderPatcher_, presentFragmentId_,
				SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, nullptr, presentVertexStage,
				&presentFragmentProgram_) < 0) {
			LOGE("Failed to create the built-in present programs");
			return false;
		}

		// Their geometry, in GPU-visible memory like every other stream
		clearVertices_ = GxmMemory::Alloc("Jazz2:ClearQuad",
			ClearQuadRingSize * 4u * sizeof(ClearVertex), SCE_GXM_MEMORY_ATTRIB_READ);
		presentVertices_ = GxmMemory::Alloc("Jazz2:PresentQuad", sizeof(PresentQuad), SCE_GXM_MEMORY_ATTRIB_READ);
		if (!clearVertices_.IsValid() || !presentVertices_.IsValid()) {
			return false;
		}
		std::memcpy(presentVertices_.Base, PresentQuad, sizeof(PresentQuad));
		return true;
	}

	bool GxmDevice::CreateSwapchain(void* windowHandle, std::int32_t width, std::int32_t height, bool vsync)
	{
		static_cast<void>(windowHandle);
		static_cast<void>(width);
		static_cast<void>(height);

		if (initialized_) {
			return true;
		}
		vsync_ = vsync;

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
		initialized_ = true;

		// The context's ring buffers, then the context itself
		contextHostMem_ = GxmMemory::Alloc("Jazz2:GxmContextHost", ContextHostMemSize, SCE_GXM_MEMORY_ATTRIB_RW);
		vdmRingBuffer_ = GxmMemory::Alloc("Jazz2:GxmVdmRing", VdmRingBufferSize, SCE_GXM_MEMORY_ATTRIB_READ);
		vertexRingBuffer_ = GxmMemory::Alloc("Jazz2:GxmVertexRing", VertexRingBufferSize, SCE_GXM_MEMORY_ATTRIB_READ);
		fragmentRingBuffer_ = GxmMemory::Alloc("Jazz2:GxmFragmentRing", FragmentRingBufferSize, SCE_GXM_MEMORY_ATTRIB_READ);
		fragmentUsseRingBuffer_ = GxmMemory::AllocFragmentUsse("Jazz2:GxmFragmentUsseRing", FragmentUsseRingBufferSize);
		if (!contextHostMem_.IsValid() || !vdmRingBuffer_.IsValid() || !vertexRingBuffer_.IsValid() ||
			!fragmentRingBuffer_.IsValid() || !fragmentUsseRingBuffer_.IsValid()) {
			LOGE("Failed to allocate the sceGxm context ring buffers");
			DestroySwapchain();
			return false;
		}

		SceGxmContextParams contextParams = {};
		contextParams.hostMem = contextHostMem_.Base;
		contextParams.hostMemSize = contextHostMem_.Size;
		contextParams.vdmRingBufferMem = vdmRingBuffer_.Base;
		contextParams.vdmRingBufferMemSize = vdmRingBuffer_.Size;
		contextParams.vertexRingBufferMem = vertexRingBuffer_.Base;
		contextParams.vertexRingBufferMemSize = vertexRingBuffer_.Size;
		contextParams.fragmentRingBufferMem = fragmentRingBuffer_.Base;
		contextParams.fragmentRingBufferMemSize = fragmentRingBuffer_.Size;
		contextParams.fragmentUsseRingBufferMem = fragmentUsseRingBuffer_.Base;
		contextParams.fragmentUsseRingBufferMemSize = fragmentUsseRingBuffer_.Size;
		contextParams.fragmentUsseRingBufferOffset = fragmentUsseRingBuffer_.UsseOffset;
		result = sceGxmCreateContext(&contextParams, &context_);
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
		result = sceGxmCreateRenderTarget(&renderTargetParams, &displayRenderTarget_);
		if (result < 0) {
			LOGE("sceGxmCreateRenderTarget({}x{}) failed with 0x{:.8x}", DisplayWidth, DisplayHeight, std::uint32_t(result));
			DestroySwapchain();
			return false;
		}

		// The display buffers the controller scans out of, with the sync object that keeps the GPU from
		// overwriting one still on screen
		const std::uint32_t displayBufferSize = std::uint32_t(DisplayStride) * std::uint32_t(DisplayHeight) * 4u;
		for (std::uint32_t i = 0; i < DisplayBufferCount; i++) {
			displayBuffers_[i] = GxmMemory::AllocCdram("Jazz2:DisplayBuffer", displayBufferSize, SCE_GXM_MEMORY_ATTRIB_RW);
			if (!displayBuffers_[i].IsValid()) {
				LOGE("Failed to allocate display buffer {}", i);
				DestroySwapchain();
			return false;
			}
			std::memset(displayBuffers_[i].Base, 0, displayBufferSize);

			result = sceGxmColorSurfaceInit(&displaySurfaces_[i], SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
				SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
				DisplayWidth, DisplayHeight, DisplayStride, displayBuffers_[i].Base);
			if (result < 0) {
				LOGE("sceGxmColorSurfaceInit(display {}) failed with 0x{:.8x}", i, std::uint32_t(result));
				DestroySwapchain();
			return false;
			}
			result = sceGxmSyncObjectCreate(&displaySyncObjects_[i]);
			if (result < 0) {
				LOGE("sceGxmSyncObjectCreate({}) failed with 0x{:.8x}", i, std::uint32_t(result));
				DestroySwapchain();
			return false;
			}
		}

		// The intermediate surface every screen-targeted draw lands in, kept bottom-up like OpenGL and
		// flipped into a display buffer at present time
		screenBuffer_ = GxmMemory::AllocCdram("Jazz2:ScreenSurface", displayBufferSize, SCE_GXM_MEMORY_ATTRIB_RW);
		if (!screenBuffer_.IsValid()) {
			LOGE("Failed to allocate the intermediate screen surface");
			DestroySwapchain();
			return false;
		}
		std::memset(screenBuffer_.Base, 0, displayBufferSize);
		result = sceGxmColorSurfaceInit(&screenSurface_, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR,
			SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
			DisplayWidth, DisplayHeight, DisplayStride, screenBuffer_.Base);
		if (result < 0) {
			LOGE("sceGxmColorSurfaceInit(screen) failed with 0x{:.8x}", std::uint32_t(result));
			DestroySwapchain();
			return false;
		}
		result = sceGxmSyncObjectCreate(&screenSyncObject_);
		if (result < 0) {
			LOGE("sceGxmSyncObjectCreate(screen) failed with 0x{:.8x}", std::uint32_t(result));
			screenSyncObject_ = nullptr;
			DestroySwapchain();
			return false;
		}
		result = sceGxmTextureInitLinear(&screenTexture_, screenBuffer_.Base, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR,
			DisplayWidth, DisplayHeight, 0);
		if (result < 0) {
			LOGE("sceGxmTextureInitLinear(screen) failed with 0x{:.8x}", std::uint32_t(result));
			DestroySwapchain();
			return false;
		}
		sceGxmTextureSetMinFilter(&screenTexture_, SCE_GXM_TEXTURE_FILTER_POINT);
		sceGxmTextureSetMagFilter(&screenTexture_, SCE_GXM_TEXTURE_FILTER_POINT);
		sceGxmTextureSetUAddrMode(&screenTexture_, SCE_GXM_TEXTURE_ADDR_CLAMP);
		sceGxmTextureSetVAddrMode(&screenTexture_, SCE_GXM_TEXTURE_ADDR_CLAMP);

		// One depth/stencil surface shared by every scene: the renderer is 2D and never needs a depth buffer's
		// contents to outlive a pass, and this stride covers any render target the pipeline creates
		const std::uint32_t depthBufferSize = std::uint32_t(DisplayStride) * std::uint32_t(DisplayHeight) * 4u;
		depthBuffer_ = GxmMemory::Alloc("Jazz2:DepthSurface", depthBufferSize, SCE_GXM_MEMORY_ATTRIB_RW);
		if (!depthBuffer_.IsValid()) {
			LOGE("Failed to allocate the depth/stencil surface");
			DestroySwapchain();
			return false;
		}
		result = sceGxmDepthStencilSurfaceInit(&depthSurface_, SCE_GXM_DEPTH_STENCIL_FORMAT_DF32,
			SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, DisplayStride, depthBuffer_.Base, nullptr);
		if (result < 0) {
			LOGE("sceGxmDepthStencilSurfaceInit() failed with 0x{:.8x}", std::uint32_t(result));
			DestroySwapchain();
			return false;
		}
		// Every scene initializes its on-chip tile depth from this value, so setting it to the far plane is
		// what gives each pass the cleared depth buffer an OpenGL frame starts with
		sceGxmDepthStencilSurfaceSetBackgroundDepth(&depthSurface_, 1.0f);

		// The shader patcher every program's vertex/fragment programs are created through
		patcherBufferMem_ = GxmMemory::Alloc("Jazz2:PatcherBuffer", PatcherBufferSize, SCE_GXM_MEMORY_ATTRIB_RW);
		patcherVertexUsseMem_ = GxmMemory::AllocVertexUsse("Jazz2:PatcherVertexUsse", PatcherVertexUsseSize);
		patcherFragmentUsseMem_ = GxmMemory::AllocFragmentUsse("Jazz2:PatcherFragmentUsse", PatcherFragmentUsseSize);
		if (!patcherBufferMem_.IsValid() || !patcherVertexUsseMem_.IsValid() || !patcherFragmentUsseMem_.IsValid()) {
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
		patcherParams.bufferMem = patcherBufferMem_.Base;
		patcherParams.bufferMemSize = patcherBufferMem_.Size;
		patcherParams.vertexUsseAllocCallback = nullptr;
		patcherParams.vertexUsseFreeCallback = nullptr;
		patcherParams.vertexUsseMem = patcherVertexUsseMem_.Base;
		patcherParams.vertexUsseMemSize = patcherVertexUsseMem_.Size;
		patcherParams.vertexUsseOffset = patcherVertexUsseMem_.UsseOffset;
		patcherParams.fragmentUsseAllocCallback = nullptr;
		patcherParams.fragmentUsseFreeCallback = nullptr;
		patcherParams.fragmentUsseMem = patcherFragmentUsseMem_.Base;
		patcherParams.fragmentUsseMemSize = patcherFragmentUsseMem_.Size;
		patcherParams.fragmentUsseOffset = patcherFragmentUsseMem_.UsseOffset;
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
		result = sceGxmShaderPatcherCreate(&patcherParams, &shaderPatcher_);
		if (result < 0) {
			LOGE("sceGxmShaderPatcherCreate() failed with 0x{:.8x}", std::uint32_t(result));
			DestroySwapchain();
			return false;
		}

		// sceGxm consumes compiled GXP binaries and the SDK ships no offline compiler for them, so the whole
		// shader set is compiled on the console by the firmware's own Cg compiler. A build that bundles the
		// module in its own VPK is served first, the standard shared location second
		if (shark_init(ShaderCompilerModulePath) < 0 && shark_init(ShaderCompilerModulePathAlt) < 0) {
			LOGE("Cannot initialize the runtime Cg compiler: \"libshacccg.suprx\" was not found. This backend "
				"compiles its shaders on the console, so that firmware module has to be extracted and placed "
				"in \"ur0:/data/\" (see the PS Vita section of the console documentation)");
			DestroySwapchain();
			return false;
		}

		// A scene's completion notification has to be written into the driver's own notification region
		// (see FinishScene())
		sceneNotification_.address = sceGxmGetNotificationRegion();
		sceneNotification_.value = 0;
		if (sceneNotification_.address == nullptr) {
			LOGE("sceGxmGetNotificationRegion() returned nothing, so scene completion cannot be waited on");
			DestroySwapchain();
			return false;
		}
		*sceneNotification_.address = 0;

		// Before the first compile of anything, so a built-in shader that fails says why
		GxmShaderProgram::InstallCompilerLogCallback();

		if (!CreateBuiltinShaders()) {
			DestroySwapchain();
			return false;
		}

		// The two static streams feeding the vertex-ID-free sprite layouts
		quadCornerStream_ = GxmMemory::Alloc("Jazz2:QuadCorners", sizeof(QuadCorners), SCE_GXM_MEMORY_ATTRIB_READ);
		batchedCornerStream_ = GxmMemory::Alloc("Jazz2:BatchedCorners", MaxBatchSize * 6u * 3u * sizeof(float), SCE_GXM_MEMORY_ATTRIB_READ);
		if (!quadCornerStream_.IsValid() || !batchedCornerStream_.IsValid()) {
			LOGE("Failed to allocate the static vertex streams");
			DestroySwapchain();
			return false;
		}
		std::memcpy(quadCornerStream_.Base, QuadCorners, sizeof(QuadCorners));
		float* batched = static_cast<float*>(batchedCornerStream_.Base);
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

		backBufferIndex_ = 0;
		frontBufferIndex_ = DisplayBufferCount - 1;
		LOGI("sceGxm initialized ({}x{}, {} display buffers, {} KB of GPU memory reserved)",
			DisplayWidth, DisplayHeight, DisplayBufferCount, GxmMemory::GetAllocatedBytes() / 1024);
		return true;
	}

	void GxmDevice::DestroySwapchain()
	{
		if (!initialized_) {
			return;
		}

		FinishScene();
		if (context_ != nullptr) {
			sceGxmFinish(context_);
		}
		sceGxmDisplayQueueFinish();

		shark_end();

		if (shaderPatcher_ != nullptr) {
			if (clearVertexProgram_ != nullptr) { sceGxmShaderPatcherReleaseVertexProgram(shaderPatcher_, clearVertexProgram_); }
			if (clearFragmentProgram_ != nullptr) { sceGxmShaderPatcherReleaseFragmentProgram(shaderPatcher_, clearFragmentProgram_); }
			if (presentVertexProgram_ != nullptr) { sceGxmShaderPatcherReleaseVertexProgram(shaderPatcher_, presentVertexProgram_); }
			if (presentFragmentProgram_ != nullptr) { sceGxmShaderPatcherReleaseFragmentProgram(shaderPatcher_, presentFragmentProgram_); }
			sceGxmShaderPatcherDestroy(shaderPatcher_);
			shaderPatcher_ = nullptr;
		}
		clearVertexProgram_ = nullptr;
		clearFragmentProgram_ = nullptr;
		presentVertexProgram_ = nullptr;
		presentFragmentProgram_ = nullptr;
		clearQuadIndex_ = 0;

		for (std::uint32_t i = 0; i < DisplayBufferCount; i++) {
			if (displaySyncObjects_[i] != nullptr) {
				sceGxmSyncObjectDestroy(displaySyncObjects_[i]);
				displaySyncObjects_[i] = nullptr;
			}
			GxmMemory::Free(displayBuffers_[i]);
		}
		if (screenSyncObject_ != nullptr) {
			sceGxmSyncObjectDestroy(screenSyncObject_);
			screenSyncObject_ = nullptr;
		}

		if (displayRenderTarget_ != nullptr) {
			sceGxmDestroyRenderTarget(displayRenderTarget_);
			displayRenderTarget_ = nullptr;
		}
		if (context_ != nullptr) {
			sceGxmDestroyContext(context_);
			context_ = nullptr;
		}

		GxmMemory::Free(screenBuffer_);
		GxmMemory::Free(depthBuffer_);
		GxmMemory::Free(clearVertices_);
		GxmMemory::Free(presentVertices_);
		GxmMemory::Free(quadCornerStream_);
		GxmMemory::Free(batchedCornerStream_);
		GxmMemory::Free(sequentialIndices_);
		GxmMemory::Free(lineStripIndices_);
		GxmMemory::Free(patcherBufferMem_);
		GxmMemory::Free(patcherVertexUsseMem_);
		GxmMemory::Free(patcherFragmentUsseMem_);
		GxmMemory::Free(fragmentUsseRingBuffer_);
		GxmMemory::Free(fragmentRingBuffer_);
		GxmMemory::Free(vertexRingBuffer_);
		GxmMemory::Free(vdmRingBuffer_);
		GxmMemory::Free(contextHostMem_);
		sequentialIndexCount_ = 0;
		lineStripVertexCount_ = 0;

		sceGxmTerminate();
		initialized_ = false;
	}

	void GxmDevice::ResizeSwapchain(std::int32_t width, std::int32_t height)
	{
		static_cast<void>(width);
		static_cast<void>(height);
	}

	void GxmDevice::PresentFrame()
	{
		if (!initialized_ || context_ == nullptr) {
			return;
		}

		// Close whatever the frame was still drawing into, then flip the screen surface into the buffer the
		// display controller will pick up next
		currentRenderTarget_ = nullptr;
		FinishScene();

		if (presentVertexProgram_ != nullptr && presentFragmentProgram_ != nullptr && EnsureSequentialIndices(4)) {
			const std::int32_t result = sceGxmBeginScene(context_, 0, displayRenderTarget_, nullptr, nullptr,
				displaySyncObjects_[backBufferIndex_], &displaySurfaces_[backBufferIndex_], &depthSurface_);
			if (result >= 0) {
				sceGxmSetViewport(context_, float(DisplayWidth) * 0.5f, float(DisplayWidth) * 0.5f,
					float(DisplayHeight) * 0.5f, float(DisplayHeight) * 0.5f, 0.5f, 0.5f);
				sceGxmSetRegionClip(context_, SCE_GXM_REGION_CLIP_NONE, 0, 0, 0, 0);
				sceGxmSetVertexProgram(context_, presentVertexProgram_);
				sceGxmSetFragmentProgram(context_, presentFragmentProgram_);
				sceGxmSetCullMode(context_, SCE_GXM_CULL_NONE);
				SetDepthStateBothFaces(SCE_GXM_DEPTH_FUNC_ALWAYS, SCE_GXM_DEPTH_WRITE_DISABLED);
				SetFragmentProgramEnabledBothFaces(SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
				sceGxmSetFragmentTexture(context_, 0, &screenTexture_);
				sceGxmSetVertexStream(context_, 0, presentVertices_.Base);
				sceGxmDraw(context_, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, SCE_GXM_INDEX_FORMAT_U16, sequentialIndices_.Base, 4);
				sceGxmEndScene(context_, nullptr, nullptr);
			} else {
				LOGE("sceGxmBeginScene(present) failed with 0x{:.8x}", std::uint32_t(result));
			}
		}

		sceGxmPadHeartbeat(&displaySurfaces_[backBufferIndex_], displaySyncObjects_[backBufferIndex_]);

		DisplayQueueCallbackData callbackData;
		callbackData.Address = displayBuffers_[backBufferIndex_].Base;
		callbackData.Vsync = vsync_;
		sceGxmDisplayQueueAddEntry(displaySyncObjects_[frontBufferIndex_], displaySyncObjects_[backBufferIndex_], &callbackData);

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
		sceGxmFinish(context_);

		// Everything recorded this frame has been consumed, so anything a growing buffer displaced can go
		ReleaseRetiredBlocks();

		frontBufferIndex_ = backBufferIndex_;
		backBufferIndex_ = (backBufferIndex_ + 1) % DisplayBufferCount;
		sceneCounter_++;

		// The next frame starts with the screen surface again, and every scene re-applies the state it needs
		sceneStateApplied_ = false;
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
