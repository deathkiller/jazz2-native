#ifdef __ANDROID__

#include "AssetFile.h"
#include "../../Common.h"

#include <sys/stat.h> // for open()
#include <fcntl.h> // for open()
#include <unistd.h> // for close()

namespace nCine
{

	///////////////////////////////////////////////////////////
	// STATIC DEFINITIONS
	///////////////////////////////////////////////////////////

	AAssetManager* AssetFile::assetManager_ = nullptr;

	///////////////////////////////////////////////////////////
	// CONSTRUCTORS and DESTRUCTOR
	///////////////////////////////////////////////////////////

	AssetFile::AssetFile(const String& filename)
		: IFileStream(filename), asset_(nullptr), startOffset_(0L)
	{
		type_ = FileType::Asset;
	}

	AssetFile::~AssetFile()
	{
		if (shouldCloseOnDestruction_)
			Close();
	}

	///////////////////////////////////////////////////////////
	// PUBLIC FUNCTIONS
	///////////////////////////////////////////////////////////

	void AssetFile::Open(FileAccessMode mode, bool shouldExitOnFailToOpen)
	{
		// Checking if the file is already opened
		if (fileDescriptor_ >= 0 || asset_ != nullptr) {
			LOGW_X("File \"%s\" is already opened", filename_.data());
		} else {
			// Opening with a file descriptor
			if ((mode & FileAccessMode::FileDescriptor) == FileAccessMode::FileDescriptor) {
				OpenFD(mode, shouldExitOnFailToOpen);
				// Opening as an asset only
			} else {
				OpenAsset(mode, shouldExitOnFailToOpen);
			}
		}
	}

	/*! This method will close a file both normally opened or fopened */
	void AssetFile::Close()
	{
		if (fileDescriptor_ >= 0) {
			const int retValue = ::close(fileDescriptor_);
			if (retValue < 0)
				LOGW_X("Cannot close the file \"%s\"", filename_.data());
			else {
				LOGI_X("File \"%s\" closed", filename_.data());
				fileDescriptor_ = -1;
			}
		} else if (asset_) {
			AAsset_close(asset_);
			asset_ = nullptr;
			LOGI_X("File \"%s\" closed", filename_.data());
		}
	}

	int32_t AssetFile::Seek(int32_t offset, SeekOrigin origin) const
	{
		int32_t seekValue = -1;

		if (fileDescriptor_ >= 0) {
			switch (origin) {
				case SeekOrigin::Begin:
					seekValue = lseek(fileDescriptor_, startOffset_ + offset, SEEK_SET);
					break;
				case SeekOrigin::Current:
					seekValue = lseek(fileDescriptor_, offset, SEEK_CUR);
					break;
				case SeekOrigin::End:
					seekValue = lseek(fileDescriptor_, startOffset_ + fileSize_ + offset, SEEK_END);
					break;
			}
			seekValue -= startOffset_;
		} else if (asset_)
			seekValue = AAsset_seek(asset_, offset, (int)origin);

		return seekValue;
	}

	int32_t AssetFile::GetPosition() const
	{
		int32_t tellValue = -1;

		if (fileDescriptor_ >= 0)
			tellValue = lseek(fileDescriptor_, 0L, SEEK_CUR) - startOffset_;
		else if (asset_)
			tellValue = AAsset_seek(asset_, 0L, SEEK_CUR);

		return tellValue;
	}

	uint32_t AssetFile::Read(void* buffer, uint32_t bytes) const
	{
		ASSERT(buffer);

		uint32_t bytesRead = 0;

		if (fileDescriptor_ >= 0) {
			int32_t bytesToRead = bytes;

			const int32_t seekValue = lseek(fileDescriptor_, 0L, SEEK_CUR);

			if (seekValue >= startOffset_ + fileSize_)
				bytesToRead = 0; // simulating EOF
			else if (seekValue + static_cast<int32_t>(bytes) > startOffset_ + fileSize_)
				bytesToRead = (startOffset_ + fileSize_) - seekValue;

			bytesRead = ::read(fileDescriptor_, buffer, bytesToRead);
		} else if (asset_)
			bytesRead = AAsset_read(asset_, buffer, bytes);

		return bytesRead;
	}

	bool AssetFile::IsOpened() const
	{
		if (fileDescriptor_ >= 0 || asset_ != nullptr)
			return true;
		else
			return false;
	}

	const char* AssetFile::assetPath(const char* path)
	{
		ASSERT(path);
		if (strncmp(path, Prefix, strlen(Prefix)) == 0) {
			// Skip leading path separator character
			return (path[7] == '/') ? path + 8 : path + 7;
		}
		return nullptr;
	}

	bool AssetFile::tryOpen(const char* path)
	{
		ASSERT(path);
		return (tryOpenFile(path) || tryOpenDirectory(path));
	}

