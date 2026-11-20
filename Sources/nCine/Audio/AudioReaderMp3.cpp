#include "AudioReaderMp3.h"

#if defined(WITH_MPG123)

#include <cstdio>

#include <Base/Move.h>

using namespace Death::IO;

#if defined(WITH_MPG123_DYNAMIC)
#	define MPG123_CALL(name) AudioReaderMp3::_##name
#else
#	define MPG123_CALL(name) ::name
#endif

namespace nCine
{
	namespace
	{
#if defined(WITH_MPG123_DYNAMIC)
		constexpr int Mpg123Ok = 0;
		constexpr int Mpg123Done = -12;
		constexpr int Mpg123NewFormat = -11;
		constexpr int Mpg123EncSigned16 = 0xD0;
		constexpr int Mpg123AddFlags = 2;
		constexpr long Mpg123Quiet = 0x20;
#else
		constexpr int Mpg123Ok = MPG123_OK;
		constexpr int Mpg123Done = MPG123_DONE;
		constexpr int Mpg123NewFormat = MPG123_NEW_FORMAT;
		constexpr int Mpg123EncSigned16 = MPG123_ENC_SIGNED_16;
		constexpr auto Mpg123AddFlags = MPG123_ADD_FLAGS;
		constexpr long Mpg123Quiet = MPG123_QUIET;
#endif

#if defined(WITH_MPG123_DYNAMIC) || (defined(MPG123_API_VERSION) && MPG123_API_VERSION >= 48)
#	define NCINE_MPG123_IO64
		int FileRead(void* handle, void* buffer, std::size_t size, std::size_t* bytesRead)
		{
			Stream* file = static_cast<Stream*>(handle);
			std::int64_t result = file->Read(buffer, std::int64_t(size));
			if (result < 0) {
				*bytesRead = 0;
				return -1;
			}
			*bytesRead = std::size_t(result);
			return 0;
		}

		std::int64_t FileSeek(void* handle, std::int64_t offset, int whence)
		{
			Stream* file = static_cast<Stream*>(handle);
			return file->Seek(offset, (SeekOrigin)whence);
		}
#else
		// Older libmpg123 (before 1.32) has only the reader of the native `off_t`
		ssize_t FileRead(void* handle, void* buffer, std::size_t size)
		{
			Stream* file = static_cast<Stream*>(handle);
			std::int64_t result = file->Read(buffer, std::int64_t(size));
			return (result < 0 ? -1 : ssize_t(result));
		}

		off_t FileSeek(void* handle, off_t offset, int whence)
		{
			Stream* file = static_cast<Stream*>(handle);
			return off_t(file->Seek(offset, (SeekOrigin)whence));
		}
#endif
	}

#if defined(WITH_MPG123_DYNAMIC)
	HMODULE AudioReaderMp3::_mpgLib = nullptr;
	bool AudioReaderMp3::_mpgLibInitialized = false;
	AudioReaderMp3::mpg123_init_t* AudioReaderMp3::_mpg123_init = nullptr;
	AudioReaderMp3::mpg123_new_t* AudioReaderMp3::_mpg123_new = nullptr;
	AudioReaderMp3::mpg123_delete_t* AudioReaderMp3::_mpg123_delete = nullptr;
	AudioReaderMp3::mpg123_param_t* AudioReaderMp3::_mpg123_param = nullptr;
	AudioReaderMp3::mpg123_reader64_t* AudioReaderMp3::_mpg123_reader64 = nullptr;
	AudioReaderMp3::mpg123_open_handle64_t* AudioReaderMp3::_mpg123_open_handle64 = nullptr;
	AudioReaderMp3::mpg123_getformat_t* AudioReaderMp3::_mpg123_getformat = nullptr;
	AudioReaderMp3::mpg123_format_none_t* AudioReaderMp3::_mpg123_format_none = nullptr;
	AudioReaderMp3::mpg123_format_t* AudioReaderMp3::_mpg123_format = nullptr;
	AudioReaderMp3::mpg123_length64_t* AudioReaderMp3::_mpg123_length64 = nullptr;
	AudioReaderMp3::mpg123_read_t* AudioReaderMp3::_mpg123_read = nullptr;
	AudioReaderMp3::mpg123_seek64_t* AudioReaderMp3::_mpg123_seek64 = nullptr;
	AudioReaderMp3::mpg123_close_t* AudioReaderMp3::_mpg123_close = nullptr;
#endif

