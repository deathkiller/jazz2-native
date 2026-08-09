#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::RSX
{
	/**
		@brief Shader-object stub of the RSX backend

		The RSX has no runtime shader compiler at all, so there is even less for this class to do than for
		its sceGxm counterpart: the microcode of every program variant is compiled offline by cgcomp and
		embedded in `Shaders/Generated/RsxGeneratedShaders.h`, and @ref RsxShaderProgram merely looks its
		pair up there and hands it to the GPU. This class therefore carries no source of its own and every
		operation succeeds trivially, only to satisfy the `RHI::Shader` contract alias.
	*/
	class RsxShader
	{
	public:
		/** @brief Compilation status of the shader (always @ref Status::Compiled for this stub) */
		enum class Status
		{
			NotCompiled,
			CompilationFailed,
			Compiled,
			CompiledWithDeferredChecks
		};

		/** @brief When the compilation status is checked (irrelevant for this stub) */
		enum class ErrorChecking
		{
			Immediate,
			Deferred
		};

		explicit RsxShader(std::uint32_t type)
			: _type(type), _status(Status::Compiled) {}
		RsxShader(std::uint32_t type, StringView filename)
			: _type(type), _status(Status::Compiled) {
			static_cast<void>(filename);
		}

		inline std::uint32_t GetGLHandle() const {
			return 0;
		}
		inline Status GetStatus() const {
			return _status;
		}

		bool LoadFromString(StringView string) {
			static_cast<void>(string);
			return true;
		}
		bool LoadFromStrings(ArrayView<const StringView> strings) {
			static_cast<void>(strings);
			return true;
		}
		bool LoadFromFile(StringView filename) {
			static_cast<void>(filename);
			return true;
		}

		bool Compile(ErrorChecking errorChecking, bool logOnErrors) {
			static_cast<void>(errorChecking);
			static_cast<void>(logOnErrors);
			return true;
		}
		bool CheckCompilation(bool logOnErrors) {
			static_cast<void>(logOnErrors);
			return true;
		}

		void SetObjectLabel(StringView label) {
			static_cast<void>(label);
		}

	private:
		std::uint32_t _type;
		Status _status;
	};
}
