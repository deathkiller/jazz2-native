#pragma once

#if !defined(WITH_OPENGL2)

#include "GLHashMap.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/// Handles OpenGL vertex array object
	class GLVertexArrayObject
	{
	public:
		GLVertexArrayObject();
		~GLVertexArrayObject();

		GLVertexArrayObject(const GLVertexArrayObject&) = delete;
		GLVertexArrayObject& operator=(const GLVertexArrayObject&) = delete;

		inline GLuint GetGLHandle() const {
			return glHandle_;
		}

		bool Bind() const;
		static bool Unbind();

		void SetObjectLabel(StringView label);

	private:
		static GLuint boundVAO_;

		GLuint glHandle_;
	};
}

#endif
