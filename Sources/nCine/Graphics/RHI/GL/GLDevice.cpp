#include "GLDevice.h"
#include "GLBlending.h"
#include "GLDepthTest.h"
#include "GLCullFace.h"
#include "GLScissorTest.h"
#include "GLClearColor.h"
#include "GLViewport.h"

namespace nCine::RhiGL
{
	// The backend-neutral enums promise GL-compatible numeric values, so this backend can translate with a plain cast
	static_assert(static_cast<GLenum>(PrimitiveType::Points) == GL_POINTS);
	static_assert(static_cast<GLenum>(PrimitiveType::Lines) == GL_LINES);
	static_assert(static_cast<GLenum>(PrimitiveType::LineLoop) == GL_LINE_LOOP);
	static_assert(static_cast<GLenum>(PrimitiveType::LineStrip) == GL_LINE_STRIP);
	static_assert(static_cast<GLenum>(PrimitiveType::Triangles) == GL_TRIANGLES);
	static_assert(static_cast<GLenum>(PrimitiveType::TriangleStrip) == GL_TRIANGLE_STRIP);
	static_assert(static_cast<GLenum>(PrimitiveType::TriangleFan) == GL_TRIANGLE_FAN);
	static_assert(static_cast<GLenum>(BufferUsage::StreamDraw) == GL_STREAM_DRAW);
	static_assert(static_cast<GLenum>(BufferUsage::StaticDraw) == GL_STATIC_DRAW);
	static_assert(static_cast<GLenum>(BufferUsage::DynamicDraw) == GL_DYNAMIC_DRAW);
	static_assert(static_cast<GLenum>(BlendingFactor::Zero) == GL_ZERO);
	static_assert(static_cast<GLenum>(BlendingFactor::One) == GL_ONE);
	static_assert(static_cast<GLenum>(BlendingFactor::SrcColor) == GL_SRC_COLOR);
	static_assert(static_cast<GLenum>(BlendingFactor::OneMinusSrcColor) == GL_ONE_MINUS_SRC_COLOR);
	static_assert(static_cast<GLenum>(BlendingFactor::SrcAlpha) == GL_SRC_ALPHA);
	static_assert(static_cast<GLenum>(BlendingFactor::OneMinusSrcAlpha) == GL_ONE_MINUS_SRC_ALPHA);
	static_assert(static_cast<GLenum>(BlendingFactor::DstAlpha) == GL_DST_ALPHA);
	static_assert(static_cast<GLenum>(BlendingFactor::OneMinusDstAlpha) == GL_ONE_MINUS_DST_ALPHA);
	static_assert(static_cast<GLenum>(BlendingFactor::DstColor) == GL_DST_COLOR);
	static_assert(static_cast<GLenum>(BlendingFactor::OneMinusDstColor) == GL_ONE_MINUS_DST_COLOR);
	static_assert(static_cast<GLenum>(BlendingFactor::SrcAlphaSaturate) == GL_SRC_ALPHA_SATURATE);
#if !defined(DEATH_TARGET_VITA)
	// vitaGL declares none of the constant-colour blend factors, ConstantColor/-Alpha blending is unused there
	static_assert(static_cast<GLenum>(BlendingFactor::ConstantColor) == GL_CONSTANT_COLOR);
	static_assert(static_cast<GLenum>(BlendingFactor::OneMinusConstantColor) == GL_ONE_MINUS_CONSTANT_COLOR);
	static_assert(static_cast<GLenum>(BlendingFactor::ConstantAlpha) == GL_CONSTANT_ALPHA);
	static_assert(static_cast<GLenum>(BlendingFactor::OneMinusConstantAlpha) == GL_ONE_MINUS_CONSTANT_ALPHA);
#endif
	static_assert(static_cast<GLenum>(BufferTarget::Vertex) == GL_ARRAY_BUFFER);
	static_assert(static_cast<GLenum>(BufferTarget::Index) == GL_ELEMENT_ARRAY_BUFFER);
	static_assert(static_cast<GLenum>(BufferTarget::Uniform) == GL_UNIFORM_BUFFER);
	static_assert(static_cast<GLbitfield>(MapFlags::Write) == GL_MAP_WRITE_BIT);
	static_assert(static_cast<GLbitfield>(MapFlags::InvalidateRange) == GL_MAP_INVALIDATE_RANGE_BIT);
	static_assert(static_cast<GLbitfield>(MapFlags::InvalidateBuffer) == GL_MAP_INVALIDATE_BUFFER_BIT);
	static_assert(static_cast<GLbitfield>(MapFlags::FlushExplicit) == GL_MAP_FLUSH_EXPLICIT_BIT);
	static_assert(static_cast<GLbitfield>(MapFlags::Unsynchronized) == GL_MAP_UNSYNCHRONIZED_BIT);
#if defined(GL_MAP_PERSISTENT_BIT)
	static_assert(static_cast<GLbitfield>(MapFlags::Persistent) == GL_MAP_PERSISTENT_BIT);
	static_assert(static_cast<GLbitfield>(MapFlags::Coherent) == GL_MAP_COHERENT_BIT);
#endif
	static_assert(static_cast<GLenum>(IndexFormat::UInt16) == GL_UNSIGNED_SHORT);
	static_assert(static_cast<GLenum>(IndexFormat::UInt32) == GL_UNSIGNED_INT);
	static_assert(static_cast<GLenum>(CullFaceMode::Front) == GL_FRONT);
	static_assert(static_cast<GLenum>(CullFaceMode::Back) == GL_BACK);
	static_assert(static_cast<GLenum>(CullFaceMode::FrontAndBack) == GL_FRONT_AND_BACK);

