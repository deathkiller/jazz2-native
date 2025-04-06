#include "GLUniformCache.h"
#include "GLUniform.h"
#include "../../../Main.h"

#include <cstring> // for memcpy()

namespace nCine
{
	GLUniformCache::GLUniformCache()
		: uniform_(nullptr), dataPointer_(nullptr), isDirty_(false)
	{
	}

	GLUniformCache::GLUniformCache(const GLUniform* uniform)
		: uniform_(uniform), dataPointer_(nullptr), isDirty_(false)
	{
		ASSERT(uniform);
	}

	const GLfloat* GLUniformCache::GetFloatVector() const
	{
		ASSERT(uniform_ == nullptr || (dataPointer_ != nullptr && CheckFloat()));
		const GLfloat* vec = nullptr;

		if (dataPointer_ != nullptr) {
			vec = reinterpret_cast<GLfloat*>(dataPointer_);
		}
		return vec;
	}

	GLfloat GLUniformCache::GetFloatValue(std::uint32_t index) const
	{
		ASSERT(uniform_ == nullptr || (dataPointer_ != nullptr && CheckFloat() && uniform_->GetComponentCount() > index));

		GLfloat value = 0.0f;

		if (dataPointer_ != nullptr) {
			value = static_cast<GLfloat>(*dataPointer_);
		}
		return value;
	}

	const GLint* GLUniformCache::GetIntVector() const
	{
		ASSERT(uniform_ == nullptr || (dataPointer_ != nullptr && CheckInt()));
		const GLint* vec = nullptr;

		if (dataPointer_ != nullptr) {
			vec = reinterpret_cast<GLint*>(dataPointer_);
		}
		return vec;
	}

	GLint GLUniformCache::GetIntValue(std::uint32_t index) const
	{
		ASSERT(uniform_ == nullptr || (dataPointer_ != nullptr && CheckInt() && uniform_->GetComponentCount() > index));
		GLint value = 0;

		if (dataPointer_ != nullptr) {
			value = static_cast<GLint>(*dataPointer_);
		}
		return value;
	}

