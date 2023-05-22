#include "AudioReaderOgg.h"

#if defined(WITH_VORBIS)

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD) && !defined(WITH_VORBIS_DYNAMIC)
#	if defined(_M_X64)
#		pragma comment(lib, "../Libs/Windows/x64/libvorbisfile.lib")
#	elif defined(_M_IX86)
#		pragma comment(lib, "../Libs/Windows/x86/libvorbisfile.lib")
#	else
#		error Unsupported architecture
#	endif
#endif

using namespace Death::IO;

namespace nCine
{
#if defined(WITH_VORBIS_DYNAMIC)
	HMODULE AudioReaderOgg::_ovLib = nullptr;
	bool AudioReaderOgg::_ovLibInitialized = false;
	AudioReaderOgg::ov_clear_t* AudioReaderOgg::_ov_clear = nullptr;
	AudioReaderOgg::ov_read_t* AudioReaderOgg::_ov_read = nullptr;
	AudioReaderOgg::ov_raw_seek_t* AudioReaderOgg::_ov_raw_seek = nullptr;
	AudioReaderOgg::ov_info_t* AudioReaderOgg::_ov_info = nullptr;
	AudioReaderOgg::ov_open_callbacks_t* AudioReaderOgg::_ov_open_callbacks = nullptr;
	AudioReaderOgg::ov_pcm_total_t* AudioReaderOgg::_ov_pcm_total = nullptr;
	AudioReaderOgg::ov_time_total_t* AudioReaderOgg::_ov_time_total = nullptr;
#endif

	AudioReaderOgg::AudioReaderOgg(std::unique_ptr<Stream> fileHandle, const OggVorbis_File& oggFile)
		: fileHandle_(std::move(fileHandle)), oggFile_(oggFile)
	{
		ASSERT(fileHandle_->IsValid());
	}

	AudioReaderOgg::~AudioReaderOgg()
	{
#if defined(WITH_VORBIS_DYNAMIC)
		_ov_clear(&oggFile_);
#else
		ov_clear(&oggFile_);
#endif
	}

	unsigned long int AudioReaderOgg::read(void* buffer, unsigned long int bufferSize) const
	{
		static int bitStream = 0;
		long bytes = 0;
		unsigned long int bufferSeek = 0;

		do {
			// Read up to a buffer's worth of decoded sound data
			// (0: little endian, 2: 16bit, 1: signed)
#if defined(WITH_VORBIS_DYNAMIC)
			bytes = _ov_read(&oggFile_, static_cast<char*>(buffer) + bufferSeek, bufferSize - bufferSeek, 0, 2, 1, &bitStream);
#else
			bytes = ov_read(&oggFile_, static_cast<char*>(buffer) + bufferSeek, bufferSize - bufferSeek, 0, 2, 1, &bitStream);
#endif
			if (bytes < 0) {
#if defined(WITH_VORBIS_DYNAMIC)
				_ov_clear(&oggFile_);
#else
				ov_clear(&oggFile_);
#endif
				FATAL_MSG("Error decoding at bitstream %d", bitStream);
			}

			// Reset the static variable at the end of a decoding process
			if (bytes <= 0) {
				bitStream = 0;
			}
			bufferSeek += bytes;
		} while (bytes > 0 && bufferSize - bufferSeek > 0);

		return bufferSeek;
	}

	void AudioReaderOgg::rewind() const
	{
#if defined(WITH_VORBIS_DYNAMIC)
		_ov_raw_seek(&oggFile_, 0);
#else
		ov_raw_seek(&oggFile_, 0);
#endif
	}

#if defined(WITH_VORBIS_DYNAMIC)
	bool AudioReaderOgg::TryLoadLibrary()
	{
		if (_ovLibInitialized) {
			return (_ovLib != nullptr);
		}

		_ovLibInitialized = true;
#if defined(WITH_OPENMPT)
		const wchar_t* LibraryName = L"openmpt-vorbis.dll";
#else
		const wchar_t* LibraryName = L"libvorbisfile.dll";
#endif
#if defined(DEATH_TARGET_WINDOWS_RT)
		_ovLib = ::LoadPackagedLibrary(LibraryName, NULL);
#else
		_ovLib = ::LoadLibrary(LibraryName);
#endif
		if (_ovLib == nullptr) {
			return false;
		}

		_ov_clear = (ov_clear_t*)::GetProcAddress(_ovLib, "ov_clear");
		_ov_read = (ov_read_t*)::GetProcAddress(_ovLib, "ov_read");
		_ov_raw_seek = (ov_raw_seek_t*)::GetProcAddress(_ovLib, "ov_raw_seek");
		_ov_info = (ov_info_t*)::GetProcAddress(_ovLib, "ov_info");
		_ov_open_callbacks = (ov_open_callbacks_t*)::GetProcAddress(_ovLib, "ov_open_callbacks");
		_ov_pcm_total = (ov_pcm_total_t*)::GetProcAddress(_ovLib, "ov_pcm_total");
		_ov_time_total = (ov_time_total_t*)::GetProcAddress(_ovLib, "ov_time_total");

		if (_ov_clear == nullptr || _ov_read == nullptr || _ov_raw_seek == nullptr || _ov_info == nullptr ||
			_ov_open_callbacks == nullptr || _ov_pcm_total == nullptr || _ov_time_total == nullptr) {
			::FreeLibrary(_ovLib);
			_ovLib = nullptr;
			return false;
		}

		return true;
	}
#endif
}

#endif