#pragma once

#include "RhiTypes.h"

#include <cstdint>

// Compile-time RHI backend selection — exactly one backend is compiled into a binary. The OpenGL
// family backend (OpenGL 3.3 core / OpenGL ES 3.0 / WebGL 2 / ANGLE) is the default when no
// `WITH_RHI_*` macro is defined by the build.
#if !defined(WITH_RHI_GL) && !defined(WITH_RHI_D3D11) && !defined(WITH_RHI_VULKAN) && \
		!defined(WITH_RHI_SOFTWARE) && !defined(WITH_RHI_GX) && !defined(WITH_RHI_PVR) && \
		!defined(WITH_RHI_GU) && !defined(WITH_RHI_GS) && !defined(WITH_RHI_RDP) && \
		!defined(WITH_RHI_GXM) && !defined(WITH_RHI_RSX) && !defined(WITH_RHI_LEGACYGL)
#	define WITH_RHI_GL
#endif

#if defined(WITH_RHI_GL)

// Rendering capability flags of the selected backend. Pipeline code guarded by these compiles only when the
// active backend advertises the capability. `RHI_CAP_SHADERS` means the backend has hardware programmable
// shaders cheap enough to drive full-screen post-processing (the bloom Blur/Downsample/Combine chain, the
// shader lighting compositing and the rescale/antialiasing passes); `RHI_CAP_FRAMEBUFFERS` means off-screen
// render targets are available. The OpenGL family backend provides both.
//
// `RHI_CAP_HEAVY_RESCALE_SHADERS` means the heaviest of the shipped rescale filters - CleanEdge, SABR and
// Monochrome - are actually available. They are ordinary shaders everywhere a compiler runs at load time or
// targets a modern profile, so every such backend advertises this; a backend that compiles its shaders
// OFFLINE against a fixed profile may find one rejected, which is a build-time fact its menu has to respect
// (see the RSX arm). Anything gated on it must also be reachable without it - the modes simply disappear.
// `RHI_CAP_BATCHING` means CPU-side batching works: many sprites that share a material are collected into
// one draw whose per-instance data travels in a uniform block, and the backend must be able to run the
// batched twin of a shader (which indexes that block) for it to be worth anything. A backend that leaves it
// undefined never receives a batched shader at all - @ref RenderResources::GetBatchedShader reports none, so
// @ref RenderBatcher collects nothing and every command is submitted on its own. Correct, just more draws.
// It is a build-time fact everywhere (a shader profile either expresses the indexing or it does not), which
// is why it lives here rather than among the runtime @ref IRhiCapabilities values.
#define RHI_CAP_SHADERS
#define RHI_CAP_FRAMEBUFFERS
#define RHI_CAP_HEAVY_RESCALE_SHADERS
#if !defined(RHI_GL_PROFILE_ES2)
// ES2 has no uniform buffer objects to put the instance array in, and the batched programs' ESSL 100 form is
// not even valid there (a "uint aMeshIndex" integer attribute), so that profile does not batch
#	define RHI_CAP_BATCHING
#endif

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
	class GLRhiCapabilities;
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

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::GL::GLRhiCapabilities;

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
#define RHI_CAP_BATCHING

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
	class SwRhiCapabilities;
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

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::Software::SwRhiCapabilities;

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
#define RHI_CAP_BATCHING

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
	class GxRhiCapabilities;
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

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::GX::GxRhiCapabilities;

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
//
// `RHI_CAP_PALETTED_TEXTURES` means an R8 texture of palette indices is resolved through the palette by the
// hardware itself, under every effect - on the PowerVR the lookup belongs to the texture read rather than to
// a shader, so an image that is already indices can be uploaded as they are instead of being expanded to
// colors first. The GX backend has the same hardware format (CI8) but only reads it through the palette when
// the effect asks for it, so it does not advertise this yet.
//
// `RHI_CAP_STREAMING_TEXTURES` means a texture's storage can be written by the CPU in place, so content
// that is regenerated every frame (the cinematics) can be produced straight into it instead of into a
// buffer that is then copied twice - the tile accelerator reads textures out of ordinary video memory,
// which is addressable, unlike a driver-owned object that has to be uploaded through an API.
#define RHI_CAP_FRAMEBUFFERS
#define RHI_CAP_PALETTED_TEXTURES
#define RHI_CAP_STREAMING_TEXTURES
#define RHI_CAP_BATCHING

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
	class PvrRhiCapabilities;
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

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::PVR::PvrRhiCapabilities;

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

