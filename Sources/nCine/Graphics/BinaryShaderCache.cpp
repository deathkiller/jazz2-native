#include "BinaryShaderCache.h"
#include "IGfxCapabilities.h"
#include "../ServiceLocator.h"
#include "../Base/Algorithms.h"
#include "../IO/FileSystem.h"
#include "../IO/IFileStream.h"
#include "../Base/HashFunctions.h"
#include "../../Common.h"

#include <cstdint>
#include <cstdlib> // for `strtoull()`

namespace nCine
{
	namespace
	{
		constexpr uint64_t HashSeed = 0x01000193811C9DC5;
		constexpr char ShaderFilenameFormat[] = "%016llx_%08x_%016llx.bin";

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
		const bool isSupported = gfxCaps.hasExtension(IGfxCapabilities::GLExtensions::ARB_GET_PROGRAM_BINARY);
		if (!isSupported) {
			LOGW_X("GL_ARB_get_program_binary extensions not supported, the binary shader cache is not enabled");
			return;
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
		if (!fs::IsDirectory(path_)) {
			fs::CreateDirectories(path_);
		} else {
			collectStatistics();
		}

		bufferSize = 64 * 1024;
		bufferPtr = std::make_unique<uint8_t[]>(bufferSize);

		const bool dirExists = fs::IsDirectory(path_);
		isAvailable_ = (isSupported && dirExists);
	}

	uint64_t BinaryShaderCache::hashSources(unsigned int count, const char** strings, int* lengths) const
	{
		uint64_t hash = 0;
		for (unsigned int i = 0; i < count; i++) {
			ASSERT(strings[i] != nullptr);
			ASSERT(lengths[i] > 0);
			if (strings[i] != nullptr && lengths[i] > 0) {
				const unsigned int length = static_cast<unsigned int>(lengths[i]);
				// Align the length to 64 bits, plus 7 bytes read after that
				const unsigned int paddedLength = length + 8 - (length % 8) + 7;
				// Recycling the loading binary buffer for zero-padded shader sources
				if (bufferSize < paddedLength) {
					bufferSize = paddedLength;
					bufferPtr = std::make_unique<uint8_t[]>(bufferSize);
				}

				for (unsigned int j = 0; j < length; j++) {
					bufferPtr[j] = strings[i][j];
				}
				// Set padding bytes to zero for a deterministic hash
				for (unsigned int j = length; j < paddedLength; j++) {
					bufferPtr[j] = '\0';
				}
				hash += fasthash64(bufferPtr.get(), length, HashSeed);
			}
		}

		return hash;
	}

	unsigned int BinaryShaderCache::binarySize(uint32_t format, uint64_t hash)
	{
		if (!isAvailable_) {
			return 0;
		}

		char shaderFilename[64];
		formatString(shaderFilename, sizeof(shaderFilename), ShaderFilenameFormat, platformHash_, format, hash);
		String shaderPath = fs::JoinPath(path_, shaderFilename);

		unsigned int fileSize = 0;
		if (fs::IsFile(shaderPath)) {
			fileSize = fs::FileSize(shaderPath);
		}
		return fileSize;
	}

	const void* BinaryShaderCache::loadFromCache(uint32_t format, uint64_t hash)
	{
		if (!isAvailable_) {
			return 0;
		}

		char shaderFilename[64];
		formatString(shaderFilename, sizeof(shaderFilename), ShaderFilenameFormat, platformHash_, format, hash);
		String shaderPath = fs::JoinPath(path_, shaderFilename);

		bool fileRead = false;
		if (fs::IsFile(shaderPath)) {
			std::unique_ptr<IFileStream> fileHandle = fs::Open(shaderPath, FileAccessMode::Read);
			const long int fileSize = fileHandle->GetSize();
			if (fileSize > 0 && fileSize < 8 * 1024 * 1024) {
				if (bufferSize < fileSize) {
					bufferSize = fileSize;
					bufferPtr = std::make_unique<uint8_t[]>(bufferSize);
				}

				fileHandle->Read(bufferPtr.get(), fileSize);
				fileHandle->Close();

				LOGI_X("Loaded binary shader \"%s\" from cache", shaderFilename);
				statistics_.LoadedShaders++;
				fileRead = true;
			}
		}

		return (fileRead ? bufferPtr.get() : nullptr);
	}

	bool BinaryShaderCache::saveToCache(int length, const void* buffer, uint32_t format, uint64_t hash)
	{
		if (!isAvailable_ || length <= 0) {
			return 0;
		}

		bool fileWritten = false;
		char shaderFilename[64];
		formatString(shaderFilename, sizeof(shaderFilename), ShaderFilenameFormat, platformHash_, format, hash);
		String shaderPath = fs::JoinPath(path_, shaderFilename);
		if (!fs::IsFile(shaderPath)) {
			std::unique_ptr<IFileStream> fileHandle = fs::Open(shaderPath, FileAccessMode::Write);
			if (fileHandle->IsOpened()) {
				fileHandle->Write(buffer, length);
				fileHandle->Close();

				LOGI_X("Saved binary shader \"%s\" to cache", shaderFilename);
				statistics_.SavedShaders++;
				statistics_.PlatformFilesCount++;
				statistics_.PlatformBytesCount += length;
				statistics_.TotalFilesCount++;
				statistics_.TotalBytesCount += length;
				fileWritten = true;
			}
		}

		return fileWritten;
	}

