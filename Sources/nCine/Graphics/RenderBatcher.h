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

		void CollectInstances(const SmallVectorImpl<RenderCommand*>& srcQueue, SmallVectorImpl<RenderCommand*>& destQueue);
		void CreateBatches(const SmallVectorImpl<RenderCommand*>& srcQueue, SmallVectorImpl<RenderCommand*>& destQueue);
		void Reset();

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

		RenderCommand* CollectCommands(SmallVectorImpl<RenderCommand*>::const_iterator start, SmallVectorImpl<RenderCommand*>::const_iterator end, SmallVectorImpl<RenderCommand*>::const_iterator& nextStart);

		std::uint8_t* AcquireMemory(std::uint32_t bytes);
		void CreateBuffer(std::uint32_t size);
	};

}
