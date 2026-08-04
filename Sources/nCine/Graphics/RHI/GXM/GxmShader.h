#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GXM
{
	/**
		@brief Shader-object stub of the sceGxm backend

		The Cg stage sources of every program variant are generated offline into
		`Shaders/Generated/CgGeneratedShaders.h` and compiled on the console by SceShaccCg when the program
		links (see @ref GxmShaderProgram), so this class carries no source of its own and every operation
		succeeds trivially, only to satisfy the `RHI::Shader` contract alias.
	*/
	class GxmShader
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

		explicit GxmShader(std::uint32_t type)
			: _type(type), _status(Status::Compiled) {}
		GxmShader(std::uint32_t type, StringView filename)
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
