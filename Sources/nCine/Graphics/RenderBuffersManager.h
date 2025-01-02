#pragma once

#include "GL/GLBufferObject.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/// Handles the memory mapping in multiple OpenGL Buffer Objects
	class RenderBuffersManager
	{
	public:
		enum class BufferTypes
		{
			Array = 0,
			ElementArray,
			Uniform,

			Count
		};

		struct BufferSpecifications
		{
			BufferTypes type;
			GLenum target;
			GLenum mapFlags;
			GLenum usageFlags;
			unsigned long maxSize;
			GLuint alignment;
		};

		struct Parameters
		{
			Parameters()
				: object(nullptr), size(0), offset(0), mapBase(nullptr) {}

			GLBufferObject* object;
			unsigned long size;
			unsigned long offset;
			GLubyte* mapBase;
		};

		RenderBuffersManager(bool useBufferMapping, unsigned long vboMaxSize, unsigned long iboMaxSize);

		/// Returns the specifications for a buffer of the specified type
		inline const BufferSpecifications& specs(BufferTypes type) const {
			return specs_[(int)type];
		}
		/// Requests an amount of bytes from the specified buffer type
		inline Parameters acquireMemory(BufferTypes type, unsigned long bytes) {
			return acquireMemory(type, bytes, specs_[(int)type].alignment);
		}
		/// Requests an amount of bytes from the specified buffer type with a custom alignment requirement
		Parameters acquireMemory(BufferTypes type, unsigned long bytes, unsigned int alignment);

	private:
		BufferSpecifications specs_[(int)BufferTypes::Count];

		struct ManagedBuffer
		{
			ManagedBuffer()
				: type(BufferTypes::Array), size(0), freeSpace(0), object(nullptr), mapBase(nullptr), hostBuffer(nullptr) {}

			BufferTypes type;
			std::unique_ptr<GLBufferObject> object;
			unsigned long size;
			unsigned long freeSpace;
			GLubyte* mapBase;
			std::unique_ptr<GLubyte[]> hostBuffer;
		};

		SmallVector<ManagedBuffer, 0> buffers_;

		void flushUnmap();
		void remap();
		void createBuffer(const BufferSpecifications& specs);

		friend class ScreenViewport;
#if defined(NCINE_PROFILING)
		friend class RenderStatistics;
#endif
	};

}
