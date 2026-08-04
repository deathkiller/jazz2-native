#include "GLUniformCache.h"
#include "GLUniform.h"
#include "../../../../Main.h"

#include <cstring> // for memcpy()

namespace nCine::RHI::GL
{
	GLUniformCache::GLUniformCache()
		: _uniform(nullptr), _dataPointer(nullptr), _isDirty(false)
	{
	}

	GLUniformCache::GLUniformCache(const GLUniform* uniform)
		: _uniform(uniform), _dataPointer(nullptr), _isDirty(false)
	{
		DEATH_ASSERT(uniform);
	}

	const GLfloat* GLUniformCache::GetFloatVector() const
	{
		DEATH_ASSERT(_uniform == nullptr || (_dataPointer != nullptr && CheckFloat()));
		const GLfloat* vec = nullptr;

		if (_dataPointer != nullptr) {
			vec = reinterpret_cast<GLfloat*>(_dataPointer);
		}
		return vec;
	}

	GLfloat GLUniformCache::GetFloatValue(std::uint32_t index) const
	{
		DEATH_ASSERT(_uniform == nullptr || (_dataPointer != nullptr && CheckFloat() && _uniform->GetComponentCount() > index));

		GLfloat value = 0.0f;

		if (_dataPointer != nullptr) {
			value = reinterpret_cast<const GLfloat*>(_dataPointer)[index];
		}
		return value;
	}

	const GLint* GLUniformCache::GetIntVector() const
	{
		DEATH_ASSERT(_uniform == nullptr || (_dataPointer != nullptr && CheckInt()));
		const GLint* vec = nullptr;

		if (_dataPointer != nullptr) {
			vec = reinterpret_cast<GLint*>(_dataPointer);
		}
		return vec;
	}

	GLint GLUniformCache::GetIntValue(std::uint32_t index) const
	{
		DEATH_ASSERT(_uniform == nullptr || (_dataPointer != nullptr && CheckInt() && _uniform->GetComponentCount() > index));
		GLint value = 0;

		if (_dataPointer != nullptr) {
			value = reinterpret_cast<const GLint*>(_dataPointer)[index];
		}
		return value;
	}

