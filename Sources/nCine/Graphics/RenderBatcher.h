#pragma once

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class RenderCommand;

	/// Batches render commands together
	class RenderBatcher
	{
	public:
		RenderBatcher();

		void collectInstances(const SmallVectorImpl<RenderCommand*>& srcQueue, SmallVectorImpl<RenderCommand*>& destQueue);
		void createBatches(const SmallVectorImpl<RenderCommand*>& srcQueue, SmallVectorImpl<RenderCommand*>& destQueue);
		void reset();

	private:
		static std::uint32_t UboMaxSize;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ManagedBuffer
		{
			ManagedBuffer()
				: size(0), freeSpace(0) {}

			std::uint32_t size;
			std::uint32_t freeSpace;
			std::unique_ptr<std::uint8_t[]> buffer;
		};
#endif

		/// Memory buffers to collect UBO data before committing it
		/*! \note It is a RAM buffer and cannot be handled by the `RenderBuffersManager` */
		SmallVector<ManagedBuffer, 0> buffers_;

		RenderCommand* collectCommands(SmallVectorImpl<RenderCommand*>::const_iterator start, SmallVectorImpl<RenderCommand*>::const_iterator end, SmallVectorImpl<RenderCommand*>::const_iterator& nextStart);

		std::uint8_t* acquireMemory(std::uint32_t bytes);
		void createBuffer(std::uint32_t size);
	};

}
