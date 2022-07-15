#pragma once

#include <memory>
#include <SmallVector.h>

using namespace Death;

namespace nCine
{
	class RenderCommand;

	/// A class that batches render commands together
	class RenderBatcher
	{
	public:
		RenderBatcher();

		void collectInstances(const SmallVectorImpl<RenderCommand*>& srcQueue, SmallVectorImpl<RenderCommand*>& destQueue);
		void createBatches(const SmallVectorImpl<RenderCommand*>& srcQueue, SmallVectorImpl<RenderCommand*>& destQueue);
		void reset();

	private:
		static unsigned int UboMaxSize;

		struct ManagedBuffer
		{
			ManagedBuffer()
				: size(0), freeSpace(0) {}

			unsigned int size;
			unsigned int freeSpace;
			std::unique_ptr<unsigned char[]> buffer;
		};

		/// Memory buffers to collect UBO data before committing it
		/*! \note It is a RAM buffer and cannot be handled by the `RenderBuffersManager` */
		SmallVector<ManagedBuffer, 0> buffers_;

		RenderCommand* collectCommands(SmallVectorImpl<RenderCommand*>::const_iterator start, SmallVectorImpl<RenderCommand*>::const_iterator end, SmallVectorImpl<RenderCommand*>::const_iterator& nextStart);

		unsigned char* acquireMemory(unsigned int bytes);
		void createBuffer(unsigned int size);
	};

}
