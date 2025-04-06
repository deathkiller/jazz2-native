#define NCINE_INCLUDE_OPENGL
#include "../CommonHeaders.h"

#include "GfxCapabilities.h"
#include "../../Main.h"

#include <cstdio>	// for sscanf()
#include <cstring>	// for checkGLExtension()

#include <Containers/ArrayView.h>

using namespace Death::Containers;

namespace nCine
{
	GfxCapabilities::GfxCapabilities()
		: glMajorVersion_(0), glMinorVersion_(0), glReleaseVersion_(0)
	{
		for (std::int32_t i = 0; i < (std::int32_t)GLIntValues::Count; i++) {
			glIntValues_[i] = 0;
		}
		for (std::int32_t i = 0; i < (std::int32_t)GLExtensions::Count; i++) {
			glExtensions_[i] = false;
		}
		for (std::int32_t i = 0; i < MaxProgramBinaryFormats; i++) {
			programBinaryFormats_[i] = -1;
		}
		Initialize();
	}

	std::int32_t GfxCapabilities::GetGLVersion(IGfxCapabilities::GLVersion version) const
	{
		switch (version) {
			case GLVersion::Major: return glMajorVersion_;
			case GLVersion::Minor: return glMinorVersion_;
			case GLVersion::Release: return glReleaseVersion_;
			default: return 0;
		}
	}

	std::int32_t GfxCapabilities::GetValue(GLIntValues valueName) const
	{
		std::int32_t value = 0;
		if (valueName >= (GLIntValues)0 && valueName < GLIntValues::Count) {
			value = glIntValues_[(std::int32_t)valueName];
		}
		return value;
	}

	std::int32_t GfxCapabilities::GetArrayValue(GLArrayIntValues valueName, std::uint32_t index) const
	{
		std::int32_t value = 0;
		if (valueName == GLArrayIntValues::PROGRAM_BINARY_FORMATS && index < std::uint32_t(glIntValues_[(std::int32_t)GLIntValues::NUM_PROGRAM_BINARY_FORMATS])) {
			value = programBinaryFormats_[index];
		}
		return value;
	}

	bool GfxCapabilities::HasExtension(GLExtensions extensionName) const
	{
		bool extensionAvailable = false;
		if (extensionName >= (GLExtensions)0 && extensionName < GLExtensions::Count) {
			extensionAvailable = glExtensions_[(std::int32_t)extensionName];
		}
		return extensionAvailable;
	}

	void GfxCapabilities::Initialize()
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

		glInfoStrings_.vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
		glInfoStrings_.renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
		glInfoStrings_.glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
		glInfoStrings_.glslVersion = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glIntValues_[(std::int32_t)GLIntValues::MAX_TEXTURE_SIZE]);
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &glIntValues_[(std::int32_t)GLIntValues::MAX_TEXTURE_IMAGE_UNITS]);
		glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE]);
		glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BUFFER_BINDINGS]);
		glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &glIntValues_[(std::int32_t)GLIntValues::MAX_VERTEX_UNIFORM_BLOCKS]);
		glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &glIntValues_[(std::int32_t)GLIntValues::MAX_FRAGMENT_UNIFORM_BLOCKS]);
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &glIntValues_[(std::int32_t)GLIntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT]);
#if !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && (!defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_1))
		glGetIntegerv(GL_MAX_VERTEX_ATTRIB_STRIDE, &glIntValues_[(std::int32_t)GLIntValues::MAX_VERTEX_ATTRIB_STRIDE]);
#endif
		glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &glIntValues_[(std::int32_t)GLIntValues::MAX_COLOR_ATTACHMENTS]);

		// MAX_UNIFORM_BLOCK_SIZE is sometimes not reported correctly, so try to adjust it here
		glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE];
		if (glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] > 64 * 1024) {
			glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 64 * 1024;
		} else if (glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] <= 0) {
			glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 16 * 1024;
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
		static_assert(std::int32_t(arraySize(ExtensionNames)) == (std::int32_t)GLExtensions::Count, "GLExtensions count mismatch");

		CheckGLExtensions(ExtensionNames, glExtensions_, (std::int32_t)GLExtensions::Count);

#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		if (HasExtension(GLExtensions::OES_GET_PROGRAM_BINARY)) {
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS_OES, &glIntValues_[(std::int32_t)GLIntValues::NUM_PROGRAM_BINARY_FORMATS]);
			ASSERT(glIntValues_[(std::int32_t)GLIntValues::NUM_PROGRAM_BINARY_FORMATS] <= MaxProgramBinaryFormats);
			glGetIntegerv(GL_PROGRAM_BINARY_FORMATS_OES, programBinaryFormats_);
		} else
#endif
		if (HasExtension(GLExtensions::ARB_GET_PROGRAM_BINARY)) {
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &glIntValues_[(std::int32_t)GLIntValues::NUM_PROGRAM_BINARY_FORMATS]);
			ASSERT(glIntValues_[(std::int32_t)GLIntValues::NUM_PROGRAM_BINARY_FORMATS] <= MaxProgramBinaryFormats);
			glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, programBinaryFormats_);
		}

