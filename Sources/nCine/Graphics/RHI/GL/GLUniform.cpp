#include "GLUniform.h"
#include "GLDebug.h"
#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::GL
{
	GLUniform::GLUniform()
		: _index(0), _blockIndex(-1), _location(-1), _size(0), _type(GL_FLOAT), _offset(0)
	{
		_name[0] = '\0';
	}

	GLUniform::GLUniform(GLuint program, GLuint index)
		: GLUniform()
	{
		GLsizei length;
		glGetActiveUniform(program, index, MaxNameLength, &length, &_size, &_type, _name);
		DEATH_ASSERT(length <= MaxNameLength);

		if (!HasReservedPrefix()) {
			_location = glGetUniformLocation(program, _name);
		}
		GL_LOG_ERRORS();
	}

	GLUniform::GLUniform(GLuint program, const char* name, GLenum type, GLint arraySize)
		: GLUniform()
	{
		std::size_t length = strnlen(name, MaxNameLength);
		DEATH_ASSERT(length < MaxNameLength);
		std::memcpy(_name, name, length);
		_name[length] = '\0';

		_type = type;
		_size = (arraySize > 0 ? arraySize : 1);

		if (!HasReservedPrefix()) {
			// A location of -1 means the uniform was optimized out by the driver - committing it is a silent no-op
			_location = glGetUniformLocation(program, _name);
		}
		GL_LOG_ERRORS();
	}

	GLenum GLUniform::GetBasicType() const
	{
		switch (_type) {
			case GL_FLOAT:
			case GL_FLOAT_VEC2:
			case GL_FLOAT_VEC3:
			case GL_FLOAT_VEC4:
				return GL_FLOAT;
			case GL_INT:
			case GL_INT_VEC2:
			case GL_INT_VEC3:
			case GL_INT_VEC4:
				return GL_INT;
#if !defined(DEATH_TARGET_VITA)	// vitaGL declares none of the bool / unsigned-int vector types
			case GL_BOOL:
			case GL_BOOL_VEC2:
			case GL_BOOL_VEC3:
			case GL_BOOL_VEC4:
				return GL_BOOL;
#endif
			case GL_UNSIGNED_INT:
#if !defined(DEATH_TARGET_VITA)
			case GL_UNSIGNED_INT_VEC2:
			case GL_UNSIGNED_INT_VEC3:
			case GL_UNSIGNED_INT_VEC4:
#endif
				return GL_UNSIGNED_INT;
			case GL_FLOAT_MAT2:
			case GL_FLOAT_MAT3:
			case GL_FLOAT_MAT4:
				return GL_FLOAT;
#if defined(RHI_GL_PROFILE_CORE) // not available in OpenGL ES
			case GL_SAMPLER_1D:
#endif
			case GL_SAMPLER_2D:
#if !defined(DEATH_TARGET_VITA)	// GL_SAMPLER_3D is not declared by vitaGL
			case GL_SAMPLER_3D:
#endif
			case GL_SAMPLER_CUBE:
#if defined(RHI_GL_PROFILE_CORE) || GL_ES_VERSION_3_2
			case GL_SAMPLER_BUFFER:
#endif
				return GL_INT;
			default:
				LOGW("No available case to handle type: {}", _type);
				return _type;
		}
	}

	std::uint32_t GLUniform::GetComponentCount() const
	{
		switch (_type) {
			case GL_FLOAT:
			case GL_INT:
#if !defined(DEATH_TARGET_VITA)
			case GL_BOOL:
#endif
			case GL_UNSIGNED_INT:
				return 1;
			case GL_FLOAT_VEC2:
			case GL_INT_VEC2:
#if !defined(DEATH_TARGET_VITA)
			case GL_BOOL_VEC2:
			case GL_UNSIGNED_INT_VEC2:
#endif
				return 2;
			case GL_FLOAT_VEC3:
			case GL_INT_VEC3:
#if !defined(DEATH_TARGET_VITA)
			case GL_BOOL_VEC3:
			case GL_UNSIGNED_INT_VEC3:
#endif
				return 3;
			case GL_FLOAT_VEC4:
			case GL_INT_VEC4:
#if !defined(DEATH_TARGET_VITA)
			case GL_BOOL_VEC4:
			case GL_UNSIGNED_INT_VEC4:
#endif
				return 4;
			case GL_FLOAT_MAT2:
				return 4;
			case GL_FLOAT_MAT3:
				return 9;
			case GL_FLOAT_MAT4:
				return 16;
#if defined(RHI_GL_PROFILE_CORE) // not available in OpenGL ES
			case GL_SAMPLER_1D:
#endif
			case GL_SAMPLER_2D:
#if !defined(DEATH_TARGET_VITA)	// GL_SAMPLER_3D is not declared by vitaGL
			case GL_SAMPLER_3D:
#endif
			case GL_SAMPLER_CUBE:
#if defined(RHI_GL_PROFILE_CORE) || GL_ES_VERSION_3_2
			case GL_SAMPLER_BUFFER:
#endif
				return 1;
			default:
				LOGW("No available case to handle type: {}", _type);
				return 0;
		}
	}

	ShaderCompiler::UniformType GLUniform::GetType() const
	{
		switch (_type) {
			case GL_FLOAT: return ShaderCompiler::UniformType::Float;
			case GL_INT: return ShaderCompiler::UniformType::Int;
			case GL_UNSIGNED_INT: return ShaderCompiler::UniformType::UInt;
#if !defined(DEATH_TARGET_VITA)
			case GL_BOOL: return ShaderCompiler::UniformType::Bool;
#endif
			case GL_FLOAT_VEC2: return ShaderCompiler::UniformType::Vec2;
			case GL_FLOAT_VEC3: return ShaderCompiler::UniformType::Vec3;
			case GL_FLOAT_VEC4: return ShaderCompiler::UniformType::Vec4;
			case GL_INT_VEC2: return ShaderCompiler::UniformType::IVec2;
			case GL_INT_VEC3: return ShaderCompiler::UniformType::IVec3;
			case GL_INT_VEC4: return ShaderCompiler::UniformType::IVec4;
#if !defined(DEATH_TARGET_VITA)	// vitaGL declares none of the bool / unsigned-int vector types
			case GL_UNSIGNED_INT_VEC2: return ShaderCompiler::UniformType::UVec2;
			case GL_UNSIGNED_INT_VEC3: return ShaderCompiler::UniformType::UVec3;
			case GL_UNSIGNED_INT_VEC4: return ShaderCompiler::UniformType::UVec4;
			case GL_BOOL_VEC2: return ShaderCompiler::UniformType::BVec2;
			case GL_BOOL_VEC3: return ShaderCompiler::UniformType::BVec3;
			case GL_BOOL_VEC4: return ShaderCompiler::UniformType::BVec4;
#endif
			case GL_FLOAT_MAT2: return ShaderCompiler::UniformType::Mat2;
			case GL_FLOAT_MAT3: return ShaderCompiler::UniformType::Mat3;
			case GL_FLOAT_MAT4: return ShaderCompiler::UniformType::Mat4;
			case GL_SAMPLER_2D: return ShaderCompiler::UniformType::Sampler2D;
#if !defined(DEATH_TARGET_VITA)	// GL_SAMPLER_3D is not declared by vitaGL
			case GL_SAMPLER_3D: return ShaderCompiler::UniformType::Sampler3D;
#endif
			case GL_SAMPLER_CUBE: return ShaderCompiler::UniformType::SamplerCube;
			default:
				LOGW("No available case to handle GL type: {}", _type);
				return ShaderCompiler::UniformType::Float;
		}
	}

	bool GLUniform::HasReservedPrefix() const
	{
		return (MaxNameLength >= 3 && _name[0] == 'g' && _name[1] == 'l' && _name[2] == '_');
	}
}
