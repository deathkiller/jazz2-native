#pragma once

#if defined(WITH_RHI_GL)

// ============================================================================
// OpenGL / OpenGL ES rendering backend
// Provides the GAPI interface by aliasing and thinly wrapping the existing
// GL/ wrapper classes.  All functions are inline so the compiler can eliminate
// every indirection and produce identical code to the old direct-GL code.
// ============================================================================

// --- Capability flags -------------------------------------------------------
// Code guarded by these flags compiles only for this backend.

#define RHI_CAP_BATCHING			// Batched draw calls via VBO + shader uniform blocks
#define RHI_CAP_SHADERS				// Programmable vertex + fragment shaders
#define RHI_CAP_UNIFORM_BLOCKS		// Uniform Buffer Objects (UBO)
#define RHI_CAP_INSTANCING			// Instanced drawing (glDraw*Instanced)
#define RHI_CAP_FRAMEBUFFERS		// Off-screen render targets (FBO)
#define RHI_CAP_DEPTHSTENCIL		// Depth and stencil renderbuffers (FBO attachments)
#define RHI_CAP_MIPMAPS				// Mipmap generation / sampling
#define RHI_CAP_TEXTURE_FLOAT		// Float-format textures
#define RHI_CAP_BUFFER_MAPPING		// glMapBufferRange / persistent mapping
#define RHI_CAP_VAO					// Vertex Array Objects
#define RHI_CAP_BINARY_SHADERS		// GL_ARB_get_program_binary

// Some sub-flags are determined at run-time (see GfxCapabilities).
// Use the flags above only for compile-time code-paths.

// --- OpenGL headers ---------------------------------------------------------
#ifndef DOXYGEN_GENERATING_OUTPUT
#	define NCINE_INCLUDE_OPENGL
#	include "../../../CommonHeaders.h"
#endif

// --- RHI class headers ------------------------------------------------------
#include "Buffer.h"
#include "Texture.h"
#include "ShaderProgram.h"
#include "ShaderUniforms.h"
#include "UniformCache.h"
#include "UniformBlockCache.h"
#include "ShaderUniformBlocks.h"
#include "Attribute.h"
#include "VertexFormat.h"
#include "VertexArrayObject.h"
#include "Framebuffer.h"
#include "Renderbuffer.h"
#include "Blending.h"
#include "DepthTest.h"
#include "ScissorTest.h"
#include "CullFace.h"
#include "ClearColor.h"
#include "Viewport.h"
#include "Debug.h"

#include "../../../../Main.h"
#include "../../../Primitives/Rect.h"

#include <memory>

namespace nCine::RHI
{
	// -------------------------------------------------------------------------
	// Type aliases for state structs
	// -------------------------------------------------------------------------

	using ScissorState    = ScissorTest::State;
	using ViewportState   = Viewport::State;
	using ClearColorState = ClearColor::State;

	static constexpr std::int32_t MaxTextureUnits = Texture::MaxTextureUnits;

	// -------------------------------------------------------------------------
	// Enum → GLenum conversion helpers
	// All are constexpr so the compiler folds them into constants.
	// -------------------------------------------------------------------------

	inline constexpr GLenum ToGLenum(BufferType type) noexcept
	{
		switch (type) {
			case BufferType::Vertex:  return GL_ARRAY_BUFFER;
			case BufferType::Index:   return GL_ELEMENT_ARRAY_BUFFER;
			case BufferType::Uniform: return GL_UNIFORM_BUFFER;
			default:                  return GL_ARRAY_BUFFER;
		}
	}

	inline constexpr GLenum ToGLenum(BufferUsage usage) noexcept
	{
		switch (usage) {
			case BufferUsage::Static:  return GL_STATIC_DRAW;
			case BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
			case BufferUsage::Stream:  return GL_STREAM_DRAW;
			default:                   return GL_DYNAMIC_DRAW;
		}
	}

