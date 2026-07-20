#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../../Main.h"

#include <Containers/StringView.h>

/**
 * @brief Logs any pending OpenGL error
 *
 * Expands to a `glGetError()` check that logs a warning when an error is pending.
 * Compiles to a no-op unless both `DEATH_TRACE` and `DEATH_TRACE_VERBOSE_GL` are defined.
 */
#if defined(DEATH_TRACE) && defined(DEATH_TRACE_VERBOSE_GL)
#	define GL_LOG_ERRORS()										\
		do {													\
			GLenum __err = glGetError();						\
			if (__err != GL_NO_ERROR) {							\
				LOGW("OpenGL returned error: {}", __err);		\
			}													\
		} while (false)
#else
#	define GL_LOG_ERRORS() do {} while (false)
#endif

using namespace Death::Containers;

namespace nCine
{
	class IGfxCapabilities;
}

// Some OpenGL|ES 3.0/3.1 headers (notably aarch64 Mesa) ship a <GLES2/gl2ext.h> that omits the KHR_debug
// object-type enums, and their unsuffixed 3.2 core spellings are only available via <GLES3/gl32.h>. Define the
// _KHR names from their fixed, spec-mandated KHR_debug values so the LabelTypes enum below compiles on such
// headers; the #ifndef guards make this a no-op wherever the GL/ES headers already provide them. (This avoids
// pulling <GLES3/gl32.h> into the header, which would define GL_ES_VERSION_3_2 mid-translation-unit and flip
// GL_ES_VERSION_3_2-gated declarations, e.g. GLBufferObject::TexBuffer.)
#if defined(WITH_OPENGLES) && GL_ES_VERSION_3_0
#	ifndef GL_BUFFER_KHR
#		define GL_BUFFER_KHR 0x82E0
#	endif
#	ifndef GL_SHADER_KHR
#		define GL_SHADER_KHR 0x82E1
#	endif
#	ifndef GL_PROGRAM_KHR
#		define GL_PROGRAM_KHR 0x82E2
#	endif
#	ifndef GL_VERTEX_ARRAY_KHR
#		define GL_VERTEX_ARRAY_KHR 0x8074
#	endif
#	ifndef GL_QUERY_KHR
#		define GL_QUERY_KHR 0x82E3
#	endif
#	ifndef GL_PROGRAM_PIPELINE_KHR
#		define GL_PROGRAM_PIPELINE_KHR 0x82E4
#	endif
#	ifndef GL_SAMPLER_KHR
#		define GL_SAMPLER_KHR 0x82E6
#	endif
#endif

namespace nCine::RHI::GL
{
	/**
		@brief Wrapper around OpenGL debug output and object labelling
		
		All-static helper built on top of the `KHR_debug` functionality. Provides
		debug message groups, message insertion and object labels, all of which
		become no-ops when a debug context is not available.
	*/
	class GLDebug
	{
	public:
		/**
		 * @brief OpenGL object types that can be labelled
		 *
		 * Maps to the OpenGL object identifier enums accepted by `glObjectLabel()`.
		 */
		enum class LabelTypes
		{
			// Apple's OpenGL headers don't expose the KHR_debug object-type enums, and PS Vita's vitaGL has no
			// KHR_debug at all (object labelling is compiled out entirely in GLDebug.cpp - GL_DEBUG_SUPPORTED is
			// never defined there for Vita). On both, these values are never passed to the driver, so use plain
			// sequential enumerators instead of the GL_* object-identifier tokens.
#if defined(DEATH_TARGET_APPLE) || defined(DEATH_TARGET_VITA)
			Buffer,
			Shader,
			Program,
			VertexArray,
			Query,
			ProgramPipeline,
			TransformFeedback,
			Sampler,
			Texture,
			RenderBuffer,
			FrameBuffer
#else
			TransformFeedback = GL_TRANSFORM_FEEDBACK,
			Texture = GL_TEXTURE,
			RenderBuffer = GL_RENDERBUFFER,
			FrameBuffer = GL_FRAMEBUFFER,
#	if ((defined(DEATH_TARGET_ANDROID) && __ANDROID_API__ >= 21) || (!defined(DEATH_TARGET_ANDROID) && defined(WITH_OPENGLES))) && GL_ES_VERSION_3_0
			Buffer = GL_BUFFER_KHR,
			Shader = GL_SHADER_KHR,
			Program = GL_PROGRAM_KHR,
			VertexArray = GL_VERTEX_ARRAY_KHR,
			Query = GL_QUERY_KHR,
			ProgramPipeline = GL_PROGRAM_PIPELINE_KHR,
			Sampler = GL_SAMPLER_KHR
#	else
			Buffer = GL_BUFFER,
			Shader = GL_SHADER,
			Program = GL_PROGRAM,
			VertexArray = GL_VERTEX_ARRAY,
			Query = GL_QUERY,
			ProgramPipeline = GL_PROGRAM_PIPELINE,
			Sampler = GL_SAMPLER
#	endif
#endif
		};

		/**
		 * @brief RAII scope for an OpenGL debug message group
		 *
		 * Pushes a debug group on construction and pops it on destruction.
		 */
		class ScopedGroup
		{
		public:
			/** @brief Pushes a debug group with the given message */
			explicit ScopedGroup(StringView message) {
				PushGroup(message);
			}
			~ScopedGroup() {
				PopGroup();
			}
		};

		/**
		 * @brief Queries debug capabilities and enables debug output if available
		 *
		 * @param gfxCaps	Graphics capabilities used to detect `KHR_debug` support
		 */
		static void Init(const IGfxCapabilities& gfxCaps);
		/** @brief Resets the running debug group id counter */
		static inline void Reset() {
			debugGroupId_ = 0;
		}

		/** @brief Returns whether OpenGL debug output is available */
		static inline bool IsAvailable() {
			return debugAvailable_;
		}

		/** @brief Pushes a named debug group */
		static void PushGroup(StringView message);
		/** @brief Pops the most recently pushed debug group */
		static void PopGroup();
		/** @brief Inserts a one-off debug marker message */
		static void MessageInsert(StringView message);

		/**
		 * @brief Sets a human-readable label on an OpenGL object
		 *
		 * @param identifier	Type of the object being labelled
		 * @param name			Name (id) of the object
		 * @param label			Label to assign
		 */
		static void SetObjectLabel(LabelTypes identifier, GLuint name, StringView label);
		/**
		 * @brief Retrieves the label of an OpenGL object
		 *
		 * @param identifier	Type of the object being queried
		 * @param name			Name (id) of the object
		 * @param bufSize		Size of the destination buffer
		 * @param length		Receives the number of characters written (may be `nullptr`)
		 * @param label			Destination buffer for the label
		 */
		static void GetObjectLabel(LabelTypes identifier, GLuint name, GLsizei bufSize, GLsizei* length, char* label);

		/** @brief Returns the maximum supported object label length */
		static inline std::int32_t GetMaxLabelLength() {
			return maxLabelLength_;
		}

	private:
		static bool debugAvailable_;
		static GLuint debugGroupId_;
		static std::int32_t maxLabelLength_;

		/**
		 * @brief Enables OpenGL debug output and registers the logging callback
		 */
		static void EnableDebugOutput();
	};

}
