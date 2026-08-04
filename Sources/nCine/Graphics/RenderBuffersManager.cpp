#include "RenderBuffersManager.h"
#include "RenderStatistics.h"
#include "RHI/Rhi.h"
#include "../ServiceLocator.h"
#include "RHI/IRhiCapabilities.h"
#include "../../Main.h"
#include "../tracy.h"

using namespace Death;
using namespace Death::Containers::Literals;

// Persistently mapped immutable buffer storage requires desktop OpenGL 4.4 or `GL_ARB_buffer_storage`,
// availability is checked at runtime so the required context version doesn't change. Emscripten has to be
// excluded explicitly, its GL headers declare `glBufferStorage()` even though WebGL cannot provide it.
#if !defined(RHI_GL_PROFILE_ES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !(defined(DEATH_TARGET_APPLE) && defined(DEATH_TARGET_ARM)) && defined(GL_MAP_PERSISTENT_BIT)
#	define NCINE_HAS_PERSISTENT_MAPPING
#endif

namespace nCine
{
	RenderBuffersManager::RenderBuffersManager(bool useBufferMapping, bool useBufferStorage, std::uint32_t vboMaxSize, std::uint32_t iboMaxSize)
		: _usePersistentMapping(false), _currentSection(0), _sectionFences{}
	{
		_buffers.reserve(4);

		const RHI::IRhiCapabilities& caps = theServiceLocator().GetRhiCapabilities();

#if defined(NCINE_HAS_PERSISTENT_MAPPING)
		const std::int32_t glMajor = caps.GetApiVersion(RHI::IRhiCapabilities::ApiVersion::Major);
		const std::int32_t glMinor = caps.GetApiVersion(RHI::IRhiCapabilities::ApiVersion::Minor);
		const bool hasBufferStorage = caps.HasExtension(RHI::IRhiCapabilities::Extensions::ARB_BUFFER_STORAGE) ||
			(glMajor > 4 || (glMajor == 4 && glMinor >= 4));
		_usePersistentMapping = (useBufferStorage && hasBufferStorage);
		if (_usePersistentMapping) {
			LOGI("Persistently mapped buffer storage is enabled for streaming buffers");
		}
#endif

		const MapFlags commonMapFlags = (useBufferMapping ? MapFlags::Write | MapFlags::InvalidateBuffer | MapFlags::FlushExplicit : MapFlags::None);

		BufferSpecifications& vboSpecs = _specs[std::int32_t(BufferTypes::Array)];
		vboSpecs.type = BufferTypes::Array;
		vboSpecs.target = BufferTarget::Vertex;
		vboSpecs.mapFlags = commonMapFlags;
		vboSpecs.usageFlags = BufferUsage::StreamDraw;
		vboSpecs.maxSize = vboMaxSize;
		vboSpecs.alignment = sizeof(float);
		vboSpecs.persistent = _usePersistentMapping;

		BufferSpecifications& iboSpecs = _specs[std::int32_t(BufferTypes::ElementArray)];
		iboSpecs.type = BufferTypes::ElementArray;
		iboSpecs.target = BufferTarget::Index;
		iboSpecs.mapFlags = commonMapFlags;
		iboSpecs.usageFlags = BufferUsage::StreamDraw;
		iboSpecs.maxSize = iboMaxSize;
		iboSpecs.alignment = sizeof(std::uint16_t);
		iboSpecs.persistent = _usePersistentMapping;

		const std::int32_t offsetAlignment = caps.GetValue(RHI::IRhiCapabilities::IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
		const std::int32_t uboMaxSize = caps.GetValue(RHI::IRhiCapabilities::IntValues::MAX_UNIFORM_BLOCK_SIZE_NORMALIZED);

		BufferSpecifications& uboSpecs = _specs[std::int32_t(BufferTypes::Uniform)];
		uboSpecs.type = BufferTypes::Uniform;
		uboSpecs.target = BufferTarget::Uniform;
		uboSpecs.mapFlags = commonMapFlags;
		uboSpecs.usageFlags = BufferUsage::StreamDraw;
		uboSpecs.maxSize = std::uint32_t(uboMaxSize);
		uboSpecs.alignment = std::uint32_t(offsetAlignment);
		uboSpecs.persistent = _usePersistentMapping;

		// Create the first buffer for each type right away
		for (std::uint32_t i = 0; i < std::uint32_t(BufferTypes::Count); i++) {
#if defined(RHI_GL_PROFILE_ES2)
			// ES2 has no uniform buffer objects (binding GL_UNIFORM_BUFFER would be an invalid enum) and nothing
			// acquires uniform-type memory on this profile (GLShaderUniformBlocks pushes loose glUniform* instead)
			if (_specs[i].type == BufferTypes::Uniform) {
				continue;
			}
#endif
			CreateBuffer(_specs[i]);
		}
	}

	RenderBuffersManager::~RenderBuffersManager()
	{
#if defined(NCINE_HAS_PERSISTENT_MAPPING)
		for (std::uint32_t i = 0; i < NumPersistentSections; i++) {
			RHI::Device::DeleteFence(_sectionFences[i]);
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
		FATAL_ASSERT_MSG(bytes <= _specs[std::int32_t(type)].maxSize, "Trying to acquire {} bytes when the maximum for buffer type \"{}\" is {}",
						   bytes, bufferTypeToString(type), _specs[std::int32_t(type)].maxSize);

		// Accepting a custom alignment only if it is a multiple of the specification one
		if (alignment % _specs[std::int32_t(type)].alignment != 0) {
			alignment = _specs[std::int32_t(type)].alignment;
		}

		Parameters params;

		for (ManagedBuffer& buffer : _buffers) {
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
			CreateBuffer(_specs[std::int32_t(type)]);
			ManagedBuffer& newBuffer = _buffers.back();
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
		RHI::Debug::ScopedGroup scoped("RenderBuffersManager::flushUnmap()"_s);

		for (ManagedBuffer& buffer : _buffers) {
#if defined(NCINE_PROFILING)
			RenderStatistics::GatherStatistics(buffer);
#endif
			const std::uint32_t usedSize = buffer.size - buffer.freeSpace;
			FATAL_ASSERT(usedSize <= _specs[std::int32_t(buffer.type)].maxSize);

			if (_specs[std::int32_t(buffer.type)].persistent) {
				// Coherent persistent mappings need no flush or unmap, the free space
				// is reclaimed when the ring advances to the next section in Remap()
				continue;
			}

			buffer.freeSpace = buffer.size;

			if (_specs[std::int32_t(buffer.type)].mapFlags == MapFlags::None) {
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
		RHI::Debug::ScopedGroup scoped("RenderBuffersManager::remap()"_s);

#if defined(NCINE_HAS_PERSISTENT_MAPPING)
		if (_usePersistentMapping) {
			// This runs right after the frame's draw calls were submitted, so a fence here protects
			// everything the GPU may still read from the current section. Advancing then waits on the
			// fence inserted `NumPersistentSections - 1` frames ago before its section is reused.
			RHI::Device::DeleteFence(_sectionFences[_currentSection]);
			_sectionFences[_currentSection] = RHI::Device::InsertFence();

			_currentSection = (_currentSection + 1) % NumPersistentSections;
			if (_sectionFences[_currentSection] != nullptr) {
				if (!RHI::Device::ClientWaitFence(_sectionFences[_currentSection], 1000000000)) {
					LOGW("Wait for persistent buffer section {} failed", _currentSection);
				}
				RHI::Device::DeleteFence(_sectionFences[_currentSection]);
			}
		}
#endif

		for (ManagedBuffer& buffer : _buffers) {
			if (_specs[std::int32_t(buffer.type)].persistent) {
				DEATH_ASSERT(buffer.mapBase != nullptr);
				buffer.sectionOffset = _currentSection * buffer.size;
				buffer.freeSpace = buffer.size;
				continue;
			}

			DEATH_ASSERT(buffer.freeSpace == buffer.size);
			DEATH_ASSERT(buffer.mapBase == nullptr);

			if (_specs[std::int32_t(buffer.type)].mapFlags == MapFlags::None) {
				buffer.object->BufferData(buffer.size, nullptr, _specs[std::int32_t(buffer.type)].usageFlags);
				buffer.mapBase = buffer.hostBuffer.get();
			} else {
				buffer.mapBase = static_cast<std::uint8_t*>(buffer.object->MapBufferRange(0, buffer.size, _specs[std::int32_t(buffer.type)].mapFlags));
			}
			FATAL_ASSERT(buffer.mapBase != nullptr);
		}
	}

	void RenderBuffersManager::CreateBuffer(const BufferSpecifications& specs)
	{
		ZoneScopedC(0x81A861);
		ManagedBuffer& managedBuffer = _buffers.emplace_back();
		managedBuffer.type = specs.type;
		managedBuffer.size = specs.maxSize;
		managedBuffer.object = std::make_unique<RHI::Buffer>(specs.target);
#if defined(NCINE_HAS_PERSISTENT_MAPPING)
		if (specs.persistent) {
			// The immutable storage holds all ring sections and stays mapped for the buffer's whole lifetime
			const MapFlags storageFlags = MapFlags::Write | MapFlags::Persistent | MapFlags::Coherent;
			const std::int64_t totalSize = std::int64_t(specs.maxSize) * NumPersistentSections;
			managedBuffer.object->BufferStorage(totalSize, nullptr, storageFlags);
			managedBuffer.mapBase = static_cast<std::uint8_t*>(managedBuffer.object->MapBufferRange(0, totalSize, storageFlags));
			managedBuffer.sectionOffset = _currentSection * specs.maxSize;
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
			if (specs.mapFlags == MapFlags::None) {
				managedBuffer.hostBuffer = std::make_unique<std::uint8_t[]>(specs.maxSize);
				managedBuffer.mapBase = managedBuffer.hostBuffer.get();
			} else {
				managedBuffer.mapBase = static_cast<std::uint8_t*>(managedBuffer.object->MapBufferRange(0, managedBuffer.size, specs.mapFlags));
			}
		}

		FATAL_ASSERT(managedBuffer.mapBase != nullptr);

#if defined(DEATH_DEBUG)
		if (RHI::Debug::IsAvailable()) {
			char debugString[128];
			std::size_t length = formatInto(debugString, "Create {} buffer 0x{:x}", bufferTypeToString(specs.type), std::uintptr_t(_buffers.back().object.get()));
			RHI::Debug::MessageInsert({ debugString, length });
		}
#endif
	}
}
