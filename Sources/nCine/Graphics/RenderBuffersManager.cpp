#include "RenderBuffersManager.h"
#include "RenderStatistics.h"
#include "GL/GLDebug.h"
#include "../ServiceLocator.h"
#include "IGfxCapabilities.h"
#include "../../Common.h"
#include "../tracy.h"

namespace nCine
{
	RenderBuffersManager::RenderBuffersManager(bool useBufferMapping, unsigned long vboMaxSize, unsigned long iboMaxSize)
	{
		buffers_.reserve(4);

		BufferSpecifications& vboSpecs = specs_[(int)BufferTypes::Array];
		vboSpecs.type = BufferTypes::Array;
		vboSpecs.target = GL_ARRAY_BUFFER;
		vboSpecs.mapFlags = useBufferMapping ? GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT : 0;
		vboSpecs.usageFlags = GL_STREAM_DRAW;
		vboSpecs.maxSize = vboMaxSize;
		vboSpecs.alignment = sizeof(GLfloat);

		BufferSpecifications& iboSpecs = specs_[(int)BufferTypes::ElementArray];
		iboSpecs.type = BufferTypes::ElementArray;
		iboSpecs.target = GL_ELEMENT_ARRAY_BUFFER;
		iboSpecs.mapFlags = useBufferMapping ? GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT : 0;
		iboSpecs.usageFlags = GL_STREAM_DRAW;
		iboSpecs.maxSize = iboMaxSize;
		iboSpecs.alignment = sizeof(GLushort);

		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
		const int offsetAlignment = gfxCaps.value(IGfxCapabilities::GLIntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
		const int uboMaxSize = gfxCaps.value(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED);

		BufferSpecifications& uboSpecs = specs_[(int)BufferTypes::Uniform];
		uboSpecs.type = BufferTypes::Uniform;
		uboSpecs.target = GL_UNIFORM_BUFFER;
		uboSpecs.mapFlags = useBufferMapping ? GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT : 0;
		uboSpecs.usageFlags = GL_STREAM_DRAW;
		uboSpecs.maxSize = static_cast<unsigned long>(uboMaxSize);
		uboSpecs.alignment = static_cast<unsigned int>(offsetAlignment);

		// Create the first buffer for each type right away
		for (unsigned int i = 0; i < (int)BufferTypes::Count; i++) {
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

	RenderBuffersManager::Parameters RenderBuffersManager::acquireMemory(BufferTypes type, unsigned long bytes, unsigned int alignment)
	{
		FATAL_ASSERT_MSG(bytes <= specs_[(int)type].maxSize, "Trying to acquire %lu bytes when the maximum for buffer type \"%s\" is %lu",
						   bytes, bufferTypeToString(type), specs_[(int)type].maxSize);

		// Accepting a custom alignment only if it is a multiple of the specification one
		if (alignment % specs_[(int)type].alignment != 0) {
			alignment = specs_[(int)type].alignment;
		}

		Parameters params;

		for (ManagedBuffer& buffer : buffers_) {
			if (buffer.type == type) {
				const unsigned long offset = buffer.size - buffer.freeSpace;
				const unsigned int alignAmount = (alignment - offset % alignment) % alignment;

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
			createBuffer(specs_[(int)type]);
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
		GLDebug::ScopedGroup scoped("RenderBuffersManager::flushUnmap()");

		for (ManagedBuffer& buffer : buffers_) {
#if defined(NCINE_PROFILING)
			RenderStatistics::gatherStatistics(buffer);
#endif
			const unsigned long usedSize = buffer.size - buffer.freeSpace;
			FATAL_ASSERT(usedSize <= specs_[(int)buffer.type].maxSize);
			buffer.freeSpace = buffer.size;

			if (specs_[(int)buffer.type].mapFlags == 0) {
				if (usedSize > 0) {
					buffer.object->bufferSubData(0, usedSize, buffer.hostBuffer.get());
				}
			} else {
				if (usedSize > 0) {
					buffer.object->flushMappedBufferRange(0, usedSize);
				}
				buffer.object->unmap();
			}

			buffer.mapBase = nullptr;
		}
	}

	void RenderBuffersManager::remap()
	{
		ZoneScopedC(0x81A861);
		GLDebug::ScopedGroup scoped("RenderBuffersManager::remap()");

		for (ManagedBuffer& buffer : buffers_) {
			ASSERT(buffer.freeSpace == buffer.size);
			ASSERT(buffer.mapBase == nullptr);

			if (specs_[(int)buffer.type].mapFlags == 0) {
				buffer.object->bufferData(buffer.size, nullptr, specs_[(int)buffer.type].usageFlags);
				buffer.mapBase = buffer.hostBuffer.get();
			} else {
				buffer.mapBase = static_cast<GLubyte*>(buffer.object->mapBufferRange(0, buffer.size, specs_[(int)buffer.type].mapFlags));
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
		managedBuffer.object->bufferData(managedBuffer.size, nullptr, specs.usageFlags);
		managedBuffer.freeSpace = managedBuffer.size;

		switch (managedBuffer.type) {
			default:
			case BufferTypes::Array:
				managedBuffer.object->setObjectLabel("Vertex_ManagedBuffer");
				break;
			case BufferTypes::ElementArray:
				managedBuffer.object->setObjectLabel("Index_ManagedBuffer");
				break;
			case BufferTypes::Uniform:
				managedBuffer.object->setObjectLabel("Uniform_ManagedBuffer");
				break;
		}

		if (specs.mapFlags == 0) {
			managedBuffer.hostBuffer = std::make_unique<GLubyte[]>(specs.maxSize);
			managedBuffer.mapBase = managedBuffer.hostBuffer.get();
		} else {
			managedBuffer.mapBase = static_cast<GLubyte*>(managedBuffer.object->mapBufferRange(0, managedBuffer.size, specs.mapFlags));
		}

		FATAL_ASSERT(managedBuffer.mapBase != nullptr);

		// TODO: GLDebug
		//debugString.format("Create %s buffer 0x%lx", bufferTypeToString(specs.type), uintptr_t(buffers_.back().object.get()));
		//GLDebug::messageInsert(debugString.data());
	}
}
