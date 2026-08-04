#pragma once

#include "RHI/RhiTypes.h"
#include "RHI/Rhi.h"
#include "RenderBuffersManager.h"

#include <memory>

namespace nCine
{
	/**
		@brief Contains the vertex and index buffer data for a drawable node
		
		Owns (or shares) the vertex buffer object and optional index buffer object that back a single render
		command, together with the draw parameters (primitive type, first vertex, vertex/index counts). Vertex
		and index data can either be supplied through host pointers or written directly into mapped buffer
		memory acquired from the @ref RenderBuffersManager.
	*/
	class Geometry
	{
		friend class RenderCommand;

	public:
		Geometry();
		~Geometry();

		Geometry(const Geometry&) = delete;
		Geometry& operator=(const Geometry&) = delete;

		/** @brief Returns the primitive type (`PrimitiveType::Triangles`, `PrimitiveType::TriangleStrip`, ...) */
		inline PrimitiveType GetPrimitiveType() const {
			return _primitiveType;
		}
		/** @brief Returns the index of the first vertex to draw */
		inline std::int32_t GetFirstVertex() const {
			return _firstVertex;
		}
		/** @brief Returns the number of vertices */
		inline std::int32_t GetVertexCount() const {
			return _numVertices;
		}
		/** @brief Returns the number of float elements that compose the vertex format */
		inline std::uint32_t GetElementsPerVertex() const {
			return _numElementsPerVertex;
		}

		/** @brief Sets all three drawing parameters at once */
		void SetDrawParameters(PrimitiveType primitiveType, std::int32_t firstVertex, std::int32_t numVertices);
		/** @brief Sets the primitive type (`PrimitiveType::Triangles`, `PrimitiveType::TriangleStrip`, ...) */
		inline void SetPrimitiveType(PrimitiveType primitiveType) {
			_primitiveType = primitiveType;
		}
		/** @brief Sets the index of the first vertex to draw */
		inline void SetFirstVertex(std::int32_t firstVertex) {
			_firstVertex = firstVertex;
		}
		/** @brief Sets the number of vertices */
		inline void SetVertexCount(std::int32_t numVertices) {
			_numVertices = numVertices;
		}
		/** @brief Sets the number of float elements that compose the vertex format */
		inline void SetElementsPerVertex(std::uint32_t numElements) {
			_numElementsPerVertex = numElements;
		}
		/** @brief Creates a custom VBO that is unique to this object */
		void CreateCustomVbo(std::uint32_t numFloats, BufferUsage usage);
		/**
		 * @brief Acquires a pointer for writing vertex data into a custom VBO owned by this object
		 *
		 * This overload allows a custom alignment to be specified.
		 *
		 * @param numFloats           Number of floats to be written
		 * @param numFloatsAlignment  Alignment in floats
		 */
		float* AcquireVertexPointer(std::uint32_t numFloats, std::uint32_t numFloatsAlignment);
		/** @brief Acquires a pointer for writing vertex data into a custom VBO owned by this object */
		inline float* AcquireVertexPointer(std::uint32_t numFloats) {
			return AcquireVertexPointer(numFloats, 1);
		}
		/** @brief Acquires a pointer for writing vertex data into a VBO owned by the buffers manager */
		float* AcquireVertexPointer();
		/** @brief Releases the pointer used to write vertex data */
		void ReleaseVertexPointer();

		/** @brief Returns a pointer into host memory containing vertex data to be copied into a VBO */
		inline const float* GetHostVertexPointer() const {
			return _hostVertexPointer;
		}
		/** @brief Sets a pointer into host memory containing vertex data to be copied into a VBO */
		void SetHostVertexPointer(const float* vertexPointer);

		/** @brief Shares the VBO of another object */
		void ShareVbo(const Geometry* geometry);

		/** @brief Returns the number of indices used to render the geometry */
		inline std::uint32_t GetIndexCount() const {
			return _numIndices;
		}
		/** @brief Sets the index of the first index to draw */
		inline void SetFirstIndex(std::uint16_t firstIndex) {
			_firstIndex = firstIndex;
		}
		/** @brief Sets the number of indices used to render the geometry */
		inline void SetIndexCount(std::uint32_t numIndices) {
			_numIndices = numIndices;
		}
		/** @brief Creates a custom IBO that is unique to this object */
		void CreateCustomIbo(std::uint32_t numIndices, BufferUsage usage);
		/** @brief Acquires a pointer for writing index data into a custom IBO owned by this object */
		std::uint16_t* AcquireIndexPointer(std::uint32_t numIndices);
		/** @brief Acquires a pointer for writing index data into an IBO owned by the buffers manager */
		std::uint16_t* AcquireIndexPointer();
		/** @brief Releases the pointer used to write index data */
		void ReleaseIndexPointer();

		/** @brief Returns a pointer into host memory containing index data to be copied into an IBO */
		inline const std::uint16_t* GetHostIndexPointer() const {
			return _hostIndexPointer;
		}
		/** @brief Sets a pointer into host memory containing index data to be copied into an IBO */
		void SetHostIndexPointer(const std::uint16_t* indexPointer);

		/** @brief Shares the IBO of another object */
		void ShareIbo(const Geometry* geometry);

	private:
		PrimitiveType _primitiveType;
		std::int32_t _firstVertex;
		std::int32_t _numVertices;
		std::uint32_t _numElementsPerVertex;
		std::uint16_t _firstIndex;
		std::uint32_t _numIndices;
		const float* _hostVertexPointer;
		const std::uint16_t* _hostIndexPointer;

		std::unique_ptr<RHI::Buffer> _vbo;
		BufferUsage _vboUsageFlags;
		RenderBuffersManager::Parameters _vboParams;
		const RenderBuffersManager::Parameters* _sharedVboParams;

		std::unique_ptr<RHI::Buffer> _ibo;
		BufferUsage _iboUsageFlags;
		RenderBuffersManager::Parameters _iboParams;
		const RenderBuffersManager::Parameters* _sharedIboParams;

		bool _hasDirtyVertices;
		bool _hasDirtyIndices;

		void Bind();
		void Draw(std::int32_t numInstances);
		void CommitVertices();
		void CommitIndices();

		inline const RenderBuffersManager::Parameters& GetVboParams() const {
			return _sharedVboParams ? *_sharedVboParams : _vboParams;
		}
		inline const RenderBuffersManager::Parameters& GetIboParams() const {
			return _sharedIboParams ? *_sharedIboParams : _iboParams;
		}
	};

}