	inline constexpr GLbitfield ToGLbitfield(MapFlags flags) noexcept
	{
		GLbitfield result = 0;
		if (HasFlag(flags, MapFlags::Read))             result |= GL_MAP_READ_BIT;
		if (HasFlag(flags, MapFlags::Write))            result |= GL_MAP_WRITE_BIT;
		if (HasFlag(flags, MapFlags::InvalidateRange))  result |= GL_MAP_INVALIDATE_RANGE_BIT;
		if (HasFlag(flags, MapFlags::InvalidateBuffer)) result |= GL_MAP_INVALIDATE_BUFFER_BIT;
		if (HasFlag(flags, MapFlags::FlushExplicit))    result |= GL_MAP_FLUSH_EXPLICIT_BIT;
		if (HasFlag(flags, MapFlags::Unsynchronized))   result |= GL_MAP_UNSYNCHRONIZED_BIT;
#if defined(GL_MAP_PERSISTENT_BIT)
		if (HasFlag(flags, MapFlags::Persistent))       result |= GL_MAP_PERSISTENT_BIT;
		if (HasFlag(flags, MapFlags::Coherent))         result |= GL_MAP_COHERENT_BIT;
#endif
		return result;
	}

	inline constexpr MapFlags FromGLbitfield(GLbitfield flags) noexcept
	{
		MapFlags result = MapFlags::None;
		if (flags & GL_MAP_READ_BIT)              result = result | MapFlags::Read;
		if (flags & GL_MAP_WRITE_BIT)             result = result | MapFlags::Write;
		if (flags & GL_MAP_INVALIDATE_RANGE_BIT)  result = result | MapFlags::InvalidateRange;
		if (flags & GL_MAP_INVALIDATE_BUFFER_BIT) result = result | MapFlags::InvalidateBuffer;
		if (flags & GL_MAP_FLUSH_EXPLICIT_BIT)    result = result | MapFlags::FlushExplicit;
		if (flags & GL_MAP_UNSYNCHRONIZED_BIT)    result = result | MapFlags::Unsynchronized;
#if defined(GL_MAP_PERSISTENT_BIT)
		if (flags & GL_MAP_PERSISTENT_BIT)        result = result | MapFlags::Persistent;
		if (flags & GL_MAP_COHERENT_BIT)          result = result | MapFlags::Coherent;
#endif
		return result;
	}

	inline constexpr GLenum ToGLenum(PrimitiveType type) noexcept
	{
		switch (type) {
			case PrimitiveType::Points:        return GL_POINTS;
			case PrimitiveType::Lines:         return GL_LINES;
			case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
			case PrimitiveType::Triangles:     return GL_TRIANGLES;
			case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
			case PrimitiveType::TriangleFan:   return GL_TRIANGLE_FAN;
			default:                           return GL_TRIANGLES;
		}
	}

	inline constexpr PrimitiveType PrimitiveTypeFromGLenum(GLenum type) noexcept
	{
		switch (type) {
			case GL_POINTS:         return PrimitiveType::Points;
			case GL_LINES:          return PrimitiveType::Lines;
			case GL_LINE_STRIP:     return PrimitiveType::LineStrip;
			case GL_TRIANGLES:      return PrimitiveType::Triangles;
			case GL_TRIANGLE_STRIP: return PrimitiveType::TriangleStrip;
			case GL_TRIANGLE_FAN:   return PrimitiveType::TriangleFan;
			default:                return PrimitiveType::Triangles;
		}
	}

	inline constexpr GLenum ToGLenum(BlendFactor factor) noexcept
	{
		switch (factor) {
			case BlendFactor::Zero:                  return GL_ZERO;
			case BlendFactor::One:                   return GL_ONE;
			case BlendFactor::SrcColor:              return GL_SRC_COLOR;
			case BlendFactor::DstColor:              return GL_DST_COLOR;
			case BlendFactor::OneMinusSrcColor:      return GL_ONE_MINUS_SRC_COLOR;
			case BlendFactor::OneMinusDstColor:      return GL_ONE_MINUS_DST_COLOR;
			case BlendFactor::SrcAlpha:              return GL_SRC_ALPHA;
			case BlendFactor::DstAlpha:              return GL_DST_ALPHA;
			case BlendFactor::OneMinusSrcAlpha:      return GL_ONE_MINUS_SRC_ALPHA;
			case BlendFactor::OneMinusDstAlpha:      return GL_ONE_MINUS_DST_ALPHA;
			case BlendFactor::ConstantColor:         return GL_CONSTANT_COLOR;
			case BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
			case BlendFactor::ConstantAlpha:         return GL_CONSTANT_ALPHA;
			case BlendFactor::OneMinusConstantAlpha: return GL_ONE_MINUS_CONSTANT_ALPHA;
			case BlendFactor::SrcAlphaSaturate:      return GL_SRC_ALPHA_SATURATE;
			default:                                 return GL_ONE;
		}
	}

