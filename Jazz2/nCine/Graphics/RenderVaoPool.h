#pragma once

#include "../Base/TimeStamp.h"
#include "GL/GLVertexArrayObject.h"
#include "GL/GLVertexFormat.h"

#include <memory>
#include <SmallVector.h>

using namespace Death;

namespace nCine
{
	class GLVertexArrayObject;

	/// The class that creates and handles the pool of VAOs
	class RenderVaoPool
	{
	public:
		explicit RenderVaoPool(unsigned int vaoPoolSize);

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
