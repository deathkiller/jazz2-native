#include "GLAttribute.h"
#include "GLDebug.h"
#include "../../../../Main.h"

#include <cstring>

namespace nCine::RHI::GL
{
	GLAttribute::GLAttribute()
		: _location(-1), _size(0), _type(GL_FLOAT)
	{
		_name[0] = '\0';
	}

	GLAttribute::GLAttribute(GLuint program, GLuint index)
		: _location(-1), _size(0), _type(GL_FLOAT)
	{
		GLsizei length;
		glGetActiveAttrib(program, index, MaxNameLength, &length, &_size, &_type, _name);
		DEATH_ASSERT(length <= MaxNameLength);

		if (!HasReservedPrefix()) {
			_location = glGetAttribLocation(program, _name);
			if (_location == -1) {
				LOGW("Attribute location not found for attribute \"{}\" ({}) in shader program {}", _name, index, program);
			}
		}
		GL_LOG_ERRORS();
	}

	GLAttribute::GLAttribute(GLuint program, const char* name, GLenum type)
		: _location(-1), _size(1), _type(type)
	{
		std::size_t length = strnlen(name, MaxNameLength);
		DEATH_ASSERT(length < MaxNameLength);
		std::memcpy(_name, name, length);
		_name[length] = '\0';

		if (!HasReservedPrefix()) {
			// Unlike GL introspection, reflection also lists attributes the driver optimized out,
			// so a location of -1 is expected here and the attribute is simply never enabled
			_location = glGetAttribLocation(program, _name);
		}
		GL_LOG_ERRORS();
	}

	GLenum GLAttribute::GetBasicType() const
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
			default:
				LOGW("No available case to handle type: {}", _type);
				return _type;
		}
	}

	std::int32_t GLAttribute::GetComponentCount() const
	{
		switch (_type) {
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
				LOGW("No available case to handle type: {}", _type);
				return 0;
		}
	}

	bool GLAttribute::HasReservedPrefix() const
	{
		return (MaxNameLength >= 3 && _name[0] == 'g' && _name[1] == 'l' && _name[2] == '_');
	}
}
