#if defined(WITH_RHI_GL)
#	define NCINE_INCLUDE_OPENGL
#	include "../CommonHeaders.h"
#endif

#include "GfxCapabilities.h"
#include "../../Main.h"

#if defined(WITH_RHI_SOFTWARE) || defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN)
#	include "RHI/Rhi.h"
#endif

#include <cstdio>	// for sscanf()
#include <cstring>	// for checkGLExtension()

#include <Containers/ArrayView.h>

using namespace Death::Containers;

namespace nCine
{
	GfxCapabilities::GfxCapabilities()
		: glMajorVersion_(0), glMinorVersion_(0), glReleaseVersion_(0)
	{
		for (std::int32_t i = 0; i < (std::int32_t)IntValues::Count; i++) {
			glIntValues_[i] = 0;
		}
		for (std::int32_t i = 0; i < (std::int32_t)Extensions::Count; i++) {
			glExtensions_[i] = false;
		}
		for (std::int32_t i = 0; i < MaxProgramBinaryFormats; i++) {
			programBinaryFormats_[i] = -1;
		}
		Init();
	}

	std::int32_t GfxCapabilities::GetApiVersion(IGfxCapabilities::ApiVersion version) const
	{
		switch (version) {
			case ApiVersion::Major: return glMajorVersion_;
			case ApiVersion::Minor: return glMinorVersion_;
			case ApiVersion::Release: return glReleaseVersion_;
			default: return 0;
		}
	}

	std::int32_t GfxCapabilities::GetValue(IntValues valueName) const
	{
		std::int32_t value = 0;
		if (valueName >= (IntValues)0 && valueName < IntValues::Count) {
			value = glIntValues_[(std::int32_t)valueName];
		}
		return value;
	}

	std::int32_t GfxCapabilities::GetArrayValue(ArrayIntValues valueName, std::uint32_t index) const
	{
		std::int32_t value = 0;
		if (valueName == ArrayIntValues::PROGRAM_BINARY_FORMATS && index < std::uint32_t(glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS])) {
			value = programBinaryFormats_[index];
		}
		return value;
	}

	bool GfxCapabilities::HasExtension(Extensions extensionName) const
	{
		bool extensionAvailable = false;
		if (extensionName >= (Extensions)0 && extensionName < Extensions::Count) {
			extensionAvailable = glExtensions_[(std::int32_t)extensionName];
		}
		return extensionAvailable;
	}

	void GfxCapabilities::Init()
	{
#if defined(WITH_RHI_SOFTWARE) || defined(WITH_RHI_D3D11) || defined(WITH_RHI_VULKAN)
		// These backends have no OpenGL context to query. The published values keep the GL-era names the
		// pipeline reads, but each backend fills them from its own source of truth below (the window backend
		// creates the device before GfxCapabilities is constructed, so the real device limits are already
		// known here). The version/renderer strings describe the pipeline's OpenGL 3.3 compatibility level
		// (the feature set the backend replays), not a live GL context.
		glMajorVersion_ = 3;
		glMinorVersion_ = 3;
		glReleaseVersion_ = 0;
		glInfoStrings_.vendor = "nCine";
		glInfoStrings_.apiVersion = "3.3";
		glInfoStrings_.shadingLanguageVersion = "";
#	if defined(WITH_RHI_D3D11)
		glInfoStrings_.renderer = "Direct3D 11";
		// Real Direct3D 11 limits: the texture dimension comes from the obtained feature level (16384 on 11_0,
		// 8192 on the 10.x fallbacks); a constant buffer holds 4096 16-byte constants = 64 KB
		// (D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT). The backend copies every bound uniform range into a pooled
		// cbuffer per draw (offsets never reach the API), so the offset alignment is only the engine-side
		// suballocation granularity: 16 bytes, one std140 vec4. MAX_COLOR_ATTACHMENTS is
		// D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT (8).
		glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_SIZE] = Rhi::Device::GetMaxTextureDimension();
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE] = 64 * 1024;
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 64 * 1024;
		glIntValues_[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT] = 16;
		glIntValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS] = 8;
