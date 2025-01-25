#pragma once

#include "GLHashMap.h"

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

		inline GLuint glHandle() const {
			return glHandle_;
		}

		bool bind() const;
		static bool unbind();

		void setObjectLabel(const char* label);

	private:
		static GLuint boundVAO_;

		GLuint glHandle_;
	};
}