#elif defined(WITH_RHI_GU)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The GU
// backend (PlayStation Portable "Allegrex GE" via PSPSDK) is a fixed-function hardware backend just like GX
// and PVR: the GE can render into any 16/32-bit surface in memory and the draw target is nothing but a
// pointer handed to sceGuDrawBufferList, so off-screen render targets are available (needed by the
// textured-background passes) and `RHI_CAP_FRAMEBUFFERS` is defined; but it has no programmable shaders, so
// `RHI_CAP_SHADERS` stays undefined and the game runs the direct tier (the same tier as the software, GX and
// PVR backends: scene straight to the display at the logical resolution, CPU lightmap composited by the
// device's lighting hook).
//
// `RHI_CAP_PALETTED_TEXTURES` means an R8 texture of palette indices is resolved through the palette by the
// hardware itself, under every effect. The GE has exactly that in the form of the CLUT (GU_PSM_T8 plus
// sceGuClutLoad), and the lookup belongs to the texture read rather than to any programmable stage, so the
// meaning PVR gives this flag carries over verbatim - an image that is already indices can be uploaded as
// they are instead of being expanded to colors first.
//
// `RHI_CAP_STREAMING_TEXTURES` means a texture's storage can be written by the CPU in place, so content that
// is regenerated every frame (the cinematics) can be produced straight into it. The GE reads textures out of
// ordinary addressable memory (either main RAM or VRAM through sceGeEdramGetAddr), never out of a
// driver-owned object, so this holds on the PSP as well.
#define RHI_CAP_FRAMEBUFFERS
#define RHI_CAP_PALETTED_TEXTURES
#define RHI_CAP_STREAMING_TEXTURES
#define RHI_CAP_BATCHING

namespace nCine::RHI::GU
{
	class GuDevice;
	class GuTexture;
	class GuBuffer;
	class GuShader;
	class GuShaderProgram;
	class GuShaderUniforms;
	class GuShaderUniformBlocks;
	class GuUniform;
	class GuUniformBlock;
	class GuUniformCache;
	class GuUniformBlockCache;
	class GuAttribute;
	class GuFramebuffer;
	class GuRenderbuffer;
	class GuRenderTarget;
	class GuVertexArray;
	class GuVertexFormat;
	class GuRhiCapabilities;
	class GuDebug;
}

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend (see the OpenGL arm above)
	using Device = RHI::GU::GuDevice;
	using Texture = RHI::GU::GuTexture;
	using Buffer = RHI::GU::GuBuffer;
	using Shader = RHI::GU::GuShader;
	using ShaderProgram = RHI::GU::GuShaderProgram;
	using ShaderUniforms = RHI::GU::GuShaderUniforms;
	using ShaderUniformBlocks = RHI::GU::GuShaderUniformBlocks;
	using Uniform = RHI::GU::GuUniform;
	using UniformBlock = RHI::GU::GuUniformBlock;
	using UniformCache = RHI::GU::GuUniformCache;
	using UniformBlockCache = RHI::GU::GuUniformBlockCache;
	using Attribute = RHI::GU::GuAttribute;
	using Framebuffer = RHI::GU::GuFramebuffer;
	using Renderbuffer = RHI::GU::GuRenderbuffer;
	using RenderTarget = RHI::GU::GuRenderTarget;
	using VertexArray = RHI::GU::GuVertexArray;
	using VertexFormat = RHI::GU::GuVertexFormat;

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::GU::GuRhiCapabilities;

	// Debug output and object labelling
	using Debug = RHI::GU::GuDebug;

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

