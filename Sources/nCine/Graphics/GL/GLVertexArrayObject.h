#pragma once

#include "GLHashMap.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Wraps an OpenGL vertex array object
		
		Manages the lifetime of a single OpenGL vertex array object (VAO), which captures the set of
		enabled vertex attribute arrays and their bindings. The currently bound VAO is cached so that
		redundant `glBindVertexArray()` calls are skipped.
	*/
	class GLVertexArrayObject
	{
	public:
		GLVertexArrayObject();
		~GLVertexArrayObject();

		GLVertexArrayObject(const GLVertexArrayObject&) = delete;
		GLVertexArrayObject& operator=(const GLVertexArrayObject&) = delete;

		/** @brief Returns the OpenGL handle of the vertex array object */
		inline GLuint GetGLHandle() const {
			return glHandle_;
		}

		/**
		 * @brief Binds the vertex array object
		 *
		 * @return `true` if a `glBindVertexArray()` call was issued, `false` if it was already bound
		 */
		bool Bind() const;
		/**
		 * @brief Unbinds any vertex array object
		 *
		 * @return `true` if a `glBindVertexArray()` call was issued, `false` if nothing was bound
		 */
		static bool Unbind();

		/** @brief Sets an OpenGL object label for the vertex array object, for debugging */
		void SetObjectLabel(StringView label);

	private:
		static GLuint boundVAO_;

		GLuint glHandle_;
	};
}
