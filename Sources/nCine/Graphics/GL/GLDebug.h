#pragma once

#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"

#if defined(DEATH_TRACE) && defined(DEATH_TRACE_GL_ERRORS)
#	include "../../../Common.h"
#	define GL_LOG_ERRORS()										\
		do {													\
			GLenum __err = glGetError();						\
			if (__err != GL_NO_ERROR) {							\
				LOGW("OpenGL returned error: %i", __err);		\
			}													\
		} while (false)
#else
#	define GL_LOG_ERRORS() do {} while (false)
#endif

namespace nCine
{
	class IGfxCapabilities;

	/// A class to handle OpenGL debug functions
	class GLDebug
	{
	public:
		enum class LabelTypes
		{
#if defined(DEATH_TARGET_APPLE)
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

		class ScopedGroup
		{
		public:
			explicit ScopedGroup(const char* message) {
				pushGroup(message);
			}
			~ScopedGroup() {
				popGroup();
			}
		};

		static void init(const IGfxCapabilities& gfxCaps);
		static inline void reset() {
			debugGroupId_ = 0;
		}

		static inline bool isAvailable() {
			return debugAvailable_;
		}

		static void pushGroup(const char* message);
		static void popGroup();
		static void messageInsert(const char* message);

		static void objectLabel(LabelTypes identifier, GLuint name, const char* label);
		static void objectLabel(LabelTypes identifier, GLuint name, GLsizei length, const char* label);
		static void getObjectLabel(LabelTypes identifier, GLuint name, GLsizei bufSize, GLsizei* length, char* label);

		static inline int maxLabelLength() {
			return maxLabelLength_;
		}

	private:
		static bool debugAvailable_;
		static GLuint debugGroupId_;
		static int maxLabelLength_;

		/// Enables OpenGL debug output and setup a callback function to log messages
		static void enableDebugOutput();
	};

}
