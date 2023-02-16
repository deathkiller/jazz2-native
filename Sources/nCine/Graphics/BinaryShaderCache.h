#pragma once

#include "GL/GLShaderProgram.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine
{
	/// The class that manages the cache of binary OpenGL shader programs
	class BinaryShaderCache
	{
	public:
		BinaryShaderCache(const StringView& path);

		inline bool isAvailable() const {
			return isAvailable_;
		}

		inline uint64_t platformHash() const {
			return platformHash_;
		}

		String getCachedShaderPath(const char* shaderName);

		bool loadFromCache(const char* shaderName, uint64_t shaderVersion, GLShaderProgram* program, GLShaderProgram::Introspection introspection);
		bool saveToCache(const char* shaderName, uint64_t shaderVersion, GLShaderProgram* program);

		/// Deletes all binary shaders that not belong to this platform from the cache directory
		void prune();
		/// Deletes all binary shaders from the cache directory
		void clear();

		/// Returns the current cache directory for binary shaders
		inline const StringView path() {
			return path_;
		}
		/// Sets a new directory as the cache for binary shaders
		bool setPath(const StringView& path);

	private:
		using glGetProgramBinary_t = void(__stdcall*)(GLuint program, GLsizei bufSize, GLsizei* length, GLenum* binaryFormat, void* binary);
		using glProgramBinary_t = void(__stdcall*)(GLuint program, GLenum binaryFormat, const void* binary, GLsizei length);

		/// A flag that indicates if the OpenGL context supports binary shaders and the cache is available
		bool isAvailable_;

		/// The hash value that identifies a specific OpenGL platform
		uint64_t platformHash_;

		/// The cache directory containing the binary shaders
		String path_;

		glGetProgramBinary_t _glGetProgramBinary;
		glProgramBinary_t _glProgramBinary;
		GLenum _glProgramBinaryLength;

		static std::size_t base64Decode(const StringView& src, uint8_t* dst, std::size_t dstLength);
		static std::size_t base64Encode(const uint8_t* src, std::size_t srcLength, char* dst, std::size_t dstLength);

		/// Deleted copy constructor
		BinaryShaderCache(const BinaryShaderCache&) = delete;
		/// Deleted assignment operator
		BinaryShaderCache& operator=(const BinaryShaderCache&) = delete;
	};
}
