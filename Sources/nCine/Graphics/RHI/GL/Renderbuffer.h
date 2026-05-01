#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI
{
	/// Handles OpenGL renderbuffer objects
	class Renderbuffer
	{
		friend class Framebuffer;

	public:
		Renderbuffer(GLenum internalFormat, GLsizei width, GLsizei height);
		~Renderbuffer();

		Renderbuffer(const Renderbuffer&) = delete;
		Renderbuffer& operator=(const Renderbuffer&) = delete;

		inline GLuint GetGLHandle() const {
			return glHandle_;
		}

		inline void SetAttachment(GLenum attachment) {
			attachment_ = attachment;
		}
		inline GLenum GetAttachment() const {
			return attachment_;
		}

		bool Bind() const;
		static bool Unbind();

		void SetObjectLabel(StringView label);

	private:
		static GLuint boundBuffer_;

		GLuint glHandle_;
		GLenum attachment_;

		void Storage(GLenum internalFormat, GLsizei width, GLsizei height);
	};

}

#endif // defined(WITH_RHI_GL)
