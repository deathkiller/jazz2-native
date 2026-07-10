#include "RenderBuffersManager.h"
#include "RenderStatistics.h"
#include "GL/GLDebug.h"
#include "../ServiceLocator.h"
#include "IGfxCapabilities.h"
#include "../../Main.h"
#include "../tracy.h"

using namespace Death;
using namespace Death::Containers::Literals;

// Persistently mapped immutable buffer storage requires desktop OpenGL 4.4 or `GL_ARB_buffer_storage`,
// availability is checked at runtime so the required context version doesn't change
#if !defined(WITH_OPENGLES) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && defined(GL_MAP_PERSISTENT_BIT)
#	define NCINE_HAS_PERSISTENT_MAPPING
#endif

namespace nCine
{
	RenderBuffersManager::RenderBuffersManager(bool useBufferMapping, bool useBufferStorage, std::uint32_t vboMaxSize, std::uint32_t iboMaxSize)
		: usePersistentMapping_(false), currentSection_(0), sectionFences_{}
	{
		buffers_.reserve(4);

		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();

#if defined(NCINE_HAS_PERSISTENT_MAPPING)
		const std::int32_t glMajor = gfxCaps.GetGLVersion(IGfxCapabilities::GLVersion::Major);
		const std::int32_t glMinor = gfxCaps.GetGLVersion(IGfxCapabilities::GLVersion::Minor);
		const bool hasBufferStorage = gfxCaps.HasExtension(IGfxCapabilities::GLExtensions::ARB_BUFFER_STORAGE) ||
			(glMajor > 4 || (glMajor == 4 && glMinor >= 4));
		usePersistentMapping_ = (useBufferStorage && hasBufferStorage);
		if (usePersistentMapping_) {
			LOGI("Persistently mapped buffer storage is enabled for streaming buffers");
		}
#endif

		BufferSpecifications& vboSpecs = specs_[std::int32_t(BufferTypes::Array)];
		vboSpecs.type = BufferTypes::Array;
		vboSpecs.target = GL_ARRAY_BUFFER;
		vboSpecs.mapFlags = useBufferMapping ? GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT : 0;
		vboSpecs.usageFlags = GL_STREAM_DRAW;
		vboSpecs.maxSize = vboMaxSize;
		vboSpecs.alignment = sizeof(GLfloat);
		vboSpecs.persistent = usePersistentMapping_;

		BufferSpecifications& iboSpecs = specs_[std::int32_t(BufferTypes::ElementArray)];
		iboSpecs.type = BufferTypes::ElementArray;
		iboSpecs.target = GL_ELEMENT_ARRAY_BUFFER;
		iboSpecs.mapFlags = useBufferMapping ? GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT : 0;
		iboSpecs.usageFlags = GL_STREAM_DRAW;
		iboSpecs.maxSize = iboMaxSize;
		iboSpecs.alignment = sizeof(GLushort);
		iboSpecs.persistent = usePersistentMapping_;

		const std::int32_t offsetAlignment = gfxCaps.GetValue(IGfxCapabilities::GLIntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
		const std::int32_t uboMaxSize = gfxCaps.GetValue(IGfxCapabilities::GLIntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED);

		BufferSpecifications& uboSpecs = specs_[std::int32_t(BufferTypes::Uniform)];
		uboSpecs.type = BufferTypes::Uniform;
		uboSpecs.target = GL_UNIFORM_BUFFER;
		uboSpecs.mapFlags = useBufferMapping ? GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT : 0;
		uboSpecs.usageFlags = GL_STREAM_DRAW;
		uboSpecs.maxSize = std::uint32_t(uboMaxSize);
		uboSpecs.alignment = std::uint32_t(offsetAlignment);
		uboSpecs.persistent = usePersistentMapping_;

		// Create the first buffer for each type right away
		for (std::uint32_t i = 0; i < std::uint32_t(BufferTypes::Count); i++) {
			CreateBuffer(specs_[i]);
		}
	}

	RenderBuffersManager::~RenderBuffersManager()
	{
#if defined(NCINE_HAS_PERSISTENT_MAPPING)
		for (std::uint32_t i = 0; i < NumPersistentSections; i++) {
			if (sectionFences_[i] != nullptr) {
				glDeleteSync(sectionFences_[i]);
				sectionFences_[i] = nullptr;
			}
		}
#endif
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
				// The alignment is calculated on the absolute offset, so it also holds within a ring section
				const std::uint32_t offset = buffer.sectionOffset + (buffer.size - buffer.freeSpace);
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
			CreateBuffer(specs_[std::int32_t(type)]);
			ManagedBuffer& newBuffer = buffers_.back();
			const std::uint32_t offset = newBuffer.sectionOffset;
			const std::uint32_t alignAmount = (alignment - offset % alignment) % alignment;
			params.object = newBuffer.object.get();
			params.offset = offset + alignAmount;
			params.size = bytes;
			newBuffer.freeSpace -= bytes + alignAmount;
			params.mapBase = newBuffer.mapBase;
		}

		return params;
	}

