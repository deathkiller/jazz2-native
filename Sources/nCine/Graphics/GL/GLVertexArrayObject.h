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

		inline GLuint glHandle() const {
			return glHandle_;
		}

		bool bind() const;
		static bool unbind();

		void setObjectLabel(const char* label);

	private:
		static GLuint boundVAO_;

		GLuint glHandle_;

		/// Deleted copy constructor
		GLVertexArrayObject(const GLVertexArrayObject&) = delete;
		/// Deleted assignment operator
		GLVertexArrayObject& operator=(const GLVertexArrayObject&) = delete;
	};

}
