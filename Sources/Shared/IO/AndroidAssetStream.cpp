#include "AndroidAssetStream.h"

#if defined(DEATH_TARGET_ANDROID)

#include <sys/stat.h>		// for open()
#include <fcntl.h>			// for open()
#include <unistd.h>			// for close()

namespace Death { namespace IO {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	ANativeActivity* AndroidAssetStream::_nativeActivity = nullptr;

	AndroidAssetStream::AndroidAssetStream(const Containers::StringView path, FileAccess mode)
		: AndroidAssetStream(Containers::String{path}, mode)
	{
	}

	AndroidAssetStream::AndroidAssetStream(Containers::String&& path, FileAccess mode)
		: _path(std::move(path)), _size(Stream::Invalid),
#if defined(DEATH_USE_FILE_DESCRIPTORS)
			_fileDescriptor(-1), _startOffset(0L)
#else
			_asset(nullptr)
#endif
	{
		Open(mode);
	}

	AndroidAssetStream::~AndroidAssetStream()
	{
		AndroidAssetStream::Dispose();
	}

	/*! This method will close a file both normally opened or fopened */
	void AndroidAssetStream::Dispose()
	{
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			const std::int32_t retValue = ::close(_fileDescriptor);
			if (retValue >= 0) {
				LOGI("File \"%s\" closed", _path.data());
				_fileDescriptor = -1;
			} else {
				LOGW("Can't close the file \"%s\"", _path.data());
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

	std::int64_t AndroidAssetStream::Seek(std::int64_t offset, SeekOrigin origin)
	{
		std::int64_t newPos = Stream::Invalid;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			switch (origin) {
				case SeekOrigin::Begin: newPos = ::lseek(_fileDescriptor, _startOffset + offset, SEEK_SET); break;
				case SeekOrigin::Current: newPos = ::lseek(_fileDescriptor, offset, SEEK_CUR); break;
				case SeekOrigin::End: newPos = ::lseek(_fileDescriptor, _startOffset + _size + offset, SEEK_END); break;
				default: return Stream::OutOfRange;
			}
			if (newPos >= _startOffset) {
				newPos -= _startOffset;
			} else if (newPos < 0) {
				newPos = Stream::OutOfRange;
			}
		}
#else
		if (_asset != nullptr) {
			newPos = AAsset_seek64(_asset, offset, static_cast<std::int32_t>(origin));
			if (newPos < 0) {
				newPos = Stream::OutOfRange;
			}
		}
#endif
		return newPos;
	}

	std::int64_t AndroidAssetStream::GetPosition() const
	{
		std::int64_t pos = Stream::Invalid;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			pos = ::lseek(_fileDescriptor, 0L, SEEK_CUR) - _startOffset;
		}
#else
		if (_asset != nullptr) {
			pos = AAsset_seek64(_asset, 0L, SEEK_CUR);
		}
#endif
		return pos;
	}

	std::int64_t AndroidAssetStream::Read(void* destination, std::int64_t bytesToRead)
	{
		if (bytesToRead <= 0) {
			return 0;
		}

		DEATH_ASSERT(destination != nullptr, "destination is null", 0);
		std::uint8_t* typedBuffer = static_cast<std::uint8_t*>(destination);

		std::int64_t bytesReadTotal = 0;
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		if (_fileDescriptor >= 0) {
			const std::int64_t pos = ::lseek(_fileDescriptor, 0L, SEEK_CUR);
			if (_startOffset + _size <= pos) {
				bytesToRead = 0; // Simulating EOF
			} else if (_startOffset + _size < pos + bytesToRead) {
				bytesToRead = (_startOffset + _size) - pos;
			}

			while (bytesToRead > 0) {
				std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? bytesToRead : INT32_MAX);
				std::int32_t bytesRead = ::read(&typedBuffer[bytesReadTotal], partialBytesToRead);
				if DEATH_UNLIKELY(bytesRead < 0) {
					return bytesRead;
				} else if DEATH_UNLIKELY(bytesRead == 0) {
					break;
				}
				bytesReadTotal += bytesRead;
				bytesToRead -= bytesRead;
			}
		}
#else
		if (_asset != nullptr) {
			do {
				std::int32_t partialBytesToRead = (bytesToRead < INT32_MAX ? bytesToRead : INT32_MAX);
				std::int32_t bytesRead = AAsset_read(_asset, &typedBuffer[bytesReadTotal], partialBytesToRead);
				if DEATH_UNLIKELY(bytesRead < 0) {
					return bytesRead;
				} else if DEATH_UNLIKELY(bytesRead == 0) {
					break;
				}
				bytesReadTotal += bytesRead;
				bytesToRead -= bytesRead;
			} while (bytesToRead > 0);
		}
#endif
		return bytesReadTotal;
	}

