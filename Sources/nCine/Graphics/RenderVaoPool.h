#pragma once

#if !defined(WITH_OPENGL2)

#include "../Base/TimeStamp.h"
#include "GL/GLVertexFormat.h"
#include "GL/GLVertexArrayObject.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/// Creates and handles the pool of VAOs
	class RenderVaoPool
	{
	public:
		explicit RenderVaoPool(std::uint32_t vaoPoolSize);

		void BindVao(const GLVertexFormat& vertexFormat);

	private:
		struct VaoBinding
		{
			std::unique_ptr<GLVertexArrayObject> object;
			GLVertexFormat format;
			TimeStamp lastBindTime;
		};

		SmallVector<VaoBinding, 0> vaoPool_;
		
		void InsertGLDebugMessage(const VaoBinding& binding);
	};

}

#endif