	void GLDevice::SetBlendingEnabled(bool enabled)
	{
		if (enabled) {
			GLBlending::Enable();
		} else {
			GLBlending::Disable();
		}
	}

	void GLDevice::SetBlendingFactors(BlendingFactor srcRgb, BlendingFactor dstRgb, BlendingFactor srcAlpha, BlendingFactor dstAlpha)
	{
		GLBlending::SetBlendFunc(static_cast<GLenum>(srcRgb), static_cast<GLenum>(dstRgb), static_cast<GLenum>(srcAlpha), static_cast<GLenum>(dstAlpha));
	}

	GLDevice::BlendingState GLDevice::GetBlendingState()
	{
		const GLBlending::State state = GLBlending::GetState();
		return { state.enabled, BlendingFactor(state.srcRgb), BlendingFactor(state.dstRgb), BlendingFactor(state.srcAlpha), BlendingFactor(state.dstAlpha) };
	}

	void GLDevice::SetBlendingState(const BlendingState& state)
	{
		GLBlending::SetState({ state.Enabled, static_cast<GLenum>(state.SrcRgb), static_cast<GLenum>(state.DstRgb), static_cast<GLenum>(state.SrcAlpha), static_cast<GLenum>(state.DstAlpha) });
	}

	void GLDevice::SetDepthTestEnabled(bool enabled)
	{
		if (enabled) {
			GLDepthTest::Enable();
		} else {
			GLDepthTest::Disable();
		}
	}

	void GLDevice::SetDepthMaskEnabled(bool enabled)
	{
		if (enabled) {
			GLDepthTest::EnableDepthMask();
		} else {
			GLDepthTest::DisableDepthMask();
		}
	}

	GLDevice::DepthTestState GLDevice::GetDepthTestState()
	{
		const GLDepthTest::State state = GLDepthTest::GetState();
		return { state.enabled, state.depthMaskEnabled };
	}

	void GLDevice::SetDepthTestState(const DepthTestState& state)
	{
		GLDepthTest::SetState({ state.TestEnabled, state.MaskEnabled });
	}

	void GLDevice::SetCullFaceEnabled(bool enabled)
	{
		if (enabled) {
			GLCullFace::Enable();
		} else {
			GLCullFace::Disable();
		}
	}

	GLDevice::CullFaceState GLDevice::GetCullFaceState()
	{
		const GLCullFace::State state = GLCullFace::GetState();
		return { state.enabled, CullFaceMode(state.mode) };
	}

	void GLDevice::SetCullFaceState(const CullFaceState& state)
	{
		GLCullFace::SetState({ state.Enabled, static_cast<GLenum>(state.Mode) });
	}

	GLDevice::ScissorState GLDevice::GetScissorState()
	{
		const GLScissorTest::State state = GLScissorTest::GetState();
		return { state.enabled, state.rect };
	}

	void GLDevice::SetScissorState(const ScissorState& state)
	{
		GLScissorTest::SetState({ state.Enabled, state.Rect });
	}

	void GLDevice::SetScissor(const Recti& rect)
	{
		GLScissorTest::Enable(rect);
	}

	void GLDevice::SetScissorTestEnabled(bool enabled)
	{
		if (enabled) {
			GLScissorTest::Enable();
		} else {
			GLScissorTest::Disable();
		}
	}

	Recti GLDevice::GetViewport()
	{
		return GLViewport::GetRect();
	}

	void GLDevice::SetViewport(const Recti& rect)
	{
		GLViewport::SetRect(rect);
	}

