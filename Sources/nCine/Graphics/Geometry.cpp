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
			RenderStatistics::RemoveCustomVbo(vbo_->GetSize());
		}
		if (ibo_ != nullptr) {
			RenderStatistics::RemoveCustomIbo(ibo_->GetSize());
		}
#endif
	}

	void Geometry::SetDrawParameters(GLenum primitiveType, GLint firstVertex, GLsizei numVertices)
	{
		primitiveType_ = primitiveType;
		firstVertex_ = firstVertex;
		numVertices_ = numVertices;

#if defined(WITH_OPENGL2)
		// OpenGL 2.x: Create vertex position data for simple sprite quads (4 vertices, TRIANGLE_STRIP)
		// In OpenGL 3.3+, positions are calculated from gl_VertexID in the vertex shader
		if (primitiveType == GL_TRIANGLE_STRIP && numVertices == 4 && vbo_ == nullptr && hostVertexPointer_ == nullptr) {
			// Create a simple quad: positions for a unit square (-0.5 to 0.5)
			// These will be transformed by the model matrix
			static const GLfloat quadPositions[] = {
				0.0f, 0.0f,  // bottom-left
				1.0f, 0.0f,  // bottom-right
				0.0f,  1.0f,  // top-left
				1.0f,  1.0f   // top-right
			};

			numElementsPerVertex_ = 2; // 2 floats per vertex (x, y)
			const std::uint32_t numFloats = numVertices * numElementsPerVertex_;
			GLfloat* vertices = AcquireVertexPointer(numFloats, numElementsPerVertex_);
			if (vertices != nullptr) {
				std::memcpy(vertices, quadPositions, numFloats * sizeof(GLfloat));
			}
		}
#endif
	}

	void Geometry::CreateCustomVbo(std::uint32_t numFloats, GLenum usage)
	{
		vbo_ = std::make_unique<GLBufferObject>(GL_ARRAY_BUFFER);
		vbo_->BufferData(numFloats * sizeof(GLfloat), nullptr, usage);

		vboUsageFlags_ = usage;
		vboParams_.object = vbo_.get();
		vboParams_.size = vbo_->GetSize();
		vboParams_.offset = 0;
		vboParams_.mapBase = nullptr;

#if defined(NCINE_PROFILING)
		RenderStatistics::AddCustomVbo(vbo_->GetSize());
#endif
	}

	GLfloat* Geometry::AcquireVertexPointer(std::uint32_t numFloats, std::uint32_t numFloatsAlignment)
	{
		DEATH_ASSERT(vbo_ == nullptr);
		hasDirtyVertices_ = true;

		if (sharedVboParams_ != nullptr) {
			vboParams_ = *sharedVboParams_;
		} else {
			const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::Array;
			if (vboParams_.mapBase == nullptr) {
				vboParams_ = RenderResources::GetBuffersManager().AcquireMemory(bufferType, numFloats * sizeof(GLfloat), numFloatsAlignment * sizeof(GLfloat));
			}
		}

		return reinterpret_cast<GLfloat*>(vboParams_.mapBase + vboParams_.offset);
	}

	/*! This method can only be used when mapping of OpenGL buffers is available */
	GLfloat* Geometry::AcquireVertexPointer()
	{
		DEATH_ASSERT(vbo_ != nullptr);
		hasDirtyVertices_ = true;

		if (vboParams_.mapBase == nullptr) {
#if defined(WITH_OPENGL2)
			DEATH_ASSERT_UNREACHABLE();
#else
			const GLenum mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::Array).mapFlags;
			FATAL_ASSERT_MSG(mapFlags != 0, "Mapping of OpenGL buffers is not available");
			vboParams_.mapBase = static_cast<GLubyte*>(vbo_->MapBufferRange(0, vbo_->GetSize(), mapFlags));
#endif
		}

		return reinterpret_cast<GLfloat*>(vboParams_.mapBase);
	}

	void Geometry::ReleaseVertexPointer()
	{
		// Don't flush and unmap if the VBO is not custom
		if (vbo_ != nullptr && vboParams_.mapBase != nullptr) {
#if defined(WITH_OPENGL2)
			DEATH_ASSERT_UNREACHABLE();
#else
			vboParams_.object->FlushMappedBufferRange(vboParams_.offset, vboParams_.size);
			vboParams_.object->Unmap();
#endif
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

	void Geometry::CreateCustomIbo(std::uint32_t numIndices, GLenum usage)
	{
		ibo_ = std::make_unique<GLBufferObject>(GL_ELEMENT_ARRAY_BUFFER);
		ibo_->BufferData(numIndices * sizeof(GLushort), nullptr, usage);

		iboUsageFlags_ = usage;
		iboParams_.object = ibo_.get();
		iboParams_.size = ibo_->GetSize();
		iboParams_.offset = 0;
		iboParams_.mapBase = nullptr;

#if defined(NCINE_PROFILING)
		RenderStatistics::AddCustomIbo(ibo_->GetSize());
#endif
	}

	GLushort* Geometry::AcquireIndexPointer(std::uint32_t numIndices)
	{
		DEATH_ASSERT(ibo_ == nullptr);
		hasDirtyIndices_ = true;

		if (sharedIboParams_ != nullptr) {
			iboParams_ = *sharedIboParams_;
		} else {
			const RenderBuffersManager::BufferTypes bufferType = RenderBuffersManager::BufferTypes::ElementArray;
			if (iboParams_.mapBase == nullptr) {
				iboParams_ = RenderResources::GetBuffersManager().AcquireMemory(bufferType, numIndices * sizeof(GLushort));
			}
		}

		return reinterpret_cast<GLushort*>(iboParams_.mapBase + iboParams_.offset);
	}

	/*! This method can only be used when mapping of OpenGL buffers is available */
	GLushort* Geometry::AcquireIndexPointer()
	{
		DEATH_ASSERT(ibo_ != nullptr);
		hasDirtyIndices_ = true;

		if (iboParams_.mapBase == nullptr) {
#if defined(WITH_OPENGL2)
			DEATH_ASSERT_UNREACHABLE();
#else
			const GLenum mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::ElementArray).mapFlags;
			FATAL_ASSERT_MSG(mapFlags != 0, "Mapping of OpenGL buffers is not available");
			iboParams_.mapBase = static_cast<GLubyte*>(ibo_->MapBufferRange(0, ibo_->GetSize(), mapFlags));
#endif
		}

		return reinterpret_cast<GLushort*>(iboParams_.mapBase);
	}

	void Geometry::ReleaseIndexPointer()
	{
		// Don't flush and unmap if the IBO is not custom
		if (ibo_ != nullptr && iboParams_.mapBase != nullptr) {
#if defined(WITH_OPENGL2)
			DEATH_ASSERT_UNREACHABLE();
#else
			iboParams_.object->FlushMappedBufferRange(iboParams_.offset, iboParams_.size);
			iboParams_.object->Unmap();
#endif
		}
		iboParams_.mapBase = nullptr;
	}

	void Geometry::SetHostIndexPointer(const GLushort* indexPointer)
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
			vboParams_.object->Bind();
		}
	}

	void Geometry::Draw(GLsizei numInstances)
	{
		const GLint vboOffset = static_cast<GLint>(GetVboParams().offset / numElementsPerVertex_ / sizeof(GLfloat)) + firstVertex_;

		void* iboOffsetPtr = nullptr;
		if (numIndices_ > 0) {
			iboOffsetPtr = reinterpret_cast<void*>(GetIboParams().offset + firstIndex_ * sizeof(GLushort));
		}

		if (numInstances == 0) {
			if (numIndices_ > 0) {
#if defined(WITH_OPENGL2) || (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
				glDrawElements(primitiveType_, numIndices_, GL_UNSIGNED_SHORT, iboOffsetPtr);
#else
				glDrawElementsBaseVertex(primitiveType_, numIndices_, GL_UNSIGNED_SHORT, iboOffsetPtr, vboOffset);
#endif
			} else {
				glDrawArrays(primitiveType_, vboOffset, numVertices_);
			}
		} else if (numInstances > 0) {
#if defined(WITH_OPENGL2)
			// OpenGL 2.x doesn't support instanced drawing - fallback to single draw call
			if (numIndices_ > 0) {
				glDrawElements(primitiveType_, numIndices_, GL_UNSIGNED_SHORT, iboOffsetPtr);
			} else {
				glDrawArrays(primitiveType_, vboOffset, numVertices_);
			}
#else
			if (numIndices_ > 0) {
#	if (defined(WITH_OPENGLES) && !GL_ES_VERSION_3_2) || defined(DEATH_TARGET_EMSCRIPTEN)
				glDrawElementsInstanced(primitiveType_, numIndices_, GL_UNSIGNED_SHORT, iboOffsetPtr, numInstances);
#	else
				glDrawElementsInstancedBaseVertex(primitiveType_, numIndices_, GL_UNSIGNED_SHORT, iboOffsetPtr, numInstances, vboOffset);
#	endif
			} else {
				glDrawArraysInstanced(primitiveType_, vboOffset, numVertices_, numInstances);
			}
#endif
		}
	}

	void Geometry::CommitVertices()
	{
		if (hostVertexPointer_ != nullptr && hasDirtyVertices_) {
			// Checking if the common VBO is allowed to use mapping and do the same for the custom one
			const GLenum mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::Array).mapFlags;
			const std::uint32_t numFloats = numVertices_ * numElementsPerVertex_;

			if (mapFlags == 0 && vbo_ != nullptr) {
				// Using buffer orphaning + `glBufferSubData()` when having a custom VBO with no mapping available
				vbo_->BufferData(vboParams_.size, nullptr, vboUsageFlags_);
				vbo_->BufferSubData(vboParams_.offset, vboParams_.size, hostVertexPointer_);
			} else {
#if defined(WITH_OPENGL2)
				DEATH_ASSERT_UNREACHABLE();
#else
				GLfloat* vertices = (vbo_ != nullptr ? AcquireVertexPointer() : AcquireVertexPointer(numFloats, numElementsPerVertex_));
				std::memcpy(vertices, hostVertexPointer_, numFloats * sizeof(GLfloat));
				ReleaseVertexPointer();
#endif
			}

			// The dirty flag is only useful with a custom VBO. If the render command uses the common one, it must always copy vertices.
			if (vbo_ != nullptr) {
				hasDirtyVertices_ = false;
			}
		}
	}

	void Geometry::CommitIndices()
	{
		if (hostIndexPointer_ != nullptr && hasDirtyIndices_) {
			// Checking if the common IBO is allowed to use mapping and do the same for the custom one
			const GLenum mapFlags = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::ElementArray).mapFlags;

			if (mapFlags == 0 && ibo_ != nullptr) {
				// Using buffer orphaning + `glBufferSubData()` when having a custom IBO with no mapping available
				ibo_->BufferData(iboParams_.size, nullptr, iboUsageFlags_);
				ibo_->BufferSubData(iboParams_.offset, iboParams_.size, hostIndexPointer_);
			} else {
#if defined(WITH_OPENGL2)
				DEATH_ASSERT_UNREACHABLE();
#else
				GLushort* indices = (ibo_ != nullptr ? AcquireIndexPointer() : AcquireIndexPointer(numIndices_));
				std::memcpy(indices, hostIndexPointer_, numIndices_ * sizeof(GLushort));
				ReleaseIndexPointer();
#endif
			}

			// The dirty flag is only useful with a custom IBO. If the render command uses the common one, it must always copy indices.
			if (ibo_ != nullptr) {
				hasDirtyIndices_ = false;
			}
		}
	}
}
