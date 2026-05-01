#if defined(WITH_RHI_GL)

#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"

#include "GfxCapabilities.h"
#include "../../../../Main.h"

#include <cstdio>	// for sscanf()
#include <cstring>	// for checkGLExtension()

#include <Containers/ArrayView.h>

using namespace Death::Containers;

namespace nCine::RHI
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

	std::int32_t GfxCapabilities::GetVersion(IGfxCapabilities::Version version) const
	{
		switch (version) {
			case Version::Major: return glMajorVersion_;
			case Version::Minor: return glMinorVersion_;
			case Version::Release: return glReleaseVersion_;
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
		const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

		if (version != nullptr) {
#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
#	if defined(DEATH_TARGET_MSVC)
			sscanf_s(version, "OpenGL ES %2d.%2d", &glMajorVersion_, &glMinorVersion_);
#	else
			sscanf(version, "OpenGL ES %2d.%2d", &glMajorVersion_, &glMinorVersion_);
#	endif
#else
#	if defined(DEATH_TARGET_MSVC)
			sscanf_s(version, "%2d.%2d.%2d", &glMajorVersion_, &glMinorVersion_, &glReleaseVersion_);
#	else
			sscanf(version, "%2d.%2d.%2d", &glMajorVersion_, &glMinorVersion_, &glReleaseVersion_);
#	endif
#endif
		}

		infoStrings_.vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
		infoStrings_.renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
		infoStrings_.glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
		infoStrings_.glslVersion = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_SIZE]);
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &glIntValues_[(std::int32_t)IntValues::MAX_TEXTURE_IMAGE_UNITS]);
		glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE]);
		glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS]);
		glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_UNIFORM_BLOCKS]);
		glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &glIntValues_[(std::int32_t)IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS]);
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &glIntValues_[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT]);
#if !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && (!defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_1))
		glGetIntegerv(GL_MAX_VERTEX_ATTRIB_STRIDE, &glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_ATTRIB_STRIDE]);
#endif
		glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &glIntValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS]);

		// MAX_UNIFORM_BLOCK_SIZE is sometimes not reported correctly, so try to adjust it here
		glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE];
		if (glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] > 64 * 1024) {
			glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 64 * 1024;
		} else if (glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] <= 0) {
			glIntValues_[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 16 * 1024;
		}

		const char* ExtensionNames[] = {
			"GL_KHR_debug", "GL_ARB_texture_storage", "GL_ARB_get_program_binary",
#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
			"GL_OES_get_program_binary",
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
			"WEBGL_compressed_texture_s3tc", "WEBGL_compressed_texture_atc", "WEBGL_compressed_texture_pvrtc", "WEBGL_compressed_texture_astc"
#else
			"GL_EXT_texture_compression_s3tc", "GL_AMD_compressed_ATC_texture", "GL_IMG_texture_compression_pvrtc", "GL_KHR_texture_compression_astc_ldr"
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
			, "WEBGL_compressed_texture_etc1",
#elif defined(WITH_OPENGLES)
			, "GL_OES_compressed_ETC1_RGB8_texture",
#endif
		};
		static_assert(std::int32_t(arraySize(ExtensionNames)) == (std::int32_t)Extensions::Count, "Extensions count mismatch");

		CheckGLExtensions(ExtensionNames, glExtensions_, (std::int32_t)Extensions::Count);

#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		if (HasExtension(Extensions::OES_GET_PROGRAM_BINARY)) {
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS_OES, &glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS]);
			DEATH_ASSERT(glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS] <= MaxProgramBinaryFormats);
			glGetIntegerv(GL_PROGRAM_BINARY_FORMATS_OES, programBinaryFormats_);
		} else
#endif
		if (HasExtension(Extensions::ARB_GET_PROGRAM_BINARY)) {
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS]);
			DEATH_ASSERT(glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS] <= MaxProgramBinaryFormats);
			glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, programBinaryFormats_);
		}

#if defined(DEATH_TRACE)
		// Log OpenGL device info
		LOGI("--- OpenGL device info ---");
		LOGI("Vendor: {}", infoStrings_.vendor);
		LOGI("Renderer: {}", infoStrings_.renderer);
		LOGI("OpenGL Version: {}", infoStrings_.glVersion);
		LOGI("GLSL Version: {}", infoStrings_.glslVersion);

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
#if !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && (!defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_1))
		LOGI("GL_MAX_VERTEX_ATTRIB_STRIDE: {}", glIntValues_[(std::int32_t)IntValues::MAX_VERTEX_ATTRIB_STRIDE]);
#endif
		LOGI("GL_MAX_COLOR_ATTACHMENTS: {}", glIntValues_[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS]);
		LOGI("GL_NUM_PROGRAM_BINARY_FORMATS: {}", glIntValues_[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS]);
		LOGI("---");
		LOGI("GL_KHR_debug: {}", glExtensions_[(std::int32_t)Extensions::KHR_DEBUG]);
		LOGI("GL_ARB_texture_storage: {}", glExtensions_[(std::int32_t)Extensions::ARB_TEXTURE_STORAGE]);
		LOGI("GL_ARB_get_program_binary: {}", glExtensions_[(std::int32_t)Extensions::ARB_GET_PROGRAM_BINARY]);
#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		LOGI("GL_OES_get_program_binary: {}", glExtensions_[(std::int32_t)Extensions::OES_GET_PROGRAM_BINARY]);
#endif
		LOGI("GL_EXT_texture_compression_s3tc: {}", glExtensions_[(std::int32_t)Extensions::EXT_TEXTURE_COMPRESSION_S3TC]);
#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		LOGI("GL_OES_compressed_ETC1_RGB8_texture: {}", glExtensions_[(std::int32_t)Extensions::OES_COMPRESSED_ETC1_RGB8_TEXTURE]);
#endif
		LOGI("GL_AMD_compressed_ATC_texture: {}", glExtensions_[(std::int32_t)Extensions::AMD_COMPRESSED_ATC_TEXTURE]);
		LOGI("GL_IMG_texture_compression_pvrtc: {}", glExtensions_[(std::int32_t)Extensions::IMG_TEXTURE_COMPRESSION_PVRTC]);
		LOGI("GL_KHR_texture_compression_astc_ldr: {}", glExtensions_[(std::int32_t)Extensions::KHR_TEXTURE_COMPRESSION_ASTC_LDR]);
		LOGI("---");
#endif
	}

	void GfxCapabilities::CheckGLExtensions(const char* extensionNames[], bool results[], std::uint32_t numExtensionsToCheck) const
	{
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
	}
}

#endif