	inline constexpr BlendFactor BlendFactorFromGLenum(GLenum factor) noexcept
	{
		switch (factor) {
			case GL_ZERO:                     return BlendFactor::Zero;
			case GL_ONE:                      return BlendFactor::One;
			case GL_SRC_COLOR:                return BlendFactor::SrcColor;
			case GL_DST_COLOR:                return BlendFactor::DstColor;
			case GL_ONE_MINUS_SRC_COLOR:      return BlendFactor::OneMinusSrcColor;
			case GL_ONE_MINUS_DST_COLOR:      return BlendFactor::OneMinusDstColor;
			case GL_SRC_ALPHA:                return BlendFactor::SrcAlpha;
			case GL_DST_ALPHA:                return BlendFactor::DstAlpha;
			case GL_ONE_MINUS_SRC_ALPHA:      return BlendFactor::OneMinusSrcAlpha;
			case GL_ONE_MINUS_DST_ALPHA:      return BlendFactor::OneMinusDstAlpha;
			case GL_CONSTANT_COLOR:           return BlendFactor::ConstantColor;
			case GL_ONE_MINUS_CONSTANT_COLOR: return BlendFactor::OneMinusConstantColor;
			case GL_CONSTANT_ALPHA:           return BlendFactor::ConstantAlpha;
			case GL_ONE_MINUS_CONSTANT_ALPHA: return BlendFactor::OneMinusConstantAlpha;
			case GL_SRC_ALPHA_SATURATE:       return BlendFactor::SrcAlphaSaturate;
			default:                          return BlendFactor::One;
		}
	}

	inline constexpr GLenum ToGLenum(SamplerFilter filter) noexcept
	{
		switch (filter) {
			case SamplerFilter::Nearest:              return GL_NEAREST;
			case SamplerFilter::Linear:               return GL_LINEAR;
			case SamplerFilter::NearestMipmapNearest: return GL_NEAREST_MIPMAP_NEAREST;
			case SamplerFilter::LinearMipmapNearest:  return GL_LINEAR_MIPMAP_NEAREST;
			case SamplerFilter::NearestMipmapLinear:  return GL_NEAREST_MIPMAP_LINEAR;
			case SamplerFilter::LinearMipmapLinear:   return GL_LINEAR_MIPMAP_LINEAR;
			default:                                  return GL_NEAREST;
		}
	}

	inline constexpr SamplerFilter SamplerFilterFromGLenum(GLenum filter) noexcept
	{
		switch (filter) {
			case GL_NEAREST:                return SamplerFilter::Nearest;
			case GL_LINEAR:                 return SamplerFilter::Linear;
			case GL_NEAREST_MIPMAP_NEAREST: return SamplerFilter::NearestMipmapNearest;
			case GL_LINEAR_MIPMAP_NEAREST:  return SamplerFilter::LinearMipmapNearest;
			case GL_NEAREST_MIPMAP_LINEAR:  return SamplerFilter::NearestMipmapLinear;
			case GL_LINEAR_MIPMAP_LINEAR:   return SamplerFilter::LinearMipmapLinear;
			default:                        return SamplerFilter::Nearest;
		}
	}

	inline constexpr GLenum ToGLenum(SamplerWrapping wrap) noexcept
	{
		switch (wrap) {
			case SamplerWrapping::ClampToEdge:    return GL_CLAMP_TO_EDGE;
			case SamplerWrapping::MirroredRepeat: return GL_MIRRORED_REPEAT;
			case SamplerWrapping::Repeat:         return GL_REPEAT;
			default:                              return GL_CLAMP_TO_EDGE;
		}
	}

