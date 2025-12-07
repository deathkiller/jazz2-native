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
	/// Manages the cache of binary OpenGL shader programs
	class BinaryShaderCache
	{
	public:
		BinaryShaderCache(StringView path);

		BinaryShaderCache(const BinaryShaderCache&) = delete;
		BinaryShaderCache& operator=(const BinaryShaderCache&) = delete;

		inline bool IsAvailable() const {
			return isAvailable_;
		}

		inline std::uint64_t GetPlatformHash() const {
			return platformHash_;
		}

		String GetCachedShaderPath(const char* shaderName);

		bool LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, GLShaderProgram* program, GLShaderProgram::Introspection introspection);
		bool SaveToCache(const char* shaderName, std::uint64_t shaderVersion, GLShaderProgram* program);

		/// Deletes all binary shaders that not belong to this platform from the cache directory
		std::uint32_t Prune();
		/// Deletes all binary shaders from the cache directory
		bool Clear();

		/// Returns the current cache directory for binary shaders
		inline const StringView Path() {
			return path_;
		}
		/// Sets a new directory as the cache for binary shaders
		bool SetPath(StringView path);

	private:
		using glGetProgramBinary_t = void(__GLAPIENTRY*)(GLuint program, GLsizei bufSize, GLsizei* length, GLenum* binaryFormat, void* binary);
		using glProgramBinary_t = void(__GLAPIENTRY*)(GLuint program, GLenum binaryFormat, const void* binary, GLsizei length);

		/// A flag that indicates if the OpenGL context supports binary shaders and the cache is available
		bool isAvailable_;

		/// The hash value that identifies a specific OpenGL platform
		std::uint64_t platformHash_;

		/// The cache directory containing the binary shaders
		String path_;

		glGetProgramBinary_t _glGetProgramBinary;
		glProgramBinary_t _glProgramBinary;
		GLenum _glProgramBinaryLength;

		static std::uint32_t GetCurrentShaderFlags();
	};
}
