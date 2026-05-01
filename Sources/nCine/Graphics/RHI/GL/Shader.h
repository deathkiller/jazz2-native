#pragma once

#if defined(WITH_RHI_GL)

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../../Main.h"

#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI
{
	/// Handles OpenGL shader objects
	class Shader
	{
	public:
		enum class Status
		{
			NotCompiled,
			CompilationFailed,
			Compiled,
			CompiledWithDeferredChecks
		};

		enum class ErrorChecking
		{
			Immediate,
			Deferred
		};

		explicit Shader(GLenum type);
		Shader(GLenum type, StringView filename);
		~Shader();

		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;

		inline GLuint GetGLHandle() const {
			return glHandle_;
		}
		inline Status GetStatus() const {
			return status_;
		}

		/// Loads a shader from the given string
		bool LoadFromString(StringView string);
		/// Loads a shader from the given string and then append the specified file
		bool LoadFromStringAndFile(StringView string, StringView filename);
		/// Loads a shader by concatenating the given strings in order
		bool LoadFromStrings(ArrayView<const StringView> strings);
		/// Loads a shader by concatenating the given strings in order, then appending the specified file
		bool LoadFromStringsAndFile(ArrayView<const StringView> strings, StringView filename);
		/// Loads a shader from the specified file
		bool LoadFromFile(StringView filename);

		bool Compile(ErrorChecking errorChecking, bool logOnErrors);
		bool CheckCompilation(bool logOnErrors);

		void SetObjectLabel(StringView label);

	private:
		static constexpr std::uint32_t MaxShaderSourceLength = 32 * 1024;

		GLuint glHandle_;
		Status status_;
	};
}

#endif // defined(WITH_RHI_GL)
