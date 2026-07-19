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

// Rendering capability flags of the selected backend. Pipeline code guarded by these compiles only when the
// active backend advertises the capability. `RHI_CAP_SHADERS` means the backend has hardware programmable
// shaders cheap enough to drive full-screen post-processing (the bloom Blur/Downsample/Combine chain, the
// shader lighting compositing and the rescale/antialiasing passes); `RHI_CAP_FRAMEBUFFERS` means off-screen
// render targets are available. The OpenGL family backend provides both.
#define RHI_CAP_SHADERS
#define RHI_CAP_FRAMEBUFFERS

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

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The software
// backend has off-screen render targets (@ref RhiSoftware::SwRenderTarget), so `RHI_CAP_FRAMEBUFFERS` is
// defined; but its "shaders" are slow CPU-transpiled effects that must NOT drive full-screen post-processing,
// so `RHI_CAP_SHADERS` is deliberately left undefined. The pipeline then skips the bloom chain, uses the cheap
// no-shader lighting path and renders the scene directly to the screen buffer instead of through the shader
// combine/rescale passes.
#define RHI_CAP_FRAMEBUFFERS

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

#elif defined(WITH_RHI_D3D11)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The Direct3D
// 11 backend is a full-pipeline hardware backend like the OpenGL family: it has programmable shaders and
// off-screen render targets, so both `RHI_CAP_SHADERS` and `RHI_CAP_FRAMEBUFFERS` are defined and the pipeline
// runs the whole bloom / lighting / combine / rescale chain exactly as it does on OpenGL.
#define RHI_CAP_SHADERS
#define RHI_CAP_FRAMEBUFFERS

namespace nCine::RhiD3D11
{
	class D3D11Device;
	class D3D11Texture;
	class D3D11BufferObject;
	class D3D11Shader;
	class D3D11ShaderProgram;
	class D3D11ShaderUniforms;
	class D3D11ShaderUniformBlocks;
	class D3D11Uniform;
	class D3D11UniformBlock;
	class D3D11UniformCache;
	class D3D11UniformBlockCache;
	class D3D11Attribute;
	class D3D11Framebuffer;
	class D3D11Renderbuffer;
	class D3D11RenderTarget;
	class D3D11VertexArray;
	class D3D11VertexFormat;
	class D3D11Debug;
}

namespace nCine::Rhi
{
	// Backend-neutral names for the classes of the selected backend. The render pipeline only refers
	// to these aliases, so that additional backends only have to provide the same set of names with
	// the same surface. This header only forward-declares them — include `Rhi.h` for the definitions.
	using Device = RhiD3D11::D3D11Device;
	using Texture = RhiD3D11::D3D11Texture;
	using Buffer = RhiD3D11::D3D11BufferObject;
	using Shader = RhiD3D11::D3D11Shader;
	using ShaderProgram = RhiD3D11::D3D11ShaderProgram;
	using ShaderUniforms = RhiD3D11::D3D11ShaderUniforms;
	using ShaderUniformBlocks = RhiD3D11::D3D11ShaderUniformBlocks;
	using Uniform = RhiD3D11::D3D11Uniform;
	using UniformBlock = RhiD3D11::D3D11UniformBlock;
	using UniformCache = RhiD3D11::D3D11UniformCache;
	using UniformBlockCache = RhiD3D11::D3D11UniformBlockCache;
	using Attribute = RhiD3D11::D3D11Attribute;
	using Framebuffer = RhiD3D11::D3D11Framebuffer;
	using Renderbuffer = RhiD3D11::D3D11Renderbuffer;
	using RenderTarget = RhiD3D11::D3D11RenderTarget;
	using VertexArray = RhiD3D11::D3D11VertexArray;
	using VertexFormat = RhiD3D11::D3D11VertexFormat;

	// Debug output and object labelling
	using Debug = RhiD3D11::D3D11Debug;

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

#elif defined(WITH_RHI_VULKAN)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). Vulkan is a
// full-pipeline hardware backend like the OpenGL family and Direct3D 11: it has programmable shaders (built
// offline as SPIR-V, embedded per program-variant) and off-screen render targets, so both `RHI_CAP_SHADERS`
// and `RHI_CAP_FRAMEBUFFERS` are defined and the pipeline runs the whole bloom / lighting / combine / rescale
// chain exactly as it does on OpenGL.
#define RHI_CAP_SHADERS
#define RHI_CAP_FRAMEBUFFERS

namespace nCine::RhiVulkan
{
	class VulkanDevice;
	class VulkanTexture;
	class VulkanBufferObject;
	class VulkanShader;
	class VulkanShaderProgram;
	class VulkanShaderUniforms;
	class VulkanShaderUniformBlocks;
	class VulkanUniform;
	class VulkanUniformBlock;
	class VulkanUniformCache;
	class VulkanUniformBlockCache;
	class VulkanAttribute;
	class VulkanFramebuffer;
	class VulkanRenderbuffer;
	class VulkanRenderTarget;
	class VulkanVertexArray;
	class VulkanVertexFormat;
	class VulkanDebug;
}

namespace nCine::Rhi
{
	// Backend-neutral names for the classes of the selected backend. The render pipeline only refers
	// to these aliases, so that additional backends only have to provide the same set of names with
	// the same surface. This header only forward-declares them — include `Rhi.h` for the definitions.
	using Device = RhiVulkan::VulkanDevice;
	using Texture = RhiVulkan::VulkanTexture;
	using Buffer = RhiVulkan::VulkanBufferObject;
	using Shader = RhiVulkan::VulkanShader;
	using ShaderProgram = RhiVulkan::VulkanShaderProgram;
	using ShaderUniforms = RhiVulkan::VulkanShaderUniforms;
	using ShaderUniformBlocks = RhiVulkan::VulkanShaderUniformBlocks;
	using Uniform = RhiVulkan::VulkanUniform;
	using UniformBlock = RhiVulkan::VulkanUniformBlock;
	using UniformCache = RhiVulkan::VulkanUniformCache;
	using UniformBlockCache = RhiVulkan::VulkanUniformBlockCache;
	using Attribute = RhiVulkan::VulkanAttribute;
	using Framebuffer = RhiVulkan::VulkanFramebuffer;
	using Renderbuffer = RhiVulkan::VulkanRenderbuffer;
	using RenderTarget = RhiVulkan::VulkanRenderTarget;
	using VertexArray = RhiVulkan::VulkanVertexArray;
	using VertexFormat = RhiVulkan::VulkanVertexFormat;

	// Debug output and object labelling
	using Debug = RhiVulkan::VulkanDebug;

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
#	error No RHI backend selected - define WITH_RHI_GL (or another WITH_RHI_* backend)
#endif