#elif defined(WITH_RHI_LEGACYGL)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The legacy
// GL backend drives the FIXED-FUNCTION half of OpenGL - immediate-mode arrays, the texture environment and
// the 1.3 texture combiners - which is what MorphOS' TinyGL implements and what remains of a desktop GL in a
// compatibility context. It belongs with PVR/GX/GU/GS/RDP rather than with the OpenGL backend: it consumes
// the same transpiled `fixed_function` effect tables, and the combiners take the place of each console's
// TEV. So `RHI_CAP_SHADERS` stays undefined and the game runs the direct tier (scene straight to the
// drawable at the logical resolution, CPU lightmap composited by the device's lighting hook).
//
// `RHI_CAP_FRAMEBUFFERS` is defined because a render target is always reachable here - through a framebuffer
// object where one works, and otherwise by rendering into the back buffer and copying the region into the
// texture (see @ref RHI::LegacyGL::LegacyGlRenderTarget), which is a GL 1.1 operation.
//
// `RHI_CAP_PALETTED_TEXTURES` is NOT defined: the paletted-texture extension the consoles' CLUT hardware
// corresponds to (GL_EXT_paletted_texture) was removed from every driver a long time ago and TinyGL never
// had it, so an indexed image is baked through its palette row into RGBA8 before it is uploaded.
//
// `RHI_CAP_STREAMING_TEXTURES` is NOT defined either. A GL texture is a driver-owned object rather than
// memory the CPU can write, so content that is regenerated every frame is uploaded like any other.
#define RHI_CAP_FRAMEBUFFERS
#define RHI_CAP_BATCHING

namespace nCine::RHI::LegacyGL
{
	class LegacyGlDevice;
	class LegacyGlTexture;
	class LegacyGlBuffer;
	class LegacyGlShader;
	class LegacyGlShaderProgram;
	class LegacyGlShaderUniforms;
	class LegacyGlShaderUniformBlocks;
	class LegacyGlUniform;
	class LegacyGlUniformBlock;
	class LegacyGlUniformCache;
	class LegacyGlUniformBlockCache;
	class LegacyGlAttribute;
	class LegacyGlFramebuffer;
	class LegacyGlRenderbuffer;
	class LegacyGlRenderTarget;
	class LegacyGlVertexArray;
	class LegacyGlVertexFormat;
	class LegacyGlRhiCapabilities;
	class LegacyGlDebug;
}

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend (see the OpenGL arm above)
	using Device = RHI::LegacyGL::LegacyGlDevice;
	using Texture = RHI::LegacyGL::LegacyGlTexture;
	using Buffer = RHI::LegacyGL::LegacyGlBuffer;
	using Shader = RHI::LegacyGL::LegacyGlShader;
	using ShaderProgram = RHI::LegacyGL::LegacyGlShaderProgram;
	using ShaderUniforms = RHI::LegacyGL::LegacyGlShaderUniforms;
	using ShaderUniformBlocks = RHI::LegacyGL::LegacyGlShaderUniformBlocks;
	using Uniform = RHI::LegacyGL::LegacyGlUniform;
	using UniformBlock = RHI::LegacyGL::LegacyGlUniformBlock;
	using UniformCache = RHI::LegacyGL::LegacyGlUniformCache;
	using UniformBlockCache = RHI::LegacyGL::LegacyGlUniformBlockCache;
	using Attribute = RHI::LegacyGL::LegacyGlAttribute;
	using Framebuffer = RHI::LegacyGL::LegacyGlFramebuffer;
	using Renderbuffer = RHI::LegacyGL::LegacyGlRenderbuffer;
	using RenderTarget = RHI::LegacyGL::LegacyGlRenderTarget;
	using VertexArray = RHI::LegacyGL::LegacyGlVertexArray;
	using VertexFormat = RHI::LegacyGL::LegacyGlVertexFormat;

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::LegacyGL::LegacyGlRhiCapabilities;

	// Debug output and object labelling
	using Debug = RHI::LegacyGL::LegacyGlDebug;

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

