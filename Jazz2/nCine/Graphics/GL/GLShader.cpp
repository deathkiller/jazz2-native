#include "GLShader.h"
#include "GLDebug.h"
#include "../../IO/FileSystem.h"
#include "../../../Common.h"

#include <string>

#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(WITH_ANGLE)
#	include "../../Application.h"
#endif

namespace nCine
{
	namespace
	{
		static std::string patchLines;
	}

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////
#if defined(ENABLE_LOG)
	char GLShader::infoLogString_[MaxInfoLogLength];
#endif

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	GLShader::GLShader(GLenum type)
		: glHandle_(0), status_(Status::NOT_COMPILED)
	{
		if (patchLines.empty()) {
#if (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0) || defined(DEATH_TARGET_EMSCRIPTEN)
			patchLines.append("#version 300 es\n");
#else
			patchLines.append("#version 330\n");
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN)
			patchLines.append("#define __EMSCRIPTEN__\n");
#elif defined(DEATH_TARGET_ANDROID)
			patchLines.append("#define __ANDROID__\n");
#elif defined(WITH_ANGLE)
			patchLines.append("#define WITH_ANGLE\n");
#endif

#if defined(DEATH_TARGET_EMSCRIPTEN) || defined(WITH_ANGLE)
			// ANGLE does not seem capable of handling large arrays that are not entirely filled.
			// A small array size will also make shader compilation a lot faster.
			if (theApplication().appConfiguration().fixedBatchSize > 0) {
				patchLines.append("#define WITH_FIXED_BATCH_SIZE\n");
				//patchLines.formatAppend("#define BATCH_SIZE (%u)\n", theApplication().appConfiguration().fixedBatchSize);
				patchLines.append("#define BATCH_SIZE (");
				patchLines.append(std::to_string(theApplication().appConfiguration().fixedBatchSize));
				patchLines.append(")\n");
			}
#endif
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

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

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

		if (errorChecking == ErrorChecking::IMMEDIATE) {
			return checkCompilation(logOnErrors);
		} else {
			status_ = Status::COMPILED_WITH_DEFERRED_CHECKS;
			return true;
		}
	}

	bool GLShader::checkCompilation(bool logOnErrors)
	{
		if (status_ == Status::COMPILED)
			return true;

		GLint status = 0;
		glGetShaderiv(glHandle_, GL_COMPILE_STATUS, &status);
		if (status == GL_FALSE) {
#if defined(ENABLE_LOG)
			if (logOnErrors) {
				GLint length = 0;
				glGetShaderiv(glHandle_, GL_INFO_LOG_LENGTH, &length);
				if (length > 0) {
					glGetShaderInfoLog(glHandle_, MaxInfoLogLength, &length, infoLogString_);
					LOGW_X("%s", infoLogString_);
				}
			}
#endif
			status_ = Status::COMPILATION_FAILED;
			return false;
		}

		status_ = Status::COMPILED;
		return true;
	}

	void GLShader::setObjectLabel(const char* label)
	{
		GLDebug::objectLabel(GLDebug::LabelTypes::SHADER, glHandle_, label);
	}

}
