#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RhiSoftware
{
	/**
		@brief Shader-object stub of the software backend

		The software backend does not compile GLSL — it drives hand-written C++ effects selected from the
		offline reflection instead — so this class carries no source and every operation succeeds trivially.
		It exists only to satisfy the `Rhi::Shader` contract alias.
	*/
	class SwShader
	{
	public:
		/** @brief Compilation status of the shader (always @ref Status::Compiled for the software backend) */
		enum class Status
		{
			NotCompiled,
			CompilationFailed,
			Compiled,
			CompiledWithDeferredChecks
		};

		/** @brief When the compilation status is checked (irrelevant for the software backend) */
		enum class ErrorChecking
		{
			Immediate,
			Deferred
		};

		explicit SwShader(std::uint32_t type)
			: type_(type), status_(Status::Compiled) {}
		SwShader(std::uint32_t type, StringView filename)
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
