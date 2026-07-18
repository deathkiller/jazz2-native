#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RhiD3D11
{
	/**
		@brief Shader-object stub of the Direct3D 11 backend

		The offline emitter already produces `HlslVsSource`/`HlslFsSource` per program-variant, and the
		program object compiles them via d3dcompiler. This class therefore carries no source and every
		operation succeeds trivially, only to satisfy the `Rhi::Shader` contract alias.
	*/
	class D3D11Shader
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

		explicit D3D11Shader(std::uint32_t type)
			: type_(type), status_(Status::Compiled) {}
		D3D11Shader(std::uint32_t type, StringView filename)
			: type_(type), status_(Status::Compiled) {
			static_cast<void>(filename);
		}

		inline std::uint32_t GetGLHandle() const {
			return 0;
		}
		inline Status GetStatus() const {
			return status_;
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
		std::uint32_t type_;
		Status status_;
	};
}
