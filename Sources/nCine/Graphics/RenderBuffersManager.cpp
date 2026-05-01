#include "RenderBuffersManager.h"
#include "RenderStatistics.h"
#include "../../Main.h"
#include "../tracy.h"

#if defined(WITH_RHI_GL)
#	include "RHI/GL/Debug.h"
#	include "../ServiceLocator.h"
#	include "IGfxCapabilities.h"
using namespace Death::Containers::Literals;
#endif

using namespace Death;

namespace nCine
{
	RenderBuffersManager::RenderBuffersManager(bool useBufferMapping, std::uint32_t vboMaxSize, std::uint32_t iboMaxSize)
	{
		buffers_.reserve(4);

		const RHI::MapFlags streamMapFlags = useBufferMapping
			? (RHI::MapFlags::Write | RHI::MapFlags::InvalidateBuffer | RHI::MapFlags::FlushExplicit)
			: RHI::MapFlags::None;

		BufferSpecifications& vboSpecs = specs_[std::int32_t(BufferTypes::Array)];
		vboSpecs.type       = BufferTypes::Array;
		vboSpecs.bufferType = RHI::BufferType::Vertex;
		vboSpecs.mapFlags   = streamMapFlags;
		vboSpecs.usageFlags = RHI::BufferUsage::Stream;
		vboSpecs.maxSize    = vboMaxSize;
		vboSpecs.alignment  = sizeof(float);

		BufferSpecifications& iboSpecs = specs_[std::int32_t(BufferTypes::ElementArray)];
		iboSpecs.type       = BufferTypes::ElementArray;
		iboSpecs.bufferType = RHI::BufferType::Index;
		iboSpecs.mapFlags   = streamMapFlags;
		iboSpecs.usageFlags = RHI::BufferUsage::Stream;
		iboSpecs.maxSize    = iboMaxSize;
		iboSpecs.alignment  = sizeof(std::uint16_t);

		std::uint32_t offsetAlignment  = 256;  // safe default
		std::uint32_t uboMaxSizeBytes  = 16 * 1024; // 16 KiB default
#if defined(WITH_RHI_GL)
		{
			const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
			offsetAlignment = std::uint32_t(gfxCaps.GetValue(IGfxCapabilities::IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT));
			uboMaxSizeBytes = std::uint32_t(gfxCaps.GetValue(IGfxCapabilities::IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED));
		}
#endif

		BufferSpecifications& uboSpecs = specs_[std::int32_t(BufferTypes::Uniform)];
		uboSpecs.type       = BufferTypes::Uniform;
		uboSpecs.bufferType = RHI::BufferType::Uniform;
		uboSpecs.mapFlags   = streamMapFlags;
		uboSpecs.usageFlags = RHI::BufferUsage::Stream;
		uboSpecs.maxSize    = uboMaxSizeBytes;
		uboSpecs.alignment  = offsetAlignment;

		// Create the first buffer for each type right away
		for (std::uint32_t i = 0; i < std::uint32_t(BufferTypes::Count); i++) {
			CreateBuffer(specs_[i]);
		}
	}

	namespace
	{
		const char* bufferTypeToString(RenderBuffersManager::BufferTypes type)
		{
			switch (type) {
				case RenderBuffersManager::BufferTypes::Array:        return "Array";
				case RenderBuffersManager::BufferTypes::ElementArray: return "Element Array";
				case RenderBuffersManager::BufferTypes::Uniform:      return "Uniform";
				default:                                              return "";
			}
		}
	}

	RenderBuffersManager::Parameters RenderBuffersManager::AcquireMemory(BufferTypes type, std::uint32_t bytes, std::uint32_t alignment)
	{
		FATAL_ASSERT_MSG(bytes <= specs_[std::int32_t(type)].maxSize, "Trying to acquire {} bytes when the maximum for buffer type \"{}\" is {}",
						   bytes, bufferTypeToString(type), specs_[std::int32_t(type)].maxSize);

		// Accepting a custom alignment only if it is a multiple of the specification one
		if (alignment % specs_[std::int32_t(type)].alignment != 0) {
			alignment = specs_[std::int32_t(type)].alignment;
		}

		Parameters params;

		for (ManagedBuffer& buffer : buffers_) {
			if (buffer.type == type) {
				const std::uint32_t offset = buffer.size - buffer.freeSpace;
				const std::uint32_t alignAmount = (alignment - offset % alignment) % alignment;

				if (buffer.freeSpace >= bytes + alignAmount) {
					params.object  = buffer.object.get();
					params.offset  = offset + alignAmount;
					params.size    = bytes;
					buffer.freeSpace -= bytes + alignAmount;
					params.mapBase = buffer.mapBase;
					break;
				}
			}
		}

		if (params.object == nullptr) {
			CreateBuffer(specs_[std::int32_t(type)]);
			params.object  = buffers_.back().object.get();
			params.offset  = 0;
			params.size    = bytes;
			buffers_.back().freeSpace -= bytes;
			params.mapBase = buffers_.back().mapBase;
		}

		return params;
	}

