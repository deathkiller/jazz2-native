#include "BinaryShaderCache.h"
#include "IGfxCapabilities.h"
#include "../ServiceLocator.h"
#include "../Base/Algorithms.h"
#include "../IO/FileSystem.h"
#include "../IO/IFileStream.h"
#include "../Base/HashFunctions.h"
#include "../../Common.h"

namespace nCine
{
	namespace
	{
		constexpr uint64_t HashSeed = 0x01000193811C9DC5;

		constexpr uint8_t B64index[256] = {
			0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
			0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
			0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  62, 63, 63, 62, 63, 52, 53, 54, 55,
			56, 57, 58, 59, 60, 61, 0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
			7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0,
			0,  0,  0,  63, 0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
			41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51
		};

		constexpr char B64chars[] = {
			'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
			'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
			'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
			'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '-'
		};

		unsigned int bufferSize = 0;
		std::unique_ptr<uint8_t[]> bufferPtr;
	}

	BinaryShaderCache::BinaryShaderCache(const StringView& path)
		: isAvailable_(false), platformHash_(0)
	{
		if (path.empty()) {
			LOGD_X("Binary shader cache is disabled");
			return;
		}

		const IGfxCapabilities& gfxCaps = theServiceLocator().gfxCapabilities();
#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_UNIX)
		const bool isSupported = gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::ARB_GET_PROGRAM_BINARY) ||
								 gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::OES_GET_PROGRAM_BINARY);
#else
		const bool isSupported = gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::ARB_GET_PROGRAM_BINARY);
#endif
		if (!isSupported) {
			LOGW_X("GL_ARB_get_program_binary extensions not supported, binary shader cache is disabled");
			return;
		}

#if defined(WITH_OPENGLES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_UNIX)
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

		// For a stable hash, the OpenGL strings need to be copied so that padding bytes can be set to zero
		char platformString[512];
		std::memset(platformString, 0, sizeof(platformString));
#if defined(DEATH_TARGET_WINDOWS)
		strncpy_s(platformString, infoStrings.renderer, sizeof(platformString));
#else
		strncpy(platformString, infoStrings.renderer, sizeof(platformString));
#endif
		platformHash_ += fasthash64(platformString, strnlen(platformString, sizeof(platformString)), HashSeed);

		std::memset(platformString, 0, sizeof(platformString));
#if defined(DEATH_TARGET_WINDOWS)
		strncpy_s(platformString, infoStrings.glVersion, sizeof(platformString));
#else
		strncpy(platformString, infoStrings.glVersion, sizeof(platformString));