	void BinaryShaderCache::prune()
	{
		uint64_t platformHash = 0;

		fs::Directory dir(path_);
		while (const StringView shaderPath = dir.GetNext()) {
			if (parseShaderPath(shaderPath, &platformHash, nullptr, nullptr)) {
				// Deleting only binary shaders with different platform hashes
				if (platformHash != platformHash_) {
					const int64_t fileSize = fs::FileSize(shaderPath);
					fs::RemoveFile(shaderPath);

					ASSERT(statistics_.TotalFilesCount > 0);
					ASSERT(statistics_.TotalBytesCount >= fileSize);
					statistics_.TotalFilesCount--;
					statistics_.TotalBytesCount -= fileSize;
				}
			}
		}
	}

	void BinaryShaderCache::clear()
	{
		fs::Directory dir(path_);
		while (const StringView shaderPath = dir.GetNext()) {
			// Deleting all binary shaders
			if (isShaderPath(shaderPath)) {
				fs::RemoveFile(shaderPath);
			}
		}

		clearStatistics();
	}

	/*! \return True if the path is a writable directory */
	bool BinaryShaderCache::setPath(const StringView& path)
	{
		if (fs::IsDirectory(path) && fs::IsWritable(path)) {
			path_ = path;
			collectStatistics();
			return true;
		}
		return false;
	}

	void BinaryShaderCache::collectStatistics()
	{
		clearStatistics();
		uint64_t platformHash = 0;

		fs::Directory dir(path_);
		while (const StringView shaderPath = dir.GetNext()) {
			if (parseShaderPath(shaderPath, &platformHash, nullptr, nullptr)) {
				const int64_t fileSize = fs::FileSize(shaderPath);

				if (platformHash == platformHash_) {
					statistics_.PlatformFilesCount++;
					statistics_.PlatformBytesCount += fileSize;
				}
				statistics_.TotalFilesCount++;
				statistics_.TotalBytesCount += fileSize;
			}
		}
		dir.Close();
	}

	void BinaryShaderCache::clearStatistics()
	{
		statistics_.LoadedShaders = 0;
		statistics_.SavedShaders = 0;
		statistics_.PlatformFilesCount = 0;
		statistics_.PlatformBytesCount = 0;
		statistics_.TotalFilesCount = 0;
		statistics_.TotalBytesCount = 0;
	}

	bool BinaryShaderCache::parseShaderPath(const StringView& path, uint64_t* platformHash, uint64_t* format, uint64_t* shaderHash)
	{
		String filename = fs::GetFileName(path);

		// The length of a binary shader filename is: 16 + "_" + 8 + "_" + 16 + ".bin"
		if (filename.size() != 46) {
			return false;
		}

		if (fs::GetExtension(filename) != "bin" || filename[16] != '_' || filename[25] != '_') {
			return false;
		}

		for (unsigned int i = 0; i < 16; i++) {
			const char c = filename[i];
			// Check if the platform component is an hexadecimal number
			if (!(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'F') && !(c >= 'a' && c <= 'f')) {
				return false;
			}
		}

		for (unsigned int i = 0; i < 8; i++) {
			const char c = filename[i + 17];
			// Check if the format component is an hexadecimal number
			if (!(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'F') && !(c >= 'a' && c <= 'f')) {
				return false;
			}
		}

		for (unsigned int i = 0; i < 16; i++) {
			const char c = filename[i + 26];
			// Check if the platform component is an hexadecimal number
			if (!(c >= '0' && c <= '9') && !(c >= 'A' && c <= 'F') && !(c >= 'a' && c <= 'f')) {
				return false;
			}
		}

		char componentString[17];
		componentString[16] = '\0';

		if (platformHash != nullptr) {
			for (unsigned int i = 0; i < 16; i++) {
				componentString[i] = filename[i];
			}
			*platformHash = strtoull(componentString, nullptr, 16);
		}

		componentString[8] = '\0';
		if (format != nullptr) {
			for (unsigned int i = 0; i < 8; i++) {
				componentString[i] = filename[i + 17];
			}
			*format = strtoul(componentString, nullptr, 16);
		}

		if (shaderHash != nullptr) {
			for (unsigned int i = 0; i < 16; i++) {
				componentString[i] = filename[i + 26];
			}
			*shaderHash = strtoull(componentString, nullptr, 16);
		}

		return true;
	}
}