#	elif defined(WITH_RHI_VULKAN)
		glInfoStrings_.renderer = "Vulkan";
		// Real limits of the selected VkPhysicalDevice, queried at device creation: maxImageDimension2D,
		// maxUniformBufferRange (normalized into the [16 KB, 64 KB] window exactly like the GL path below) and
		// minUniformBufferOffsetAlignment. The backend re-aligns its own per-frame uniform ring at bind time
		// (see AllocUbo), so the engine-side suballocation does not strictly need the device alignment - it
		// follows it anyway so the published value is the real one. The render passes drive a single color
		// attachment today (MRT unimplemented); 8 mirrors the common device limit and only bounds
		// Viewport::SetTexture().
		glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_SIZE] = Rhi::Device::GetMaxTextureDimension();
		const std::int32_t maxUniformRange = Rhi::Device::GetMaxUniformBufferRange();
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE] = maxUniformRange;
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] =
			(maxUniformRange > 64 * 1024 ? 64 * 1024 : (maxUniformRange <= 0 ? 16 * 1024 : maxUniformRange));
		glIntValues_[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT] = Rhi::Device::GetUniformBufferOffsetAlignment();
		glIntValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS] = 8;
#	else
		glInfoStrings_.renderer = "Software Rasterizer";
		// The CPU rasterizer samples heap-allocated texel stores addressed with 32-bit coordinates, so it has no
		// hard dimension cap of its own; 16384 is published for parity with the hardware backends (it only feeds
		// the texture-load assert). Uniforms are plain host memory read by the transpiled effects (no device
		// alignment), so 16 bytes / 64 KB are the engine-side packing granularity and the engine's normalized
		// block cap. MAX_COLOR_ATTACHMENTS mirrors SwRenderTarget::MaxColorAttachments.
		glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_SIZE] = 16384;
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE] = 64 * 1024;
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 64 * 1024;
		glIntValues_[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT] = 16;
		glIntValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS] = std::int32_t(RhiSoftware::SwRenderTarget::MaxColorAttachments);

		LOGI("Software renderer is enabled");
#	endif
		// Values shared by all three backends: the per-draw texture-unit budget every backend's bind tracking
		// supports (Rhi::Texture::MaxTextureUnits), the pipeline's uniform-binding budget (8, matching the
		// backends' MaxUniformBindings), the 2048-byte vertex stride every target guarantees (the Direct3D 11
		// input-layout limit, the Vulkan spec minimum for maxVertexInputBindingStride, unconstrained on the CPU
		// rasterizer) and no GL program-binary formats (BinaryShaderCache is disabled on these backends).
		glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_IMAGE_UNITS] = std::int32_t(Rhi::Texture::MaxTextureUnits);
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS] = 8;
		glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_UNIFORM_BLOCKS] = 8;
		glIntValues_[(std::int32_t)IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS] = 8;
		glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_ATTRIB_STRIDE] = 2048;
		glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS] = 0;
#else
		const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

		if (version != nullptr) {
#	if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
#		if defined(DEATH_TARGET_MSVC)
			sscanf_s(version, "OpenGL ES %2d.%2d", &glMajorVersion_, &glMinorVersion_);
#		else
			sscanf(version, "OpenGL ES %2d.%2d", &glMajorVersion_, &glMinorVersion_);
#		endif
#	else
#		if defined(DEATH_TARGET_MSVC)
			sscanf_s(version, "%2d.%2d.%2d", &glMajorVersion_, &glMinorVersion_, &glReleaseVersion_);
#		else
			sscanf(version, "%2d.%2d.%2d", &glMajorVersion_, &glMinorVersion_, &glReleaseVersion_);
#		endif
#	endif
		}

		glInfoStrings_.vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
		glInfoStrings_.renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
		glInfoStrings_.apiVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
		glInfoStrings_.shadingLanguageVersion = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_SIZE]);
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_IMAGE_UNITS]);
#	if defined(RHI_GL_PROFILE_ES2)
		// ES2 has no uniform buffer objects, MRT or the related glGet enums (all ES 3.0) - querying them would
		// raise GL_INVALID_ENUM and leave the values at 0 (a zero UNIFORM_BUFFER_OFFSET_ALIGNMENT would even
		// divide-by-zero in RenderBuffersManager). Publish safe synthetic values for the pipeline code that reads
		// them before its ES2 arms skip the UBO work, and log the real ES2 uniform-vector limits instead
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE] = 16 * 1024;
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 16 * 1024;
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS] = 0;
		glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_UNIFORM_BLOCKS] = 0;
		glIntValues_[(std::int32_t)IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS] = 0;
		glIntValues_[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT] = 16;
		glIntValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS] = 1;

		GLint maxVertexUniformVectors = 0, maxFragmentUniformVectors = 0, maxVaryingVectors = 0;
		glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &maxVertexUniformVectors);
		glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &maxFragmentUniformVectors);
		glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryingVectors);
		LOGI("GL_MAX_VERTEX_UNIFORM_VECTORS: {}, GL_MAX_FRAGMENT_UNIFORM_VECTORS: {}, GL_MAX_VARYING_VECTORS: {}",
			maxVertexUniformVectors, maxFragmentUniformVectors, maxVaryingVectors);
