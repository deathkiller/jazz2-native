#pragma once

#include "GL/GLShaderProgram.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

#if defined(DEATH_TARGET_WINDOWS) && defined(DEATH_TARGET_X86) && defined(DEATH_TARGET_32BIT)
#	define __GLAPIENTRY __stdcall
#else
#	define __GLAPIENTRY
#endif

namespace nCine
{
	/**
		@brief Caches compiled OpenGL shader programs in binary form on disk
		
		Stores and retrieves linked shader program binaries (via `glGetProgramBinary`/`glProgramBinary`) so
		that shaders compiled in a previous run can be reloaded without recompiling. Cached binaries are keyed
		by shader name, a version stamp and a platform hash; binaries from other platforms can be pruned.
	*/
	class BinaryShaderCache
	{
	public:
		BinaryShaderCache(StringView path);

		BinaryShaderCache(const BinaryShaderCache&) = delete;
		BinaryShaderCache& operator=(const BinaryShaderCache&) = delete;

		/** @brief Returns whether binary shaders are supported and the cache can be used */
		inline bool IsAvailable() const {
			return isAvailable_;
		}

		/** @brief Returns the hash value that identifies the current OpenGL platform */
		inline std::uint64_t GetPlatformHash() const {
			return platformHash_;
		}

		/** @brief Returns the on-disk path of the cached binary for the specified shader */
		String GetCachedShaderPath(const char* shaderName);

		/** @brief Loads a binary into the program from the cache, returning `true` on success */
		bool LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, GLShaderProgram* program, GLShaderProgram::Introspection introspection);
		/** @brief Saves the program binary to the cache, returning `true` on success */
		bool SaveToCache(const char* shaderName, std::uint64_t shaderVersion, GLShaderProgram* program);

		/** @brief Deletes all binary shaders that don't belong to this platform from the cache directory */
		std::uint32_t Prune();
		/** @brief Deletes all binary shaders from the cache directory */
		bool Clear();

		/** @brief Returns the current cache directory for binary shaders */
		inline const StringView Path() {
			return path_;
		}
		/** @brief Sets a new directory as the cache for binary shaders */
		bool SetPath(StringView path);

	private:
		using glGetProgramBinary_t = void(__GLAPIENTRY*)(GLuint program, GLsizei bufSize, GLsizei* length, GLenum* binaryFormat, void* binary);
		using glProgramBinary_t = void(__GLAPIENTRY*)(GLuint program, GLenum binaryFormat, const void* binary, GLsizei length);

		/** @brief Whether the OpenGL context supports binary shaders and the cache is available */
		bool isAvailable_;

		/** @brief The hash value that identifies a specific OpenGL platform */
		std::uint64_t platformHash_;

		/** @brief The cache directory containing the binary shaders */
		String path_;

		glGetProgramBinary_t _glGetProgramBinary;
		glProgramBinary_t _glProgramBinary;
		GLenum _glProgramBinaryLength;
	};
}