	bool AssetFile::tryOpenFile(const char* path)
	{
		ASSERT(path);
		const char* strippedPath = assetPath(path);
		if (strippedPath == nullptr)
			return false;

		AAsset* asset = AAssetManager_open(assetManager_, strippedPath, AASSET_MODE_UNKNOWN);
		if (asset) {
			AAsset_close(asset);
			return true;
		}

		return false;
	}

	bool AssetFile::tryOpenDirectory(const char* path)
	{
		ASSERT(path);
		const char* strippedPath = assetPath(path);
		if (strippedPath == nullptr)
			return false;

		AAsset* asset = AAssetManager_open(assetManager_, strippedPath, AASSET_MODE_UNKNOWN);
		if (asset) {
			AAsset_close(asset);
			return false;
		}

		AAssetDir* assetDir = AAssetManager_openDir(assetManager_, strippedPath);
		if (assetDir) {
			AAssetDir_close(assetDir);
			return true;
		}

		return false;
	}

	off_t AssetFile::length(const char* path)
	{
		ASSERT(path);

		off_t assetLength = 0;
		const char* strippedPath = assetPath(path);
		if (strippedPath == nullptr)
			return assetLength;

		AAsset* asset = AAssetManager_open(assetManager_, strippedPath, AASSET_MODE_UNKNOWN);
		if (asset) {
			assetLength = AAsset_getLength(asset);
			AAsset_close(asset);
		}

		return assetLength;
	}

	AAssetDir* AssetFile::openDir(const char* dirName)
	{
		ASSERT(dirName);
		return AAssetManager_openDir(assetManager_, dirName);
	}

	void AssetFile::closeDir(AAssetDir* assetDir)
	{
		ASSERT(assetDir);
		AAssetDir_close(assetDir);
	}

	void AssetFile::rewindDir(AAssetDir* assetDir)
	{
		ASSERT(assetDir);
		AAssetDir_rewind(assetDir);
	}

	const char* AssetFile::nextFileName(AAssetDir* assetDir)
	{
		ASSERT(assetDir);
		return AAssetDir_getNextFileName(assetDir);
	}

	///////////////////////////////////////////////////////////
	// PRIVATE FUNCTIONS
	///////////////////////////////////////////////////////////

	void AssetFile::OpenFD(FileAccessMode mode, bool shouldExitOnFailToOpen)
	{
		// An asset file can only be read
		if (mode == (FileAccessMode::FileDescriptor | FileAccessMode::Read)) {
			asset_ = AAssetManager_open(assetManager_, filename_.data(), AASSET_MODE_UNKNOWN);
			if (asset_ == nullptr) {
				if (shouldExitOnFailToOpen) {
					LOGF_X("Cannot open the file \"%s\"", filename_.data());
					exit(EXIT_FAILURE);
				} else {
					LOGE_X("Cannot open the file \"%s\"", filename_.data());
					return;
				}
			}

			off_t outStart = 0;
			off_t outLength = 0;
			fileDescriptor_ = AAsset_openFileDescriptor(asset_, &outStart, &outLength);
			startOffset_ = outStart;
			fileSize_ = outLength;

			lseek(fileDescriptor_, startOffset_, SEEK_SET);
			AAsset_close(asset_);
			asset_ = nullptr;

			if (fileDescriptor_ < 0) {
				if (shouldExitOnFailToOpen) {
					LOGF_X("Cannot open the file \"%s\"", filename_.data());
					exit(EXIT_FAILURE);
				} else {
					LOGE_X("Cannot open the file \"%s\"", filename_.data());
					return;
				}
			} else {
				LOGI_X("File \"%s\" opened", filename_.data());
			}
		} else {
			LOGE_X("Cannot open the file \"%s\", wrong open mode", filename_.data());
		}
	}

	void AssetFile::OpenAsset(FileAccessMode mode, bool shouldExitOnFailToOpen)
	{
		// An asset file can only be read
		if (mode == FileAccessMode::Read) {
			asset_ = AAssetManager_open(assetManager_, filename_.data(), AASSET_MODE_UNKNOWN);
			if (asset_ == nullptr) {
				if (shouldExitOnFailToOpen) {
					LOGF_X("Cannot open the file \"%s\"", filename_.data());
					exit(EXIT_FAILURE);
				} else {
					LOGE_X("Cannot open the file \"%s\"", filename_.data());
					return;
				}
			} else {
				LOGI_X("File \"%s\" opened", filename_.data());
			}

			// Calculating file size
			fileSize_ = AAsset_getLength(asset_);
		} else {
			LOGE_X("Cannot open the file \"%s\", wrong open mode", filename_.data());
		}
	}

}

#endif