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
		static const std::uint32_t MaxAttributes = 16;

		/// Vertex format attribute
		class Attribute
		{
			friend class GLVertexFormat;

		public:
			Attribute();

			void init(std::uint32_t index, GLint size, GLenum type);
			bool operator==(const Attribute& other) const;
			bool operator!=(const Attribute& other) const;

			inline bool isEnabled() const {
				return enabled_;
			}
			inline const GLBufferObject* vbo() const {
				return vbo_;
			}
			inline unsigned int index() const {
				return index_;
			}
			inline GLint size() const {
				return size_;
			}
			inline GLenum type() const {
				return type_;
			}
			inline bool isNormalized() const {
				return normalized_ == GL_TRUE;
			}
			inline GLsizei stride() const {
				return stride_;
			}
			inline const GLvoid* pointer() const {
				return pointer_;
			}
			inline std::uint32_t baseOffset() const {
				return baseOffset_;
			}

			void setVboParameters(GLsizei stride, const GLvoid* pointer);
			inline void setVbo(const GLBufferObject* vbo) {
				vbo_ = vbo;
			}
			inline void setBaseOffset(std::uint32_t baseOffset) {
				baseOffset_ = baseOffset;
			}

			inline void setSize(GLint size) {
				size_ = size;
			}
			inline void setType(GLenum type) {
				type_ = type;
			}
			inline void setNormalized(bool normalized) {
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

		inline std::uint32_t numAttributes() const {
			return std::uint32_t(attributes_.size());
		}

		inline const GLBufferObject* ibo() const {
			return ibo_;
		}
		inline void setIbo(const GLBufferObject* ibo) {
			ibo_ = ibo;
		}
		void define();
		void reset();

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
