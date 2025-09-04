#pragma once

#include "GL/GLBufferObject.h"
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

		/// Returns the primitive type (`GL_TRIANGLES`, `GL_TRIANGLE_STRIP`, ...)
		inline GLenum GetPrimitiveType() const {
			return primitiveType_;
		}
		/// Returns the index of the first vertex to draw
		inline GLint GetFirstVertex() const {
			return firstVertex_;
		}
		/// Returns the number of vertices
		inline GLsizei GetVertexCount() const {
			return numVertices_;
		}
		/// Returns the number of float elements that composes the vertex format
		inline std::uint32_t GetElementsPerVertex() const {
			return numElementsPerVertex_;
		}

		/// Sets all three drawing parameters
		void SetDrawParameters(GLenum primitiveType, GLint firstVertex, GLsizei numVertices);
		/// Sets the primitive type (`GL_TRIANGLES`, `GL_TRIANGLE_STRIP`, ...)
		inline void SetPrimitiveType(GLenum primitiveType) {
			primitiveType_ = primitiveType;
		}
		/// Sets the index number of the first vertex to draw
		inline void SetFirstVertex(GLint firstVertex) {
			firstVertex_ = firstVertex;
		}
		/// Sets the number of vertices
		inline void SetVertexCount(GLsizei numVertices) {
			numVertices_ = numVertices;
		}
		/// Sets the number of float elements that composes the vertex format
		inline void SetElementsPerVertex(std::uint32_t numElements) {
			numElementsPerVertex_ = numElements;
		}
		/// Creates a custom VBO that is unique to this `Geometry` object
		void CreateCustomVbo(std::uint32_t numFloats, GLenum usage);
		/// Retrieves a pointer that can be used to write vertex data from a custom VBO owned by this object
		/*! This overloaded version allows a custom alignment specification */
		GLfloat* AcquireVertexPointer(std::uint32_t numFloats, std::uint32_t numFloatsAlignment);
		/// Retrieves a pointer that can be used to write vertex data from a custom VBO owned by this object
		inline GLfloat* AcquireVertexPointer(std::uint32_t numFloats) {
			return AcquireVertexPointer(numFloats, 1);
		}
		/// Retrieves a pointer that can be used to write vertex data from a VBO owned by the buffers manager
		GLfloat* AcquireVertexPointer();
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
		inline void SetFirstIndex(GLushort firstIndex) {
			firstIndex_ = firstIndex;
		}
		/// Sets the number of indices used to render the geometry
		inline void SetIndexCount(std::uint32_t numIndices) {
			numIndices_ = numIndices;
		}
		/// Creates a custom IBO that is unique to this `Geometry` object
		void CreateCustomIbo(std::uint32_t numIndices, GLenum usage);
		/// Retrieves a pointer that can be used to write index data from a custom IBO owned by this object
		GLushort* AcquireIndexPointer(std::uint32_t numIndices);
		/// Retrieves a pointer that can be used to write index data from a IBO owned by the buffers manager
		GLushort* AcquireIndexPointer();
		/// Releases the pointer used to write index data
		void ReleaseIndexPointer();

		/// Returns a pointer into host memory containing index data to be copied into a IBO
		inline const GLushort* GetHostIndexPointer() const {
			return hostIndexPointer_;
		}
		/// Sets a pointer into host memory containing index data to be copied into a IBO
		void SetHostIndexPointer(const GLushort* indexPointer);

		/// Shares the IBO of another `Geometry` object
		void ShareIbo(const Geometry* geometry);

	private:
		GLenum primitiveType_;
		GLint firstVertex_;
		GLsizei numVertices_;
		std::uint32_t numElementsPerVertex_;
		GLushort firstIndex_;
		std::uint32_t numIndices_;
		const float* hostVertexPointer_;
		const GLushort* hostIndexPointer_;

		std::unique_ptr<GLBufferObject> vbo_;
		GLenum vboUsageFlags_;
		RenderBuffersManager::Parameters vboParams_;
		const RenderBuffersManager::Parameters* sharedVboParams_;

		std::unique_ptr<GLBufferObject> ibo_;
		GLenum iboUsageFlags_;
		RenderBuffersManager::Parameters iboParams_;
		const RenderBuffersManager::Parameters* sharedIboParams_;

		bool hasDirtyVertices_;
		bool hasDirtyIndices_;

		void Bind();
		void Draw(GLsizei numInstances);
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
