#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include <Containers/SmallVector.h>

using namespace Death::Containers;

namespace nCine::RHI::GL
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
				return _enabled;
			}
			/** @brief Returns the source vertex buffer object */
			inline const GLBufferObject* GetVbo() const {
				return _vbo;
			}
			/** @brief Returns the shader location index of the attribute */
			inline std::uint32_t GetIndex() const {
				return _index;
			}
			/** @brief Returns the number of components per attribute */
			inline GLint GetSize() const {
				return _size;
			}
			/** @brief Returns the data type of each component (e.g., `GL_FLOAT`) */
			inline GLenum GetType() const {
				return _type;
			}
			/** @brief Returns `true` if integer values are normalized to a floating-point range */
			inline bool IsNormalized() const {
				return _normalized == GL_TRUE;
			}
			/** @brief Returns the byte offset between consecutive attributes */
			inline GLsizei GetStride() const {
				return _stride;
			}
			/** @brief Returns the byte offset of the first component within the buffer */
			inline const GLvoid* GetPointer() const {
				return _pointer;
			}
			/** @brief Returns the additional base vertex byte offset */
			inline std::uint32_t GetBaseOffset() const {
				return _baseOffset;
			}

			/** @brief Sets the stride and the byte offset of the attribute within the buffer */
			void SetVboParameters(GLsizei stride, const GLvoid* pointer);
			/** @brief Sets the source vertex buffer object */
			inline void setVbo(const GLBufferObject* vbo) {
				_vbo = vbo;
			}
			/** @brief Sets the additional base vertex byte offset */
			inline void SetBaseOffset(std::uint32_t baseOffset) {
				_baseOffset = baseOffset;
			}

			/** @brief Sets the number of components per attribute */
			inline void SetSize(GLint size) {
				_size = size;
			}
			/** @brief Sets the data type of each component */
			inline void SetType(GLenum type) {
				_type = type;
			}
			/** @brief Sets whether integer values are normalized to a floating-point range */
			inline void SetNormalized(bool normalized) {
				_normalized = normalized;
			}

		private:
			const GLBufferObject* _vbo;
			const GLvoid* _pointer;
			std::uint32_t _index;
			GLint _size;
			GLenum _type;
			GLsizei _stride;
			/** @brief Used to simulate the missing `glDrawElementsBaseVertex()` on OpenGL ES 3.0 */
			std::uint32_t _baseOffset;
			bool _enabled;
			GLboolean _normalized;
		};

		GLVertexFormat();
		GLVertexFormat(const GLVertexFormat& other) = default;
		GLVertexFormat& operator=(const GLVertexFormat& other) = default;

		/** @brief Returns the number of attributes in the format */
		inline std::uint32_t GetAttributeCount() const {
			return std::uint32_t(_attributes.size());
		}

		/** @brief Returns the index buffer object, or `nullptr` if none */
		inline const GLBufferObject* GetIbo() const {
			return _ibo;
		}
		/** @brief Sets the index buffer object */
		inline void SetIbo(const GLBufferObject* ibo) {
			_ibo = ibo;
		}
		/** @brief Applies the vertex format to the currently bound program and VAO */
		void Define();
		/** @brief Disables all attributes and clears the index buffer */
		void Reset();

		/**
		 * @brief Calculates a hash of the format for fast inequality checks
		 *
		 * Formats that compare equal have equal fingerprints, so a fingerprint mismatch proves
		 * inequality, while a match still has to be confirmed with `operator==()`.
		 */
		std::uint64_t CalculateFingerprint() const;

		/** @brief Returns the attribute at the given index */
		inline Attribute& operator[](std::uint32_t index) {
			return _attributes[index];
		}
		/** @brief Returns the attribute at the given index */
		inline const Attribute& operator[](std::uint32_t index) const {
			return _attributes[index];
		}

		bool operator==(const GLVertexFormat& other) const;
		bool operator!=(const GLVertexFormat& other) const;

	private:
		SmallVector<Attribute, MaxAttributes> _attributes;
		const GLBufferObject* _ibo;
	};
}
