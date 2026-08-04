#include "Geometry.h"
#include "RenderResources.h"
#include "RenderStatistics.h"

#include <cstring> // for memcpy()

namespace nCine
{
	Geometry::Geometry()
		: _primitiveType(PrimitiveType::Triangles), _firstVertex(0), _numVertices(0), _numElementsPerVertex(2), _firstIndex(0), _numIndices(0),
			_hostVertexPointer(nullptr), _hostIndexPointer(nullptr), _vboUsageFlags(BufferUsage::StaticDraw), _sharedVboParams(nullptr),
			_iboUsageFlags(BufferUsage::StaticDraw), _sharedIboParams(nullptr), _hasDirtyVertices(true), _hasDirtyIndices(true)
	{
	}

	Geometry::~Geometry()
	{
#if defined(NCINE_PROFILING)
		if (_vbo != nullptr) {
			RenderStatistics::RemoveCustomVbo(_vbo->GetSize());
		}
		if (_ibo != nullptr) {
			RenderStatistics::RemoveCustomIbo(_ibo->GetSize());
		}
#endif
	}

	void Geometry::SetDrawParameters(PrimitiveType primitiveType, std::int32_t firstVertex, std::int32_t numVertices)
	{
		_primitiveType = primitiveType;
		_firstVertex = firstVertex;
		_numVertices = numVertices;
	}

	void Geometry::CreateCustomVbo(std::uint32_t numFloats, BufferUsage usage)
	{
		_vbo = std::make_unique<RHI::Buffer>(BufferTarget::Vertex);
		_vbo->BufferData(numFloats * sizeof(float), nullptr, usage);

		_vboUsageFlags = usage;
		_vboParams.object = _vbo.get();
		_vboParams.size = _vbo->GetSize();
		_vboParams.offset = 0;
		_vboParams.mapBase = nullptr;

#if defined(NCINE_PROFILING)
		RenderStatistics::AddCustomVbo(_vbo->GetSize());
#endif
	}

	float* Geometry::AcquireVertexPointer(std::uint32_t numFloats, std::uint32_t numFloatsAlignment)
	{
		DEATH_ASSERT(_vbo == nullptr);
		_hasDirtyVertices = true;

		if (_sharedVboParams != nullptr) {
			_vboParams = *_sharedVboParams;
		} else {
			const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::Array;
			if (_vboParams.mapBase == nullptr) {
				_vboParams = RenderResources::GetBuffersManager().AcquireMemory(bufferType, numFloats * sizeof(float), numFloatsAlignment * sizeof(float));
			}
		}

		return reinterpret_cast<float*>(_vboParams.mapBase + _vboParams.offset);
	}

	/**
	 * @brief Acquires a pointer for writing vertex data into a VBO owned by the buffers manager
	 *
	 * This method can only be used when mapping of OpenGL buffers is available.
	 */
	float* Geometry::AcquireVertexPointer()
	{
		DEATH_ASSERT(_vbo != nullptr);
		_hasDirtyVertices = true;

		if (_vboParams.mapBase == nullptr) {
			const MapFlags mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::Array).mapFlags;
			FATAL_ASSERT_MSG(mapFlags != MapFlags::None, "Buffer mapping is not available");
			_vboParams.mapBase = static_cast<std::uint8_t*>(_vbo->MapBufferRange(0, _vbo->GetSize(), mapFlags));
		}

