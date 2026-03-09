#pragma once

#include "RHI/RHI.h"

#if defined(RHI_CAP_VAO)

#include "../Base/TimeStamp.h"

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

		void BindVao(const RHI::VertexFormat& vertexFormat);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct VaoBinding
		{
			std::unique_ptr<RHI::VertexArrayObject> object;
			RHI::VertexFormat format;
			TimeStamp lastBindTime;
		};
#endif

		SmallVector<VaoBinding, 0> vaoPool_;

		void InsertGLDebugMessage(const VaoBinding& binding);
	};

}

#endif