	AudioReaderMp3::AudioReaderMp3(std::unique_ptr<Stream> fileHandle, mpg123_handle* handle)
		: _fileHandle(Death::move(fileHandle)), _handle(handle)
	{
		DEATH_ASSERT(_fileHandle->IsValid());
	}

	AudioReaderMp3::~AudioReaderMp3()
	{
		CloseDecoder(_handle);
	}

	std::int32_t AudioReaderMp3::Read(void* buffer, std::int32_t bufferSize) const
	{
		if (_handle == nullptr) {
			return 0;
		}

		std::size_t bufferSeek = 0;
		std::int32_t idleCalls = 0;
		while (bufferSeek < std::size_t(bufferSize)) {
			std::size_t bytes = 0;
			int result = MPG123_CALL(mpg123_read)(_handle, static_cast<unsigned char*>(buffer) + bufferSeek, std::size_t(bufferSize) - bufferSeek, &bytes);
			bufferSeek += bytes;
			if (result == Mpg123Done) {
				break;
			}
			if (result != Mpg123Ok && result != Mpg123NewFormat) {
				LOGE("Error {} while decoding MP3", result);
				break;
			}
			// The output format is fixed, so a new format only means more frames, but nothing decoded several times in a row is the end
			if (bytes == 0 && ++idleCalls > 8) {
				break;
			}
		}

		return std::int32_t(bufferSeek);
	}

	void AudioReaderMp3::Rewind() const
	{
		if (_handle != nullptr) {
#if defined(NCINE_MPG123_IO64)
			MPG123_CALL(mpg123_seek64)(_handle, 0, SEEK_SET);
#else
			::mpg123_seek(_handle, 0, SEEK_SET);
#endif
		}
	}

	mpg123_handle* AudioReaderMp3::OpenDecoder(Stream* stream, std::int32_t& frequency, std::int32_t& channels, std::int64_t& samples)
	{
		if (!TryLoadLibrary()) {
			LOGE("libmpg123 is not available");
			return nullptr;
		}

		int error = 0;
		mpg123_handle* handle = MPG123_CALL(mpg123_new)(nullptr, &error);
		if (handle == nullptr) {
			LOGE("mpg123_new() failed with error {}", error);
			return nullptr;
		}

		MPG123_CALL(mpg123_param)(handle, Mpg123AddFlags, Mpg123Quiet, 0.0);

#if defined(NCINE_MPG123_IO64)
		error = MPG123_CALL(mpg123_reader64)(handle, FileRead, FileSeek, nullptr);
		if (error == Mpg123Ok) {
#	if defined(WITH_MPG123_DYNAMIC) || MPG123_API_VERSION >= 49
			error = MPG123_CALL(mpg123_open_handle64)(handle, stream);
#	else
			// libmpg123 1.32 has mpg123_reader64() but not mpg123_open_handle64() yet
			error = ::mpg123_open_handle(handle, stream);
#	endif
		}
#else
		error = ::mpg123_replace_reader_handle(handle, FileRead, FileSeek, nullptr);
		if (error == Mpg123Ok) {
			error = ::mpg123_open_handle(handle, stream);
		}
#endif
		if (error != Mpg123Ok) {
			LOGE("Failed to open MP3 stream with error {}", error);
			MPG123_CALL(mpg123_delete)(handle);
			return nullptr;
		}

		long rate = 0;
		int numChannels = 0, encoding = 0;
		error = MPG123_CALL(mpg123_getformat)(handle, &rate, &numChannels, &encoding);
		if (error != Mpg123Ok || rate <= 0 || (numChannels != 1 && numChannels != 2)) {
			LOGE("Unsupported MP3 format (error {}, {} Hz, {} channels)", error, rate, numChannels);
			CloseDecoder(handle);
			return nullptr;
		}

		// Always 16-bit signed samples in the host byte order, as the other formats are decoded
		MPG123_CALL(mpg123_format_none)(handle);
		MPG123_CALL(mpg123_format)(handle, rate, numChannels, Mpg123EncSigned16);

#if defined(NCINE_MPG123_IO64)
		std::int64_t length = MPG123_CALL(mpg123_length64)(handle);
#else
		std::int64_t length = std::int64_t(::mpg123_length(handle));
#endif

		frequency = std::int32_t(rate);
		channels = numChannels;
		samples = (length > 0 ? length : 0);
		return handle;
	}

