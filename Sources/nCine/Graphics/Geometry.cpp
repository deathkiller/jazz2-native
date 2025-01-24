#include "Geometry.h"
#include "RenderResources.h"
#include "RenderStatistics.h"

#include <cstring> // for memcpy()

namespace nCine
{
	Geometry::Geometry()
		: primitiveType_(GL_TRIANGLES), firstVertex_(0), numVertices_(0), numElementsPerVertex_(2), firstIndex_(0), numIndices_(0),
			hostVertexPointer_(nullptr), hostIndexPointer_(nullptr), vboUsageFlags_(0), sharedVboParams_(nullptr), iboUsageFlags_(0),
			sharedIboParams_(nullptr), hasDirtyVertices_(true), hasDirtyIndices_(true)
	{
	}

	Geometry::~Geometry()
	{
#if defined(NCINE_PROFILING)
		if (vbo_ != nullptr) {
			RenderStatistics::removeCustomVbo(vbo_->size());
		}
		if (ibo_ != nullptr) {
			RenderStatistics::removeCustomIbo(ibo_->size());
		}
#endif
	}

	void Geometry::setDrawParameters(GLenum primitiveType, GLint firstVertex, GLsizei numVertices)
	{
		primitiveType_ = primitiveType;
		firstVertex_ = firstVertex;
		numVertices_ = numVertices;
	}

	void Geometry::createCustomVbo(std::uint32_t numFloats, GLenum usage)
	{
		vbo_ = std::make_unique<GLBufferObject>(GL_ARRAY_BUFFER);
		vbo_->bufferData(numFloats * sizeof(GLfloat), nullptr, usage);

		vboUsageFlags_ = usage;
		vboParams_.object = vbo_.get();
		vboParams_.size = vbo_->size();
		vboParams_.offset = 0;
		vboParams_.mapBase = nullptr;

#if defined(NCINE_PROFILING)
		RenderStatistics::addCustomVbo(vbo_->size());
#endif
	}

	GLfloat* Geometry::acquireVertexPointer(std::uint32_t numFloats, std::uint32_t numFloatsAlignment)
	{
		ASSERT(vbo_ == nullptr);
		hasDirtyVertices_ = true;

		if (sharedVboParams_ != nullptr) {
			vboParams_ = *sharedVboParams_;
		} else {
			const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::Array;
			if (vboParams_.mapBase == nullptr) {
				vboParams_ = RenderResources::buffersManager().acquireMemory(bufferType, numFloats * sizeof(GLfloat), numFloatsAlignment * sizeof(GLfloat));
			}
		}

		return reinterpret_cast<GLfloat*>(vboParams_.mapBase + vboParams_.offset);
	}

	/*! This method can only be used when mapping of OpenGL buffers is available */
	GLfloat* Geometry::acquireVertexPointer()
	{
		ASSERT(vbo_ != nullptr);
		hasDirtyVertices_ = true;

		if (vboParams_.mapBase == nullptr) {
			const GLenum mapFlags = RenderResources::buffersManager().specs(RenderBuffersManager::BufferTypes::Array).mapFlags;
			FATAL_ASSERT_MSG(mapFlags, "Mapping of OpenGL buffers is not available");
			vboParams_.mapBase = static_cast<GLubyte*>(vbo_->mapBufferRange(0, vbo_->size(), mapFlags));
		}

		return reinterpret_cast<GLfloat*>(vboParams_.mapBase);
	}

	void Geometry::releaseVertexPointer()
	{
		// Don't flush and unmap if the VBO is not custom
		if (vbo_ != nullptr && vboParams_.mapBase != nullptr) {
			vboParams_.object->flushMappedBufferRange(vboParams_.offset, vboParams_.size);
			vboParams_.object->unmap();
		}
		vboParams_.mapBase = nullptr;
	}

	void Geometry::setHostVertexPointer(const float* vertexPointer)
	{
		hasDirtyVertices_ = true;
		hostVertexPointer_ = vertexPointer;
	}

	void Geometry::shareVbo(const Geometry* geometry)
	{
		if (geometry == nullptr) {
			sharedVboParams_ = nullptr;
		} else if (geometry != this) {
			vbo_.reset(nullptr);
			sharedVboParams_ = &geometry->vboParams_;
		}
	}

	void Geometry::createCustomIbo(std::uint32_t numIndices, GLenum usage)
	{
		ibo_ = std::make_unique<GLBufferObject>(GL_ELEMENT_ARRAY_BUFFER);
		ibo_->bufferData(numIndices * sizeof(GLushort), nullptr, usage);

		iboUsageFlags_ = usage;
		iboParams_.object = ibo_.get();
		iboParams_.size = ibo_->size();
		iboParams_.offset = 0;
		iboParams_.mapBase = nullptr;

#if defined(NCINE_PROFILING)
		RenderStatistics::addCustomIbo(ibo_->size());
#endif
	}

	GLushort* Geometry::acquireIndexPointer(std::uint32_t numIndices)
	{
		ASSERT(ibo_ == nullptr);
		hasDirtyIndices_ = true;

		if (sharedIboParams_ != nullptr) {
			iboParams_ = *sharedIboParams_;
		} else {
			const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::ElementArray;
			if (iboParams_.mapBase == nullptr) {
				iboParams_ = RenderResources::buffersManager().acquireMemory(bufferType, numIndices * sizeof(GLushort));
			}
		}

		return reinterpret_cast<GLushort*>(iboParams_.mapBase + iboParams_.offset);
	}

