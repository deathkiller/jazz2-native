#pragma once

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
			// Value of the pool bind counter when this VAO was last bound, used for LRU eviction
			// (kept before the large vertex format so the LRU scan stays within the first cache line)
			std::uint64_t lastBindIndex = 0;
			// Fingerprint of the format, so a scan rejects mismatches without a deep comparison
			std::uint64_t fingerprint = 0;
			GLVertexFormat format;
		};
#endif

		SmallVector<VaoBinding, 0> vaoPool_;
		// Monotonically increasing counter, incremented on every BindVao() call
		std::uint64_t bindIndex_ = 0;

		void InsertGLDebugMessage(const VaoBinding& binding);
	};

}