#elif defined(WITH_RHI_GS)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The GS
// backend (PlayStation 2 "Graphics Synthesizer" via PS2SDK) is a fixed-function hardware backend just like
// GX, PVR and GU: a render target is nothing but an address written into `FRAME.FBP`, so off-screen render
// targets are available (needed by the textured-background passes) and `RHI_CAP_FRAMEBUFFERS` is defined; but
// the GS has one texture function and one blend equation per draw and no programmable stage at all, so
// `RHI_CAP_SHADERS` stays undefined and the game runs the direct tier (the same tier as the software, GX, PVR
// and GU backends: scene straight to the display at the logical resolution, CPU lightmap composited by the
// device's lighting hook).
//
// `RHI_CAP_PALETTED_TEXTURES` means an R8 texture of palette indices is resolved through the palette by the
// hardware itself, under every effect. The GS has exactly that in `PSMT8` plus a CLUT selected per draw
// through `TEX0.CBP`, and the lookup belongs to the texture read rather than to any programmable stage, so
// the meaning PVR and GU give this flag carries over verbatim.
//
// `RHI_CAP_STREAMING_TEXTURES` is deliberately NOT defined, and this is the one place the GS differs from the
// other fixed-function consoles. The flag promises that a texture's storage can be written by the CPU in
// place, so content regenerated every frame (the cinematics) can be produced straight into it. The PowerVR
// and the GE both read textures out of ordinary addressable memory, but the Graphics Synthesizer's local
// memory has no host mapping whatsoever - every texel arrives through a GIF transfer - so there is no pointer
// to hand out and `GsTexture::MapStreamingTexels()` always fails. Callers take the copy-through-a-buffer path
// instead.
#define RHI_CAP_FRAMEBUFFERS
#define RHI_CAP_PALETTED_TEXTURES
#define RHI_CAP_BATCHING

namespace nCine::RHI::GS
{
	class GsDevice;
	class GsTexture;
	class GsBuffer;
	class GsShader;
	class GsShaderProgram;
	class GsShaderUniforms;
	class GsShaderUniformBlocks;
	class GsUniform;
	class GsUniformBlock;
	class GsUniformCache;
	class GsUniformBlockCache;
	class GsAttribute;
	class GsFramebuffer;
	class GsRenderbuffer;
	class GsRenderTarget;
	class GsVertexArray;
	class GsVertexFormat;
	class GsRhiCapabilities;
	class GsDebug;
}

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend (see the OpenGL arm above)
	using Device = RHI::GS::GsDevice;
	using Texture = RHI::GS::GsTexture;
	using Buffer = RHI::GS::GsBuffer;
	using Shader = RHI::GS::GsShader;
	using ShaderProgram = RHI::GS::GsShaderProgram;
	using ShaderUniforms = RHI::GS::GsShaderUniforms;
	using ShaderUniformBlocks = RHI::GS::GsShaderUniformBlocks;
	using Uniform = RHI::GS::GsUniform;
	using UniformBlock = RHI::GS::GsUniformBlock;
	using UniformCache = RHI::GS::GsUniformCache;
	using UniformBlockCache = RHI::GS::GsUniformBlockCache;
	using Attribute = RHI::GS::GsAttribute;
	using Framebuffer = RHI::GS::GsFramebuffer;
	using Renderbuffer = RHI::GS::GsRenderbuffer;
	using RenderTarget = RHI::GS::GsRenderTarget;
	using VertexArray = RHI::GS::GsVertexArray;
	using VertexFormat = RHI::GS::GsVertexFormat;

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::GS::GsRhiCapabilities;

	// Debug output and object labelling
	using Debug = RHI::GS::GsDebug;

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

