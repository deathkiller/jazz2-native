#pragma once

#include "RhiTypes.h"

#include <cstdint>

// Compile-time RHI backend selection — exactly one backend is compiled into a binary. The OpenGL
// family backend (OpenGL 3.3 core / OpenGL ES 3.0 / WebGL 2 / ANGLE) is the default when no
// `WITH_RHI_*` macro is defined by the build.
#if !defined(WITH_RHI_GL) && !defined(WITH_RHI_D3D11) && !defined(WITH_RHI_VULKAN) && !defined(WITH_RHI_SOFTWARE)
#	define WITH_RHI_GL
#endif

#if defined(WITH_RHI_GL)

namespace nCine::RhiGL
{
	class GLDevice;
	class GLTexture;
	class GLBufferObject;
	class GLShader;
	class GLShaderProgram;
	class GLShaderUniforms;
	class GLShaderUniformBlocks;
	class GLUniform;
	class GLUniformBlock;
	class GLUniformCache;
	class GLUniformBlockCache;
	class GLAttribute;
	class GLFramebuffer;
	class GLRenderbuffer;
	class GLRenderTarget;
	class GLVertexArrayObject;
	class GLVertexFormat;
	class GLDebug;
}

namespace nCine::Rhi
{
	// Backend-neutral names for the classes of the selected backend. The render pipeline only refers
	// to these aliases, so that additional backends only have to provide the same set of names with
	// the same surface. This header only forward-declares them — include `Rhi.h` for the definitions.
	using Device = RhiGL::GLDevice;
	using Texture = RhiGL::GLTexture;
	using Buffer = RhiGL::GLBufferObject;
	using Shader = RhiGL::GLShader;
	using ShaderProgram = RhiGL::GLShaderProgram;
	using ShaderUniforms = RhiGL::GLShaderUniforms;
	using ShaderUniformBlocks = RhiGL::GLShaderUniformBlocks;
	using Uniform = RhiGL::GLUniform;
	using UniformBlock = RhiGL::GLUniformBlock;
	using UniformCache = RhiGL::GLUniformCache;
	using UniformBlockCache = RhiGL::GLUniformBlockCache;
	using Attribute = RhiGL::GLAttribute;
	using Framebuffer = RhiGL::GLFramebuffer;
	using Renderbuffer = RhiGL::GLRenderbuffer;
	using RenderTarget = RhiGL::GLRenderTarget;
	using VertexArray = RhiGL::GLVertexArrayObject;
	using VertexFormat = RhiGL::GLVertexFormat;

	// Debug output and object labelling
	using Debug = RhiGL::GLDebug;

	/**
		@brief Locates a sub-range within a buffer object, together with its mapped memory
	*/
	struct BufferRange
	{
		BufferRange()
			: object(nullptr), size(0), offset(0), mapBase(nullptr) {}

		/** @brief Buffer object the range belongs to */
		Buffer* object;
		/** @brief Size of the range in bytes */
		std::uint32_t size;
		/** @brief Byte offset of the range within the buffer object */
		std::uint32_t offset;
		/** @brief Base pointer of the mapped (or host) buffer memory */
		std::uint8_t* mapBase;
	};
}

#elif defined(WITH_RHI_SOFTWARE)

namespace nCine::RhiSoftware
{
	class SwDevice;
	class SwTexture;
	class SwBuffer;
	class SwShader;
	class SwShaderProgram;
	class SwShaderUniforms;
	class SwShaderUniformBlocks;
	class SwUniform;
	class SwUniformBlock;
	class SwUniformCache;
	class SwUniformBlockCache;
	class SwAttribute;
	class SwFramebuffer;
	class SwRenderbuffer;
	class SwRenderTarget;
	class SwVertexArray;
	class SwVertexFormat;
	class SwDebug;
}

namespace nCine::Rhi
{
	// Backend-neutral names for the classes of the selected backend. The render pipeline only refers
	// to these aliases, so that additional backends only have to provide the same set of names with
	// the same surface. This header only forward-declares them — include `Rhi.h` for the definitions.
	using Device = RhiSoftware::SwDevice;
	using Texture = RhiSoftware::SwTexture;
	using Buffer = RhiSoftware::SwBuffer;
	using Shader = RhiSoftware::SwShader;
	using ShaderProgram = RhiSoftware::SwShaderProgram;
	using ShaderUniforms = RhiSoftware::SwShaderUniforms;
	using ShaderUniformBlocks = RhiSoftware::SwShaderUniformBlocks;
	using Uniform = RhiSoftware::SwUniform;
	using UniformBlock = RhiSoftware::SwUniformBlock;
	using UniformCache = RhiSoftware::SwUniformCache;
	using UniformBlockCache = RhiSoftware::SwUniformBlockCache;
	using Attribute = RhiSoftware::SwAttribute;
	using Framebuffer = RhiSoftware::SwFramebuffer;
	using Renderbuffer = RhiSoftware::SwRenderbuffer;
	using RenderTarget = RhiSoftware::SwRenderTarget;
	using VertexArray = RhiSoftware::SwVertexArray;
	using VertexFormat = RhiSoftware::SwVertexFormat;

	// Debug output and object labelling
	using Debug = RhiSoftware::SwDebug;

	/**
		@brief Locates a sub-range within a buffer object, together with its mapped memory
	*/
	struct BufferRange
	{
		BufferRange()
			: object(nullptr), size(0), offset(0), mapBase(nullptr) {}

		/** @brief Buffer object the range belongs to */
		Buffer* object;
		/** @brief Size of the range in bytes */
		std::uint32_t size;
		/** @brief Byte offset of the range within the buffer object */
		std::uint32_t offset;
		/** @brief Base pointer of the mapped (or host) buffer memory */
		std::uint8_t* mapBase;
	};
}

#else
#	error No RHI backend selected — define WITH_RHI_GL (or another WITH_RHI_* backend)
#endif
