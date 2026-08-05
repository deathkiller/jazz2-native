#include "GLUniformBlock.h"
#include "GLShaderProgram.h"
#include "GLDebug.h"
#include "../IRhiCapabilities.h"
#include "../../../ServiceLocator.h"

#include <cstring>

namespace nCine::RHI::GL
{
	GLUniformBlock::GLUniformBlock()
		: _program(0), _index(0), _size(0), _alignAmount(0), _bindingIndex(-1)
	{
		_name[0] = '\0';
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
		_program = program;
		_index = index;

		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_DATA_SIZE, &_size);
		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_NAME_LENGTH, &nameLength);
		DEATH_ASSERT(nameLength <= MaxNameLength);
		glGetActiveUniformBlockName(program, index, MaxNameLength, &nameLength, _name);
		glGetActiveUniformBlockiv(program, index, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &uniformCount);

		if (discover == DiscoverUniforms::Enabled && uniformCount > 0) {
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
				blockUniform._index = uniformIndices[i];
				blockUniform._blockIndex = static_cast<GLint>(index);
				blockUniform._type = static_cast<GLenum>(uniformTypes[i]);
				blockUniform._size = uniformSizes[i];
				blockUniform._offset = uniformOffsets[i];

#if !defined(DEATH_TARGET_EMSCRIPTEN)
				DEATH_ASSERT(uniformNameLengths[i] <= GLUniform::MaxNameLength,
					("Uniform {} name length is {}, which is more than {}", i, uniformNameLengths[i], GLUniform::MaxNameLength), );
#endif

#if defined(RHI_GL_PROFILE_CORE) && !defined(DEATH_TARGET_EMSCRIPTEN)
				glGetActiveUniformName(program, uniformIndices[i], MaxNameLength, &uniformNameLengths[i], blockUniform._name);
#else
				// Some drivers do not accept a `nullptr` for size and type
				GLint unusedSize;
				GLenum unusedType;
				glGetActiveUniform(program, uniformIndices[i], MaxNameLength, &uniformNameLengths[i], &unusedSize, &unusedType, blockUniform._name);
#endif
				_blockUniforms[blockUniform._name] = blockUniform;
			}
		}

		GL_LOG_ERRORS();

		// Align to the uniform buffer offset alignment or `glBindBufferRange()` will generate an `INVALID_VALUE` error
		static const std::int32_t offsetAlignment = theServiceLocator().GetRhiCapabilities().GetValue(IRhiCapabilities::IntValues::UniformBufferOffsetAlignment);
		_alignAmount = (offsetAlignment - _size % offsetAlignment) % offsetAlignment;
		_size += _alignAmount;
#endif
	}

	GLUniformBlock::GLUniformBlock(GLuint program, GLuint index)
		: GLUniformBlock(program, index, DiscoverUniforms::Enabled)
	{
	}

	GLUniformBlock::GLUniformBlock(GLuint program, const char* name, GLuint index, GLint dataSize)
		: GLUniformBlock()
	{
		_program = program;
		_index = index;
		_size = dataSize;

		std::size_t length = strnlen(name, MaxNameLength);
		DEATH_ASSERT(length < MaxNameLength);
		std::memcpy(_name, name, length);
		_name[length] = '\0';

		// Align to the uniform buffer offset alignment or `glBindBufferRange()` will generate an `INVALID_VALUE` error
		static const std::int32_t offsetAlignment = theServiceLocator().GetRhiCapabilities().GetValue(IRhiCapabilities::IntValues::UniformBufferOffsetAlignment);
		_alignAmount = (offsetAlignment - _size % offsetAlignment) % offsetAlignment;
		_size += _alignAmount;
	}

	void GLUniformBlock::SetBlockBinding(GLuint blockBinding)
	{
#if defined(RHI_GL_PROFILE_ES2)
		// glUniformBlockBinding is ES 3.0; the OpenGL|ES 2.0 profile has no uniform blocks to bind, so this is
		// never called (block members are pushed as loose uniforms)
		static_cast<void>(blockBinding);
#else
		DEATH_ASSERT(_program != 0);

		if (_bindingIndex != static_cast<GLint>(blockBinding)) {
			glUniformBlockBinding(_program, _index, blockBinding);
			GL_LOG_ERRORS();
			_bindingIndex = static_cast<GLint>(blockBinding);
		}
#endif
	}
}
