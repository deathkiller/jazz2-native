#include "GLUniformBlock.h"
#include "GLShaderProgram.h"
#include "GLDebug.h"
#include "../IGfxCapabilities.h"
#include "../../ServiceLocator.h"

namespace nCine
{
	GLUniformBlock::GLUniformBlock()
		: program_(0), index_(0), size_(0), alignAmount_(0), bindingIndex_(-1)
	{
		name_[0] = '\0';
	}

	GLUniformBlock::GLUniformBlock(GLuint program, GLuint index, DiscoverUniforms discover)
		: GLUniformBlock()
	{
		GLint nameLength = 0;
		GLint uniformCount = 0;
		program_ = program;
		index_ = index;

		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_DATA_SIZE, &size_);
		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_NAME_LENGTH, &nameLength);
		ASSERT(nameLength <= MaxNameLength);
		glGetActiveUniformBlockName(program, index, MaxNameLength, &nameLength, name_);
		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &uniformCount);

		if (discover == DiscoverUniforms::ENABLED && uniformCount > 0) {
			ASSERT(uniformCount <= MaxNumBlockUniforms);
			GLuint uniformIndices[MaxNumBlockUniforms];
			GLint uniformTypes[MaxNumBlockUniforms];
			GLint uniformSizes[MaxNumBlockUniforms];
			GLint uniformOffsets[MaxNumBlockUniforms];
			GLint uniformNameLengths[MaxNumBlockUniforms];

			GLint uniformQueryIndices[MaxNumBlockUniforms];
			glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, uniformQueryIndices);
			for (int i = 0; i < uniformCount; i++)
				uniformIndices[i] = static_cast<GLuint>(uniformQueryIndices[i]);

			glGetActiveUniformsiv(program, uniformCount, uniformIndices, GL_UNIFORM_TYPE, uniformTypes);
			glGetActiveUniformsiv(program, uniformCount, uniformIndices, GL_UNIFORM_SIZE, uniformSizes);
			glGetActiveUniformsiv(program, uniformCount, uniformIndices, GL_UNIFORM_OFFSET, uniformOffsets);
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			glGetActiveUniformsiv(program, uniformCount, uniformIndices, GL_UNIFORM_NAME_LENGTH, uniformNameLengths);
#endif

			for (int i = 0; i < uniformCount; i++) {
				GLUniform blockUniform;
				blockUniform.index_ = uniformIndices[i];
				blockUniform.blockIndex_ = static_cast<GLint>(index);
				blockUniform.type_ = static_cast<GLenum>(uniformTypes[i]);
				blockUniform.size_ = uniformSizes[i];
				blockUniform.offset_ = uniformOffsets[i];

#if !defined(DEATH_TARGET_EMSCRIPTEN)
				ASSERT_MSG(uniformNameLengths[i] <= GLUniform::MaxNameLength, "Uniform %d name length is %d, which is more than %d", i, uniformNameLengths[i], GLUniform::MaxNameLength);
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
		static const int offsetAlignment = theServiceLocator().GetGfxCapabilities().value(IGfxCapabilities::GLIntValues::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
		alignAmount_ = (offsetAlignment - size_ % offsetAlignment) % offsetAlignment;
		size_ += alignAmount_;
	}

	GLUniformBlock::GLUniformBlock(GLuint program, GLuint index)
		: GLUniformBlock(program, index, DiscoverUniforms::ENABLED)
	{
	}

	void GLUniformBlock::setBlockBinding(GLuint blockBinding)
	{
		ASSERT(program_ != 0);

		if (bindingIndex_ != static_cast<GLint>(blockBinding)) {
			glUniformBlockBinding(program_, index_, blockBinding);
			GL_LOG_ERRORS();
			bindingIndex_ = static_cast<GLint>(blockBinding);
		}
	}
}
