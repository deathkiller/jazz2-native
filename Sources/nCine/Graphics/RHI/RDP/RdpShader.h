#pragma once

#include <cstdint>

#include <Containers/ArrayView.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::RDP
{
	/**
		@brief Shader-object stub of the RDP backend

		The RDP is fixed-function - the backend drives color-combiner/blender configurations resolved from
		the program's identity and the offline reflection instead of compiling GLSL - so this class carries
		no source and every operation succeeds trivially. It exists only to satisfy the `RHI::Shader`
		contract alias.
	*/
	class RdpShader
	{
	public:
		/** @brief Compilation status of the shader (always @ref Status::Compiled for the RDP backend) */
		enum class Status
		{
			NotCompiled,
			CompilationFailed,
			Compiled,
			CompiledWithDeferredChecks
		};

		/** @brief When the compilation status is checked (irrelevant for the RDP backend) */
		enum class ErrorChecking
		{
			Immediate,
			Deferred
		};

		explicit RdpShader(std::uint32_t type)
			: _type(type), _status(Status::Compiled) {}
		RdpShader(std::uint32_t type, StringView filename)
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
