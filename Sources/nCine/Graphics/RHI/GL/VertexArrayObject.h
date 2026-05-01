#pragma once

#if defined(WITH_RHI_GL)

#include "HashMap.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI
{
	/// Handles OpenGL vertex array object
	class VertexArrayObject
	{
	public:
		VertexArrayObject();
		~VertexArrayObject();

		VertexArrayObject(const VertexArrayObject&) = delete;
		VertexArrayObject& operator=(const VertexArrayObject&) = delete;

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

#endif // defined(WITH_RHI_GL)
