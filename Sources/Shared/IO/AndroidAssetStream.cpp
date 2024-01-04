#include "AndroidAssetStream.h"

#if defined(DEATH_TARGET_ANDROID)

#include <sys/stat.h>		// For open()
#include <fcntl.h>			// For open()
#include <unistd.h>			// For close()

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	AAssetManager* AndroidAssetStream::_assetManager = nullptr;
	const char* AndroidAssetStream::_internalDataPath = nullptr;

	AndroidAssetStream::AndroidAssetStream(const Containers::String& path, FileAccessMode mode)
		: _shouldCloseOnDestruction(true),
#if defined(DEATH_USE_FILE_DESCRIPTORS)
			_fileDescriptor(-1), _startOffset(0L)
#else
			_asset(nullptr)
#endif
	{
		_type = Type::AndroidAsset;
		_path = path;
		Open(mode);
	}

	AndroidAssetStream::~AndroidAssetStream()
	{
		if (_shouldCloseOnDestruction) {
			Close();
		}
	}

	/*! This method will close a file both normally opened or fopened */
	void AndroidAssetStream::Close()
	{
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			const std::int32_t retValue = ::close(_fileDescriptor);
			if (retValue < 0) {
				LOGW("Cannot close the file \"%s\"", _path.data());
			} else {
				LOGI("File \"%s\" closed", _path.data());
				_fileDescriptor = -1;
			}
		}
#else
		if (_asset != nullptr) {
			AAsset_close(_asset);
			_asset = nullptr;
			LOGI("File \"%s\" closed", _path.data());
		}
#endif
	}

	std::int32_t AndroidAssetStream::Seek(std::int32_t offset, SeekOrigin origin)
	{
		std::int32_t seekValue = -1;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			switch (origin) {
				case SeekOrigin::Begin: seekValue = ::lseek(_fileDescriptor, _startOffset + offset, SEEK_SET); break;
				case SeekOrigin::Current: seekValue = ::lseek(_fileDescriptor, offset, SEEK_CUR); break;
				case SeekOrigin::End: seekValue = ::lseek(_fileDescriptor, _startOffset + _size + offset, SEEK_END); break;
			}
			seekValue -= _startOffset;
		}
#else
		if (_asset != nullptr) {
			seekValue = AAsset_seek(_asset, offset, (std::int32_t)origin);
		}
#endif
		return seekValue;
	}

	std::int32_t AndroidAssetStream::GetPosition() const
	{
		std::int32_t tellValue = -1;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			tellValue = ::lseek(_fileDescriptor, 0L, SEEK_CUR) - _startOffset;
		}
#else
		if (_asset != nullptr) {
			tellValue = AAsset_seek(_asset, 0L, SEEK_CUR);
		}
#endif
		return tellValue;
	}

	std::int32_t AndroidAssetStream::Read(void* buffer, std::int32_t bytes)
	{
		DEATH_ASSERT(buffer != nullptr, 0, "buffer is nullptr");

		std::int32_t bytesRead = 0;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			std::int32_t bytesToRead = bytes;
			const std::int32_t seekValue = ::lseek(_fileDescriptor, 0L, SEEK_CUR);
			if (seekValue >= _startOffset + _size) {
				bytesToRead = 0; // Simulating EOF
			} else if (seekValue + static_cast<std::int32_t>(bytes) > _startOffset + _size) {
				bytesToRead = (_startOffset + _size) - seekValue;
			}
			bytesRead = ::read(_fileDescriptor, buffer, bytesToRead);
		}
#else
		if (_asset != nullptr) {
			bytesRead = AAsset_read(_asset, buffer, bytes);
		}
