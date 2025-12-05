#pragma once

#include "../Base/TimeStamp.h"
#include "GL/GLVertexArrayObject.h"
#include "GL/GLVertexFormat.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class GLVertexArrayObject;

	/// Creates and handles the pool of VAOs
	/*! On OpenGL 3.0+, this class manages a pool of actual Vertex Array Objects to cache
	    vertex format configurations. On OpenGL 2.x (where VAOs don't exist), this class
	    still tracks the last used vertex format to avoid redundant GL state changes. */
	class RenderVaoPool
	{
	public:
		explicit RenderVaoPool(std::uint32_t vaoPoolSize);

		void BindVao(const GLVertexFormat& vertexFormat);

	private:
#if !defined(WITH_OPENGL2)
		struct VaoBinding
		{
			std::unique_ptr<GLVertexArrayObject> object;
			GLVertexFormat format;
			TimeStamp lastBindTime;
		};

		SmallVector<VaoBinding, 0> vaoPool_;
		
		void InsertGLDebugMessage(const VaoBinding& binding);
#else
		// OpenGL 2.x: No VAO support, we just track the last format for optimization
		SmallVector<int, 0> vaoPool_;  // Unused on OpenGL 2, but needed for reserve() call
		GLVertexFormat lastFormat_;
#endif
	};

}
