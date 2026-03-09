#pragma once

#include "RHI/RHI.h"
#include "RHI/RenderBufferParams.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/// Handles the memory mapping in multiple GPU Buffer Objects
	class RenderBuffersManager
	{
		friend class ScreenViewport;
#if defined(NCINE_PROFILING)
		friend class RenderStatistics;
#endif

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
			BufferTypes       type;
			RHI::BufferType  bufferType;
			RHI::MapFlags    mapFlags;
			RHI::BufferUsage usageFlags;
			std::uint32_t     maxSize;
			std::uint32_t     alignment;
		};

		/// Alias for backward compatibility — see BufferParams in RenderBufferParams.h
		using Parameters = BufferParams;

		RenderBuffersManager(bool useBufferMapping, std::uint32_t vboMaxSize, std::uint32_t iboMaxSize);

		/// Returns the specifications for a buffer of the specified type
		inline const BufferSpecifications& Specs(BufferTypes type) const {
			return specs_[std::int32_t(type)];
		}
		/// Requests an amount of bytes from the specified buffer type
		inline Parameters AcquireMemory(BufferTypes type, std::uint32_t bytes) {
			return AcquireMemory(type, bytes, specs_[std::int32_t(type)].alignment);
		}
		/// Requests an amount of bytes from the specified buffer type with a custom alignment requirement
		Parameters AcquireMemory(BufferTypes type, std::uint32_t bytes, std::uint32_t alignment);

	private:
		BufferSpecifications specs_[std::int32_t(BufferTypes::Count)];

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ManagedBuffer
		{
			ManagedBuffer()
				: type(BufferTypes::Array), size(0), freeSpace(0), object(nullptr), mapBase(nullptr) {}

			BufferTypes type;
			std::unique_ptr<RHI::Buffer> object;
			std::uint32_t size;
			std::uint32_t freeSpace;
			std::uint8_t* mapBase;
			std::unique_ptr<std::uint8_t[]> hostBuffer;
		};
#endif

		SmallVector<ManagedBuffer, 0> buffers_;

		void FlushUnmap();
		void Remap();
		void CreateBuffer(const BufferSpecifications& specs);
	};

}
