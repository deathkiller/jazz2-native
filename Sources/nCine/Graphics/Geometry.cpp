#include "Geometry.h"
#include "RenderResources.h"
#include "RenderStatistics.h"

#include <cstring> // for memcpy()

namespace nCine
{
	Geometry::Geometry()
		: primitiveType_(Rhi::PrimitiveType::Triangles), firstVertex_(0), numVertices_(0), numElementsPerVertex_(2), firstIndex_(0), numIndices_(0),
			hostVertexPointer_(nullptr), hostIndexPointer_(nullptr), vboUsageFlags_(Rhi::BufferUsage::Dynamic), sharedVboParams_(nullptr),
			iboUsageFlags_(Rhi::BufferUsage::Dynamic), sharedIboParams_(nullptr), hasDirtyVertices_(true), hasDirtyIndices_(true)
	{
	}

	Geometry::~Geometry()
	{
#if defined(NCINE_PROFILING)
		if (vbo_ != nullptr) {
			RenderStatistics::RemoveCustomVbo(vbo_->GetSize());
		}
		if (ibo_ != nullptr) {
			RenderStatistics::RemoveCustomIbo(ibo_->GetSize());
		}
#endif
	}

	void Geometry::SetDrawParameters(Rhi::PrimitiveType primitiveType, std::int32_t firstVertex, std::int32_t numVertices)
	{
		primitiveType_ = primitiveType;
		firstVertex_   = firstVertex;
		numVertices_   = numVertices;
	}

	void Geometry::CreateCustomVbo(std::uint32_t numFloats, Rhi::BufferUsage usage)
	{
		vbo_ = Rhi::CreateBuffer(Rhi::BufferType::Vertex);
		Rhi::BufferData(*vbo_, numFloats * sizeof(float), nullptr, usage);

		vboUsageFlags_     = usage;
		vboParams_.object  = vbo_.get();
		vboParams_.size    = std::uint32_t(vbo_->GetSize());
		vboParams_.offset  = 0;
		vboParams_.mapBase = nullptr;

#if defined(NCINE_PROFILING)
		RenderStatistics::AddCustomVbo(vbo_->GetSize());
#endif
	}

	float* Geometry::AcquireVertexPointer(std::uint32_t numFloats, std::uint32_t numFloatsAlignment)
	{
		DEATH_ASSERT(vbo_ == nullptr);
		hasDirtyVertices_ = true;

		if (sharedVboParams_ != nullptr) {
			vboParams_ = *sharedVboParams_;
		} else {
			const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::Array;
			if (vboParams_.mapBase == nullptr) {
				vboParams_ = RenderResources::GetBuffersManager().AcquireMemory(bufferType, numFloats * sizeof(float), numFloatsAlignment * sizeof(float));
			}
		}

		return reinterpret_cast<float*>(vboParams_.mapBase + vboParams_.offset);
	}

	/// This method can only be used when mapping of GPU buffers is available
	float* Geometry::AcquireVertexPointer()
	{
		DEATH_ASSERT(vbo_ != nullptr);
		hasDirtyVertices_ = true;

		if (vboParams_.mapBase == nullptr) {
			const Rhi::MapFlags mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::Array).mapFlags;
			FATAL_ASSERT_MSG(mapFlags != Rhi::MapFlags::None, "Mapping of GPU buffers is not available");
			vboParams_.mapBase = static_cast<std::uint8_t*>(Rhi::MapBufferRange(*vbo_, 0, vbo_->GetSize(), mapFlags));
		}

