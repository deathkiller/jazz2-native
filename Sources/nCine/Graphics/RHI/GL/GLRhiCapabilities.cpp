#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"

#include "GLRhiCapabilities.h"

#include <cstdio>	// for sscanf()
#include <cstring>	// for CheckGLExtensions()

#include <Containers/ArrayView.h>

using namespace Death::Containers;

namespace nCine::RHI::GL
{
	GLRhiCapabilities::GLRhiCapabilities()
	{
		const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

		if (version != nullptr) {
#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
#	if defined(DEATH_TARGET_MSVC)
			sscanf_s(version, "OpenGL ES %2d.%2d", &_majorVersion, &_minorVersion);
#	else
			sscanf(version, "OpenGL ES %2d.%2d", &_majorVersion, &_minorVersion);
#	endif
#else
#	if defined(DEATH_TARGET_MSVC)
			sscanf_s(version, "%2d.%2d.%2d", &_majorVersion, &_minorVersion, &_releaseVersion);
#	else
			sscanf(version, "%2d.%2d.%2d", &_majorVersion, &_minorVersion, &_releaseVersion);
#	endif
#endif
		}

		_infoStrings.vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
		_infoStrings.renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
		_infoStrings.apiVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
		_infoStrings.shadingLanguageVersion = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &_intValues[(std::int32_t)IntValues::MAX_TEXTURE_SIZE]);
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &_intValues[(std::int32_t)IntValues::MAX_TEXTURE_IMAGE_UNITS]);
#if defined(RHI_GL_PROFILE_ES2)
		// ES2 has no uniform buffer objects, MRT or the related glGet enums (all ES 3.0) - querying them would
		// raise GL_INVALID_ENUM and leave the values at 0 (a zero UNIFORM_BUFFER_OFFSET_ALIGNMENT would even
		// divide-by-zero in RenderBuffersManager). Publish safe synthetic values for the pipeline code that reads
		// them before its ES2 arms skip the UBO work, and log the real ES2 uniform-vector limits instead
		_intValues[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE] = 16 * 1024;
		_intValues[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED] = 16 * 1024;
		_intValues[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS] = 0;
		_intValues[(std::int32_t)IntValues::MAX_VERTEX_UNIFORM_BLOCKS] = 0;
		_intValues[(std::int32_t)IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS] = 0;
		_intValues[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT] = 16;
		_intValues[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS] = 1;

		GLint maxVertexUniformVectors = 0, maxFragmentUniformVectors = 0, maxVaryingVectors = 0;
		glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &maxVertexUniformVectors);
		glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &maxFragmentUniformVectors);
		glGetIntegerv(GL_MAX_VARYING_VECTORS, &maxVaryingVectors);
		LOGI("GL_MAX_VERTEX_UNIFORM_VECTORS: {}, GL_MAX_FRAGMENT_UNIFORM_VECTORS: {}, GL_MAX_VARYING_VECTORS: {}",
			maxVertexUniformVectors, maxFragmentUniformVectors, maxVaryingVectors);
#else
		glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &_intValues[(std::int32_t)IntValues::MAX_UNIFORM_BLOCK_SIZE]);
		glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &_intValues[(std::int32_t)IntValues::MAX_UNIFORM_BUFFER_BINDINGS]);
		glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &_intValues[(std::int32_t)IntValues::MAX_VERTEX_UNIFORM_BLOCKS]);
		glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &_intValues[(std::int32_t)IntValues::MAX_FRAGMENT_UNIFORM_BLOCKS]);
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &_intValues[(std::int32_t)IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT]);
#	if !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && (defined(RHI_GL_PROFILE_CORE) || GL_ES_VERSION_3_1)
		glGetIntegerv(GL_MAX_VERTEX_ATTRIB_STRIDE, &_intValues[(std::int32_t)IntValues::MAX_VERTEX_ATTRIB_STRIDE]);
#	endif
		glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &_intValues[(std::int32_t)IntValues::MAX_COLOR_ATTACHMENTS]);

		// MAX_UNIFORM_BLOCK_SIZE is sometimes not reported correctly, so try to adjust it here
		NormalizeUniformBlockSize();