		return reinterpret_cast<float*>(_vboParams.mapBase);
	}

	void Geometry::ReleaseVertexPointer()
	{
		// Don't flush and unmap if the VBO is not custom
		if (_vbo != nullptr && _vboParams.mapBase != nullptr) {
			_vboParams.object->FlushMappedBufferRange(_vboParams.offset, _vboParams.size);
			_vboParams.object->Unmap();
		}
		_vboParams.mapBase = nullptr;
	}

	void Geometry::SetHostVertexPointer(const float* vertexPointer)
	{
		_hasDirtyVertices = true;
		_hostVertexPointer = vertexPointer;
	}

	void Geometry::ShareVbo(const Geometry* geometry)
	{
		if (geometry == nullptr) {
			_sharedVboParams = nullptr;
		} else if (geometry != this) {
			_vbo.reset(nullptr);
			_sharedVboParams = &geometry->_vboParams;
		}
	}

	void Geometry::CreateCustomIbo(std::uint32_t numIndices, BufferUsage usage)
	{
		_ibo = std::make_unique<RHI::Buffer>(BufferTarget::Index);
		_ibo->BufferData(numIndices * sizeof(std::uint16_t), nullptr, usage);

		_iboUsageFlags = usage;
		_iboParams.object = _ibo.get();
		_iboParams.size = _ibo->GetSize();
		_iboParams.offset = 0;
		_iboParams.mapBase = nullptr;

#if defined(NCINE_PROFILING)
		RenderStatistics::AddCustomIbo(_ibo->GetSize());
#endif
	}

	std::uint16_t* Geometry::AcquireIndexPointer(std::uint32_t numIndices)
	{
		DEATH_ASSERT(_ibo == nullptr);
		_hasDirtyIndices = true;

		if (_sharedIboParams != nullptr) {
			_iboParams = *_sharedIboParams;
		} else {
			const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::ElementArray;
			if (_iboParams.mapBase == nullptr) {
				_iboParams = RenderResources::GetBuffersManager().AcquireMemory(bufferType, numIndices * sizeof(std::uint16_t));
			}
		}

		return reinterpret_cast<std::uint16_t*>(_iboParams.mapBase + _iboParams.offset);
	}

	/**
	 * @brief Acquires a pointer for writing index data into an IBO owned by the buffers manager
	 *
	 * This method can only be used when mapping of OpenGL buffers is available.
	 */
	std::uint16_t* Geometry::AcquireIndexPointer()
	{
		DEATH_ASSERT(_ibo != nullptr);
		_hasDirtyIndices = true;

		if (_iboParams.mapBase == nullptr) {
			const MapFlags mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::ElementArray).mapFlags;
			FATAL_ASSERT_MSG(mapFlags != MapFlags::None, "Buffer mapping is not available");
			_iboParams.mapBase = static_cast<std::uint8_t*>(_ibo->MapBufferRange(0, _ibo->GetSize(), mapFlags));
		}

		return reinterpret_cast<std::uint16_t*>(_iboParams.mapBase);
	}

	void Geometry::ReleaseIndexPointer()
	{
		// Don't flush and unmap if the IBO is not custom
		if (_ibo != nullptr && _iboParams.mapBase != nullptr) {
			_iboParams.object->FlushMappedBufferRange(_iboParams.offset, _iboParams.size);
			_iboParams.object->Unmap();
		}
		_iboParams.mapBase = nullptr;
	}

	void Geometry::SetHostIndexPointer(const std::uint16_t* indexPointer)
	{
		_hasDirtyIndices = true;
		_hostIndexPointer = indexPointer;
	}

	void Geometry::ShareIbo(const Geometry* geometry)
	{
		if (geometry == nullptr) {
			_sharedIboParams = nullptr;
		} else if (geometry != this) {
			_ibo.reset(nullptr);
			_sharedIboParams = &geometry->_iboParams;
		}
	}

	void Geometry::Bind()
	{
		if (_vboParams.object != nullptr) {
			_vboParams.object->Bind();
		}
	}

	void Geometry::Draw(std::int32_t numInstances)
	{
		const std::int32_t baseVertex = std::int32_t(GetVboParams().offset / _numElementsPerVertex / sizeof(float)) + _firstVertex;

		std::uintptr_t indexOffset = 0;
		if (_numIndices > 0) {
			indexOffset = GetIboParams().offset + _firstIndex * sizeof(std::uint16_t);
		}

		if (numInstances == 0) {
			if (_numIndices > 0) {
				RHI::Device::DrawElements(_primitiveType, _numIndices, indexOffset, baseVertex);
			} else {
				RHI::Device::DrawArrays(_primitiveType, baseVertex, _numVertices);
			}
		} else if (numInstances > 0) {
			if (_numIndices > 0) {
				RHI::Device::DrawElementsInstanced(_primitiveType, _numIndices, indexOffset, numInstances, baseVertex);
			} else {
				RHI::Device::DrawArraysInstanced(_primitiveType, baseVertex, _numVertices, numInstances);
			}
		}
	}

	void Geometry::CommitVertices()
	{
		if (_hostVertexPointer != nullptr && _hasDirtyVertices) {
			// Checking if the common VBO is allowed to use mapping and do the same for the custom one
			const MapFlags mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::Array).mapFlags;
			const std::uint32_t numFloats = _numVertices * _numElementsPerVertex;

			if (mapFlags == MapFlags::None && _vbo != nullptr) {
				// Using buffer orphaning + a subdata upload when having a custom VBO with no mapping available
				_vbo->BufferData(_vboParams.size, nullptr, _vboUsageFlags);
				_vbo->BufferSubData(_vboParams.offset, _vboParams.size, _hostVertexPointer);
			} else {
				float* vertices = _vbo ? AcquireVertexPointer() : AcquireVertexPointer(numFloats, _numElementsPerVertex);
				memcpy(vertices, _hostVertexPointer, numFloats * sizeof(float));
				ReleaseVertexPointer();
			}

			// The dirty flag is only useful with a custom VBO. If the render command uses the common one, it must always copy vertices.
			if (_vbo != nullptr) {
				_hasDirtyVertices = false;
			}
		}
	}

	void Geometry::CommitIndices()
	{
		if (_hostIndexPointer != nullptr && _hasDirtyIndices) {
			// Checking if the common IBO is allowed to use mapping and do the same for the custom one
			const MapFlags mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::ElementArray).mapFlags;

			if (mapFlags == MapFlags::None && _ibo != nullptr) {
				// Using buffer orphaning + a subdata upload when having a custom IBO with no mapping available
				_ibo->BufferData(_iboParams.size, nullptr, _iboUsageFlags);
				_ibo->BufferSubData(_iboParams.offset, _iboParams.size, _hostIndexPointer);
			} else {
				std::uint16_t* indices = _ibo ? AcquireIndexPointer() : AcquireIndexPointer(_numIndices);
				memcpy(indices, _hostIndexPointer, _numIndices * sizeof(std::uint16_t));
				ReleaseIndexPointer();
			}

			// The dirty flag is only useful with a custom IBO. If the render command uses the common one, it must always copy indices.
			if (_ibo != nullptr) {
				_hasDirtyIndices = false;
			}
		}
	}
}
