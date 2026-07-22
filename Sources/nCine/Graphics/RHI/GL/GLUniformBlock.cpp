#include "GLUniformBlock.h"
#include "GLShaderProgram.h"
#include "GLDebug.h"
#include "../../IGfxCapabilities.h"
#include "../../../ServiceLocator.h"

#include <cstring>

namespace nCine::RHI::GL
{
	GLUniformBlock::GLUniformBlock()
		: program_(0), index_(0), size_(0), alignAmount_(0), bindingIndex_(-1)
	{
		name_[0] = '\0';
	}

	GLUniformBlock::GLUniformBlock(GLuint program, GLuint index, DiscoverUniforms discover)
		: GLUniformBlock()
	{
#if defined(RHI_GL_PROFILE_ES2)
		// Uniform buffer objects are unused under the OpenGL|ES 2.0 profile, whose reflection path never
		// invokes this discovery constructor (DiscoverUniformBlocks() is a no-op there). The block-introspection
		// entry points (glGetActiveUniformBlock*/glGetActiveUniformsiv) and their GL_UNIFORM_BLOCK_* enums are
		// ES 3.0 and strict ES 2.0 headers such as vitaGL's declare none of them, so leave the members at their
		// default-constructed values.
		static_cast<void>(program);
		static_cast<void>(index);
		static_cast<void>(discover);
#else
		GLint nameLength = 0;
		GLint uniformCount = 0;
		program_ = program;
		index_ = index;

		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size_);
		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_NAME_LENGTH, &nameLength);
		DEATH_ASSERT(nameLength <= MaxNameLength);
		glGetActiveUniformBlockName(program, index, MaxNameLength, &nameLength, name_);
		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &uniformCount);

		if (discover == DiscoverUniforms::ENABLED && uniformCount > 0) {
			DEATH_ASSERT(uniformCount <= MaxNumBlockUniforms);
			GLuint uniformIndices[MaxNumBlockUniforms];
			GLint uniformTypes[MaxNumBlockUniforms];
			GLint uniformSizes[MaxNumBlockUniforms];
			GLint uniformOffsets[MaxNumBlockUniforms];
			GLint uniformNameLengths[MaxNumBlockUniforms];

			GLint uniformQueryIndices[MaxNumBlockUniforms];
			glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, uniformQueryIndices);
			for (std::int32_t i = 0; i < uniformCount; i++)
				uniformIndices[i] = static_cast<GLuint>(uniformQueryIndices[i]);

			glGetActiveUniformsiv(program, uniformCount, uniformIndices, GL_UNIFORM_TYPE, uniformTypes);
			glGetActiveUniformsiv(program, uniformCount, uniformIndices, GL_UNIFORM_SIZE, uniformSizes);
			glGetActiveUniformsiv(program, uniformCount, uniformIndices, GL_UNIFORM_OFFSET, uniformOffsets);
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			glGetActiveUniformsiv(program, uniformCount, uniformIndices, GL_UNIFORM_NAME_LENGTH, uniformNameLengths);
#endif

			for (std::int32_t i = 0; i < uniformCount; i++) {
				GLUniform blockUniform;
				blockUniform.index_ = uniformIndices[i];
				blockUniform.blockIndex_ = static_cast<GLint>(index);
				blockUniform.type_ = static_cast<GLenum>(uniformTypes[i]);
				blockUniform.size_ = uniformSizes[i];
				blockUniform.offset_ = uniformOffsets[i];

#if !defined(DEATH_TARGET_EMSCRIPTEN)
				DEATH_ASSERT(uniformNameLengths[i] <= GLUniform::MaxNameLength,
					("Uniform {} name length is {}, which is more than {}", i, uniformNameLengths[i], GLUniform::MaxNameLength), );
#endif

#if !defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN)
				glGetActiveUniformName(program, uniformIndices[i], MaxNameLength, &uniformNameLengths[i], blockUniform.name_);
#else
				// Some drivers do not accept a `nullptr` for size and type
				GLint unusedSize;
				GLenum unusedType;
				glGetActiveUniform(program, uniformIndices[i], MaxNameLength, &uniformNameLengths[i], &unusedSize, &unusedType, blockUniform.name_);
#endif
				blockUniforms_[blockUniform.name_] = blockUniform;
			}
		}

		GL_LOG_ERRORS();

		// Align to the uniform buffer offset alignment or `glBindBufferRange()` will generate an `INVALID_VALUE` error
		static const std::int32_t offsetAlignment = theServiceLocator().GetGfxCapabilities().GetValue(IGfxCapabilities::IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
		alignAmount_ = (offsetAlignment - size_ % offsetAlignment) % offsetAlignment;
		size_ += alignAmount_;
#endif
	}

	GLUniformBlock::GLUniformBlock(GLuint program, GLuint index)
		: GLUniformBlock(program, index, DiscoverUniforms::ENABLED)
	{
	}

	GLUniformBlock::GLUniformBlock(GLuint program, const char* name, GLuint index, GLint dataSize)
		: GLUniformBlock()
	{
		program_ = program;
		index_ = index;
		size_ = dataSize;

		std::size_t length = strnlen(name, MaxNameLength);
		DEATH_ASSERT(length < MaxNameLength);
		std::memcpy(name_, name, length);
		name_[length] = '\0';

		// Align to the uniform buffer offset alignment or `glBindBufferRange()` will generate an `INVALID_VALUE` error
		static const std::int32_t offsetAlignment = theServiceLocator().GetGfxCapabilities().GetValue(IGfxCapabilities::IntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
		alignAmount_ = (offsetAlignment - size_ % offsetAlignment) % offsetAlignment;
		size_ += alignAmount_;
	}

	void GLUniformBlock::SetBlockBinding(GLuint blockBinding)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glUniformBlockBinding is ES 3.0; the OpenGL|ES 2.0 profile has no uniform blocks to bind, so this is
		// never called (block members are pushed as loose uniforms)
		static_cast<void>(blockBinding);
#else
		DEATH_ASSERT(program_ != 0);

		if (bindingIndex_ != static_cast<GLint>(blockBinding)) {
			glUniformBlockBinding(program_, index_, blockBinding);
			GL_LOG_ERRORS();
			bindingIndex_ = static_cast<GLint>(blockBinding);
		}
#endif
	}
}
