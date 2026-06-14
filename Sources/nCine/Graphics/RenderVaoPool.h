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

	/**
		@brief Pool of reusable Vertex Array Objects
		
		Caches VAOs keyed by their vertex format so the same configuration can be rebound across
		frames instead of recreated. When the pool is full the least recently used binding is
		recycled.
	*/
	class RenderVaoPool
	{
	public:
		explicit RenderVaoPool(std::uint32_t vaoPoolSize);

		/** @brief Binds a VAO matching the specified vertex format, reusing a pooled one or creating it */
		void BindVao(const GLVertexFormat& vertexFormat);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct VaoBinding
		{
			std::unique_ptr<GLVertexArrayObject> object;
			GLVertexFormat format;
			TimeStamp lastBindTime;
		};
#endif

		SmallVector<VaoBinding, 0> vaoPool_;

		void InsertGLDebugMessage(const VaoBinding& binding);
	};

}