#	else
		glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE]);
		glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS]);
		glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_UNIFORM_BLOCKS]);
		glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &glIntValues_[(std::int32_t)IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS]);
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &glIntValues_[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT]);
#		if !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && (!defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_1))
		glGetIntegerv(GL_MAX_VERTEX_ATTRIB_STRIDE, &glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_ATTRIB_STRIDE]);
#		endif
		glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &glIntValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS]);

		// MAX_UNIFORM_BLOCK_SIZE is sometimes not reported correctly, so try to adjust it here
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE];
		if (glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] > 64 * 1024) {
			glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 64 * 1024;
		} else if (glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] <= 0) {
			glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 16 * 1024;
		}
#	endif

		const char* ExtensionNames[] = {
			"GL_KHR_debug", "GL_ARB_texture_storage", "GL_ARB_buffer_storage", "GL_ARB_get_program_binary",
#	if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
			"GL_OES_get_program_binary",
#	endif
#	if defined(DEATH_TARGET_EMSCRIPTEN)
			"WEBGL_compressed_texture_s3tc", "WEBGL_compressed_texture_atc", "WEBGL_compressed_texture_pvrtc", "WEBGL_compressed_texture_astc"
#	else
			"GL_EXT_texture_compression_s3tc", "GL_AMD_compressed_ATC_texture", "GL_IMG_texture_compression_pvrtc", "GL_KHR_texture_compression_astc_ldr"
#	endif
#	if defined(DEATH_TARGET_EMSCRIPTEN)
			, "WEBGL_compressed_texture_etc1",
#	elif defined(WITH_OPENGLES)
			, "GL_OES_compressed_ETC1_RGB8_texture",
#	endif
		};
		static_assert(std::int32_t(arraySize(ExtensionNames)) == (std::int32_t)Extensions::Count, "Extensions count mismatch");

		CheckGLExtensions(ExtensionNames, glExtensions_, (std::int32_t)Extensions::Count);

#	if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		if (HasExtension(Extensions::OES_GET_PROGRAM_BINARY)) {
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS_OES, &glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS]);
			DEATH_ASSERT(glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS] <= MaxProgramBinaryFormats);
			glGetIntegerv(GL_PROGRAM_BINARY_FORMATS_OES, programBinaryFormats_);
		} else
#	endif
		if (HasExtension(Extensions::ARB_GET_PROGRAM_BINARY)) {
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS]);
			DEATH_ASSERT(glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS] <= MaxProgramBinaryFormats);
			glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, programBinaryFormats_);
		}

#	if defined(DEATH_TRACE)
		// Log OpenGL device info
		LOGI("--- OpenGL device info ---");
		LOGI("Vendor: {}", glInfoStrings_.vendor);
		LOGI("Renderer: {}", glInfoStrings_.renderer);
		LOGI("OpenGL Version: {}", glInfoStrings_.apiVersion);
		LOGI("GLSL Version: {}", glInfoStrings_.shadingLanguageVersion);
		//LOGI("--- OpenGL device info ---");

		// Capabilities
		LOGI("--- OpenGL device capabilities ---");
		LOGI("GL_MAX_TEXTURE_SIZE: {}", glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_SIZE]);
		LOGI("GL_MAX_TEXTURE_IMAGE_UNITS: {}", glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_IMAGE_UNITS]);
		if (glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE] != glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED]) {
			LOGI("GL_MAX_UNIFORM_BLOCK_SIZE: {} ({})", glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE], glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED]);
		} else {
			LOGI("GL_MAX_UNIFORM_BLOCK_SIZE: {}", glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE]);
		}
		LOGI("GL_MAX_UNIFORM_BUFFER_BINDINGS: {}", glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS]);
		LOGI("GL_MAX_VERTEX_UNIFORM_BLOCKS: {}", glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_UNIFORM_BLOCKS]);
		LOGI("GL_MAX_FRAGMENT_UNIFORM_BLOCKS: {}", glIntValues_[(std::int32_t)IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS]);
		LOGI("GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: {}", glIntValues_[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT]);
