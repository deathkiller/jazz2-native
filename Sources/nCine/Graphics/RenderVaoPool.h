#pragma once

#include "../Base/TimeStamp.h"
#include "RenderAPI/RHI.h"

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

		void BindVao(const Rhi::VertexFormat& vertexFormat);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct VaoBinding
		{
			std::unique_ptr<Rhi::VertexArrayObject> object;
			Rhi::VertexFormat format;
			TimeStamp lastBindTime;
		};
#endif

		SmallVector<VaoBinding, 0> vaoPool_;

		void InsertGLDebugMessage(const VaoBinding& binding);
	};

}