	bool GLUniformCache::SetFloatVector(const GLfloat* vec)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat()) {
			return false;
		}

		isDirty_ = true;
		std::memcpy(dataPointer_, vec, sizeof(GLfloat) * uniform_->GetComponentCount());
		return true;
	}

	bool GLUniformCache::SetFloatValue(GLfloat v0)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat() || !CheckComponents(1)) {
			return false;
		}

		isDirty_ = true;
		GLfloat* data = reinterpret_cast<GLfloat*>(dataPointer_);
		data[0] = v0;
		return true;
	}

	bool GLUniformCache::SetFloatValue(GLfloat v0, GLfloat v1)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat() || !CheckComponents(2)) {
			return false;
		}

		isDirty_ = true;
		GLfloat* data = reinterpret_cast<GLfloat*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		return true;
	}

	bool GLUniformCache::SetFloatValue(GLfloat v0, GLfloat v1, GLfloat v2)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat() || !CheckComponents(3)) {
			return false;
		}

		isDirty_ = true;
		GLfloat* data = reinterpret_cast<GLfloat*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		return true;
	}

	bool GLUniformCache::SetFloatValue(GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckFloat() || !CheckComponents(4)) {
			return false;
		}

		isDirty_ = true;
		GLfloat* data = reinterpret_cast<GLfloat*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		data[3] = v3;
		return true;
	}

	bool GLUniformCache::SetIntVector(const GLint* vec)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt()) {
			return false;
		}

		isDirty_ = true;
		std::memcpy(dataPointer_, vec, sizeof(GLint) * uniform_->GetComponentCount());
		return true;
	}

	bool GLUniformCache::SetIntValue(GLint v0)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt() || !CheckComponents(1)) {
			return false;
		}

		isDirty_ = true;
		GLint* data = reinterpret_cast<GLint*>(dataPointer_);
		data[0] = v0;
		return true;
	}

	bool GLUniformCache::SetIntValue(GLint v0, GLint v1)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt() || !CheckComponents(2)) {
			return false;
		}

		isDirty_ = true;
		GLint* data = reinterpret_cast<GLint*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		return true;
	}

	bool GLUniformCache::SetIntValue(GLint v0, GLint v1, GLint v2)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt() || !CheckComponents(3)) {
			return false;
		}

		isDirty_ = true;
		GLint* data = reinterpret_cast<GLint*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		return true;
	}

	bool GLUniformCache::SetIntValue(GLint v0, GLint v1, GLint v2, GLint v3)
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !CheckInt() || !CheckComponents(4)) {
			return false;
		}

		isDirty_ = true;
		GLint* data = reinterpret_cast<GLint*>(dataPointer_);
		data[0] = v0;
		data[1] = v1;
		data[2] = v2;
		data[3] = v3;
		return true;
	}

	bool GLUniformCache::CommitValue()
	{
		ASSERT(uniform_ == nullptr || dataPointer_ != nullptr);
		if (uniform_ == nullptr || dataPointer_ == nullptr || !isDirty_) {
			return false;
		}

		// The uniform must not belong to any uniform block
		ASSERT(uniform_->GetBlockIndex() == -1);

		const GLint location = uniform_->GetLocation();
		switch (uniform_->GetType()) {
			case GL_FLOAT:
				glUniform1fv(location, 1, reinterpret_cast<const GLfloat*>(dataPointer_));
				break;
			case GL_FLOAT_VEC2:
				glUniform2fv(location, 1, reinterpret_cast<const GLfloat*>(dataPointer_));
				break;
			case GL_FLOAT_VEC3:
				glUniform3fv(location, 1, reinterpret_cast<const GLfloat*>(dataPointer_));
				break;
			case GL_FLOAT_VEC4:
				glUniform4fv(location, 1, reinterpret_cast<const GLfloat*>(dataPointer_));
				break;
			case GL_INT:
				glUniform1iv(location, 1, reinterpret_cast<const GLint*>(dataPointer_));
				break;
			case GL_INT_VEC2:
				glUniform2iv(location, 1, reinterpret_cast<const GLint*>(dataPointer_));
				break;
			case GL_INT_VEC3:
				glUniform3iv(location, 1, reinterpret_cast<const GLint*>(dataPointer_));
				break;
			case GL_INT_VEC4:
				glUniform4iv(location, 1, reinterpret_cast<const GLint*>(dataPointer_));
				break;
			case GL_FLOAT_MAT2:
				glUniformMatrix2fv(location, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(dataPointer_));
				break;
			case GL_FLOAT_MAT3:
				glUniformMatrix3fv(location, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(dataPointer_));
				break;
			case GL_FLOAT_MAT4:
				glUniformMatrix4fv(location, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(dataPointer_));
				break;
#if !defined(WITH_OPENGLES) // not available in OpenGL ES
			case GL_SAMPLER_1D:
#endif
			case GL_SAMPLER_2D:
			case GL_SAMPLER_3D:
			case GL_SAMPLER_CUBE:
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
			case GL_SAMPLER_BUFFER:
#endif
				glUniform1iv(location, 1, reinterpret_cast<const GLint*>(dataPointer_));
				break;
			default:
				LOGW("No available case to handle type: %u", uniform_->GetType());
				break;
		}

		isDirty_ = false;
		return true;
	}

	bool GLUniformCache::CheckFloat() const
	{
		if (uniform_->GetBasicType() != GL_FLOAT) {
			LOGE("Uniform \"%s\" is not floating point", uniform_->GetName());
			return false;
		} else {
			return true;
		}
	}

	bool GLUniformCache::CheckInt() const
	{
		if (uniform_->GetBasicType() != GL_INT &&
			uniform_->GetBasicType() != GL_BOOL &&
#if !defined(WITH_OPENGLES) // not available in OpenGL ES
			uniform_->GetBasicType() != GL_SAMPLER_1D &&
#endif
			uniform_->GetBasicType() != GL_SAMPLER_2D &&
			uniform_->GetBasicType() != GL_SAMPLER_3D &&
			uniform_->GetBasicType() != GL_SAMPLER_CUBE
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
			&& uniform_->GetBasicType() != GL_SAMPLER_BUFFER
#endif
		) {
			LOGE("Uniform \"%s\" is not integer", uniform_->GetName());
			return false;
		} else {
			return true;
		}
	}

	bool GLUniformCache::CheckComponents(std::uint32_t requiredComponents) const
	{
		if (uniform_->GetComponentCount() != requiredComponents) {
			LOGE("Uniform \"%s\" has %u components, not %u", uniform_->GetName(), uniform_->GetComponentCount(), requiredComponents);
			return false;
		} else {
			return true;
		}
	}
}
