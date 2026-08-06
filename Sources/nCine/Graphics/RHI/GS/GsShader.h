#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GS
{
	/**
		@brief Shader-object stub of the GS backend

		The Graphics Synthesizer is fixed-function - it has one texture function and one blend equation per
		draw, and no programmable stage at all - so the backend dispatches the effect functions transpiled
		from the shaders' `fixed_function` blocks instead of compiling anything. This class carries no source
		and every operation succeeds trivially; it exists only to satisfy the `RHI::Shader` contract alias.
	*/
	class GsShader
	{
	public:
		/** @brief Compilation status of the shader (always @ref Status::Compiled for the GS backend) */
		enum class Status
		{
			NotCompiled,
			CompilationFailed,
			Compiled,
			CompiledWithDeferredChecks
		};

		/** @brief When the compilation status is checked (irrelevant for the GS backend) */
		enum class ErrorChecking
		{
			Immediate,
			Deferred
		};

		explicit GsShader(std::uint32_t type)
			: _type(type), _status(Status::Compiled) {}
		GsShader(std::uint32_t type, StringView filename)
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