	void RenderBuffersManager::FlushUnmap()
	{
		ZoneScopedC(0x81A861);
#if defined(WITH_RHI_GL)
		RHI::Debug::ScopedGroup scoped("RenderBuffersManager::flushUnmap()"_s);
#endif

		for (ManagedBuffer& buffer : buffers_) {
#if defined(NCINE_PROFILING)
			RenderStatistics::GatherStatistics(buffer);
#endif
			const std::uint32_t usedSize = buffer.size - buffer.freeSpace;
			FATAL_ASSERT(usedSize <= specs_[std::int32_t(buffer.type)].maxSize);
			buffer.freeSpace = buffer.size;

			if (specs_[std::int32_t(buffer.type)].mapFlags == RHI::MapFlags::None) {
				if (usedSize > 0) {
					RHI::BufferSubData(*buffer.object, 0, usedSize, buffer.hostBuffer.get());
				}
			} else {
				if (usedSize > 0) {
					RHI::FlushMappedBufferRange(*buffer.object, 0, usedSize);
				}
				RHI::UnmapBuffer(*buffer.object);
			}

			buffer.mapBase = nullptr;
		}
	}

	void RenderBuffersManager::Remap()
	{
		ZoneScopedC(0x81A861);
#if defined(WITH_RHI_GL)
		RHI::Debug::ScopedGroup scoped("RenderBuffersManager::remap()"_s);
#endif

		for (ManagedBuffer& buffer : buffers_) {
			DEATH_ASSERT(buffer.freeSpace == buffer.size);
			DEATH_ASSERT(buffer.mapBase == nullptr);

			const RHI::MapFlags mapFlags = specs_[std::int32_t(buffer.type)].mapFlags;
			if (mapFlags == RHI::MapFlags::None) {
				RHI::BufferData(*buffer.object, buffer.size, nullptr, specs_[std::int32_t(buffer.type)].usageFlags);
				buffer.mapBase = buffer.hostBuffer.get();
			} else {
				buffer.mapBase = static_cast<std::uint8_t*>(RHI::MapBufferRange(*buffer.object, 0, buffer.size, mapFlags));
			}
			FATAL_ASSERT(buffer.mapBase != nullptr);
		}
	}

	void RenderBuffersManager::CreateBuffer(const BufferSpecifications& specs)
	{
		ZoneScopedC(0x81A861);
		ManagedBuffer& managedBuffer = buffers_.emplace_back();
		managedBuffer.type   = specs.type;
		managedBuffer.size   = specs.maxSize;
		managedBuffer.object = RHI::CreateBuffer(specs.bufferType);
		RHI::BufferData(*managedBuffer.object, managedBuffer.size, nullptr, specs.usageFlags);
		managedBuffer.freeSpace = managedBuffer.size;

#if defined(WITH_RHI_GL)
		switch (managedBuffer.type) {
			default:
			case BufferTypes::Array:
				RHI::SetBufferLabel(*managedBuffer.object, "Vertex_ManagedBuffer"_s);
				break;
			case BufferTypes::ElementArray:
				RHI::SetBufferLabel(*managedBuffer.object, "Index_ManagedBuffer"_s);
				break;
			case BufferTypes::Uniform:
				RHI::SetBufferLabel(*managedBuffer.object, "Uniform_ManagedBuffer"_s);
				break;
		}
#endif

		if (specs.mapFlags == RHI::MapFlags::None) {
			managedBuffer.hostBuffer = std::make_unique<std::uint8_t[]>(specs.maxSize);
			managedBuffer.mapBase    = managedBuffer.hostBuffer.get();
		} else {
			managedBuffer.mapBase = static_cast<std::uint8_t*>(RHI::MapBufferRange(*managedBuffer.object, 0, managedBuffer.size, specs.mapFlags));
		}

		FATAL_ASSERT(managedBuffer.mapBase != nullptr);

#if defined(WITH_RHI_GL) && defined(DEATH_DEBUG)
		if (RHI::Debug::IsAvailable()) {
			char debugString[128];
			std::size_t length = formatInto(debugString, "Create {} buffer 0x{:x}", bufferTypeToString(specs.type), std::uintptr_t(buffers_.back().object.get()));
			RHI::Debug::MessageInsert({ debugString, length });
		}
#endif
	}
}