	inline constexpr SamplerWrapping SamplerWrappingFromGLenum(GLenum wrap) noexcept
	{
		switch (wrap) {
			case GL_CLAMP_TO_EDGE:    return SamplerWrapping::ClampToEdge;
			case GL_MIRRORED_REPEAT:  return SamplerWrapping::MirroredRepeat;
			case GL_REPEAT:           return SamplerWrapping::Repeat;
			default:                  return SamplerWrapping::ClampToEdge;
		}
	}

	// -------------------------------------------------------------------------
	// Buffer factory helpers
	// -------------------------------------------------------------------------

	inline std::unique_ptr<Buffer> CreateBuffer(BufferType type)
	{
		return std::make_unique<Buffer>(ToGLenum(type));
	}

	// -------------------------------------------------------------------------
	// Buffer operation wrappers
	// High-level code calls these instead of Buffer methods directly,
	// so SW / DC backends can substitute their own implementations.
	// -------------------------------------------------------------------------

	inline void BufferData(Buffer& buf, std::size_t size, const void* data, BufferUsage usage)
	{
		buf.BufferData(static_cast<GLsizeiptr>(size), data, ToGLenum(usage));
	}

	inline void BufferSubData(Buffer& buf, std::size_t offset, std::size_t size, const void* data)
	{
		buf.BufferSubData(static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
	}

	inline void* MapBufferRange(Buffer& buf, std::size_t offset, std::size_t size, MapFlags flags)
	{
		return buf.MapBufferRange(static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), ToGLbitfield(flags));
	}