	bool GLUniformCache::SetFloatVector(const GLfloat* vec)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat()) {
			return false;
		}

		_isDirty = true;
		std::memcpy(_dataPointer, vec, sizeof(GLfloat) * _uniform->GetComponentCount());
		return true;
	}

	bool GLUniformCache::SetFloatValue(GLfloat v0)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat() || !CheckComponents(1)) {
			return false;
		}

		_isDirty = true;
		GLfloat* data = reinterpret_cast<GLfloat*>(_dataPointer);
		data[0] = v0;
		return true;
	}

	bool GLUniformCache::SetFloatValue(GLfloat v0, GLfloat v1)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat() || !CheckComponents(2)) {
			return false;
		}

		_isDirty = true;
		GLfloat* data = reinterpret_cast<GLfloat*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		return true;
	}

	bool GLUniformCache::SetFloatValue(GLfloat v0, GLfloat v1, GLfloat v2)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat() || !CheckComponents(3)) {
			return false;
		}

		_isDirty = true;
		GLfloat* data = reinterpret_cast<GLfloat*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		return true;
	}

	bool GLUniformCache::SetFloatValue(GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckFloat() || !CheckComponents(4)) {
			return false;
		}

		_isDirty = true;
		GLfloat* data = reinterpret_cast<GLfloat*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		data[3] = v3;
		return true;
	}

	bool GLUniformCache::SetIntVector(const GLint* vec)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt()) {
			return false;
		}

		_isDirty = true;
		std::memcpy(_dataPointer, vec, sizeof(GLint) * _uniform->GetComponentCount());
		return true;
	}

	bool GLUniformCache::SetIntValue(GLint v0)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt() || !CheckComponents(1)) {
			return false;
		}

		_isDirty = true;
		GLint* data = reinterpret_cast<GLint*>(_dataPointer);
		data[0] = v0;
		return true;
	}

	bool GLUniformCache::SetIntValue(GLint v0, GLint v1)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt() || !CheckComponents(2)) {
			return false;
		}

		_isDirty = true;
		GLint* data = reinterpret_cast<GLint*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		return true;
	}

	bool GLUniformCache::SetIntValue(GLint v0, GLint v1, GLint v2)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt() || !CheckComponents(3)) {
			return false;
		}

		_isDirty = true;
		GLint* data = reinterpret_cast<GLint*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		return true;
	}

	bool GLUniformCache::SetIntValue(GLint v0, GLint v1, GLint v2, GLint v3)
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !CheckInt() || !CheckComponents(4)) {
			return false;
		}

		_isDirty = true;
		GLint* data = reinterpret_cast<GLint*>(_dataPointer);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		data[3] = v3;
		return true;
	}

	bool GLUniformCache::CommitValue()
	{
		DEATH_ASSERT(_uniform == nullptr || _dataPointer != nullptr);
		if (_uniform == nullptr || _dataPointer == nullptr || !_isDirty) {
			return false;
		}

		// The uniform must not belong to any uniform block
		DEATH_ASSERT(_uniform->GetBlockIndex() == -1);

		const GLint location = _uniform->GetLocation();
		switch (_uniform->GetGLType()) {
			case GL_FLOAT:
				glUniform1fv(location, 1, reinterpret_cast<const GLfloat*>(_dataPointer));
				break;
			case GL_FLOAT_VEC2:
				glUniform2fv(location, 1, reinterpret_cast<const GLfloat*>(_dataPointer));
				break;
			case GL_FLOAT_VEC3:
				glUniform3fv(location, 1, reinterpret_cast<const GLfloat*>(_dataPointer));
				break;
			case GL_FLOAT_VEC4:
				glUniform4fv(location, 1, reinterpret_cast<const GLfloat*>(_dataPointer));
				break;
			case GL_INT:
				glUniform1iv(location, 1, reinterpret_cast<const GLint*>(_dataPointer));
				break;
			case GL_INT_VEC2:
				glUniform2iv(location, 1, reinterpret_cast<const GLint*>(_dataPointer));
				break;
			case GL_INT_VEC3:
				glUniform3iv(location, 1, reinterpret_cast<const GLint*>(_dataPointer));
				break;
			case GL_INT_VEC4:
				glUniform4iv(location, 1, reinterpret_cast<const GLint*>(_dataPointer));
				break;
			case GL_FLOAT_MAT2:
				glUniformMatrix2fv(location, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(_dataPointer));
				break;
			case GL_FLOAT_MAT3:
				glUniformMatrix3fv(location, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(_dataPointer));
				break;
			case GL_FLOAT_MAT4:
				glUniformMatrix4fv(location, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(_dataPointer));
				break;
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
				glUniform1iv(location, 1, reinterpret_cast<const GLint*>(_dataPointer));
				break;
			default:
				LOGW("No available case to handle type: {}", _uniform->GetGLType());
				break;
		}

		_isDirty = false;
		return true;
	}

	bool GLUniformCache::CheckFloat() const
	{
		if (_uniform->GetBasicType() != GL_FLOAT) {
			LOGE("Uniform \"{}\" is not floating point", _uniform->GetName());
			return false;
		} else {
			return true;
		}
	}

	bool GLUniformCache::CheckInt() const
	{
		if (_uniform->GetBasicType() != GL_INT &&
#if !defined(DEATH_TARGET_VITA)	// vitaGL declares neither GL_BOOL nor GL_SAMPLER_3D
			_uniform->GetBasicType() != GL_BOOL &&
#endif
#if defined(RHI_GL_PROFILE_CORE) // not available in OpenGL ES
			_uniform->GetBasicType() != GL_SAMPLER_1D &&
#endif
			_uniform->GetBasicType() != GL_SAMPLER_2D &&
#if !defined(DEATH_TARGET_VITA)
			_uniform->GetBasicType() != GL_SAMPLER_3D &&
#endif
			_uniform->GetBasicType() != GL_SAMPLER_CUBE
#if defined(RHI_GL_PROFILE_CORE) || GL_ES_VERSION_3_2
			&& _uniform->GetBasicType() != GL_SAMPLER_BUFFER
#endif
		) {
			LOGE("Uniform \"{}\" is not integer", _uniform->GetName());
			return false;
		} else {
			return true;
		}
	}

	bool GLUniformCache::CheckComponents(std::uint32_t requiredComponents) const
	{
		if (_uniform->GetComponentCount() != requiredComponents) {
			LOGE("Uniform \"{}\" has {} components, not {}", _uniform->GetName(), _uniform->GetComponentCount(), requiredComponents);
			return false;
		} else {
			return true;
		}
	}
}