#endif

		const char* ExtensionNames[] = {
			"GL_KHR_debug", "GL_ARB_texture_storage", "GL_ARB_buffer_storage", "GL_ARB_get_program_binary",
#if defined(RHI_GL_PROFILE_ES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
			"GL_OES_get_program_binary",
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
			"WEBGL_compressed_texture_s3tc", "WEBGL_compressed_texture_atc", "WEBGL_compressed_texture_pvrtc", "WEBGL_compressed_texture_astc"
#else
			"GL_EXT_texture_compression_s3tc", "GL_AMD_compressed_ATC_texture", "GL_IMG_texture_compression_pvrtc", "GL_KHR_texture_compression_astc_ldr"
#endif
#if defined(DEATH_TARGET_EMSCRIPTEN)
			, "WEBGL_compressed_texture_etc1",
#elif defined(RHI_GL_PROFILE_ES)
			, "GL_OES_compressed_ETC1_RGB8_texture",
#endif
		};
		static_assert(std::int32_t(arraySize(ExtensionNames)) == (std::int32_t)Extensions::Count, "Extensions count mismatch");

		CheckGLExtensions(ExtensionNames, _extensions, (std::int32_t)Extensions::Count);

		// PS Vita's vitaGL exposes no program-binary format enums (neither the ARB `GL_PROGRAM_BINARY_FORMATS` nor
		// the OES spelling) and does not support shader binary caching, so this query is skipped there,
		// NUM_PROGRAM_BINARY_FORMATS then stays 0 and the cache stays off.
#if !defined(DEATH_TARGET_VITA)
#	if defined(RHI_GL_PROFILE_ES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		if (HasExtension(Extensions::OES_GET_PROGRAM_BINARY)) {
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS_OES, &_intValues[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS]);
			DEATH_ASSERT(_intValues[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS] <= MaxProgramBinaryFormats);
			glGetIntegerv(GL_PROGRAM_BINARY_FORMATS_OES, _programBinaryFormats);
		} else
#	endif
		if (HasExtension(Extensions::ARB_GET_PROGRAM_BINARY)) {
			glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &_intValues[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS]);
			DEATH_ASSERT(_intValues[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS] <= MaxProgramBinaryFormats);
			glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, _programBinaryFormats);
		}
#endif

		// The device info and the limits themselves are reported the same way by every backend
		LogCapabilities();

#if defined(DEATH_TRACE)
		// Program binary formats and extension availability, both of which only this backend has. A zero
		// format count is worth logging rather than hiding, because it is why BinaryShaderCache stays off.
		LOGI("Program binary formats: {}", _intValues[(std::int32_t)IntValues::NUM_PROGRAM_BINARY_FORMATS]);
		LOGI("GL_KHR_debug: {}", _extensions[(std::int32_t)Extensions::KHR_DEBUG]);
		LOGI("GL_ARB_texture_storage: {}", _extensions[(std::int32_t)Extensions::ARB_TEXTURE_STORAGE]);
		LOGI("GL_ARB_buffer_storage: {}", _extensions[(std::int32_t)Extensions::ARB_BUFFER_STORAGE]);
		LOGI("GL_ARB_get_program_binary: {}", _extensions[(std::int32_t)Extensions::ARB_GET_PROGRAM_BINARY]);
#	if defined(RHI_GL_PROFILE_ES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		LOGI("GL_OES_get_program_binary: {}", _extensions[(std::int32_t)Extensions::OES_GET_PROGRAM_BINARY]);
#	endif
		LOGI("GL_EXT_texture_compression_s3tc: {}", _extensions[(std::int32_t)Extensions::EXT_TEXTURE_COMPRESSION_S3TC]);
#	if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
		LOGI("GL_OES_compressed_ETC1_RGB8_texture: {}", _extensions[(std::int32_t)Extensions::OES_COMPRESSED_ETC1_RGB8_TEXTURE]);
#	endif
		LOGI("GL_AMD_compressed_ATC_texture: {}", _extensions[(std::int32_t)Extensions::AMD_COMPRESSED_ATC_TEXTURE]);
		LOGI("GL_IMG_texture_compression_pvrtc: {}", _extensions[(std::int32_t)Extensions::IMG_TEXTURE_COMPRESSION_PVRTC]);
		LOGI("GL_KHR_texture_compression_astc_ldr: {}", _extensions[(std::int32_t)Extensions::KHR_TEXTURE_COMPRESSION_ASTC_LDR]);
		LOGI("---");

		// Every extension the driver advertises, not just the ones the engine looks for
		/*GLint numExtensions;
		glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);

		LOGI("--- OpenGL extensions ---");
		for (GLuint i = 0; i < static_cast<GLuint>(numExtensions); i++) {
			LOGI("Extension {}: {}", i, glGetStringi(GL_EXTENSIONS, i));
		}
		LOGI("--- OpenGL extensions ---");*/
#endif
	}

	void GLRhiCapabilities::CheckGLExtensions(const char* extensionNames[], bool results[], std::uint32_t numExtensionsToCheck) const
	{
#if defined(RHI_GL_PROFILE_ES2)
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
#else
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
#endif
	}
}
