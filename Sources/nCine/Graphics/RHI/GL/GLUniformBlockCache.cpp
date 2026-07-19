#include "GLUniformBlockCache.h"
#include "GLUniformBlock.h"
#include "../../../../Main.h"

#include <cstring>
#include <vector>

namespace nCine::RhiGL
{
	GLUniformBlockCache::GLUniformBlockCache()
		: uniformBlock_(nullptr), dataPointer_(nullptr), usedSize_(0)
	{
	}

	GLUniformBlockCache::GLUniformBlockCache(GLUniformBlock* uniformBlock)
		: uniformBlock_(uniformBlock), dataPointer_(nullptr), usedSize_(0)
	{
		DEATH_ASSERT(uniformBlock);
		usedSize_ = uniformBlock->GetSize();

		static_assert(UniformHashSize >= GLUniformBlock::BlockUniformHashSize, "Uniform cache is smaller than the number of uniforms");

		for (const GLUniform& uniform : uniformBlock->blockUniforms_) {
			GLUniformCache uniformCache(&uniform);
			uniformCaches_[uniform.GetName()] = uniformCache;
		}
	}

	GLuint GLUniformBlockCache::GetIndex() const
	{
		GLuint index = 0;
		if (uniformBlock_ != nullptr) {
			index = uniformBlock_->GetIndex();
		}
		return index;
	}

	GLuint GLUniformBlockCache::GetBindingIndex() const
	{
		GLuint bindingIndex = 0;
		if (uniformBlock_ != nullptr) {
			bindingIndex = uniformBlock_->GetBindingIndex();
		}
		return bindingIndex;
	}

	GLint GLUniformBlockCache::GetSize() const
	{
		GLint size = 0;
		if (uniformBlock_ != nullptr) {
			size = uniformBlock_->GetSize();
		}
		return size;
	}

	std::uint8_t GLUniformBlockCache::GetAlignAmount() const
	{
		std::uint8_t alignAmount = 0;
		if (uniformBlock_ != nullptr) {
			alignAmount = uniformBlock_->GetAlignAmount();
		}
		return alignAmount;
	}

	void GLUniformBlockCache::SetDataPointer(GLubyte* dataPointer)
	{
		dataPointer_ = dataPointer;

		for (GLUniformCache& uniformCache : uniformCaches_) {
			uniformCache.SetDataPointer(dataPointer_ + uniformCache.GetUniform()->GetOffset());
		}
	}

	void GLUniformBlockCache::SetUsedSize(GLint usedSize)
	{
		if (usedSize >= 0) {
			usedSize_ = usedSize;
		}
	}

	bool GLUniformBlockCache::CopyData(std::uint32_t destIndex, const GLubyte* src, std::uint32_t numBytes)
	{
		if (destIndex + numBytes > std::uint32_t(GetSize()) || numBytes == 0 || src == nullptr || dataPointer_ == nullptr) {
			return false;
		}
		std::memcpy(&dataPointer_[destIndex], src, numBytes);
		return true;
	}

	GLUniformCache* GLUniformBlockCache::GetUniform(StringView name)
	{
		return uniformCaches_.find(String::nullTerminatedView(name));
	}

	void GLUniformBlockCache::SetBlockBinding(GLuint blockBinding)
	{
		if (uniformBlock_) {
			uniformBlock_->SetBlockBinding(blockBinding);
		}
	}

#if defined(RHI_GL_PROFILE_ES2)
	void GLUniformBlockCache::CommitAsLooseUniforms()
	{
		// Each member cache points into the host-side block buffer at the member's offset (SetDataPointer);
		// on ES2 the member is a real loose uniform, so push it directly with the matching glUniform* call
		// (the location was resolved in GLShaderProgram::ImportReflection). Mirrors GLUniformCache::CommitValue.
		//
		// The host data uses the std140 layout the reflection computed, but glUniform*v expects tightly packed
		// values. The layouts differ wherever std140 pads to 16-byte boundaries: array elements of scalar /
		// vec2 / vec3 types (stride 16 vs their tight size) and the columns of mat2 / mat3 (column stride 16
		// vs 8 / 12) - repack those into a scratch buffer before uploading. vec4-sized rows (vec4, ivec4,
		// mat4 columns) are already dense.
		for (GLUniformCache& memberCache : uniformCaches_) {
			const GLUniform* uniform = memberCache.GetUniform();
			if (uniform == nullptr) {
				continue;
			}
			const GLint location = uniform->GetLocation();
			const GLubyte* data = memberCache.GetDataPointer();
			if (location < 0 || data == nullptr) {
				continue;
			}
			const GLsizei count = GLsizei(uniform->GetSize() > 0 ? uniform->GetSize() : 1);

			// Rows per element and tight bytes per row: mats are N column-vectors, everything else is 1 row
			std::int32_t rowsPerElement = 1;
			std::int32_t tightRowBytes = 0;
			switch (uniform->GetGLType()) {
				case GL_FLOAT: case GL_INT: tightRowBytes = 4; break;
				case GL_FLOAT_VEC2: case GL_INT_VEC2: tightRowBytes = 8; break;
				case GL_FLOAT_VEC3: case GL_INT_VEC3: tightRowBytes = 12; break;
				case GL_FLOAT_VEC4: case GL_INT_VEC4: tightRowBytes = 16; break;
				case GL_FLOAT_MAT2: rowsPerElement = 2; tightRowBytes = 8; break;
				case GL_FLOAT_MAT3: rowsPerElement = 3; tightRowBytes = 12; break;
				case GL_FLOAT_MAT4: rowsPerElement = 4; tightRowBytes = 16; break;
				default: continue;
			}
			// std140: each row (array element or matrix column) is padded to a vec4 boundary, except that a
			// non-array scalar/vec member is stored at its tight size with no trailing padding to skip
			const bool strided = (tightRowBytes < 16) && (count > 1 || rowsPerElement > 1);

			static std::vector<GLubyte> scratch;
			if (strided) {
				const std::int32_t totalRows = count * rowsPerElement;
				scratch.resize(std::size_t(totalRows) * tightRowBytes);
				for (std::int32_t row = 0; row < totalRows; row++) {
					std::memcpy(scratch.data() + std::size_t(row) * tightRowBytes, data + std::size_t(row) * 16, tightRowBytes);
				}
				data = scratch.data();
			}

			const GLfloat* asFloat = reinterpret_cast<const GLfloat*>(data);
			const GLint* asInt = reinterpret_cast<const GLint*>(data);
			switch (uniform->GetGLType()) {
				case GL_FLOAT:			glUniform1fv(location, count, asFloat); break;
				case GL_FLOAT_VEC2:		glUniform2fv(location, count, asFloat); break;
				case GL_FLOAT_VEC3:		glUniform3fv(location, count, asFloat); break;
				case GL_FLOAT_VEC4:		glUniform4fv(location, count, asFloat); break;
				case GL_INT:			glUniform1iv(location, count, asInt); break;
				case GL_INT_VEC2:		glUniform2iv(location, count, asInt); break;
				case GL_INT_VEC3:		glUniform3iv(location, count, asInt); break;
				case GL_INT_VEC4:		glUniform4iv(location, count, asInt); break;
				case GL_FLOAT_MAT2:		glUniformMatrix2fv(location, count, GL_FALSE, asFloat); break;
				case GL_FLOAT_MAT3:		glUniformMatrix3fv(location, count, GL_FALSE, asFloat); break;
				case GL_FLOAT_MAT4:		glUniformMatrix4fv(location, count, GL_FALSE, asFloat); break;
				default: break;
			}
		}
	}
#endif
}