#endif
		platformHash_ += fasthash64(platformString, strnlen(platformString, sizeof(platformString)), HashSeed);

		path_ = path;
		fs::CreateDirectories(path_);

		bufferSize = 64 * 1024;
		bufferPtr = std::make_unique<uint8_t[]>(bufferSize);

		const bool pathExists = fs::IsDirectory(path_);
		isAvailable_ = (isSupported && pathExists);
	}

	String BinaryShaderCache::getCachedShaderPath(const char* shaderName)
	{
		if (!isAvailable_) {
			return { };
		}

		uint8_t inputBuffer[64];
		char outputBuffer[(sizeof(inputBuffer) * 3) / 2 + 1];

		if (shaderName == nullptr) {
			return { };
		}

		std::size_t shaderNameLength = strlen(shaderName);
		if (shaderNameLength == 0 || 8 + shaderNameLength > sizeof(inputBuffer)) {
			return { };
		}

		inputBuffer[0] = (platformHash_ & 0xff);
		inputBuffer[1] = ((platformHash_ >> 8) & 0xff);
		inputBuffer[2] = ((platformHash_ >> 16) & 0xff);
		inputBuffer[3] = ((platformHash_ >> 24) & 0xff);
		inputBuffer[4] = ((platformHash_ >> 32) & 0xff);
		inputBuffer[5] = ((platformHash_ >> 40) & 0xff);
		inputBuffer[6] = ((platformHash_ >> 48) & 0xff);
		inputBuffer[7] = ((platformHash_ >> 56) & 0xff);

		std::memcpy(&inputBuffer[8], shaderName, shaderNameLength);

		std::size_t outputLength = base64Encode(inputBuffer, 8 + shaderNameLength, outputBuffer, sizeof(outputBuffer));
		if (outputLength == 0) {
			return { };
		}

		return fs::JoinPath(path_, StringView(outputBuffer, outputLength));
	}

	bool BinaryShaderCache::loadFromCache(const char* shaderName, uint64_t shaderVersion, GLShaderProgram* program, GLShaderProgram::Introspection introspection)
	{
		String cachePath = getCachedShaderPath(shaderName);
		if (cachePath.empty()) {
			return false;
		}

		std::unique_ptr<IFileStream> fileHandle = fs::Open(cachePath, FileAccessMode::Read);
		const long int fileSize = fileHandle->GetSize();
		if (fileSize <= 28 || fileSize > 8 * 1024 * 1024) {
			return false;
		}

		if (bufferSize < fileSize) {
			bufferSize = fileSize;
			bufferPtr = std::make_unique<uint8_t[]>(bufferSize);
		}

		fileHandle->Read(bufferPtr.get(), fileSize);
		fileHandle->Close();

		uint64_t signature = *(uint64_t*)&bufferPtr[0];
		uint64_t cachedShaderVersion = *(uint64_t*)&bufferPtr[8];

		// Shader version must be the same
		if (signature != 0x20AA8C9FF0BFBBEF || cachedShaderVersion != shaderVersion) {
			return false;
		}

		int32_t batchSize = *(int32_t*)&bufferPtr[16];
		uint32_t binaryFormat = *(uint32_t*)&bufferPtr[20];
		int32_t bufferLength = *(int32_t*)&bufferPtr[24];
		void* buffer = &bufferPtr[28];

		if (bufferLength <= 0 || bufferLength > fileSize - 28) {
			return false;
		}

		_glProgramBinary(program->glHandle(), binaryFormat, buffer, bufferLength);
		program->setBatchSize(batchSize);
		return program->finalizeAfterLinking(introspection);
	}

	bool BinaryShaderCache::saveToCache(const char* shaderName, uint64_t shaderVersion, GLShaderProgram* program)
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
			bufferPtr = std::make_unique<uint8_t[]>(bufferSize);
		} 

		length = 0;
		unsigned int binaryFormat = 0;
		_glGetProgramBinary(program->glHandle(), bufferSize, &length, &binaryFormat, bufferPtr.get());
		if (length <= 0 || length > bufferSize) {
			return false;
		}

		std::unique_ptr<IFileStream> fileHandle = fs::Open(cachePath, FileAccessMode::Write);
		if (!fileHandle->IsOpened()) {
			return false;
		}

		fileHandle->WriteValue<uint64_t>(0x20AA8C9FF0BFBBEF);
		fileHandle->WriteValue<uint64_t>(shaderVersion);
		fileHandle->WriteValue<int32_t>(program->batchSize());
		fileHandle->WriteValue<uint32_t>(binaryFormat);
		fileHandle->WriteValue<int32_t>(length);
		fileHandle->Write(bufferPtr.get(), length);

		return true;
	}

	void BinaryShaderCache::prune()
	{
		uint8_t inputBuffer[64];

		fs::Directory dir(path_);
		while (const StringView shaderPath = dir.GetNext()) {
			StringView filename = fs::GetFileNameWithoutExtension(shaderPath);
			if (base64Decode(filename, inputBuffer, sizeof(inputBuffer)) >= sizeof(uint64_t)) {
				uint64_t platformHash = (uint64_t)inputBuffer[0] | ((uint64_t)inputBuffer[1] << 8) | ((uint64_t)inputBuffer[2] << 16) | ((uint64_t)inputBuffer[3] << 24) |
					((uint64_t)inputBuffer[4] << 32) | ((uint64_t)inputBuffer[5] << 40) | ((uint64_t)inputBuffer[6] << 48) | ((uint64_t)inputBuffer[7] << 56);

				if (platformHash != platformHash_) {
					fs::RemoveFile(shaderPath);
				}
			}
		}
	}

	void BinaryShaderCache::clear()
	{
		fs::Directory dir(path_);
		while (const StringView shaderPath = dir.GetNext()) {
			fs::RemoveFile(shaderPath);
		}
	}

	/*! \return True if the path is a writable directory */
	bool BinaryShaderCache::setPath(const StringView& path)
	{
		if (!fs::IsDirectory(path) || !fs::IsWritable(path)) {
			return false;
		}

		path_ = path;
		return true;
	}

	std::size_t BinaryShaderCache::base64Decode(const StringView& src, uint8_t* dst, std::size_t dstLength)
	{
		std::size_t srcLength = src.size();;
		uint8_t* p = (uint8_t*)src.data();
		bool pad = (srcLength > 0 && (srcLength % 4 || p[srcLength - 1] == '='));
		const std::size_t L = ((srcLength + 3) / 4 - pad) * 4;

		if (dstLength < L / 4 * 3) {
			return 0;
		}

		size_t j = 0;
		for (std::size_t i = 0; i < L; i += 4) {
			int32_t n = B64index[p[i]] << 18 | B64index[p[i + 1]] << 12 | B64index[p[i + 2]] << 6 | B64index[p[i + 3]];
			dst[j++] = n >> 16;
			dst[j++] = n >> 8 & 0xFF;
			dst[j++] = n & 0xFF;
		}
		if (pad) {
			int32_t n = B64index[p[L]] << 18 | B64index[p[L + 1]] << 12;
			dst[j++] = n >> 16;

			if (srcLength > L + 2 && p[L + 2] != '=') {
				n |= B64index[p[L + 2]] << 6;
				dst[j++] = (n >> 8 & 0xFF);
			}
		}
		return j;
	}

	std::size_t BinaryShaderCache::base64Encode(const uint8_t* src, std::size_t srcLength, char* dst, std::size_t dstLength)
	{
		std::size_t requiredLength = 4 * ((srcLength + 2) / 3);
		if (requiredLength > dstLength) {
			return 0;
		}

		std::size_t j = 0;
		for (std::size_t i = 0; i < srcLength;) {
			uint32_t octet1 = (i < srcLength ? (uint8_t)src[i++] : 0);
			uint32_t octet2 = (i < srcLength ? (uint8_t)src[i++] : 0);
			uint32_t octet3 = (i < srcLength ? (uint8_t)src[i++] : 0);

			uint32_t triple = (octet1 << 0x10) + (octet2 << 0x08) + octet3;

			dst[j++] = B64chars[(triple >> (3 * 6)) & 0x3F];
			dst[j++] = B64chars[(triple >> (2 * 6)) & 0x3F];
			dst[j++] = B64chars[(triple >> (1 * 6)) & 0x3F];
			dst[j++] = B64chars[(triple >> (0 * 6)) & 0x3F];
		}

		if (j < dstLength) {
			dst[j] = '\0';
		}

		return j;
	}
}