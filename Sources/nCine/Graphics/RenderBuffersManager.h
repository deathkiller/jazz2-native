#pragma once

#include "RHI/Rhi.h"

#include <memory>

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Suballocates vertex, index and uniform data from a pool of OpenGL buffer objects
		
		Maintains one or more mapped (or host-backed) buffer objects per buffer type and hands out aligned
		sub-ranges of them to render commands through @ref AcquireMemory(). Buffers are remapped at the start
		of a frame and flushed to the GPU once all commands have written their data.
	*/
	class RenderBuffersManager
	{
		friend class ScreenViewport;
#if defined(NCINE_PROFILING)
		friend class RenderStatistics;
#endif

	public:
		/** @brief Type of data a buffer holds */
		enum class BufferTypes
		{
			Array = 0,		/**< Vertex buffer object */
			ElementArray,	/**< Index (element array) buffer object */
			Uniform,		/**< Uniform buffer object */

			Count			/**< Number of buffer types */
		};

		/** @brief Configuration of a buffer object for a given type */
		struct BufferSpecifications
		{
			/** @brief Buffer type the specification applies to */
			BufferTypes type;
			/** @brief Buffer binding target */
			BufferTarget target;
			/** @brief Buffer mapping flags, or `None` when a host buffer is used instead of mapping */
			MapFlags mapFlags;
			/** @brief Buffer usage hint */
			BufferUsage usageFlags;
			/** @brief Maximum size of a single buffer object in bytes (one ring section with persistent storage) */
			std::uint32_t maxSize;
			/** @brief Required alignment in bytes for sub-allocations */
			std::uint32_t alignment;
			/** @brief Whether the buffer uses persistently mapped immutable storage with a section ring */
			bool persistent;
		};

		/** @brief Result of a memory request, locating a sub-range within a buffer object */
		using Parameters = RHI::BufferRange;

		RenderBuffersManager(bool useBufferMapping, bool useBufferStorage, std::uint32_t vboMaxSize, std::uint32_t iboMaxSize);
		~RenderBuffersManager();

		/** @brief Returns the specifications for a buffer of the specified type */
		inline const BufferSpecifications& Specs(BufferTypes type) const {
			return specs_[std::int32_t(type)];
		}
		/** @brief Requests an amount of bytes from the specified buffer type */
		inline Parameters AcquireMemory(BufferTypes type, std::uint32_t bytes) {
			return AcquireMemory(type, bytes, specs_[std::int32_t(type)].alignment);
		}
		/** @brief Requests an amount of bytes from the specified buffer type with a custom alignment requirement */
		Parameters AcquireMemory(BufferTypes type, std::uint32_t bytes, std::uint32_t alignment);

	private:
		BufferSpecifications specs_[std::int32_t(BufferTypes::Count)];

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ManagedBuffer
		{
			ManagedBuffer()
				: type(BufferTypes::Array), size(0), freeSpace(0), sectionOffset(0), object(nullptr), mapBase(nullptr), hostBuffer(nullptr) {}

			BufferTypes type;
			std::unique_ptr<RHI::Buffer> object;
			std::uint32_t size;
			std::uint32_t freeSpace;
			// Byte offset of the current ring section, always zero without persistent storage
			std::uint32_t sectionOffset;
			std::uint8_t* mapBase;
			std::unique_ptr<std::uint8_t[]> hostBuffer;
		};
#endif

		// Number of ring sections that persistently mapped buffers are divided into
		static constexpr std::uint32_t NumPersistentSections = 3;

		SmallVector<ManagedBuffer, 0> buffers_;
		// Whether the common buffers use persistently mapped immutable storage with a section ring
		bool usePersistentMapping_;
		// Current section of the persistent buffer ring
		std::uint32_t currentSection_;
		// Fences protecting each ring section from being overwritten while the GPU still reads it
		FenceHandle sectionFences_[NumPersistentSections];

		void FlushUnmap();
		void Remap();
		void CreateBuffer(const BufferSpecifications& specs);
	};

}
