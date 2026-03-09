#pragma once

#include "RHI/RHI.h"
#include "RenderBuffersManager.h"

#include <memory>

namespace nCine
{
	/// Contains geometric data for a drawable node
	class Geometry
	{
		friend class RenderCommand;

	public:
		/// Default constructor
		Geometry();
		~Geometry();

		Geometry(const Geometry&) = delete;
		Geometry& operator=(const Geometry&) = delete;

		/// Returns the primitive type (Triangles, TriangleStrip, ...)
		inline RHI::PrimitiveType GetPrimitiveType() const {
			return primitiveType_;
		}
		/// Returns the index of the first vertex to draw
		inline std::int32_t GetFirstVertex() const {
			return firstVertex_;
		}
		/// Returns the number of vertices
		inline std::int32_t GetVertexCount() const {
			return numVertices_;
		}
		/// Returns the number of float elements that composes the vertex format
		inline std::uint32_t GetElementsPerVertex() const {
			return numElementsPerVertex_;
		}

		/// Sets all three drawing parameters
		void SetDrawParameters(RHI::PrimitiveType primitiveType, std::int32_t firstVertex, std::int32_t numVertices);
		/// Sets the primitive type
		inline void SetPrimitiveType(RHI::PrimitiveType primitiveType) {
			primitiveType_ = primitiveType;
		}
		/// Sets the index number of the first vertex to draw
		inline void SetFirstVertex(std::int32_t firstVertex) {
			firstVertex_ = firstVertex;
		}
		/// Sets the number of vertices
		inline void SetVertexCount(std::int32_t numVertices) {
			numVertices_ = numVertices;
		}
		/// Sets the number of float elements that composes the vertex format
		inline void SetElementsPerVertex(std::uint32_t numElements) {
			numElementsPerVertex_ = numElements;
		}
		/// Creates a custom VBO that is unique to this `Geometry` object
		void CreateCustomVbo(std::uint32_t numFloats, RHI::BufferUsage usage);
		/// Retrieves a pointer that can be used to write vertex data from a custom VBO owned by this object
		/*! This overloaded version allows a custom alignment specification */
		float* AcquireVertexPointer(std::uint32_t numFloats, std::uint32_t numFloatsAlignment);
		/// Retrieves a pointer that can be used to write vertex data from a custom VBO owned by this object
		inline float* AcquireVertexPointer(std::uint32_t numFloats) {
			return AcquireVertexPointer(numFloats, 1);
		}
		/// Retrieves a pointer that can be used to write vertex data from a VBO owned by the buffers manager
		float* AcquireVertexPointer();
		/// Releases the pointer used to write vertex data
		void ReleaseVertexPointer();

		/// Returns a pointer into host memory containing vertex data to be copied into a VBO
		inline const float* GetHostVertexPointer() const {
			return hostVertexPointer_;
		}
		/// Sets a pointer into host memory containing vertex data to be copied into a VBO
		void SetHostVertexPointer(const float* vertexPointer);

		/// Shares the VBO of another `Geometry` object
		void ShareVbo(const Geometry* geometry);

		/// Returns the number of indices used to render the geometry
		inline std::uint32_t GetIndexCount() const {
			return numIndices_;
		}
		/// Sets the index number of the first index to draw
		inline void SetFirstIndex(std::uint16_t firstIndex) {
			firstIndex_ = firstIndex;
		}
		/// Sets the number of indices used to render the geometry
		inline void SetIndexCount(std::uint32_t numIndices) {
			numIndices_ = numIndices;
		}
		/// Creates a custom IBO that is unique to this `Geometry` object
		void CreateCustomIbo(std::uint32_t numIndices, RHI::BufferUsage usage);
		/// Retrieves a pointer that can be used to write index data from a custom IBO owned by this object
		std::uint16_t* AcquireIndexPointer(std::uint32_t numIndices);
		/// Retrieves a pointer that can be used to write index data from a IBO owned by the buffers manager
		std::uint16_t* AcquireIndexPointer();
		/// Releases the pointer used to write index data
		void ReleaseIndexPointer();

		/// Returns a pointer into host memory containing index data to be copied into a IBO
		inline const std::uint16_t* GetHostIndexPointer() const {
			return hostIndexPointer_;
		}
		/// Sets a pointer into host memory containing index data to be copied into a IBO
		void SetHostIndexPointer(const std::uint16_t* indexPointer);

		/// Shares the IBO of another `Geometry` object
		void ShareIbo(const Geometry* geometry);

	private:
		RHI::PrimitiveType primitiveType_;
		std::int32_t        firstVertex_;
		std::int32_t        numVertices_;
		std::uint32_t       numElementsPerVertex_;
		std::uint16_t       firstIndex_;
		std::uint32_t       numIndices_;
		const float*            hostVertexPointer_;
		const std::uint16_t*    hostIndexPointer_;

		std::unique_ptr<RHI::Buffer> vbo_;
		RHI::BufferUsage             vboUsageFlags_;
		RenderBuffersManager::Parameters  vboParams_;
		const RenderBuffersManager::Parameters* sharedVboParams_;

		std::unique_ptr<RHI::Buffer> ibo_;
		RHI::BufferUsage             iboUsageFlags_;
		RenderBuffersManager::Parameters  iboParams_;
		const RenderBuffersManager::Parameters* sharedIboParams_;

		bool hasDirtyVertices_;
		bool hasDirtyIndices_;

		void Bind();
		void Draw(std::int32_t numInstances);
		void CommitVertices();
		void CommitIndices();

		inline const RenderBuffersManager::Parameters& GetVboParams() const {
			return sharedVboParams_ ? *sharedVboParams_ : vboParams_;
		}
		inline const RenderBuffersManager::Parameters& GetIboParams() const {
			return sharedIboParams_ ? *sharedIboParams_ : iboParams_;
		}
	};

}
