#pragma once

#include "RhiTypes.h"

#include <cstdint>

// Compile-time RHI backend selection — exactly one backend is compiled into a binary. The OpenGL
// family backend (OpenGL 3.3 core / OpenGL ES 3.0 / WebGL 2 / ANGLE) is the default when no
// `WITH_RHI_*` macro is defined by the build.
#if !defined(WITH_RHI_GL) && !defined(WITH_RHI_D3D11) && !defined(WITH_RHI_VULKAN) && !defined(WITH_RHI_SOFTWARE) && !defined(WITH_RHI_GX) && !defined(WITH_RHI_PVR)
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

namespace nCine::RHI::GL
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

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend. The render pipeline only refers
	// to these aliases, so that additional backends only have to provide the same set of names with
	// the same surface. This header only forward-declares them — include `Rhi.h` for the definitions.
	using Device = RHI::GL::GLDevice;
	using Texture = RHI::GL::GLTexture;
	using Buffer = RHI::GL::GLBufferObject;
	using Shader = RHI::GL::GLShader;
	using ShaderProgram = RHI::GL::GLShaderProgram;
	using ShaderUniforms = RHI::GL::GLShaderUniforms;
	using ShaderUniformBlocks = RHI::GL::GLShaderUniformBlocks;
	using Uniform = RHI::GL::GLUniform;
	using UniformBlock = RHI::GL::GLUniformBlock;
	using UniformCache = RHI::GL::GLUniformCache;
	using UniformBlockCache = RHI::GL::GLUniformBlockCache;
	using Attribute = RHI::GL::GLAttribute;
	using Framebuffer = RHI::GL::GLFramebuffer;
	using Renderbuffer = RHI::GL::GLRenderbuffer;
	using RenderTarget = RHI::GL::GLRenderTarget;
	using VertexArray = RHI::GL::GLVertexArrayObject;
	using VertexFormat = RHI::GL::GLVertexFormat;

	// Debug output and object labelling
	using Debug = RHI::GL::GLDebug;

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
// backend has off-screen render targets (@ref RHI::Software::SwRenderTarget), so `RHI_CAP_FRAMEBUFFERS` is
// defined; but its "shaders" are slow CPU-transpiled effects that must NOT drive full-screen post-processing,
// so `RHI_CAP_SHADERS` is deliberately left undefined. The pipeline then skips the bloom chain, uses the cheap
// no-shader lighting path and renders the scene directly to the screen buffer instead of through the shader
// combine/rescale passes.
#define RHI_CAP_FRAMEBUFFERS

namespace nCine::RHI::Software
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

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend. The render pipeline only refers
	// to these aliases, so that additional backends only have to provide the same set of names with
	// the same surface. This header only forward-declares them — include `Rhi.h` for the definitions.
	using Device = RHI::Software::SwDevice;
	using Texture = RHI::Software::SwTexture;
	using Buffer = RHI::Software::SwBuffer;
	using Shader = RHI::Software::SwShader;
	using ShaderProgram = RHI::Software::SwShaderProgram;
	using ShaderUniforms = RHI::Software::SwShaderUniforms;
	using ShaderUniformBlocks = RHI::Software::SwShaderUniformBlocks;
	using Uniform = RHI::Software::SwUniform;
	using UniformBlock = RHI::Software::SwUniformBlock;
	using UniformCache = RHI::Software::SwUniformCache;
	using UniformBlockCache = RHI::Software::SwUniformBlockCache;
	using Attribute = RHI::Software::SwAttribute;
	using Framebuffer = RHI::Software::SwFramebuffer;
	using Renderbuffer = RHI::Software::SwRenderbuffer;
	using RenderTarget = RHI::Software::SwRenderTarget;
	using VertexArray = RHI::Software::SwVertexArray;
	using VertexFormat = RHI::Software::SwVertexFormat;

	// Debug output and object labelling
	using Debug = RHI::Software::SwDebug;

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

#elif defined(WITH_RHI_GX)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The GX
// backend (Nintendo GameCube/Wii "Flipper"/"Hollywood") is a fixed-function hardware backend: it has
// off-screen render targets (EFB copy-out, needed by the textured-background passes), so
// `RHI_CAP_FRAMEBUFFERS` is defined; but it has no programmable shaders, so `RHI_CAP_SHADERS` stays
// undefined and the game runs the direct tier (scene straight to the screen at the logical resolution,
// CPU lightmap composited by the device's lighting hook - the same tier as the software backend).
#define RHI_CAP_FRAMEBUFFERS

