#include "GLShader.h"
#include "GLDebug.h"
#include "../RenderResources.h"
#include "../BinaryShaderCache.h"
#include "../../Application.h"
#include "../../IO/FileSystem.h"
#include "../../../Common.h"

#include <string>

namespace nCine
{
	namespace
	{
		static std::string patchLines;

		const char* typeToString(GLenum type)
		{
			switch (type) {
				case GL_VERTEX_SHADER: return "Vertex";
				case GL_FRAGMENT_SHADER: return "Fragment";
				default: return "Unknown";
			}
		}
	}

#if defined(NCINE_LOG)
	char GLShader::infoLogString_[MaxInfoLogLength];
#endif

	GLShader::GLShader(GLenum type)
		: type_(type), glHandle_(0), sourceHash_(0), status_(Status::NotCompiled)
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

	bool GLShader::loadFromString(const char* string)
	{
		const char* strings[2] = { string, nullptr };
		return loadFromStringsAndFile(strings, { });
	}
	
	bool GLShader::loadFromStringAndFile(const char* string, const StringView& filename)
	{
		const char* strings[2] = { string, nullptr };
		return loadFromStringsAndFile(strings, filename);
	}

	bool GLShader::loadFromStringsAndFile(const char** strings, const StringView& filename)
	{
		const bool noStrings = (strings == nullptr || strings[0] == nullptr);
		if (noStrings && filename.empty()) {
			return false;
		}
		
		SmallVector<const char*, 4> sourceStrings;
		SmallVector<GLint, 4> sourceLengths;
		
		sourceStrings.push_back(patchLines.data());
		sourceLengths.push_back(static_cast<GLint>(patchLines.length()));
		
		if (!noStrings) {
			for (uint32_t i = 0; strings[i] != nullptr; i++) {
				sourceStrings.push_back(strings[i]);
				const unsigned long sourceLength = strnlen(strings[i], MaxShaderSourceLength);
				sourceLengths.push_back(sourceLength);
			}
		}

		String fileSource;
		if (!filename.empty()) {
			std::unique_ptr<IFileStream> fileHandle = fs::Open(filename, FileAccessMode::Read);
			if (!fileHandle->IsOpened()) {
				return false;
			}
			
			const GLint fileLength = static_cast<int>(fileHandle->GetSize());
			fileSource = String(NoInit, fileLength);
			fileHandle->Read(fileSource.data(), fileLength);
			
			sourceStrings.push_back(fileSource.data());
			sourceLengths.push_back(static_cast<GLint>(fileLength));

			setObjectLabel(filename.data());
		}

		size_t count = sourceStrings.size();
		sourceHash_ = RenderResources::binaryShaderCache().hashSources(count, sourceStrings.data(), sourceLengths.data());
		LOGD_X("%s Shader %u - hash: 0x%016lx", typeToString(type_), glHandle_, sourceHash_);
		glShaderSource(glHandle_, count, sourceStrings.data(), sourceLengths.data());

		return (count > 1);
	}
	
	bool GLShader::loadFromFile(const StringView& filename)
	{
		return loadFromStringsAndFile(nullptr, filename);
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
					LOGW_X("%s Shader: %s", typeToString(type_), infoLogString_);
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