		return reinterpret_cast<float*>(vboParams_.mapBase);
	}

	void Geometry::ReleaseVertexPointer()
	{
		// Don't flush and unmap if the VBO is not custom
		if (vbo_ != nullptr && vboParams_.mapBase != nullptr) {
			Rhi::FlushMappedBufferRange(*vboParams_.object, vboParams_.offset, vboParams_.size);
			Rhi::UnmapBuffer(*vboParams_.object);
		}
		vboParams_.mapBase = nullptr;
	}

	void Geometry::SetHostVertexPointer(const float* vertexPointer)
	{
		hasDirtyVertices_ = true;
		hostVertexPointer_ = vertexPointer;
	}

	void Geometry::ShareVbo(const Geometry* geometry)
	{
		if (geometry == nullptr) {
			sharedVboParams_ = nullptr;
		} else if (geometry != this) {
			vbo_.reset(nullptr);
			sharedVboParams_ = &geometry->vboParams_;
		}
	}

	void Geometry::CreateCustomIbo(std::uint32_t numIndices, Rhi::BufferUsage usage)
	{
		ibo_ = Rhi::CreateBuffer(Rhi::BufferType::Index);
		Rhi::BufferData(*ibo_, numIndices * sizeof(std::uint16_t), nullptr, usage);

		iboUsageFlags_     = usage;
		iboParams_.object  = ibo_.get();
		iboParams_.size    = std::uint32_t(ibo_->GetSize());
		iboParams_.offset  = 0;
		iboParams_.mapBase = nullptr;

#if defined(NCINE_PROFILING)
		RenderStatistics::AddCustomIbo(ibo_->GetSize());
#endif
	}

	std::uint16_t* Geometry::AcquireIndexPointer(std::uint32_t numIndices)
	{
		DEATH_ASSERT(ibo_ == nullptr);
		hasDirtyIndices_ = true;

		if (sharedIboParams_ != nullptr) {
			iboParams_ = *sharedIboParams_;
		} else {
			const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::ElementArray;
			if (iboParams_.mapBase == nullptr) {
				iboParams_ = RenderResources::GetBuffersManager().AcquireMemory(bufferType, numIndices * sizeof(std::uint16_t));
			}
		}

		return reinterpret_cast<std::uint16_t*>(iboParams_.mapBase + iboParams_.offset);
	}

	/// This method can only be used when mapping of GPU buffers is available
	std::uint16_t* Geometry::AcquireIndexPointer()
	{
		DEATH_ASSERT(ibo_ != nullptr);
		hasDirtyIndices_ = true;

		if (iboParams_.mapBase == nullptr) {
			const Rhi::MapFlags mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::ElementArray).mapFlags;
			FATAL_ASSERT_MSG(mapFlags != Rhi::MapFlags::None, "Mapping of GPU buffers is not available");
			iboParams_.mapBase = static_cast<std::uint8_t*>(Rhi::MapBufferRange(*ibo_, 0, ibo_->GetSize(), mapFlags));
		}

		return reinterpret_cast<std::uint16_t*>(iboParams_.mapBase);
	}

	void Geometry::ReleaseIndexPointer()
	{
		// Don't flush and unmap if the IBO is not custom
		if (ibo_ != nullptr && iboParams_.mapBase != nullptr) {
			Rhi::FlushMappedBufferRange(*iboParams_.object, iboParams_.offset, iboParams_.size);
			Rhi::UnmapBuffer(*iboParams_.object);
		}
		iboParams_.mapBase = nullptr;
	}

	void Geometry::SetHostIndexPointer(const std::uint16_t* indexPointer)
	{
		hasDirtyIndices_ = true;
		hostIndexPointer_ = indexPointer;
	}

	void Geometry::ShareIbo(const Geometry* geometry)
	{
		if (geometry == nullptr) {
			sharedIboParams_ = nullptr;
		} else if (geometry != this) {
			ibo_.reset(nullptr);
			sharedIboParams_ = &geometry->iboParams_;
		}
	}

	void Geometry::Bind()
	{
		if (vboParams_.object != nullptr) {
			Rhi::BindBuffer(*vboParams_.object);
		}
	}

	void Geometry::Draw(std::int32_t numInstances)
	{
		const std::int32_t vboOffset = static_cast<std::int32_t>(GetVboParams().offset / numElementsPerVertex_ / sizeof(float)) + firstVertex_;

		const void* iboOffsetPtr = nullptr;
		if (numIndices_ > 0) {
			iboOffsetPtr = reinterpret_cast<const void*>(GetIboParams().offset + firstIndex_ * sizeof(std::uint16_t));
		}

		if (numInstances == 0) {
			if (numIndices_ > 0) {
				Rhi::DrawIndexed(primitiveType_, numIndices_, iboOffsetPtr, vboOffset);
			} else {
				Rhi::Draw(primitiveType_, vboOffset, numVertices_);
			}
		} else if (numInstances > 0) {
			if (numIndices_ > 0) {
				Rhi::DrawIndexedInstanced(primitiveType_, numIndices_, iboOffsetPtr, numInstances, vboOffset);
			} else {
				Rhi::DrawInstanced(primitiveType_, vboOffset, numVertices_, numInstances);
			}
		}
	}

	void Geometry::CommitVertices()
	{
		if (hostVertexPointer_ != nullptr && hasDirtyVertices_) {
			// Checking if the common VBO is allowed to use mapping and do the same for the custom one
			const Rhi::MapFlags mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::Array).mapFlags;
			const std::uint32_t numFloats = numVertices_ * numElementsPerVertex_;

			if (mapFlags == Rhi::MapFlags::None && vbo_ != nullptr) {
				// Using buffer orphaning + sub-data update when mapping is unavailable
				Rhi::BufferData(*vbo_, vboParams_.size, nullptr, vboUsageFlags_);
				Rhi::BufferSubData(*vbo_, vboParams_.offset, vboParams_.size, hostVertexPointer_);
			} else {
				float* vertices = vbo_ ? AcquireVertexPointer() : AcquireVertexPointer(numFloats, numElementsPerVertex_);
				memcpy(vertices, hostVertexPointer_, numFloats * sizeof(float));
				ReleaseVertexPointer();
			}

			// The dirty flag is only useful with a custom VBO.
			if (vbo_ != nullptr) {
				hasDirtyVertices_ = false;
			}
		}
	}

	void Geometry::CommitIndices()
	{
		if (hostIndexPointer_ != nullptr && hasDirtyIndices_) {
			const Rhi::MapFlags mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::ElementArray).mapFlags;

			if (mapFlags == Rhi::MapFlags::None && ibo_ != nullptr) {
				Rhi::BufferData(*ibo_, iboParams_.size, nullptr, iboUsageFlags_);
				Rhi::BufferSubData(*ibo_, iboParams_.offset, iboParams_.size, hostIndexPointer_);
			} else {
				std::uint16_t* indices = ibo_ ? AcquireIndexPointer() : AcquireIndexPointer(numIndices_);
				memcpy(indices, hostIndexPointer_, numIndices_ * sizeof(std::uint16_t));
				ReleaseIndexPointer();
			}

			if (ibo_ != nullptr) {
				hasDirtyIndices_ = false;
			}
		}
	}
}