	void AudioReaderMp3::CloseDecoder(mpg123_handle* handle)
	{
		if (handle != nullptr) {
			MPG123_CALL(mpg123_close)(handle);
			MPG123_CALL(mpg123_delete)(handle);
		}
	}

	bool AudioReaderMp3::TryLoadLibrary()
	{
#if defined(WITH_MPG123_DYNAMIC)
		if (_mpgLibInitialized) {
			return (_mpgLib != nullptr);
		}

		_mpgLibInitialized = true;
#	if defined(WITH_OPENMPT)
		const wchar_t* LibraryName = L"openmpt-mpg123.dll";
#	else
		const wchar_t* LibraryName = L"libmpg123-0.dll";
#	endif
#	if defined(DEATH_TARGET_WINDOWS_RT)
		_mpgLib = ::LoadPackagedLibrary(LibraryName, NULL);
#	else
		_mpgLib = ::LoadLibrary(LibraryName);
#	endif
		if (_mpgLib == nullptr) {
			return false;
		}

		_mpg123_init = (mpg123_init_t*)::GetProcAddress(_mpgLib, "mpg123_init");
		_mpg123_new = (mpg123_new_t*)::GetProcAddress(_mpgLib, "mpg123_new");
		_mpg123_delete = (mpg123_delete_t*)::GetProcAddress(_mpgLib, "mpg123_delete");
		_mpg123_param = (mpg123_param_t*)::GetProcAddress(_mpgLib, "mpg123_param");
		_mpg123_reader64 = (mpg123_reader64_t*)::GetProcAddress(_mpgLib, "mpg123_reader64");
		_mpg123_open_handle64 = (mpg123_open_handle64_t*)::GetProcAddress(_mpgLib, "mpg123_open_handle64");
		if (_mpg123_open_handle64 == nullptr) {
			// libmpg123 1.32 opens the mpg123_reader64() handle through mpg123_open_handle()
			_mpg123_open_handle64 = (mpg123_open_handle64_t*)::GetProcAddress(_mpgLib, "mpg123_open_handle");
		}
		_mpg123_getformat = (mpg123_getformat_t*)::GetProcAddress(_mpgLib, "mpg123_getformat");
		_mpg123_format_none = (mpg123_format_none_t*)::GetProcAddress(_mpgLib, "mpg123_format_none");
		_mpg123_format = (mpg123_format_t*)::GetProcAddress(_mpgLib, "mpg123_format");
		_mpg123_length64 = (mpg123_length64_t*)::GetProcAddress(_mpgLib, "mpg123_length64");
		_mpg123_read = (mpg123_read_t*)::GetProcAddress(_mpgLib, "mpg123_read");
		_mpg123_seek64 = (mpg123_seek64_t*)::GetProcAddress(_mpgLib, "mpg123_seek64");
		_mpg123_close = (mpg123_close_t*)::GetProcAddress(_mpgLib, "mpg123_close");

		if (_mpg123_init == nullptr || _mpg123_new == nullptr || _mpg123_delete == nullptr || _mpg123_param == nullptr ||
			_mpg123_reader64 == nullptr || _mpg123_open_handle64 == nullptr || _mpg123_getformat == nullptr ||
			_mpg123_format_none == nullptr || _mpg123_format == nullptr || _mpg123_length64 == nullptr ||
			_mpg123_read == nullptr || _mpg123_seek64 == nullptr || _mpg123_close == nullptr) {
			// Older versions (before 1.32) don't have the functions with 64-bit offsets
			::FreeLibrary(_mpgLib);
			_mpgLib = nullptr;
			return false;
		}

		_mpg123_init();
		return true;
#else
		static bool initialized = false;
		if (!initialized) {
			initialized = true;
			::mpg123_init();
		}
		return true;
#endif
	}
}

#endif