#if defined(DEATH_TRACE)
		// Log OpenGL device info
		LOGI("--- OpenGL device info ---");
		LOGI("Vendor: %s", glInfoStrings_.vendor);
		LOGI("Renderer: %s", glInfoStrings_.renderer);
		LOGI("OpenGL Version: %s", glInfoStrings_.glVersion);
		LOGI("GLSL Version: %s", glInfoStrings_.glslVersion);
		//LOGI("--- OpenGL device info ---");

		// Capabilities
		LOGI("--- OpenGL device capabilities ---");
		LOGI("GL_MAX_TEXTURE_SIZE: %d", glIntValues_[(std::int32_t)GLIntValues::MAX_TEXTURE_SIZE]);
		LOGI("GL_MAX_TEXTURE_IMAGE_UNITS: %d", glIntValues_[(std::int32_t)GLIntValues::MAX_TEXTURE_IMAGE_UNITS]);
		if (glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE] != glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED]) {
			LOGI("GL_MAX_UNIFORM_BLOCK_SIZE: %d (%d)", glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE], glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED]);
		} else {
			LOGI("GL_MAX_UNIFORM_BLOCK_SIZE: %d", glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BLOCK_SIZE]);
		}
		LOGI("GL_MAX_UNIFORM_BUFFER_BINDINGS: %d", glIntValues_[(std::int32_t)GLIntValues::MAX_UNIFORM_BUFFER_BINDINGS]);
		LOGI("GL_MAX_VERTEX_UNIFORM_BLOCKS: %d", glIntValues_[(std::int32_t)GLIntValues::MAX_VERTEX_UNIFORM_BLOCKS]);
		LOGI("GL_MAX_FRAGMENT_UNIFORM_BLOCKS: %d", glIntValues_[(std::int32_t)GLIntValues::MAX_FRAGMENT_UNIFORM_BLOCKS]);
		LOGI("GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: %d", glIntValues_[(std::int32_t)GLIntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT]);
#if !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && (!defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_1))
		LOGI("GL_MAX_VERTEX_ATTRIB_STRIDE: %d", glIntValues_[(std::int32_t)GLIntValues::MAX_VERTEX_ATTRIB_STRIDE]);
#endif
		LOGI("GL_MAX_COLOR_ATTACHMENTS: %d", glIntValues_[(std::int32_t)GLIntValues::MAX_COLOR_ATTACHMENTS]);
		LOGI("GL_NUM_PROGRAM_BINARY_FORMATS: %d", glIntValues_[(std::int32_t)GLIntValues::NUM_PROGRAM_BINARY_FORMATS]);
		LOGI("---");
		LOGI("GL_KHR_debug: %d", glExtensions_[(std::int32_t)GLExtensions::KHR_DEBUG]);
		LOGI("GL_ARB_texture_storage: %d", glExtensions_[(std::int32_t)GLExtensions::ARB_TEXTURE_STORAGE]);
		LOGI("GL_ARB_get_program_binary: %d", glExtensions_[(std::int32_t)GLExtensions::ARB_GET_PROGRAM_BINARY]);
#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		LOGI("GL_OES_get_program_binary: %d", glExtensions_[(std::int32_t)GLExtensions::OES_GET_PROGRAM_BINARY]);
#endif
		LOGI("GL_EXT_texture_compression_s3tc: %d", glExtensions_[(std::int32_t)GLExtensions::EXT_TEXTURE_COMPRESSION_S3TC]);
#if defined(WITH_OPENGLES) || defined(DEATH_TARGET_EMSCRIPTEN)
		LOGI("GL_OES_compressed_ETC1_RGB8_texture: %d", glExtensions_[(std::int32_t)GLExtensions::OES_COMPRESSED_ETC1_RGB8_TEXTURE]);
#endif
		LOGI("GL_AMD_compressed_ATC_texture: %d", glExtensions_[(std::int32_t)GLExtensions::AMD_COMPRESSED_ATC_TEXTURE]);
		LOGI("GL_IMG_texture_compression_pvrtc: %d", glExtensions_[(std::int32_t)GLExtensions::IMG_TEXTURE_COMPRESSION_PVRTC]);
		LOGI("GL_KHR_texture_compression_astc_ldr: %d", glExtensions_[(std::int32_t)GLExtensions::KHR_TEXTURE_COMPRESSION_ASTC_LDR]);
		//LOGI("--- OpenGL device capabilities ---");
		LOGI("---");

		// Extensions
		/*GLint numExtensions;
		glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);

		LOGI("--- OpenGL extensions ---");
		for (GLuint i = 0; i < static_cast<GLuint>(numExtensions); i++) {
			LOGI("Extension %u: %s", i, glGetStringi(GL_EXTENSIONS, i));
		}
		LOGI("--- OpenGL extensions ---");*/
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
			const std::size_t extLength = strlen(extension);

			for (std::uint32_t j = 0; j < numExtensionsToCheck; j++) {
				const size_t nameLength = strlen(extensionNames[j]);
				if (!results[j] && nameLength == extLength && strncmp(extensionNames[j], extension, extLength) == 0) {
					results[j] = true;
				}
			}
		}
	}
}
