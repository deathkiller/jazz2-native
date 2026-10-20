#pragma once

#include "RHI/Rhi.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
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
		void BindVao(const RHI::VertexFormat& vertexFormat);
		/**
		 * @brief Binds a VAO without any attributes, for a draw that reads nothing from a buffer
		 *
		 * The same as @ref BindVao() with an empty vertex format, but without searching the pool, a VAO of its own
		 * is kept for it, so drawing many such commands in a row costs a single comparison each.
		 */
		void BindEmptyVao();

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct VaoBinding
		{
			std::unique_ptr<RHI::VertexArray> object;
			// Value of the pool bind counter when this VAO was last bound, used for LRU eviction
			// (kept before the large vertex format so the LRU scan stays within the first cache line)
			std::uint64_t lastBindIndex = 0;
			// Fingerprint of the format, so a scan rejects mismatches without a deep comparison
			std::uint64_t fingerprint = 0;
			RHI::VertexFormat format;
		};
#endif

		SmallVector<VaoBinding, 0> _vaoPool;
		// Monotonically increasing counter, incremented on every BindVao() call
		std::uint64_t _bindIndex = 0;
		// Not a part of the pool, so it's never recycled, see BindEmptyVao(). No attribute is ever enabled in it.
		std::unique_ptr<RHI::VertexArray> _emptyVao;

		void InsertGLDebugMessage(const VaoBinding& binding);
	};

}