#endif
		return bytesRead;
	}

	std::int32_t AndroidAssetStream::Write(const void* buffer, std::int32_t bytes)
	{
		return 0;
	}

	bool AndroidAssetStream::IsValid() const
	{
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		return (_fileDescriptor >= 0);
#else
		return (_asset != nullptr);
#endif
	}

	void AndroidAssetStream::InitializeAssetManager(struct android_app* state)
	{
		_assetManager = state->activity->assetManager;
		_internalDataPath = state->activity->internalDataPath;
	}

	const char* AndroidAssetStream::TryGetAssetPath(const char* path)
	{
		DEATH_ASSERT(path != nullptr, nullptr, "path is nullptr");
		if (strncmp(path, Prefix.data(), Prefix.size()) == 0) {
			// Skip leading path separator character
			return (path[7] == '/' ? path + 8 : path + 7);
		}
		return nullptr;
	}

	bool AndroidAssetStream::TryOpen(const char* path)
	{
		DEATH_ASSERT(path != nullptr, false, "path is nullptr");
		return (TryOpenFile(path) || TryOpenDirectory(path));
	}

	bool AndroidAssetStream::TryOpenFile(const char* path)
	{
		DEATH_ASSERT(path != nullptr, false, "path is nullptr");
		const char* strippedPath = TryGetAssetPath(path);
		if (strippedPath == nullptr) {
			return false;
		}

		AAsset* asset = AAssetManager_open(_assetManager, strippedPath, AASSET_MODE_UNKNOWN);
		if (asset != nullptr) {
			AAsset_close(asset);
			return true;
		}

		return false;
	}

	bool AndroidAssetStream::TryOpenDirectory(const char* path)
	{
		DEATH_ASSERT(path != nullptr, false, "path is nullptr");
		const char* strippedPath = TryGetAssetPath(path);
		if (strippedPath == nullptr) {
			return false;
		}

		AAsset* asset = AAssetManager_open(_assetManager, strippedPath, AASSET_MODE_UNKNOWN);
		if (asset != nullptr) {
			AAsset_close(asset);
			return false;
		}

		AAssetDir* assetDir = AAssetManager_openDir(_assetManager, strippedPath);
		if (assetDir != nullptr) {
			AAssetDir_close(assetDir);
			return true;
		}

		return false;
	}

	off_t AndroidAssetStream::GetLength(const char* path)
	{
		DEATH_ASSERT(path != nullptr, 0, "path is nullptr");

		off_t assetLength = 0;
		const char* strippedPath = TryGetAssetPath(path);
		if (strippedPath == nullptr) {
			return assetLength;
		}

		AAsset* asset = AAssetManager_open(_assetManager, strippedPath, AASSET_MODE_UNKNOWN);
		if (asset != nullptr) {
			assetLength = AAsset_getLength(asset);
			AAsset_close(asset);
		}

		return assetLength;
	}

	AAssetDir* AndroidAssetStream::OpenDirectory(const char* dirName)
	{
		DEATH_ASSERT(dirName != nullptr, nullptr, "dirName is nullptr");
		return AAssetManager_openDir(_assetManager, dirName);
	}

	void AndroidAssetStream::CloseDirectory(AAssetDir* assetDir)
	{
		AAssetDir_close(assetDir);
	}

	void AndroidAssetStream::RewindDirectory(AAssetDir* assetDir)
	{
		AAssetDir_rewind(assetDir);
	}

	const char* AndroidAssetStream::GetNextFileName(AAssetDir* assetDir)
	{
		return AAssetDir_getNextFileName(assetDir);
	}

	void AndroidAssetStream::Open(FileAccessMode mode)
	{
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		// An asset file can only be read
		if (mode != FileAccessMode::Read) {
			LOGE("Cannot open file \"%s\" - wrong open mode", _path.data());
			return;
		}

		AAsset* asset = AAssetManager_open(_assetManager, _path.data(), AASSET_MODE_UNKNOWN);
		if (asset == nullptr) {
			LOGE("Cannot open file \"%s\"", _path.data());
			return;
		}

		off_t outStart = 0;
		off_t outLength = 0;
		_fileDescriptor = AAsset_openFileDescriptor(asset, &outStart, &outLength);
		_startOffset = outStart;
		_size = outLength;

		::lseek(_fileDescriptor, _startOffset, SEEK_SET);
		AAsset_close(asset);
		asset = nullptr;

		if (_fileDescriptor < 0) {
			LOGE("Cannot open file \"%s\"", _path.data());
			return;
		}

		LOGI("File \"%s\" opened", _path.data());
#else
		// An asset file can only be read
		if (mode != FileAccessMode::Read) {
			LOGE("Cannot open file \"%s\" - wrong open mode", _path.data());
			return;
		}

		_asset = AAssetManager_open(_assetManager, _path.data(), AASSET_MODE_UNKNOWN);
		if (_asset == nullptr) {
			LOGE("Cannot open file \"%s\"", _path.data());
			return;
		}

		LOGI("File \"%s\" opened", _path.data());

		// Calculating file size
		_size = AAsset_getLength(_asset);
#endif
	}
}}

#endif