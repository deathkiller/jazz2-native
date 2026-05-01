#if defined(WITH_RHI_GL)
#include "Shader.h"
#include "Debug.h"
#include "../../RenderResources.h"
#include "../../../Application.h"
#include "../../../../Main.h"

#include <string>

#include <IO/FileSystem.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine::RHI
{
	namespace
	{
#if (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0) || defined(DEATH_TARGET_EMSCRIPTEN)
		static constexpr StringView CommonShaderVersion = "#version 300 es\n"_s;
#else
		static constexpr StringView CommonShaderVersion = "#version 330\n"_s;
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
		static constexpr StringView CommonShaderDefines = "#define DEATH_TARGET_EMSCRIPTEN\n#line 0\n"_s;
#elif defined(DEATH_TARGET_ANDROID)
		static constexpr StringView CommonShaderDefines = "#define DEATH_TARGET_ANDROID\n#line 0\n"_s;
#elif defined(WITH_ANGLE)
		static constexpr StringView CommonShaderDefines = "#define WITH_ANGLE\n#line 0\n"_s;
#else
		static constexpr StringView CommonShaderDefines = "#line 0\n"_s;
#endif
	}

	Shader::Shader(GLenum type)
		: glHandle_(0), status_(Status::NotCompiled)
	{
		glHandle_ = glCreateShader(type);
	}

	Shader::Shader(GLenum type, StringView filename)
		: Shader(type)
	{
		LoadFromFile(filename);
	}

	Shader::~Shader()
	{
		glDeleteShader(glHandle_);
	}

	bool Shader::LoadFromString(StringView string)
	{
		return LoadFromStringsAndFile(arrayView({ string }), {});
	}
	
	bool Shader::LoadFromStringAndFile(StringView string, StringView filename)
	{
		return LoadFromStringsAndFile(arrayView({ string }), filename);
	}

	bool Shader::LoadFromStrings(ArrayView<const StringView> strings)
	{
		return LoadFromStringsAndFile(strings, {});
	}

	bool Shader::LoadFromStringsAndFile(ArrayView<const StringView> strings, StringView filename)
	{
		if (strings.empty() && filename.empty()) {
			return false;
		}
		
		SmallVector<const char*, 10> sourceStrings;
		SmallVector<GLint, 10> sourceLengths;

		sourceStrings.push_back(CommonShaderVersion.data());
		sourceLengths.push_back(static_cast<GLint>(CommonShaderVersion.size()));
		sourceStrings.push_back(CommonShaderDefines.data());
		sourceLengths.push_back(static_cast<GLint>(CommonShaderDefines.size()));

		for (auto string : strings) {
			sourceStrings.push_back(string.data());
			sourceLengths.push_back(static_cast<GLint>(string.size()));
		}

		String fileSource;
		if (!filename.empty()) {
			std::unique_ptr<Stream> fileHandle = fs::Open(filename, FileAccess::Read);
			if (!fileHandle->IsValid()) {
				return false;
			}
			
			const GLint fileLength = static_cast<int>(fileHandle->GetSize());
			fileSource = String{NoInit, static_cast<std::size_t>(fileLength)};
			fileHandle->Read(fileSource.data(), fileLength);
			
			sourceStrings.push_back(fileSource.data());
			sourceLengths.push_back(static_cast<GLint>(fileLength));

			SetObjectLabel(filename.data());
		}

		std::size_t count = sourceStrings.size();
		glShaderSource(glHandle_, GLsizei(count), sourceStrings.data(), sourceLengths.data());
		return (count > 1);
	}
	
	bool Shader::LoadFromFile(StringView filename)
	{
		return LoadFromStringsAndFile({}, filename);
	}

	bool Shader::Compile(ErrorChecking errorChecking, bool logOnErrors)
	{
		glCompileShader(glHandle_);

		if (errorChecking == ErrorChecking::Immediate) {
			return CheckCompilation(logOnErrors);
		} else {
			status_ = Status::CompiledWithDeferredChecks;
			return true;
		}
	}

	bool Shader::CheckCompilation(bool logOnErrors)
	{
		if (status_ == Status::Compiled) {
			return true;
		}

		GLint status = GL_FALSE;
		glGetShaderiv(glHandle_, GL_COMPILE_STATUS, &status);
		if (status == GL_FALSE) {
#if defined(DEATH_TRACE)
			if (logOnErrors) {
				GLint length = 0;
				glGetShaderiv(glHandle_, GL_INFO_LOG_LENGTH, &length);
				if (length > 0) {
					static char buffer[2048];
					glGetShaderInfoLog(glHandle_, sizeof(buffer), &length, buffer);
					// Trim whitespace - driver messages usually contain newline(s) at the end
					*(MutableStringView(buffer).trimmed().end()) = '\0';
					LOGW("Shader: {}", buffer);
				}
				DEATH_ASSERT_BREAK();
			}
#endif
			status_ = Status::CompilationFailed;
			return false;
		}

		status_ = Status::Compiled;
		return true;
	}

	void Shader::SetObjectLabel(StringView label)
	{
		Debug::SetObjectLabel(Debug::LabelTypes::Shader, glHandle_, label);
	}
}

#endif // defined(WITH_RHI_GL)
