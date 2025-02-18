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
		struct VaoBinding
		{
			std::unique_ptr<GLVertexArrayObject> object;
			GLVertexFormat format;
			TimeStamp lastBindTime;
		};

		SmallVector<VaoBinding, 0> vaoPool_;

		void insertGLDebugMessage(const VaoBinding& binding);
	};

}