#elif defined(WITH_RHI_RDP)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The RDP
// backend (Nintendo 64 "Reality Display Processor" via libdragon's rdpq) is a fixed-function hardware
// backend just like GX, PVR, GU and GS: a render target is nothing but a surface pointer handed to
// rdpq_attach() - any RDRAM surface will do - so off-screen render targets are available (needed by the
// textured-background passes) and `RHI_CAP_FRAMEBUFFERS` is defined; but the RDP's color combiner and
// blender are configured per draw and there is no programmable stage at all, so `RHI_CAP_SHADERS` stays
// undefined and the game runs the direct tier (the same tier as the software, GX, PVR, GU and GS
// backends: scene straight to the display at the logical resolution, CPU lightmap composited by the
// device's lighting hook).
//
// `RHI_CAP_PALETTED_TEXTURES` means an R8 texture of palette indices is resolved through the palette by
// the hardware itself, under every effect. The RDP has exactly that in CI8 texels resolved through the
// TLUT in the upper half of TMEM (loaded per draw by the device from whatever palette texture the
// material bound), and the lookup belongs to the texture read rather than to any programmable stage, so
// the meaning PVR, GU and GS give this flag carries over verbatim.
//
// `RHI_CAP_STREAMING_TEXTURES` means a texture's storage can be written by the CPU in place, so content
// that is regenerated every frame (the cinematics) can be produced straight into it. The RDP reads
// textures out of ordinary RDRAM, which the CPU addresses directly (only a cache writeback separates the
// two), so this holds on the Nintendo 64 as well - the cinematics' indexed frames decode straight into
// their CI8 store.
#define RHI_CAP_FRAMEBUFFERS
#define RHI_CAP_PALETTED_TEXTURES
#define RHI_CAP_STREAMING_TEXTURES
#define RHI_CAP_BATCHING

namespace nCine::RHI::RDP
{
	class RdpDevice;
	class RdpTexture;
	class RdpBuffer;
	class RdpShader;
	class RdpShaderProgram;
	class RdpShaderUniforms;
	class RdpShaderUniformBlocks;
	class RdpUniform;
	class RdpUniformBlock;
	class RdpUniformCache;
	class RdpUniformBlockCache;
	class RdpAttribute;
	class RdpFramebuffer;
	class RdpRenderbuffer;
	class RdpRenderTarget;
	class RdpVertexArray;
	class RdpVertexFormat;
	class RdpRhiCapabilities;
	class RdpDebug;
}

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend (see the OpenGL arm above)
	using Device = RHI::RDP::RdpDevice;
	using Texture = RHI::RDP::RdpTexture;
	using Buffer = RHI::RDP::RdpBuffer;
	using Shader = RHI::RDP::RdpShader;
	using ShaderProgram = RHI::RDP::RdpShaderProgram;
	using ShaderUniforms = RHI::RDP::RdpShaderUniforms;
	using ShaderUniformBlocks = RHI::RDP::RdpShaderUniformBlocks;
	using Uniform = RHI::RDP::RdpUniform;
	using UniformBlock = RHI::RDP::RdpUniformBlock;
	using UniformCache = RHI::RDP::RdpUniformCache;
	using UniformBlockCache = RHI::RDP::RdpUniformBlockCache;
	using Attribute = RHI::RDP::RdpAttribute;
	using Framebuffer = RHI::RDP::RdpFramebuffer;
	using Renderbuffer = RHI::RDP::RdpRenderbuffer;
	using RenderTarget = RHI::RDP::RdpRenderTarget;
	using VertexArray = RHI::RDP::RdpVertexArray;
	using VertexFormat = RHI::RDP::RdpVertexFormat;

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::RDP::RdpRhiCapabilities;

	// Debug output and object labelling
	using Debug = RHI::RDP::RdpDebug;

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

#elif defined(WITH_RHI_GXM)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The PS Vita
// backend drives the console's own graphics API (sceGxm) directly, in the same spirit as the PVR/GX/GU
// backends of the older consoles - but the Vita's PowerVR SGX is a UNIFIED-SHADER part and sceGxm is a
// shader-only API (there is no texture-combiner stage to configure), so this is a full-pipeline backend like
// OpenGL or Direct3D 11: both capabilities are advertised and the whole bloom / lighting / combine / rescale
// chain runs. What it removes compared to the OpenGL path on the same console is only vitaGL, the
// OpenGL|ES 2.0 translation layer that sits between the engine and sceGxm.
#define RHI_CAP_SHADERS
#define RHI_CAP_FRAMEBUFFERS
#define RHI_CAP_HEAVY_RESCALE_SHADERS
#define RHI_CAP_BATCHING

