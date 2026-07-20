#include "GLUniform.h"
#include "GLDebug.h"
#include "../../../../Main.h"

#include <cstring>

namespace nCine::RhiGL
{
	GLUniform::GLUniform()
		: index_(0), blockIndex_(-1), location_(-1), size_(0), type_(GL_FLOAT), offset_(0)
	{
		name_[0] = '\0';
	}

	GLUniform::GLUniform(GLuint program, GLuint index)
		: GLUniform()
	{
		GLsizei length;
		glGetActiveUniform(program, index, MaxNameLength, &length, &size_, &type_, name_);
		DEATH_ASSERT(length <= MaxNameLength);

		if (!HasReservedPrefix()) {
			location_ = glGetUniformLocation(program, name_);
		}
		GL_LOG_ERRORS();
	}

	GLUniform::GLUniform(GLuint program, const char* name, GLenum type, GLint arraySize)
		: GLUniform()
	{
		std::size_t length = strnlen(name, MaxNameLength);
		DEATH_ASSERT(length < MaxNameLength);
		std::memcpy(name_, name, length);
		name_[length] = '\0';

		type_ = type;
		size_ = (arraySize > 0 ? arraySize : 1);

		if (!HasReservedPrefix()) {
			// A location of -1 means the uniform was optimized out by the driver - committing it is a silent no-op
			location_ = glGetUniformLocation(program, name_);
		}
		GL_LOG_ERRORS();
	}

	GLenum GLUniform::GetBasicType() const
	{
		switch (type_) {
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
#if !defined(WITH_OPENGLES) // not available in OpenGL ES
			case GL_SAMPLER_1D:
#endif
			case GL_SAMPLER_2D:
#if !defined(DEATH_TARGET_VITA)	// GL_SAMPLER_3D is not declared by vitaGL
			case GL_SAMPLER_3D:
#endif
			case GL_SAMPLER_CUBE:
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
			case GL_SAMPLER_BUFFER:
#endif
				return GL_INT;
			default:
				LOGW("No available case to handle type: {}", type_);
				return type_;
		}
	}

	std::uint32_t GLUniform::GetComponentCount() const
	{
		switch (type_) {
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
#if !defined(WITH_OPENGLES) // not available in OpenGL ES
			case GL_SAMPLER_1D:
#endif
			case GL_SAMPLER_2D:
#if !defined(DEATH_TARGET_VITA)	// GL_SAMPLER_3D is not declared by vitaGL
			case GL_SAMPLER_3D:
#endif
			case GL_SAMPLER_CUBE:
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
			case GL_SAMPLER_BUFFER:
#endif
				return 1;
			default:
				LOGW("No available case to handle type: {}", type_);
				return 0;
		}
	}

	ShaderCompiler::UniformType GLUniform::GetType() const
	{
		switch (type_) {
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
				LOGW("No available case to handle GL type: {}", type_);
				return ShaderCompiler::UniformType::Float;
		}
	}

	bool GLUniform::HasReservedPrefix() const
	{
		return (MaxNameLength >= 3 && name_[0] == 'g' && name_[1] == 'l' && name_[2] == '_');
	}
}
