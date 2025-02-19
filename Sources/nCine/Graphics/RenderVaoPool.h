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
	class RenderVaoPool
	{
	public:
		explicit RenderVaoPool(std::uint32_t vaoPoolSize);

		void bindVao(const GLVertexFormat& vertexFormat);

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

		void insertGLDebugMessage(const VaoBinding& binding);
	};

}