	void RenderBuffersManager::FlushUnmap()
	{
		ZoneScopedC(0x81A861);
		GLDebug::ScopedGroup scoped("RenderBuffersManager::flushUnmap()"_s);

		for (ManagedBuffer& buffer : buffers_) {
#if defined(NCINE_PROFILING)
			RenderStatistics::GatherStatistics(buffer);
#endif
			const std::uint32_t usedSize = buffer.size - buffer.freeSpace;
			FATAL_ASSERT(usedSize <= specs_[std::int32_t(buffer.type)].maxSize);

			if (specs_[std::int32_t(buffer.type)].persistent) {
				// Coherent persistent mappings need no flush or unmap, the free space
				// is reclaimed when the ring advances to the next section in Remap()
				continue;
			}

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

	void RenderBuffersManager::Remap()
	{
		ZoneScopedC(0x81A861);
		GLDebug::ScopedGroup scoped("RenderBuffersManager::remap()"_s);

#if defined(NCINE_HAS_PERSISTENT_MAPPING)
		if (usePersistentMapping_) {
			// This runs right after the frame's draw calls were submitted, so a fence here protects
			// everything the GPU may still read from the current section. Advancing then waits on the
			// fence inserted `NumPersistentSections - 1` frames ago before its section is reused.
			if (sectionFences_[currentSection_] != nullptr) {
				glDeleteSync(sectionFences_[currentSection_]);
			}
			sectionFences_[currentSection_] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

			currentSection_ = (currentSection_ + 1) % NumPersistentSections;
			if (sectionFences_[currentSection_] != nullptr) {
				const GLenum result = glClientWaitSync(sectionFences_[currentSection_], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
				if (result == GL_TIMEOUT_EXPIRED || result == GL_WAIT_FAILED) {
					LOGW("Wait for persistent buffer section {} failed (0x{:x})", currentSection_, result);
				}
				glDeleteSync(sectionFences_[currentSection_]);
				sectionFences_[currentSection_] = nullptr;
			}
		}
#endif

		for (ManagedBuffer& buffer : buffers_) {
			if (specs_[std::int32_t(buffer.type)].persistent) {
				DEATH_ASSERT(buffer.mapBase != nullptr);
				buffer.sectionOffset = currentSection_ * buffer.size;
				buffer.freeSpace = buffer.size;
				continue;
			}

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

	void RenderBuffersManager::CreateBuffer(const BufferSpecifications& specs)
	{
		ZoneScopedC(0x81A861);
		ManagedBuffer& managedBuffer = buffers_.emplace_back();
		managedBuffer.type = specs.type;
		managedBuffer.size = specs.maxSize;
		managedBuffer.object = std::make_unique<GLBufferObject>(specs.target);
#if defined(NCINE_HAS_PERSISTENT_MAPPING)
		if (specs.persistent) {
			// The immutable storage holds all ring sections and stays mapped for the buffer's whole lifetime
			const GLbitfield storageFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
			const GLsizeiptr totalSize = GLsizeiptr(specs.maxSize) * NumPersistentSections;
			managedBuffer.object->BufferStorage(totalSize, nullptr, storageFlags);
			managedBuffer.mapBase = static_cast<GLubyte*>(managedBuffer.object->MapBufferRange(0, totalSize, storageFlags));
			managedBuffer.sectionOffset = currentSection_ * specs.maxSize;
		} else
#endif
		{
			managedBuffer.object->BufferData(managedBuffer.size, nullptr, specs.usageFlags);
		}
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

		if (!specs.persistent) {
			if (specs.mapFlags == 0) {
				managedBuffer.hostBuffer = std::make_unique<GLubyte[]>(specs.maxSize);
				managedBuffer.mapBase = managedBuffer.hostBuffer.get();
			} else {
				managedBuffer.mapBase = static_cast<GLubyte*>(managedBuffer.object->MapBufferRange(0, managedBuffer.size, specs.mapFlags));
			}
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