	inline void FlushMappedBufferRange(Buffer& buf, std::size_t offset, std::size_t size)
	{
		buf.FlushMappedBufferRange(static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size));
	}

	inline void UnmapBuffer(Buffer& buf)
	{
		buf.Unmap();
	}

	inline bool BindBuffer(Buffer& buf)
	{
		return buf.Bind();
	}

	inline void BindBufferBase(Buffer& buf, std::uint32_t index)
	{
		buf.BindBufferBase(index);
	}

	inline void BindBufferRange(Buffer& buf, std::uint32_t index, std::size_t offset, std::size_t size)
	{
		buf.BindBufferRange(index, static_cast<GLintptr>(offset), static_cast<GLsizei>(size));
	}

	inline void SetBufferLabel(Buffer& buf, Death::Containers::StringView label)
	{
		buf.SetObjectLabel(label);
	}

	// -------------------------------------------------------------------------
	// Texture operation wrappers
	// -------------------------------------------------------------------------

	inline std::unique_ptr<Texture> CreateTexture(GLenum target = GL_TEXTURE_2D)
	{
		return std::make_unique<Texture>(target);
	}

	inline void BindTexture(const Texture& tex, std::uint32_t unit)
	{
		tex.Bind(unit);
	}

	inline void UnbindTexture(std::uint32_t unit)
	{
		Texture::Unbind(unit);
	}

	// -------------------------------------------------------------------------
	// Draw call wrappers
	// Geometry::Draw() calls these instead of raw glDraw* so the SW/DC
	// backends can plug in their own rasterisers.
	// -------------------------------------------------------------------------

	inline void Draw(PrimitiveType type, std::int32_t firstVertex, std::int32_t count)
	{
		glDrawArrays(ToGLenum(type), firstVertex, count);
	}

	inline void DrawInstanced(PrimitiveType type, std::int32_t firstVertex, std::int32_t count, std::int32_t instanceCount)
	{
		glDrawArraysInstanced(ToGLenum(type), firstVertex, count, instanceCount);
	}

	/// Draw indexed geometry.
	/// @param baseVertex  Added to each index before fetching vertex data.
	///                    Only used on desktop GL; ignored (0) on ES 3.0/Emscripten.
	inline void DrawIndexed(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t baseVertex = 0)
	{
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
		(void)baseVertex;
		glDrawElements(ToGLenum(type), count, GL_UNSIGNED_SHORT, indexOffset);
#else
		glDrawElementsBaseVertex(ToGLenum(type), count, GL_UNSIGNED_SHORT, indexOffset, baseVertex);
#endif
	}

	inline void DrawIndexedInstanced(PrimitiveType type, std::int32_t count, const void* indexOffset, std::int32_t instanceCount, std::int32_t baseVertex = 0)
	{
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
		(void)baseVertex;
		glDrawElementsInstanced(ToGLenum(type), count, GL_UNSIGNED_SHORT, indexOffset, instanceCount);
#else
		glDrawElementsInstancedBaseVertex(ToGLenum(type), count, GL_UNSIGNED_SHORT, indexOffset, instanceCount, baseVertex);
#endif
	}

	// -------------------------------------------------------------------------
	// Render-state helpers - thin wrappers over the existing GL* singletons
	// -------------------------------------------------------------------------

	inline void SetBlending(bool enabled, BlendFactor src, BlendFactor dst)
	{
		if (enabled) {
			Blending::Enable();
			Blending::SetBlendFunc(ToGLenum(src), ToGLenum(dst));
		} else {
			Blending::Disable();
		}
	}

	inline void SetDepthTest(bool enabled)
	{
		if (enabled) {
			DepthTest::Enable();
		} else {
			DepthTest::Disable();
		}
	}

	inline void SetScissorTest(bool enabled, std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		if (enabled) {
			ScissorTest::Enable(x, y, width, height);
		} else {
			ScissorTest::Disable();
		}
	}

	inline void SetScissorTest(bool enabled, const nCine::Recti& rect)
	{
		if (enabled) {
			ScissorTest::Enable(rect);
		} else {
			ScissorTest::Disable();
		}
	}

	inline ScissorTest::State GetScissorState()
	{
		return ScissorTest::GetState();
	}

	inline void SetScissorState(const ScissorTest::State& state)
	{
		ScissorTest::SetState(state);
	}

	inline void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		Viewport::SetRect(x, y, width, height);
	}

	inline void SetClearColor(float r, float g, float b, float a)
	{
		ClearColor::SetColor(r, g, b, a);
	}

	inline void Clear(ClearFlags flags)
	{
		GLbitfield mask = 0;
		if (static_cast<std::uint32_t>(flags & ClearFlags::Color)   != 0) mask |= GL_COLOR_BUFFER_BIT;
		if (static_cast<std::uint32_t>(flags & ClearFlags::Depth)   != 0) mask |= GL_DEPTH_BUFFER_BIT;
		if (static_cast<std::uint32_t>(flags & ClearFlags::Stencil) != 0) mask |= GL_STENCIL_BUFFER_BIT;
		if (mask) glClear(mask);
	}

	inline void SetDepthMask(bool enabled)
	{
		if (enabled) {
			DepthTest::EnableDepthMask();
		} else {
			DepthTest::DisableDepthMask();
		}
	}

	inline ViewportState GetViewportState()
	{
		return Viewport::GetState();
	}

	inline void SetViewportState(const ViewportState& s)
	{
		Viewport::SetState(s);
	}

	inline ClearColorState GetClearColorState()
	{
		return ClearColor::GetState();
	}

	inline void SetClearColorState(const ClearColorState& s)
	{
		ClearColor::SetState(s);
	}

	// -------------------------------------------------------------------------
	// Framebuffer helpers - thin wrappers hiding backend-specific API
	// -------------------------------------------------------------------------

	inline void FramebufferBind(Framebuffer& fbo)
	{
		fbo.Bind(GL_DRAW_FRAMEBUFFER);
	}

	inline void FramebufferUnbind()
	{
		Framebuffer::Unbind(GL_DRAW_FRAMEBUFFER);
	}

	inline void FramebufferSetDrawBuffers(Framebuffer& fbo, std::uint32_t n)
	{
		fbo.DrawBuffers(n);
	}

	inline void FramebufferAttachTexture(Framebuffer& fbo, Texture& texture, std::uint32_t colorIndex)
	{
		fbo.AttachTexture(texture, GLenum(GL_COLOR_ATTACHMENT0 + colorIndex));
	}

	inline void FramebufferDetachTexture(Framebuffer& fbo, std::uint32_t colorIndex)
	{
		fbo.DetachTexture(GLenum(GL_COLOR_ATTACHMENT0 + colorIndex));
	}

	inline bool FramebufferIsComplete(Framebuffer& fbo)
	{
		return fbo.IsStatusComplete();
	}

	template<typename StringViewType>
	inline void FramebufferSetLabel(Framebuffer& fbo, StringViewType label)
	{
		fbo.SetObjectLabel(label);
	}

}

#endif
