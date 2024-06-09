#include "BinaryShaderCache.h"
#include "IGfxCapabilities.h"
#include "../ServiceLocator.h"
#include "../Base/Algorithms.h"
#include "../Base/HashFunctions.h"
#include "../../Common.h"

#include <IO/FileSystem.h>
#include <IO/Stream.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	namespace
	{
		std::int32_t bufferSize = 0;
		std::unique_ptr<std::uint8_t[]> bufferPtr;
	}

	BinaryShaderCache::BinaryShaderCache(const StringView& path)
		: isAvailable_(false), platformHash_(0)
	{
		if (path.empty()) {
			LOGD("Binary shader cache is disabled");
			return;
		}

		const IGfxCapabilities& gfxCaps = theServiceLocator().GetGfxCapabilities();
#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
		const bool isSupported = gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::ARB_GET_PROGRAM_BINARY) ||
								 gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::OES_GET_PROGRAM_BINARY);
#else
		const bool isSupported = gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::ARB_GET_PROGRAM_BINARY);
#endif
		if (!isSupported) {
			LOGW("GL_ARB_get_program_binary extensions not supported, binary shader cache is disabled");
			return;
		}

#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX) && (!defined(DEATH_TARGET_WINDOWS_RT) || defined(WITH_ANGLE))
		if (gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::OES_GET_PROGRAM_BINARY)) {
			_glGetProgramBinary = glGetProgramBinaryOES;
			_glProgramBinary = glProgramBinaryOES;
			_glProgramBinaryLength = GL_PROGRAM_BINARY_LENGTH_OES;
		} else
#endif
		{
			_glGetProgramBinary = glGetProgramBinary;
			_glProgramBinary = glProgramBinary;
			_glProgramBinaryLength = GL_PROGRAM_BINARY_LENGTH;
		}

		const IGfxCapabilities::GlInfoStrings& infoStrings = gfxCaps.glInfoStrings();

		platformHash_ += CityHash64(infoStrings.renderer, strlen(infoStrings.renderer));
		platformHash_ += CityHash64(infoStrings.glVersion, strlen(infoStrings.glVersion));

		char platformHashString[24];
		std::int32_t platformHashLength = formatString(platformHashString, sizeof(platformHashString), "%016llx", platformHash_);
		path_ = fs::CombinePath(path, { platformHashString, (std::size_t)platformHashLength });
		fs::CreateDirectories(path_);

		bufferSize = 64 * 1024;
		bufferPtr = std::make_unique<std::uint8_t[]>(bufferSize);

		const bool pathExists = fs::DirectoryExists(path_);
		isAvailable_ = (isSupported && pathExists);
	}

	String BinaryShaderCache::getCachedShaderPath(const char* shaderName)
	{
		if (!isAvailable_ || shaderName == nullptr || shaderName[0] == '\0') {
			return { };
		}

		std::uint64_t shaderNameHash = CityHash64(shaderName, strlen(shaderName));

		char filename[32];
		std::int32_t filenameLength = formatString(filename, sizeof(filename), "%016llx.shader", shaderNameHash);
		return fs::CombinePath(path_, { filename, (std::size_t)filenameLength });
	}

	bool BinaryShaderCache::loadFromCache(const char* shaderName, std::uint64_t shaderVersion, GLShaderProgram* program, GLShaderProgram::Introspection introspection)
	{
		String cachePath = getCachedShaderPath(shaderName);
		if (cachePath.empty()) {
			return false;
		}

		std::unique_ptr<Stream> fileHandle = fs::Open(cachePath, FileAccess::Read);
		const std::int32_t fileSize = fileHandle->GetSize();
		if (fileSize <= 28 || fileSize > 8 * 1024 * 1024) {
			return false;
		}

		if (bufferSize < fileSize) {
			bufferSize = fileSize;
			bufferPtr = std::make_unique<std::uint8_t[]>(bufferSize);
		}

		fileHandle->Read(bufferPtr.get(), fileSize);
		fileHandle->Dispose();

		std::uint64_t signature = *(std::uint64_t*)&bufferPtr[0];
		std::uint64_t cachedShaderVersion = *(std::uint64_t*)&bufferPtr[8];

		// Shader version must be the same
		if (signature != 0x20AA8C9FF0BFBBEF || cachedShaderVersion != shaderVersion) {
			return false;
		}

		std::int32_t batchSize = *(std::int32_t*)&bufferPtr[16];
		std::uint32_t binaryFormat = *(std::uint32_t*)&bufferPtr[20];
		std::int32_t bufferLength = *(std::int32_t*)&bufferPtr[24];
		void* buffer = &bufferPtr[28];

		if (bufferLength <= 0 || bufferLength > fileSize - 28) {
			return false;
		}

		_glProgramBinary(program->glHandle(), binaryFormat, buffer, bufferLength);
		program->setBatchSize(batchSize);
		return program->finalizeAfterLinking(introspection);
	}

	bool BinaryShaderCache::saveToCache(const char* shaderName, std::uint64_t shaderVersion, GLShaderProgram* program)
	{
		String cachePath = getCachedShaderPath(shaderName);
		if (cachePath.empty()) {
			return false;
		}

		GLint length = 0;
		glGetProgramiv(program->glHandle(), _glProgramBinaryLength, &length);
		if (length <= 0) {
			return false;
		}

		if (bufferSize < length) {
			bufferSize = length;
			bufferPtr = std::make_unique<std::uint8_t[]>(bufferSize);
		} 

		length = 0;
		unsigned int binaryFormat = 0;
		_glGetProgramBinary(program->glHandle(), bufferSize, &length, &binaryFormat, bufferPtr.get());
		if (length <= 0 || length > bufferSize) {
			return false;
		}

		std::unique_ptr<Stream> fileHandle = fs::Open(cachePath, FileAccess::Write);
		if (!fileHandle->IsValid()) {
			return false;
		}

		fileHandle->WriteValue<std::uint64_t>(0x20AA8C9FF0BFBBEF);
		fileHandle->WriteValue<std::uint64_t>(shaderVersion);
		fileHandle->WriteValue<std::int32_t>(program->batchSize());
		fileHandle->WriteValue<std::uint32_t>(binaryFormat);
		fileHandle->WriteValue<std::int32_t>(length);
		fileHandle->Write(bufferPtr.get(), length);

		return true;
	}

	std::uint32_t BinaryShaderCache::prune()
	{
		auto platformHashString = fs::GetFileName(path_);

		std::uint32_t filesRemoved = 0;
		for (auto shaderPath : fs::Directory(fs::GetDirectoryName(path_))) {
			if (fs::DirectoryExists(shaderPath)) {
				StringView filename = fs::GetFileName(shaderPath);
				if (filename != platformHashString) {
					fs::RemoveDirectoryRecursive(shaderPath);
					filesRemoved++;
				}
			} else if (fs::GetExtension(shaderPath) == "shader"_s) {
				fs::RemoveFile(shaderPath);
				filesRemoved++;
			}
		}

		return filesRemoved;
	}

	bool BinaryShaderCache::clear()
	{
		bool success = fs::RemoveDirectoryRecursive(path_);
		fs::CreateDirectories(path_);
		return success;
	}

	bool BinaryShaderCache::setPath(const StringView& path)
	{
		if (!fs::DirectoryExists(path) || !fs::IsWritable(path)) {
			return false;
		}

		path_ = path;
		return true;
	}
}