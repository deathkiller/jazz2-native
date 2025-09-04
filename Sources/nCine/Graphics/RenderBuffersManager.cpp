#include "RenderBuffersManager.h"
#include "RenderStatistics.h"
#include "GL/GLDebug.h"
#include "../ServiceLocator.h"
#include "IGfxCapabilities.h"
#include "../../Main.h"
#include "../tracy.h"

using namespace Death;
using namespace Death::Containers::Literals;

namespace nCine
{
	RenderBuffersManager::RenderBuffersManager(bool useBufferMapping, std::uint32_t vboMaxSize, std::uint32_t iboMaxSize)
	{
		buffers_.reserve(4);

		BufferSpecifications& vboSpecs = specs_[std::int32_t(BufferTypes::Array)];
		vboSpecs.type = BufferTypes::Array;
		vboSpecs.target = GL_ARRAY_BUFFER;
		vboSpecs.mapFlags = useBufferMapping ? GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT : 0;
		vboSpecs.usageFlags = GL_STREAM_DRAW;
		vboSpecs.maxSize = vboMaxSize;
		vboSpecs.alignment = sizeof(GLfloat);

		BufferSpecifications& iboSpecs = specs_[std::int32_t(BufferTypes::ElementArray)];
		iboSpecs.type = BufferTypes::ElementArray;
		iboSpecs.target = GL_ELEMENT_ARRAY_BUFFER;
		iboSpecs.mapFlags = useBufferMapping ? GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT : 0;
		iboSpecs.usageFlags = GL_STREAM_DRAW;
		iboSpecs.maxSize = iboMaxSize;
		iboSpecs.alignment = sizeof(GLushort);

		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		const std::int32_t offsetAlignment = gfxCaps.GetValue(IGfxCapabilities::GLIntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
		const std::int32_t uboMaxSize = gfxCaps.GetValue(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED);

		BufferSpecifications& uboSpecs = specs_[std::int32_t(BufferTypes::Uniform)];
		uboSpecs.type = BufferTypes::Uniform;
		uboSpecs.target = GL_UNIFORM_BUFFER;
		uboSpecs.mapFlags = useBufferMapping ? GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT : 0;
		uboSpecs.usageFlags = GL_STREAM_DRAW;
		uboSpecs.maxSize = std::uint32_t(uboMaxSize);
		uboSpecs.alignment = std::uint32_t(offsetAlignment);

		// Create the first buffer for each type right away
		for (std::uint32_t i = 0; i < std::uint32_t(BufferTypes::Count); i++) {
			createBuffer(specs_[i]);
		}
	}

	namespace
	{
		const char* bufferTypeToString(RenderBuffersManager::BufferTypes type)
		{
			switch (type) {
				case RenderBuffersManager::BufferTypes::Array: return "Array";
				case RenderBuffersManager::BufferTypes::ElementArray: return "Element Array";
				case RenderBuffersManager::BufferTypes::Uniform: return "Uniform";
				default: return "";
			}
		}
	}

	RenderBuffersManager::Parameters RenderBuffersManager::acquireMemory(BufferTypes type, std::uint32_t bytes, std::uint32_t alignment)
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
					params.object = buffer.object.get();
					params.offset = offset + alignAmount;
					params.size = bytes;
					buffer.freeSpace -= bytes + alignAmount;
					params.mapBase = buffer.mapBase;
					break;
				}
			}
		}

		if (params.object == nullptr) {
			createBuffer(specs_[std::int32_t(type)]);
			params.object = buffers_.back().object.get();
			params.offset = 0;
			params.size = bytes;
			buffers_.back().freeSpace -= bytes;
			params.mapBase = buffers_.back().mapBase;
		}

		return params;
	}

	void RenderBuffersManager::flushUnmap()
	{
		ZoneScopedC(0x81A861);
		GLDebug::ScopedGroup scoped("RenderBuffersManager::flushUnmap()"_s);

		for (ManagedBuffer& buffer : buffers_) {
#if defined(NCINE_PROFILING)
			RenderStatistics::GatherStatistics(buffer);
#endif
			const std::uint32_t usedSize = buffer.size - buffer.freeSpace;
			FATAL_ASSERT(usedSize <= specs_[std::int32_t(buffer.type)].maxSize);
			buffer.freeSpace = buffer.size;

			if (specs_[std::int32_t(buffer.type)].mapFlags == 0) {
				if (usedSize > 0) {
					buffer.object->BufferSubData(0, usedSize, buffer.hostBuffer.get());
				}
			} else {
				if (usedSize > 0) {
					buffer.object->FlushMappedBufferRange(0, usedSize);
				}
				buffer.object->Unmap();
			}

			buffer.mapBase = nullptr;
		}
	}

	void RenderBuffersManager::remap()
	{
		ZoneScopedC(0x81A861);
		GLDebug::ScopedGroup scoped("RenderBuffersManager::remap()"_s);

		for (ManagedBuffer& buffer : buffers_) {
			DEATH_ASSERT(buffer.freeSpace == buffer.size);
			DEATH_ASSERT(buffer.mapBase == nullptr);

			if (specs_[std::int32_t(buffer.type)].mapFlags == 0) {
				buffer.object->BufferData(buffer.size, nullptr, specs_[std::int32_t(buffer.type)].usageFlags);
				buffer.mapBase = buffer.hostBuffer.get();
			} else {
				buffer.mapBase = static_cast<GLubyte*>(buffer.object->MapBufferRange(0, buffer.size, specs_[std::int32_t(buffer.type)].mapFlags));
			}
			FATAL_ASSERT(buffer.mapBase != nullptr);
		}
	}

	void RenderBuffersManager::createBuffer(const BufferSpecifications& specs)
	{
		ZoneScopedC(0x81A861);
		ManagedBuffer& managedBuffer = buffers_.emplace_back();
		managedBuffer.type = specs.type;
		managedBuffer.size = specs.maxSize;
		managedBuffer.object = std::make_unique<GLBufferObject>(specs.target);
		managedBuffer.object->BufferData(managedBuffer.size, nullptr, specs.usageFlags);
		managedBuffer.freeSpace = managedBuffer.size;

		switch (managedBuffer.type) {
			default:
			case BufferTypes::Array:
				managedBuffer.object->SetObjectLabel("Vertex_ManagedBuffer"_s);
				break;
			case BufferTypes::ElementArray:
				managedBuffer.object->SetObjectLabel("Index_ManagedBuffer"_s);
				break;
			case BufferTypes::Uniform:
				managedBuffer.object->SetObjectLabel("Uniform_ManagedBuffer"_s);
				break;
		}

		if (specs.mapFlags == 0) {
			managedBuffer.hostBuffer = std::make_unique<GLubyte[]>(specs.maxSize);
			managedBuffer.mapBase = managedBuffer.hostBuffer.get();
		} else {
			managedBuffer.mapBase = static_cast<GLubyte*>(managedBuffer.object->MapBufferRange(0, managedBuffer.size, specs.mapFlags));
		}

		FATAL_ASSERT(managedBuffer.mapBase != nullptr);

#if defined(DEATH_DEBUG)
		if (GLDebug::IsAvailable()) {
			char debugString[128];
			std::size_t length = formatInto(debugString, "Create {} buffer 0x{:x}", bufferTypeToString(specs.type), std::uintptr_t(buffers_.back().object.get()));
			GLDebug::MessageInsert({ debugString, length });
		}
#endif
	}
}