namespace nCine::RHI::GX
{
	class GxDevice;
	class GxTexture;
	class GxBuffer;
	class GxShader;
	class GxShaderProgram;
	class GxShaderUniforms;
	class GxShaderUniformBlocks;
	class GxUniform;
	class GxUniformBlock;
	class GxUniformCache;
	class GxUniformBlockCache;
	class GxAttribute;
	class GxFramebuffer;
	class GxRenderbuffer;
	class GxRenderTarget;
	class GxVertexArray;
	class GxVertexFormat;
	class GxDebug;
}

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend (see the OpenGL arm above)
	using Device = RHI::GX::GxDevice;
	using Texture = RHI::GX::GxTexture;
	using Buffer = RHI::GX::GxBuffer;
	using Shader = RHI::GX::GxShader;
	using ShaderProgram = RHI::GX::GxShaderProgram;
	using ShaderUniforms = RHI::GX::GxShaderUniforms;
	using ShaderUniformBlocks = RHI::GX::GxShaderUniformBlocks;
	using Uniform = RHI::GX::GxUniform;
	using UniformBlock = RHI::GX::GxUniformBlock;
	using UniformCache = RHI::GX::GxUniformCache;
	using UniformBlockCache = RHI::GX::GxUniformBlockCache;
	using Attribute = RHI::GX::GxAttribute;
	using Framebuffer = RHI::GX::GxFramebuffer;
	using Renderbuffer = RHI::GX::GxRenderbuffer;
	using RenderTarget = RHI::GX::GxRenderTarget;
	using VertexArray = RHI::GX::GxVertexArray;
	using VertexFormat = RHI::GX::GxVertexFormat;

	// Debug output and object labelling
	using Debug = RHI::GX::GxDebug;

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

#elif defined(WITH_RHI_PVR)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The PVR
// backend (Sega Dreamcast "PowerVR CLX2" via KallistiOS) is a fixed-function hardware backend: it has
// off-screen render targets (the tile accelerator can render a scene into a texture, needed by the
// textured-background passes), so `RHI_CAP_FRAMEBUFFERS` is defined; but it has no programmable shaders,
// so `RHI_CAP_SHADERS` stays undefined and the game runs the direct tier (the same tier as the software
// and GX backends: scene straight to the display at the logical resolution, CPU lightmap composited by
// the device's lighting hook).
#define RHI_CAP_FRAMEBUFFERS

namespace nCine::RHI::PVR
{
	class PvrDevice;
	class PvrTexture;
	class PvrBuffer;
	class PvrShader;
	class PvrShaderProgram;
	class PvrShaderUniforms;
	class PvrShaderUniformBlocks;
	class PvrUniform;
	class PvrUniformBlock;
	class PvrUniformCache;
	class PvrUniformBlockCache;
	class PvrAttribute;
	class PvrFramebuffer;
	class PvrRenderbuffer;
	class PvrRenderTarget;
	class PvrVertexArray;
	class PvrVertexFormat;
	class PvrDebug;
}

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend (see the OpenGL arm above)
	using Device = RHI::PVR::PvrDevice;
	using Texture = RHI::PVR::PvrTexture;
	using Buffer = RHI::PVR::PvrBuffer;
	using Shader = RHI::PVR::PvrShader;
	using ShaderProgram = RHI::PVR::PvrShaderProgram;
	using ShaderUniforms = RHI::PVR::PvrShaderUniforms;
	using ShaderUniformBlocks = RHI::PVR::PvrShaderUniformBlocks;
	using Uniform = RHI::PVR::PvrUniform;
	using UniformBlock = RHI::PVR::PvrUniformBlock;
	using UniformCache = RHI::PVR::PvrUniformCache;
	using UniformBlockCache = RHI::PVR::PvrUniformBlockCache;
	using Attribute = RHI::PVR::PvrAttribute;
	using Framebuffer = RHI::PVR::PvrFramebuffer;
	using Renderbuffer = RHI::PVR::PvrRenderbuffer;
	using RenderTarget = RHI::PVR::PvrRenderTarget;
	using VertexArray = RHI::PVR::PvrVertexArray;
	using VertexFormat = RHI::PVR::PvrVertexFormat;

	// Debug output and object labelling
	using Debug = RHI::PVR::PvrDebug;

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

