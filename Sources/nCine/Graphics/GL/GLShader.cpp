#include "GLShader.h"
#include "GLDebug.h"
#include "../../Application.h"
#include "../../IO/FileSystem.h"
#include "../../../Common.h"

#include <string>

namespace nCine
{
	namespace
	{
		static std::string patchLines;
	}

#if defined(NCINE_LOG)
	char GLShader::infoLogString_[MaxInfoLogLength];
#endif

	GLShader::GLShader(GLenum type)
		: glHandle_(0), status_(Status::NotCompiled)
	{
		if (patchLines.empty()) {
#if (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0) || defined(DEATH_TARGET_EMSCRIPTEN)
			patchLines.append("#version 300 es\n");
#else
			patchLines.append("#version 330\n");
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
			patchLines.append("#define DEATH_TARGET_EMSCRIPTEN\n");
#elif defined(DEATH_TARGET_ANDROID)
			patchLines.append("#define DEATH_TARGET_ANDROID\n");
#elif defined(WITH_ANGLE)
			patchLines.append("#define WITH_ANGLE\n");
#endif

			// ANGLE does not seem capable of handling large arrays that are not entirely filled.
			// A small array size will also make shader compilation a lot faster.
			if (theApplication().appConfiguration().fixedBatchSize > 0) {
				patchLines.append("#define WITH_FIXED_BATCH_SIZE\n");
				patchLines.append("#define BATCH_SIZE (");
				patchLines.append(std::to_string(theApplication().appConfiguration().fixedBatchSize));
				patchLines.append(")\n");
			}

			// Exclude patch lines when counting line numbers in info logs
			patchLines.append("#line 0\n");
		}

		glHandle_ = glCreateShader(type);
	}

	GLShader::GLShader(GLenum type, const StringView& filename)
		: GLShader(type)
	{
		loadFromFile(filename);
	}

	GLShader::~GLShader()
	{
		glDeleteShader(glHandle_);
	}

	void GLShader::loadFromString(const char* string)
	{
		ASSERT(string);

		const GLchar* source_lines[2] = { patchLines.data(), string };
		glShaderSource(glHandle_, 2, source_lines, nullptr);
	}

	void GLShader::loadFromFile(const StringView& filename)
	{
		std::unique_ptr<IFileStream> fileHandle = fs::Open(filename, FileAccessMode::Read);
		if (fileHandle->IsOpened()) {
			const GLint length = static_cast<int>(fileHandle->GetSize());
			std::string source(length, '\0');
			fileHandle->Read(source.data(), length);

			const GLchar* source_lines[2] = { patchLines.data(), source.data() };
			const GLint lengths[2] = { static_cast<GLint>(patchLines.length()), length };
			glShaderSource(glHandle_, 2, source_lines, lengths);

			setObjectLabel(filename.data());
		}
	}

	bool GLShader::compile(ErrorChecking errorChecking, bool logOnErrors)
	{
		glCompileShader(glHandle_);

		if (errorChecking == ErrorChecking::Immediate) {
			return checkCompilation(logOnErrors);
		} else {
			status_ = Status::CompiledWithDeferredChecks;
			return true;
		}
	}

	bool GLShader::checkCompilation(bool logOnErrors)
	{
		if (status_ == Status::Compiled) {
			return true;
		}

		GLint status = 0;
		glGetShaderiv(glHandle_, GL_COMPILE_STATUS, &status);
		if (status == GL_FALSE) {
#if defined(NCINE_LOG)
			if (logOnErrors) {
				GLint length = 0;
				glGetShaderiv(glHandle_, GL_INFO_LOG_LENGTH, &length);
				if (length > 0) {
					glGetShaderInfoLog(glHandle_, MaxInfoLogLength, &length, infoLogString_);
					// Trim whitespace - driver messages usually contain newline(s) at the end
					*(MutableStringView(infoLogString_).trimmed().end()) = '\0';
					LOGW_X("%s", infoLogString_);
				}
			}
#endif
			status_ = Status::CompilationFailed;
			return false;
		}

		status_ = Status::Compiled;
		return true;
	}

	void GLShader::setObjectLabel(const char* label)
	{
		GLDebug::objectLabel(GLDebug::LabelTypes::Shader, glHandle_, label);
	}
}