	void GLDevice::InitViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		GLViewport::InitRect(x, y, width, height);
	}

	Colorf GLDevice::GetClearColor()
	{
		return GLClearColor::GetColor();
	}

	void GLDevice::SetClearColor(const Colorf& color)
	{
		GLClearColor::SetColor(color);
	}

	void GLDevice::Clear(ClearFlags flags)
	{
		GLbitfield mask = 0;
		if ((flags & ClearFlags::Color) == ClearFlags::Color) {
			mask |= GL_COLOR_BUFFER_BIT;
		}
		if ((flags & ClearFlags::Depth) == ClearFlags::Depth) {
			mask |= GL_DEPTH_BUFFER_BIT;
		}
		if ((flags & ClearFlags::Stencil) == ClearFlags::Stencil) {
			mask |= GL_STENCIL_BUFFER_BIT;
		}
		if (mask != 0) {
			glClear(mask);
		}
	}

	void GLDevice::DrawArrays(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices)
	{
		glDrawArrays(static_cast<GLenum>(primitive), firstVertex, numVertices);
	}

	void GLDevice::DrawArraysInstanced(PrimitiveType primitive, std::int32_t firstVertex, std::int32_t numVertices, std::int32_t numInstances)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// ES2 has no instanced drawing; the profile never issues instanced commands (CPU sprite batching is
		// disabled and every RenderCommand draws single-instance), so keeping the ES 3.0 symbol out of the
		// build is both a spec guard and a link-time aid for true ES2 loaders
		static_cast<void>(primitive); static_cast<void>(firstVertex); static_cast<void>(numVertices); static_cast<void>(numInstances);
		FATAL_ASSERT_MSG(false, "Instanced drawing is not supported on OpenGL|ES 2.0");
#else
		glDrawArraysInstanced(static_cast<GLenum>(primitive), firstVertex, numVertices, numInstances);
#endif
	}

	void GLDevice::DrawElements(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t baseVertex)
	{
		void* indexOffsetPtr = reinterpret_cast<void*>(indexOffset);
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
		// Base vertex is emulated by the caller offsetting the vertex format instead
		static_cast<void>(baseVertex);
		glDrawElements(static_cast<GLenum>(primitive), GLsizei(numIndices), static_cast<GLenum>(indexFormat), indexOffsetPtr);
#else
		glDrawElementsBaseVertex(static_cast<GLenum>(primitive), GLsizei(numIndices), static_cast<GLenum>(indexFormat), indexOffsetPtr, baseVertex);
#endif
	}

	void GLDevice::DrawElementsInstanced(PrimitiveType primitive, std::uint32_t numIndices, IndexFormat indexFormat, std::uintptr_t indexOffset, std::int32_t numInstances, std::int32_t baseVertex)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// See DrawArraysInstanced() - unreachable on the ES2 profile
		static_cast<void>(primitive); static_cast<void>(numIndices); static_cast<void>(indexFormat);
		static_cast<void>(indexOffset); static_cast<void>(numInstances); static_cast<void>(baseVertex);
		FATAL_ASSERT_MSG(false, "Instanced drawing is not supported on OpenGL|ES 2.0");
#else
		void* indexOffsetPtr = reinterpret_cast<void*>(indexOffset);
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
		// Base vertex is emulated by the caller offsetting the vertex format instead
		static_cast<void>(baseVertex);
		glDrawElementsInstanced(static_cast<GLenum>(primitive), GLsizei(numIndices), static_cast<GLenum>(indexFormat), indexOffsetPtr, numInstances);
#else
		glDrawElementsInstancedBaseVertex(static_cast<GLenum>(primitive), GLsizei(numIndices), static_cast<GLenum>(indexFormat), indexOffsetPtr, numInstances, baseVertex);
#endif
#endif
	}

	FenceHandle GLDevice::InsertFence()
	{
#if defined(RHI_GL_PROFILE_ES2)
		// Fence sync objects are ES 3.0; the only user is the persistent-mapping ring, which is compiled out
		// on every OpenGL|ES configuration (NCINE_HAS_PERSISTENT_MAPPING excludes WITH_OPENGLES)
		return nullptr;
#else
		return glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
#endif
	}

	void GLDevice::DeleteFence(FenceHandle& fence)
	{
#if defined(RHI_GL_PROFILE_ES2)
		fence = nullptr;
#else
		if (fence != nullptr) {
			glDeleteSync(static_cast<GLsync>(fence));
			fence = nullptr;
		}
#endif
	}

	bool GLDevice::ClientWaitFence(FenceHandle fence, std::uint64_t timeoutNs)
	{
#if defined(RHI_GL_PROFILE_ES2)
		static_cast<void>(fence); static_cast<void>(timeoutNs);
		return true;
#else
		const GLenum result = glClientWaitSync(static_cast<GLsync>(fence), GL_SYNC_FLUSH_COMMANDS_BIT, timeoutNs);
		return (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED);
#endif
	}

	void GLDevice::SetupInitialState()
	{
#if !defined(DEATH_TARGET_VITA)
		// vitaGL does not declare GL_DITHER, dithering is not part of its fixed-function state
		glDisable(GL_DITHER);
#endif
		GLBlending::SetBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GLDepthTest::Enable();
	}
}
