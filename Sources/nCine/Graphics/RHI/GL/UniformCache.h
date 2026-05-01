#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../../Main.h"

namespace nCine::RHI
{
	class Uniform;

	/// Caches a uniform value and then updates it in the shader
	class UniformCache
	{
	public:
		UniformCache();
		explicit UniformCache(const Uniform* uniform);

		inline const Uniform* GetUniform() const {
			return uniform_;
		}
		inline const GLubyte* GetDataPointer() const {
			return dataPointer_;
		}
		inline void SetDataPointer(GLubyte* dataPointer) {
			dataPointer_ = dataPointer;
		}

		const GLfloat* GetFloatVector() const;
		GLfloat GetFloatValue(std::uint32_t index) const;
		const GLint* GetIntVector() const;
		GLint GetIntValue(std::uint32_t index) const;

		bool SetFloatVector(const GLfloat* vec);
		bool SetFloatValue(GLfloat v0);
		bool SetFloatValue(GLfloat v0, GLfloat v1);
		bool SetFloatValue(GLfloat v0, GLfloat v1, GLfloat v2);
		bool SetFloatValue(GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
		bool SetIntVector(const GLint* vec);
		bool SetIntValue(GLint v0);
		bool SetIntValue(GLint v0, GLint v1);
		bool SetIntValue(GLint v0, GLint v1, GLint v2);
		bool SetIntValue(GLint v0, GLint v1, GLint v2, GLint v3);

		inline bool IsDirty() const {
			return isDirty_;
		}
		inline void SetDirty(bool isDirty) {
			isDirty_ = isDirty;
		}
		bool CommitValue();

	private:
		const Uniform* uniform_;
		GLubyte* dataPointer_;
		/// A flag to signal if the uniform needs to be committed
		bool isDirty_;

		bool CheckFloat() const;
		bool CheckInt() const;
		bool CheckComponents(std::uint32_t requiredComponents) const;
	};

}

#endif // defined(WITH_RHI_GL)