#	if !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && (!defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_1))
		LOGI("GL_MAX_VERTEX_ATTRIB_STRIDE: {}", glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_ATTRIB_STRIDE]);
#	endif
		LOGI("GL_MAX_COLOR_ATTACHMENTS: {}", glIntValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS]);
		LOGI("GL_NUM_PROGRAM_BINARY_FORMATS: {}", glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS]);
		LOGI("---");
		LOGI("GL_KHR_debug: {}", glExtensions_[(std::int32_t)Extensions::KHR_DEBUG]);
		LOGI("GL_ARB_texture_storage: {}", glExtensions_[(std::int32_t)Extensions::ARB_TEXTURE_STORAGE]);
		LOGI("GL_ARB_buffer_storage: {}", glExtensions_[(std::int32_t)Extensions::ARB_BUFFER_STORAGE]);
		LOGI("GL_ARB_get_program_binary: {}", glExtensions_[(std::int32_t)Extensions::ARB_GET_PROGRAM_BINARY]);
#	if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		LOGI("GL_OES_get_program_binary: {}", glExtensions_[(std::int32_t)Extensions::OES_GET_PROGRAM_BINARY]);
#	endif
		LOGI("GL_EXT_texture_compression_s3tc: {}", glExtensions_[(std::int32_t)Extensions::EXT_TEXTURE_COMPRESSION_S3TC]);
#	if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		LOGI("GL_OES_compressed_ETC1_RGB8_texture: {}", glExtensions_[(std::int32_t)Extensions::OES_COMPRESSED_ETC1_RGB8_TEXTURE]);
#	endif
		LOGI("GL_AMD_compressed_ATC_texture: {}", glExtensions_[(std::int32_t)Extensions::AMD_COMPRESSED_ATC_TEXTURE]);
		LOGI("GL_IMG_texture_compression_pvrtc: {}", glExtensions_[(std::int32_t)Extensions::IMG_TEXTURE_COMPRESSION_PVRTC]);
		LOGI("GL_KHR_texture_compression_astc_ldr: {}", glExtensions_[(std::int32_t)Extensions::KHR_TEXTURE_COMPRESSION_ASTC_LDR]);
		//LOGI("--- OpenGL device capabilities ---");
		LOGI("---");

		// Extensions
		/*GLint numExtensions;
		glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);

		LOGI("--- OpenGL extensions ---");
		for (GLuint i = 0; i < static_cast<GLuint>(numExtensions); i++) {
			LOGI("Extension {}: {}", i, glGetStringi(GL_EXTENSIONS, i));
		}
		LOGI("--- OpenGL extensions ---");*/
#	endif
#endif
	}

	void GfxCapabilities::CheckGLExtensions(const char* extensionNames[], bool results[], std::uint32_t numExtensionsToCheck) const
	{
#if !defined(WITH_RHI_GL)
		// Supported only with GL backend
#else
#	if defined(RHI_GL_PROFILE_ES2)
		// ES2 has neither GL_NUM_EXTENSIONS nor glGetStringi() (both ES 3.0) - the extension list is the classic
		// single space-separated glGetString(GL_EXTENSIONS) string, matched here token by token
		const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
		if (extensions == nullptr) {
			return;
		}

		const char* extension = extensions;
		while (*extension != '\0') {
			while (*extension == ' ') {
				extension++;
			}
			std::size_t extLength = 0;
			while (extension[extLength] != '\0' && extension[extLength] != ' ') {
				extLength++;
			}
			if (extLength == 0) {
				break;
			}

			for (std::uint32_t j = 0; j < numExtensionsToCheck; j++) {
				const std::size_t nameLength = std::strlen(extensionNames[j]);
				if (!results[j] && nameLength == extLength && strncmp(extensionNames[j], extension, extLength) == 0) {
					results[j] = true;
				}
			}
			extension += extLength;
		}
#	else
		GLint numExtensions;
		glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
		if (numExtensions <= 0) {
			return;
		}

		for (GLuint i = 0; i < static_cast<GLuint>(numExtensions); i++) {
			const char* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
			const std::size_t extLength = std::strlen(extension);

			for (std::uint32_t j = 0; j < numExtensionsToCheck; j++) {
				const size_t nameLength = std::strlen(extensionNames[j]);
				if (!results[j] && nameLength == extLength && strncmp(extensionNames[j], extension, extLength) == 0) {
					results[j] = true;
				}
			}
		}
#	endif
#endif
	}
}