	/*! This method can only be used when mapping of OpenGL buffers is available */
	GLushort* Geometry::acquireIndexPointer()
	{
		ASSERT(ibo_ != nullptr);
		hasDirtyIndices_ = true;

		if (iboParams_.mapBase == nullptr) {
			const GLenum mapFlags = RenderResources::buffersManager().specs(RenderBuffersManager::BufferTypes::ElementArray).mapFlags;
			FATAL_ASSERT_MSG(mapFlags, "Mapping of OpenGL buffers is not available");
			iboParams_.mapBase = static_cast<GLubyte*>(ibo_->mapBufferRange(0, ibo_->size(), mapFlags));
		}

		return reinterpret_cast<GLushort*>(iboParams_.mapBase);
	}

	void Geometry::releaseIndexPointer()
	{
		// Don't flush and unmap if the IBO is not custom
		if (ibo_ != nullptr && iboParams_.mapBase != nullptr) {
			iboParams_.object->flushMappedBufferRange(iboParams_.offset, iboParams_.size);
			iboParams_.object->unmap();
		}
		iboParams_.mapBase = nullptr;
	}

	void Geometry::setHostIndexPointer(const GLushort* indexPointer)
	{
		hasDirtyIndices_ = true;
		hostIndexPointer_ = indexPointer;
	}

	void Geometry::shareIbo(const Geometry* geometry)
	{
		if (geometry == nullptr) {
			sharedIboParams_ = nullptr;
		} else if (geometry != this) {
			ibo_.reset(nullptr);
			sharedIboParams_ = &geometry->iboParams_;
		}
	}

	void Geometry::bind()
	{
		if (vboParams_.object != nullptr) {
			vboParams_.object->bind();
		}
	}

	void Geometry::draw(GLsizei numInstances)
	{
		const GLint vboOffset = static_cast<GLint>(vboParams().offset / numElementsPerVertex_ / sizeof(GLfloat)) + firstVertex_;

		void* iboOffsetPtr = nullptr;
		if (numIndices_ > 0) {
			iboOffsetPtr = reinterpret_cast<void*>(iboParams().offset + firstIndex_ * sizeof(GLushort));
		}

		if (numInstances == 0) {
			if (numIndices_ > 0) {
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
				glDrawElements(primitiveType_, numIndices_, GL_UNSIGNED_SHORT, iboOffsetPtr);
#else
				glDrawElementsBaseVertex(primitiveType_, numIndices_, GL_UNSIGNED_SHORT, iboOffsetPtr, vboOffset);
#endif
			} else {
				glDrawArrays(primitiveType_, vboOffset, numVertices_);
			}
		} else if (numInstances > 0) {
			if (numIndices_ > 0) {
#if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
				glDrawElementsInstanced(primitiveType_, numIndices_, GL_UNSIGNED_SHORT, iboOffsetPtr, numInstances);
#else
				glDrawElementsInstancedBaseVertex(primitiveType_, numIndices_, GL_UNSIGNED_SHORT, iboOffsetPtr, numInstances, vboOffset);
#endif
			} else {
				glDrawArraysInstanced(primitiveType_, vboOffset, numVertices_, numInstances);
			}
		}
	}

	void Geometry::commitVertices()
	{
		if (hostVertexPointer_ != nullptr && hasDirtyVertices_) {
			// Checking if the common VBO is allowed to use mapping and do the same for the custom one
			const GLenum mapFlags = RenderResources::buffersManager().specs(RenderBuffersManager::BufferTypes::Array).mapFlags;
			const std::uint32_t numFloats = numVertices_ * numElementsPerVertex_;

			if (mapFlags == 0 && vbo_ != nullptr) {
				// Using buffer orphaning + `glBufferSubData()` when having a custom VBO with no mapping available
				vbo_->bufferData(vboParams_.size, nullptr, vboUsageFlags_);
				vbo_->bufferSubData(vboParams_.offset, vboParams_.size, hostVertexPointer_);
			} else {
				GLfloat* vertices = vbo_ ? acquireVertexPointer() : acquireVertexPointer(numFloats, numElementsPerVertex_);
				memcpy(vertices, hostVertexPointer_, numFloats * sizeof(GLfloat));
				releaseVertexPointer();
			}

			// The dirty flag is only useful with a custom VBO. If the render command uses the common one, it must always copy vertices.
			if (vbo_ != nullptr) {
				hasDirtyVertices_ = false;
			}
		}
	}

	void Geometry::commitIndices()
	{
		if (hostIndexPointer_ != nullptr && hasDirtyIndices_) {
			// Checking if the common IBO is allowed to use mapping and do the same for the custom one
			const GLenum mapFlags = RenderResources::buffersManager().specs(RenderBuffersManager::BufferTypes::ElementArray).mapFlags;

			if (mapFlags == 0 && ibo_ != nullptr) {
				// Using buffer orphaning + `glBufferSubData()` when having a custom IBO with no mapping available
				ibo_->bufferData(iboParams_.size, nullptr, iboUsageFlags_);
				ibo_->bufferSubData(iboParams_.offset, iboParams_.size, hostIndexPointer_);
			} else {
				GLushort* indices = ibo_ ? acquireIndexPointer() : acquireIndexPointer(numIndices_);
				memcpy(indices, hostIndexPointer_, numIndices_ * sizeof(GLushort));
				releaseIndexPointer();
			}

			// The dirty flag is only useful with a custom IBO. If the render command uses the common one, it must always copy indices.
			if (ibo_ != nullptr) {
				hasDirtyIndices_ = false;
			}
		}
	}
}
