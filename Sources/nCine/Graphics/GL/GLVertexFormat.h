#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../CommonHeaders.h"
#endif

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine
{
	class GLBufferObject;

	/**
		@brief Describes the format of a vertex
		
		Collects all the state that specifies the format of a vertex: a set of @ref Attribute entries
		(each describing a component count, type, normalization, stride, offset and source VBO) plus an
		optional index buffer. @ref Define() applies the format to the currently bound program and VAO by
		issuing the vertex attribute pointer calls, while @ref Reset() disables all attributes.
	*/
	class GLVertexFormat
	{
	public:
		/** @brief The minimum guaranteed value for `GL_MAX_VERTEX_ATTRIBS` */
		static constexpr std::uint32_t MaxAttributes = 16;

		/**
		 * @brief A single vertex attribute within a vertex format
		 *
		 * Describes one vertex attribute: its shader location index, component count, GL type,
		 * normalization, stride, offset and the source vertex buffer object.
		 */
		class Attribute
		{
			friend class GLVertexFormat;

		public:
			Attribute();

			/** @brief Initializes the attribute as enabled with the given index, component count and type */
			void Init(std::uint32_t index, GLint size, GLenum type);
			bool operator==(const Attribute& other) const;
			bool operator!=(const Attribute& other) const;

			/** @brief Returns `true` if the attribute is enabled */
			inline bool IsEnabled() const {
				return enabled_;
			}
			/** @brief Returns the source vertex buffer object */
			inline const GLBufferObject* GetVbo() const {
				return vbo_;
			}
			/** @brief Returns the shader location index of the attribute */
			inline std::uint32_t GetIndex() const {
				return index_;
			}
			/** @brief Returns the number of components per attribute */
			inline GLint GetSize() const {
				return size_;
			}
			/** @brief Returns the data type of each component (e.g. `GL_FLOAT`) */
			inline GLenum GetType() const {
				return type_;
			}
			/** @brief Returns `true` if integer values are normalized to a floating-point range */
			inline bool IsNormalized() const {
				return normalized_ == GL_TRUE;
			}
			/** @brief Returns the byte offset between consecutive attributes */
			inline GLsizei GetStride() const {
				return stride_;
			}
			/** @brief Returns the byte offset of the first component within the buffer */
			inline const GLvoid* GetPointer() const {
				return pointer_;
			}
			/** @brief Returns the additional base vertex byte offset */
			inline std::uint32_t GetBaseOffset() const {
				return baseOffset_;
			}

			/** @brief Sets the stride and the byte offset of the attribute within the buffer */
			void SetVboParameters(GLsizei stride, const GLvoid* pointer);
			/** @brief Sets the source vertex buffer object */
			inline void setVbo(const GLBufferObject* vbo) {
				vbo_ = vbo;
			}
			/** @brief Sets the additional base vertex byte offset */
			inline void SetBaseOffset(std::uint32_t baseOffset) {
				baseOffset_ = baseOffset;
			}

			/** @brief Sets the number of components per attribute */
			inline void SetSize(GLint size) {
				size_ = size;
			}
			/** @brief Sets the data type of each component */
			inline void SetType(GLenum type) {
				type_ = type;
			}
			/** @brief Sets whether integer values are normalized to a floating-point range */
			inline void SetNormalized(bool normalized) {
				normalized_ = normalized;
			}

		private:
			bool enabled_;
			const GLBufferObject* vbo_;
			std::uint32_t index_;
			GLint size_;
			GLenum type_;
			GLboolean normalized_;
			GLsizei stride_;
			const GLvoid* pointer_;
			/** @brief Used to simulate the missing `glDrawElementsBaseVertex()` on OpenGL ES 3.0 */
			std::uint32_t baseOffset_;
		};

		GLVertexFormat();
		GLVertexFormat(const GLVertexFormat& other) = default;
		GLVertexFormat& operator=(const nCine::GLVertexFormat& other) = default;

		/** @brief Returns the number of attributes in the format */
		inline std::uint32_t GetAttributeCount() const {
			return std::uint32_t(attributes_.size());
		}

		/** @brief Returns the index buffer object, or `nullptr` if none */
		inline const GLBufferObject* GetIbo() const {
			return ibo_;
		}
		/** @brief Sets the index buffer object */
		inline void SetIbo(const GLBufferObject* ibo) {
			ibo_ = ibo;
		}
		/** @brief Applies the vertex format to the currently bound program and VAO */
		void Define();
		/** @brief Disables all attributes and clears the index buffer */
		void Reset();

		/** @brief Returns the attribute at the given index */
		inline Attribute& operator[](std::uint32_t index) {
			return attributes_[index];
		}
		/** @brief Returns the attribute at the given index */
		inline const Attribute& operator[](std::uint32_t index) const {
			return attributes_[index];
		}

		bool operator==(const GLVertexFormat& other) const;
		bool operator!=(const GLVertexFormat& other) const;

	private:
		SmallVector<Attribute, MaxAttributes> attributes_;
		const GLBufferObject* ibo_;
	};
}
