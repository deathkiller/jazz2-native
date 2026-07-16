#include "GLShader.h"
#include "GLDebug.h"
#include "../../RenderResources.h"
#include "../../../Application.h"
#include "../../../../Main.h"

#include <string>

#include <IO/FileSystem.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine::RhiGL
{
	namespace
	{
#if defined(RHI_GL_PROFILE_ES2)
		// Real OpenGL|ES 2.0 profile: ESSL 100 (attribute/varying/gl_FragColor, no UBOs, no gl_VertexID).
		// The engine feeds the ProgramVariant's *100 (Essl100Emitter) sources on this path.
		static constexpr StringView CommonShaderVersion = "#version 100\n"_s;
#elif (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0) || defined(DEATH_TARGET_EMSCRIPTEN)
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

	GLShader::GLShader(GLenum type)
		: glHandle_(0), status_(Status::NotCompiled)
	{
		glHandle_ = glCreateShader(type);
	}

	GLShader::GLShader(GLenum type, StringView filename)
		: GLShader(type)
	{
		LoadFromFile(filename);
	}

	GLShader::~GLShader()
	{
		glDeleteShader(glHandle_);
	}

	bool GLShader::LoadFromString(StringView string)
	{
		return LoadFromStringsAndFile(arrayView({ string }), {});
	}
	
	bool GLShader::LoadFromStringAndFile(StringView string, StringView filename)
	{
		return LoadFromStringsAndFile(arrayView({ string }), filename);
	}

	bool GLShader::LoadFromStrings(ArrayView<const StringView> strings)
	{
		return LoadFromStringsAndFile(strings, {});
	}

	bool GLShader::LoadFromStringsAndFile(ArrayView<const StringView> strings, StringView filename)
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
	
	bool GLShader::LoadFromFile(StringView filename)
	{
		return LoadFromStringsAndFile({}, filename);
	}

	bool GLShader::Compile(ErrorChecking errorChecking, bool logOnErrors)
	{
		glCompileShader(glHandle_);

		if (errorChecking == ErrorChecking::Immediate) {
			return CheckCompilation(logOnErrors);
		} else {
			status_ = Status::CompiledWithDeferredChecks;
			return true;
		}
	}

	bool GLShader::CheckCompilation(bool logOnErrors)
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
#if !defined(RHI_GL_PROFILE_ES2)
				// The desktop ES2 test profile keeps effect shaders that fail the strict ESSL 100 compiler
				// non-fatal (logged, returns false) so the app still boots and the core sprite/menu path can be
				// validated; the GL 3.3 / ES 3.0 path keeps the immediate debug break on a shader error.
				DEATH_ASSERT_BREAK();
#endif
			}
#endif
			status_ = Status::CompilationFailed;
			return false;
		}

		status_ = Status::Compiled;
		return true;
	}

	void GLShader::SetObjectLabel(StringView label)
	{
		GLDebug::SetObjectLabel(GLDebug::LabelTypes::Shader, glHandle_, label);
	}
}