namespace nCine::RHI::GXM
{
	class GxmDevice;
	class GxmTexture;
	class GxmBufferObject;
	class GxmShader;
	class GxmShaderProgram;
	class GxmShaderUniforms;
	class GxmShaderUniformBlocks;
	class GxmUniform;
	class GxmUniformBlock;
	class GxmUniformCache;
	class GxmUniformBlockCache;
	class GxmAttribute;
	class GxmFramebuffer;
	class GxmRenderbuffer;
	class GxmRenderTarget;
	class GxmVertexArray;
	class GxmVertexFormat;
	class GxmRhiCapabilities;
	class GxmDebug;
}

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend. The render pipeline only refers
	// to these aliases, so that additional backends only have to provide the same set of names with
	// the same surface. This header only forward-declares them — include `Rhi.h` for the definitions.
	using Device = RHI::GXM::GxmDevice;
	using Texture = RHI::GXM::GxmTexture;
	using Buffer = RHI::GXM::GxmBufferObject;
	using Shader = RHI::GXM::GxmShader;
	using ShaderProgram = RHI::GXM::GxmShaderProgram;
	using ShaderUniforms = RHI::GXM::GxmShaderUniforms;
	using ShaderUniformBlocks = RHI::GXM::GxmShaderUniformBlocks;
	using Uniform = RHI::GXM::GxmUniform;
	using UniformBlock = RHI::GXM::GxmUniformBlock;
	using UniformCache = RHI::GXM::GxmUniformCache;
	using UniformBlockCache = RHI::GXM::GxmUniformBlockCache;
	using Attribute = RHI::GXM::GxmAttribute;
	using Framebuffer = RHI::GXM::GxmFramebuffer;
	using Renderbuffer = RHI::GXM::GxmRenderbuffer;
	using RenderTarget = RHI::GXM::GxmRenderTarget;
	using VertexArray = RHI::GXM::GxmVertexArray;
	using VertexFormat = RHI::GXM::GxmVertexFormat;

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::GXM::GxmRhiCapabilities;

	// Debug output and object labelling
	using Debug = RHI::GXM::GxmDebug;

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

#elif defined(WITH_RHI_RSX)

// Rendering capability flags of the selected backend (see the OpenGL arm above for the meaning). The PlayStation
// 3 backend drives the console's RSX through PSL1GHT's librsx/libgcm_sys, in the same spirit as the PVR/GX/GU/GS
// backends of the older consoles - but the RSX is an NV47, a fully PROGRAMMABLE part with real vertex and
// fragment shaders, so this is a full-pipeline backend like OpenGL, Direct3D 11 or GXM: both capabilities are
// advertised and the whole bloom / lighting / combine / rescale chain runs.
//
// The one thing that separates it from GXM, which it otherwise mirrors closely, is where its shaders come from.
// The Vita compiles the emitted Cg on the console through SceShaccCg; the PS3 has no runtime shader compiler at
// all, so the very same Cg is compiled to NV40 microcode offline by cgcomp and embedded per program-variant -
// the arrangement the Vulkan backend uses for its SPIR-V. A program whose Cg exceeded what the vp40/fp40
// profiles can express therefore has no microcode to bind, which is a build-time fact rather than a runtime one
// (see `RsxShaderProgram`). Three of the shipped rescale filters are in that position - CleanEdge wants 92
// temporary registers against the profile's 64, SABR a TEXCOORD index the profile does not reach, and
// Monochrome an indexed texcoord fetch it cannot express - so `RHI_CAP_HEAVY_RESCALE_SHADERS` is NOT defined
// here and the menu leaves those three modes out instead of offering a mode that would bind nothing.
//
// `RHI_CAP_BATCHING` is NOT defined either, for the same class of reason. A batched shader indexes the
// instance array through the vertex program's address register, and the microcode cgcomp emits for that is
// rejected: RPCS3 demands a register type in all three source slots of every instruction and abandons the
// program when one is zero ("Src check failed. Aborting"), returning before the position output is written,
// so a batched draw renders nothing while unbatched ones are correct. Whether real hardware would accept it
// is untested. Re-tested 2026-08-11 after the streaming-buffer race behind the invisible tilemap was fixed,
// in case they shared a cause: they do not - text came back with letters missing and glyphs mangled. Do not
// define this again without new information about the microcode encoding itself.
//
// `RHI_CAP_STREAMING_TEXTURES` holds: RSX textures live in memory the PPE can address directly - either the
// 256 MB of GDDR3 mapped through the GPU aperture or main XDR memory the GPU reads over the bus - so content
// that is regenerated every frame (the cinematics) can be written straight into a texture's storage instead of
// being copied through a staging buffer, exactly as on the PowerVR and the GE.
#define RHI_CAP_SHADERS
#define RHI_CAP_FRAMEBUFFERS
#define RHI_CAP_STREAMING_TEXTURES

