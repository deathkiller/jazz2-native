#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../../Main.h"

namespace nCine::RHI::GL
{
	class GLUniform;

	/**
		@brief Caches the value of a single uniform and uploads it to the shader
		
		Holds a host-side data pointer for one @ref GLUniform and the `Set*` methods that write
		values into it. A dirty flag tracks whether the cached value differs from what was last
		uploaded; @ref CommitValue() then flushes it to the GL with the appropriate `glUniform*`
		call, avoiding redundant uploads. The cache must reference a uniform that is not part of
		a uniform block.
	*/
	class GLUniformCache
	{
	public:
		/** @brief Creates an empty cache not associated with any uniform */
		GLUniformCache();
		/** @brief Creates a cache for the specified uniform */
		explicit GLUniformCache(const GLUniform* uniform);

		/** @brief Returns the uniform described by this cache */
		inline const GLUniform* GetUniform() const {
			return _uniform;
		}
		/** @brief Returns the host-side data pointer holding the cached value */
		inline const GLubyte* GetDataPointer() const {
			return _dataPointer;
		}
		/** @brief Sets the host-side data pointer holding the cached value */
		inline void SetDataPointer(GLubyte* dataPointer) {
			_dataPointer = dataPointer;
		}

		/** @brief Returns the cached value as a float vector, or `nullptr` if unavailable */
		const GLfloat* GetFloatVector() const;
		/** @brief Returns the float component at the specified index */
		GLfloat GetFloatValue(std::uint32_t index) const;
		/** @brief Returns the cached value as an integer vector, or `nullptr` if unavailable */
		const GLint* GetIntVector() const;
		/** @brief Returns the integer component at the specified index */
		GLint GetIntValue(std::uint32_t index) const;

		/** @brief Sets the cached value from a float vector and returns whether it succeeded */
		bool SetFloatVector(const GLfloat* vec);
		/** @brief Sets a single-component float value and returns whether it succeeded */
		bool SetFloatValue(GLfloat v0);
		/** @brief Sets a two-component float value and returns whether it succeeded */
		bool SetFloatValue(GLfloat v0, GLfloat v1);
		/** @brief Sets a three-component float value and returns whether it succeeded */
		bool SetFloatValue(GLfloat v0, GLfloat v1, GLfloat v2);
		/** @brief Sets a four-component float value and returns whether it succeeded */
		bool SetFloatValue(GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
		/** @brief Sets the cached value from an integer vector and returns whether it succeeded */
		bool SetIntVector(const GLint* vec);
		/** @brief Sets a single-component integer value and returns whether it succeeded */
		bool SetIntValue(GLint v0);
		/** @brief Sets a two-component integer value and returns whether it succeeded */
		bool SetIntValue(GLint v0, GLint v1);
		/** @brief Sets a three-component integer value and returns whether it succeeded */
		bool SetIntValue(GLint v0, GLint v1, GLint v2);
		/** @brief Sets a four-component integer value and returns whether it succeeded */
		bool SetIntValue(GLint v0, GLint v1, GLint v2, GLint v3);

		/** @brief Returns whether the cached value still needs to be committed to the GL */
		inline bool IsDirty() const {
			return _isDirty;
		}
		/** @brief Sets the dirty flag controlling whether the value is committed */
		inline void SetDirty(bool isDirty) {
			_isDirty = isDirty;
		}
		/**
		 * @brief Uploads the cached value to the shader if it is dirty
		 *
		 * Issues the matching `glUniform*` call for the uniform type and clears the dirty flag.
		 * Does nothing and returns `false` if there is no value, no data pointer or the value is
		 * not dirty.
		 */
		bool CommitValue();

	private:
		const GLUniform* _uniform;
		GLubyte* _dataPointer;
		// A flag to signal if the uniform needs to be committed
		bool _isDirty;

		bool CheckFloat() const;
		bool CheckInt() const;
		bool CheckComponents(std::uint32_t requiredComponents) const;
	};

}