namespace nCine::RHI::D3D11
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

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend. The render pipeline only refers
	// to these aliases, so that additional backends only have to provide the same set of names with
	// the same surface. This header only forward-declares them — include `Rhi.h` for the definitions.
	using Device = RHI::D3D11::D3D11Device;
	using Texture = RHI::D3D11::D3D11Texture;
	using Buffer = RHI::D3D11::D3D11BufferObject;
	using Shader = RHI::D3D11::D3D11Shader;
	using ShaderProgram = RHI::D3D11::D3D11ShaderProgram;
	using ShaderUniforms = RHI::D3D11::D3D11ShaderUniforms;
	using ShaderUniformBlocks = RHI::D3D11::D3D11ShaderUniformBlocks;
	using Uniform = RHI::D3D11::D3D11Uniform;
	using UniformBlock = RHI::D3D11::D3D11UniformBlock;
	using UniformCache = RHI::D3D11::D3D11UniformCache;
	using UniformBlockCache = RHI::D3D11::D3D11UniformBlockCache;
	using Attribute = RHI::D3D11::D3D11Attribute;
	using Framebuffer = RHI::D3D11::D3D11Framebuffer;
	using Renderbuffer = RHI::D3D11::D3D11Renderbuffer;
	using RenderTarget = RHI::D3D11::D3D11RenderTarget;
	using VertexArray = RHI::D3D11::D3D11VertexArray;
	using VertexFormat = RHI::D3D11::D3D11VertexFormat;

	// Debug output and object labelling
	using Debug = RHI::D3D11::D3D11Debug;

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

namespace nCine::RHI::Vulkan
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

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend. The render pipeline only refers
	// to these aliases, so that additional backends only have to provide the same set of names with
	// the same surface. This header only forward-declares them — include `Rhi.h` for the definitions.
	using Device = RHI::Vulkan::VulkanDevice;
	using Texture = RHI::Vulkan::VulkanTexture;
	using Buffer = RHI::Vulkan::VulkanBufferObject;
	using Shader = RHI::Vulkan::VulkanShader;
	using ShaderProgram = RHI::Vulkan::VulkanShaderProgram;
	using ShaderUniforms = RHI::Vulkan::VulkanShaderUniforms;
	using ShaderUniformBlocks = RHI::Vulkan::VulkanShaderUniformBlocks;
	using Uniform = RHI::Vulkan::VulkanUniform;
	using UniformBlock = RHI::Vulkan::VulkanUniformBlock;
	using UniformCache = RHI::Vulkan::VulkanUniformCache;
	using UniformBlockCache = RHI::Vulkan::VulkanUniformBlockCache;
	using Attribute = RHI::Vulkan::VulkanAttribute;
	using Framebuffer = RHI::Vulkan::VulkanFramebuffer;
	using Renderbuffer = RHI::Vulkan::VulkanRenderbuffer;
	using RenderTarget = RHI::Vulkan::VulkanRenderTarget;
	using VertexArray = RHI::Vulkan::VulkanVertexArray;
	using VertexFormat = RHI::Vulkan::VulkanVertexFormat;

	// Debug output and object labelling
	using Debug = RHI::Vulkan::VulkanDebug;

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

// Derived tier macro: the full GPU post-processing pipeline (scene FBO + lighting FBO + blur chain +
// rescale/antialiasing passes) requires BOTH programmable shaders and off-screen render targets, so
// game code gates it on this single macro instead of ad-hoc compounds of the two capabilities (or a
// backend-identity test like WITH_RHI_SOFTWARE, which would silently misroute a future fixed-function
// console backend). Backends lacking either capability take the direct tier: the scene renders
// straight into the screen buffer at the logical resolution (the backend's presentation stretches it
// to the display - a backend without RHI_CAP_SHADERS must provide Device::ResizeScreenFramebuffer())
// and lighting is composited by the CPU lightmap path in CombineRenderer.
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
#	define RHI_CAP_POSTPROCESSING
#endif