	std::int64_t AndroidAssetStream::Write(const void* source, std::int64_t bytesToWrite)
	{
		// Not supported
		return Stream::Invalid;
	}

	bool AndroidAssetStream::Flush()
	{
		// Not supported
		return true;
	}

	bool AndroidAssetStream::IsValid()
	{
#if defined(DEATH_USE_FILE_DESCRIPTORS)
		return (_fileDescriptor >= 0);
#else
		return (_asset != nullptr);
#endif
	}

	std::int64_t AndroidAssetStream::GetSize() const
	{
		return _size;
	}

	Containers::StringView AndroidAssetStream::GetPath() const
	{
		return _path;
	}

	void AndroidAssetStream::InitializeAssetManager(struct android_app* state)
	{
		_nativeActivity = state->activity;
	}

	Containers::StringView AndroidAssetStream::TryGetAssetPath(const Containers::StringView path)
	{
		if (path.hasPrefix(Prefix)) {
			std::size_t prefixLength = Prefix.size();
			return path.exceptPrefix(prefixLength < path.size() && (path[prefixLength] == '/' || path[prefixLength] == '\\')
						? prefixLength + 1
						: prefixLength);
		}
		return {};
	}

	const char* AndroidAssetStream::GetInternalDataPath()
	{
		return _nativeActivity->internalDataPath;
	}

	const char* AndroidAssetStream::GetExternalDataPath()
	{
		return _nativeActivity->externalDataPath;
	}

	const char* AndroidAssetStream::GetObbPath()
	{
		return _nativeActivity->obbPath;
	}

	bool AndroidAssetStream::TryOpen(const Containers::StringView path)
	{
		return (TryOpenFile(path) || TryOpenDirectory(path));
	}

	bool AndroidAssetStream::TryOpenFile(const Containers::StringView path)
	{
		AAsset* asset = AAssetManager_open(_nativeActivity->assetManager, path.data(), AASSET_MODE_UNKNOWN);
		if (asset != nullptr) {
			AAsset_close(asset);
			return true;
		}

		return false;
	}

	bool AndroidAssetStream::TryOpenDirectory(const Containers::StringView path)
	{
		AAssetDir* assetDir = AAssetManager_openDir(_nativeActivity->assetManager, path.data());
		if (assetDir != nullptr) {
			AAssetDir_close(assetDir);
			return true;
		}

		return false;
	}

	std::int64_t AndroidAssetStream::GetFileSize(const Containers::StringView path)
	{
		off64_t assetLength = 0;
		AAsset* asset = AAssetManager_open(_nativeActivity->assetManager, path.data(), AASSET_MODE_UNKNOWN);
		if (asset != nullptr) {
			assetLength = AAsset_getLength64(asset);
			AAsset_close(asset);
		}

		return assetLength;
	}

	AAssetDir* AndroidAssetStream::OpenDirectory(const Containers::StringView path)
	{
		return AAssetManager_openDir(_nativeActivity->assetManager, path.data());
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

	void AndroidAssetStream::Open(FileAccess mode)
	{
		FileAccess maskedMode = mode & FileAccess::ReadWrite;
		if (maskedMode != FileAccess::Read) {
			LOGE("Can't open file \"%s\" - wrong open mode", _path.data());
			return;
		}

#if defined(DEATH_USE_FILE_DESCRIPTORS)
		// An asset file can only be read
		AAsset* asset = AAssetManager_open(_nativeActivity->assetManager, _path.data(), AASSET_MODE_RANDOM);
		if (asset == nullptr) {
			LOGE("Can't open file \"%s\"", _path.data());
			return;
		}

		off64_t outStart = 0;
		off64_t outLength = 0;
		_fileDescriptor = AAsset_openFileDescriptor64(asset, &outStart, &outLength);
		_startOffset = outStart;
		_size = outLength;

		::lseek(_fileDescriptor, _startOffset, SEEK_SET);
		AAsset_close(asset);
		asset = nullptr;

		if (_fileDescriptor < 0) {
			LOGE("Can't open file \"%s\"", _path.data());
			return;
		}

		LOGI("File \"%s\" opened", _path.data());
#else
		// An asset file can only be read
		_asset = AAssetManager_open(_nativeActivity->assetManager, _path.data(), AASSET_MODE_RANDOM);
		if (_asset == nullptr) {
			LOGE("Can't open file \"%s\"", _path.data());
			return;
		}

		LOGI("File \"%s\" opened", _path.data());

		// Calculating file size
		_size = AAsset_getLength64(_asset);
#endif
	}

}}

#endif