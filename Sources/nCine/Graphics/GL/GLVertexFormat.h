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

	/// Collects all the state that specifies the format of a vertex
	class GLVertexFormat
	{
	public:
		/// The minimum value for `GL_MAX_VERTEX_ATTRIBS`
		static constexpr std::uint32_t MaxAttributes = 16;

		/// Vertex format attribute
		class Attribute
		{
			friend class GLVertexFormat;

		public:
			Attribute();

			void Init(std::uint32_t index, GLint size, GLenum type);
			bool operator==(const Attribute& other) const;
			bool operator!=(const Attribute& other) const;

			inline bool IsEnabled() const {
				return enabled_;
			}
			inline const GLBufferObject* GetVbo() const {
				return vbo_;
			}
			inline std::uint32_t GetIndex() const {
				return index_;
			}
			inline GLint GetSize() const {
				return size_;
			}
			inline GLenum GetType() const {
				return type_;
			}
			inline bool IsNormalized() const {
				return normalized_ == GL_TRUE;
			}
			inline GLsizei GetStride() const {
				return stride_;
			}
			inline const GLvoid* GetPointer() const {
				return pointer_;
			}
			inline std::uint32_t GetBaseOffset() const {
				return baseOffset_;
			}

			void SetVboParameters(GLsizei stride, const GLvoid* pointer);
			inline void setVbo(const GLBufferObject* vbo) {
				vbo_ = vbo;
			}
			inline void SetBaseOffset(std::uint32_t baseOffset) {
				baseOffset_ = baseOffset;
			}

			inline void SetSize(GLint size) {
				size_ = size;
			}
			inline void setType(GLenum type) {
				type_ = type;
			}
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
			/// Used to simulate missing `glDrawElementsBaseVertex()` on OpenGL ES 3.0
			std::uint32_t baseOffset_;
		};

		GLVertexFormat();
		GLVertexFormat(const GLVertexFormat& other) = default;
		GLVertexFormat& operator=(const nCine::GLVertexFormat& other) = default;

		inline std::uint32_t GetAttributeCount() const {
			return std::uint32_t(attributes_.size());
		}

		inline const GLBufferObject* GetIbo() const {
			return ibo_;
		}
		inline void SetIbo(const GLBufferObject* ibo) {
			ibo_ = ibo;
		}
		void Define();
		void Reset();

		inline Attribute& operator[](std::uint32_t index) {
			return attributes_[index];
		}
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
