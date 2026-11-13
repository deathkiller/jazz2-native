#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GX2
{
	/**
		@brief Shader-object stub of the GX2 backend

		The game does not compile shaders on the console: every program variant is compiled offline by
		CafeGLSL and embedded in a generated table, which @ref Gx2ShaderProgram looks up by program identity.
		This class therefore carries no source of its own and every operation succeeds trivially, only to
		satisfy the `RHI::Shader` contract alias.
	*/
	class Gx2Shader
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

		explicit Gx2Shader(std::uint32_t type)
			: _type(type), _status(Status::Compiled) {}
		Gx2Shader(std::uint32_t type, StringView filename)
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
