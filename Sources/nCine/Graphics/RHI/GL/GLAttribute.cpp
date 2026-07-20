#include "GLAttribute.h"
#include "GLDebug.h"
#include "../../../../Main.h"

#include <cstring>

namespace nCine::RhiGL
{
	GLAttribute::GLAttribute()
		: location_(-1), size_(0), type_(GL_FLOAT)
	{
		name_[0] = '\0';
	}

	GLAttribute::GLAttribute(GLuint program, GLuint index)
		: location_(-1), size_(0), type_(GL_FLOAT)
	{
		GLsizei length;
		glGetActiveAttrib(program, index, MaxNameLength, &length, &size_, &type_, name_);
		DEATH_ASSERT(length <= MaxNameLength);

		if (!HasReservedPrefix()) {
			location_ = glGetAttribLocation(program, name_);
			if (location_ == -1) {
				LOGW("Attribute location not found for attribute \"{}\" ({}) in shader program {}", name_, index, program);
			}
		}
		GL_LOG_ERRORS();
	}

	GLAttribute::GLAttribute(GLuint program, const char* name, GLenum type)
		: location_(-1), size_(1), type_(type)
	{
		std::size_t length = strnlen(name, MaxNameLength);
		DEATH_ASSERT(length < MaxNameLength);
		std::memcpy(name_, name, length);
		name_[length] = '\0';

		if (!HasReservedPrefix()) {
			// Unlike GL introspection, reflection also lists attributes the driver optimized out,
			// so a location of -1 is expected here and the attribute is simply never enabled
			location_ = glGetAttribLocation(program, name_);
		}
		GL_LOG_ERRORS();
	}

	GLenum GLAttribute::GetBasicType() const
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
			default:
				LOGW("No available case to handle type: {}", type_);
				return type_;
		}
	}

	std::int32_t GLAttribute::GetComponentCount() const
	{
		switch (type_) {
			case GL_BYTE:
			case GL_UNSIGNED_BYTE:
			case GL_SHORT:
			case GL_UNSIGNED_SHORT:
				return 1;
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
			default:
				LOGW("No available case to handle type: {}", type_);
				return 0;
		}
	}

	bool GLAttribute::HasReservedPrefix() const
	{
		return (MaxNameLength >= 3 && name_[0] == 'g' && name_[1] == 'l' && name_[2] == '_');
	}
}