namespace nCine::RHI::RSX
{
	class RsxDevice;
	class RsxTexture;
	class RsxBufferObject;
	class RsxShader;
	class RsxShaderProgram;
	class RsxShaderUniforms;
	class RsxShaderUniformBlocks;
	class RsxUniform;
	class RsxUniformBlock;
	class RsxUniformCache;
	class RsxUniformBlockCache;
	class RsxAttribute;
	class RsxFramebuffer;
	class RsxRenderbuffer;
	class RsxRenderTarget;
	class RsxVertexArray;
	class RsxVertexFormat;
	class RsxRhiCapabilities;
	class RsxDebug;
}

namespace nCine::RHI
{
	// Backend-neutral names for the classes of the selected backend (see the OpenGL arm above)
	using Device = RHI::RSX::RsxDevice;
	using Texture = RHI::RSX::RsxTexture;
	using Buffer = RHI::RSX::RsxBufferObject;
	using Shader = RHI::RSX::RsxShader;
	using ShaderProgram = RHI::RSX::RsxShaderProgram;
	using ShaderUniforms = RHI::RSX::RsxShaderUniforms;
	using ShaderUniformBlocks = RHI::RSX::RsxShaderUniformBlocks;
	using Uniform = RHI::RSX::RsxUniform;
	using UniformBlock = RHI::RSX::RsxUniformBlock;
	using UniformCache = RHI::RSX::RsxUniformCache;
	using UniformBlockCache = RHI::RSX::RsxUniformBlockCache;
	using Attribute = RHI::RSX::RsxAttribute;
	using Framebuffer = RHI::RSX::RsxFramebuffer;
	using Renderbuffer = RHI::RSX::RsxRenderbuffer;
	using RenderTarget = RHI::RSX::RsxRenderTarget;
	using VertexArray = RHI::RSX::RsxVertexArray;
	using VertexFormat = RHI::RSX::RsxVertexFormat;

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::RSX::RsxRhiCapabilities;

	// Debug output and object labelling
	using Debug = RHI::RSX::RsxDebug;

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
#define RHI_CAP_HEAVY_RESCALE_SHADERS
#define RHI_CAP_BATCHING

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
	class D3D11RhiCapabilities;
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

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::D3D11::D3D11RhiCapabilities;

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
// chain exactly as it does on OpenGL. SPIR-V has no profile limits to trip over, so every rescale filter
// compiles and `RHI_CAP_HEAVY_RESCALE_SHADERS` holds as well.
#define RHI_CAP_SHADERS
#define RHI_CAP_FRAMEBUFFERS
#define RHI_CAP_HEAVY_RESCALE_SHADERS
#define RHI_CAP_BATCHING

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
	class VulkanRhiCapabilities;
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

	// Runtime capabilities of the selected backend
	using Capabilities = RHI::Vulkan::VulkanRhiCapabilities;

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
